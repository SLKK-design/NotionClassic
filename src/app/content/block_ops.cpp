/* block_ops.cpp — Block array manipulation, toggle cache, and editing actions.
   Extracted from main.cpp.                                                    */
#include "block_ops.h"
#include <Memory.h>
#include <Windows.h>
#include <Controls.h>
#include <TextEdit.h>
#include <Quickdraw.h>
#include <Sound.h>
#include "HttpClient.h"       // HttpClient type for extern gHttpClient
#include "page_lifecycle.h"   // for prepareForPageList, doPagePickerCancel
#include "block_render.h"     // updateBlocks, SyncActiveBlock, beginIndentOperation
#include "save_pipeline.h"    // doSave, runApiThread, afterAddInsert, afterDeleteArchive
#include "run_styles.h"       // RebuildRunsFromTE, ToggleRunStyleBit
#include "navigation.h"       // gNavDelta etc.
#include "page_fetch.h"       // DoFetchPages, DoFetchPageChildren, DoLibMovePage
#include "rich_text_json.h"   // build_block_json, build_children_body
#include "Settings.h"         // SetListEmojiGWorld
#include "library_window.h"   // ShowPagePickerForLink, FetchPageChildrenFunc, LibMovePageFunc
#include "emoji_text.h"       // SyncBlockText, kEmojiSentinel, TextToTE
#include "helpers.h"
#include "app_state.h"
#include "debug_con.h"
#include <string.h>
#include <string>
#include <algorithm>

extern const long kSavedDisplayTicks;
using std::string;

extern AppState     gAppState;
extern AppSettings  gSettings;
extern Block*       pageElm;
extern int          pageElmCount;
extern int          gMaxBlocks;
extern int          blockSelect;
extern TEHandle     textActive;
extern HttpClient   gHttpClient;
extern int          gCurRequest;
extern uint32_t     gAddSerial;
extern uint32_t     gRecreateSerial;
extern int          gScrollOffset;
extern int          gTotalContentHeight;
extern WindowPtr    gWindow;
extern ControlHandle gScrollBar;
extern SaveQueue    gSaveQueue;
extern uint32_t     gToggleSerial;
extern int          gRelayoutHold;   /* block_render.cpp */
extern UndoState    gUndo;
extern void MarkDirty();
extern const char* EffPageID();
extern const char* EffAPIKey();
extern std::string BlocksURL(const char* id);
extern std::string PagesURL(const char* id);

/* ------------------------------------------------------------------ */
/* Block type name table (extern declared in blocks.h)                 */
/* ------------------------------------------------------------------ */
const char* const kBlockTypeNames[] = {
    "paragraph", "heading_1", "heading_2", "heading_3",
    "bulleted_list_item", "numbered_list_item", "to_do", "toggle",
    "code", "quote", "callout",
    "toggle_heading_1", "toggle_heading_2", "toggle_heading_3",
    "divider", "title",
    "child_page"
};

/* ------------------------------------------------------------------ */
/* PackRows / UnpackRows — rows in a compact, movable handle           */
/* ------------------------------------------------------------------ */
/* Per row a fixed header, then only the text, runs and emoji it uses
   (~150-400 B instead of a ~4 KB Block).  Used by the toggle cache and
   the delete safety copy.  Fields are read from the source rows and the
   header buffer directly — see feedback on the m68k -Os struct-copy
   miscompile before "simplifying" into a local struct copy.           */
struct RowHdr {
    char      id[37], parentId[37];
    BlockType type;
    short     textLen;
    uint8_t   runCount, emojiCount, cutShort;
    bool      check, open, isChild, isCont, hasChildren, kidsPartial,
              dirty, recreate, leftParent, inOldParent, kidsLeft, saveFirst;
    int       depth;
    char      iconName[32];
    short     iconSprX, iconSprY;
};

Handle PackRows(int first, int n) {
    long size = 0;
    for (int i = first; i < first + n; i++) {
        const Block& b = pageElm[i];
        size += (long)sizeof(RowHdr) + b.textLen + (long)b.runCount * sizeof(TextRun)
              + (long)b.emojiCount * 32;
    }
    Handle h = NewHandle(size);
    if (!h) return nil;
    HLock(h);
    char* p = *h;
    static RowHdr hd;   /* static: one 140 B header, filled field by field */
    for (int i = first; i < first + n; i++) {
        const Block& b = pageElm[i];
        memcpy(hd.id, b.id, 37); memcpy(hd.parentId, b.parentId, 37);
        hd.type = b.type; hd.textLen = b.textLen;
        hd.runCount = b.runCount; hd.emojiCount = b.emojiCount; hd.cutShort = b.cutShort;
        hd.check = b.check; hd.open = b.open; hd.isChild = b.isChild; hd.isCont = b.isCont;
        hd.hasChildren = b.hasChildren; hd.kidsPartial = b.kidsPartial;
        hd.dirty = b.dirty; hd.recreate = b.recreate; hd.saveFirst = b.saveFirst;
        hd.leftParent = b.leftParent; hd.inOldParent = b.inOldParent; hd.kidsLeft = b.kidsLeft;
        hd.depth = b.depth;
        memcpy(hd.iconName, b.iconName, 32); hd.iconSprX = b.iconSprX; hd.iconSprY = b.iconSprY;
        memcpy(p, &hd, sizeof hd);                         p += sizeof hd;
        memcpy(p, b.text, b.textLen);                      p += b.textLen;
        memcpy(p, b.runs, b.runCount * sizeof(TextRun));   p += b.runCount * sizeof(TextRun);
        memcpy(p, b.emojiSeqs, b.emojiCount * 32);         p += b.emojiCount * 32;
    }
    HUnlock(h);
    return h;
}

int UnpackRows(Handle h, int count, int at, int depth) {
    int added = count;
    if (added > gMaxBlocks - pageElmCount) added = gMaxBlocks - pageElmCount;
    if (added <= 0 || at < 0 || at > pageElmCount) return 0;
    /* Room for all rows in one shift, then rebuild each in place */
    memmove(&pageElm[at + added], &pageElm[at], sizeof(Block) * (pageElmCount - at));
    pageElmCount += added;
    HLock(h);
    const char* p = *h;
    static RowHdr hd;
    for (int i = 0; i < added; i++) {
        memcpy(&hd, p, sizeof hd); p += sizeof hd;
        Block& b = pageElm[at + i];
        b = Block();   /* the slot held a stale copy of a shifted row */
        memcpy(b.id, hd.id, 37); memcpy(b.parentId, hd.parentId, 37);
        b.type = hd.type; b.cutShort = hd.cutShort;
        b.check = hd.check; b.open = hd.open; b.isChild = hd.isChild; b.isCont = hd.isCont;
        b.hasChildren = hd.hasChildren; b.kidsPartial = hd.kidsPartial;
        b.dirty = hd.dirty; b.recreate = hd.recreate; b.saveFirst = hd.saveFirst;
        b.leftParent = hd.leftParent; b.inOldParent = hd.inOldParent; b.kidsLeft = hd.kidsLeft;
        b.depth = (depth >= 0) ? depth : hd.depth;
        memcpy(b.iconName, hd.iconName, 32); b.iconSprX = hd.iconSprX; b.iconSprY = hd.iconSprY;
        b.textLen = hd.textLen;
        memcpy(b.text, p, hd.textLen); b.text[hd.textLen] = '\0';  p += hd.textLen;
        b.runCount = hd.runCount;
        memcpy(b.runs, p, hd.runCount * sizeof(TextRun));          p += hd.runCount * sizeof(TextRun);
        b.emojiCount = hd.emojiCount;
        memcpy(b.emojiSeqs, p, hd.emojiCount * 32);                p += hd.emojiCount * 32;
    }
    HUnlock(h);
    return added;
}

/* ------------------------------------------------------------------ */
/* Toggle cache                                                        */
/* ------------------------------------------------------------------ */
static const int  kToggleCacheSlots = 4;
ToggleCacheEntry* gToggleCache      = NULL;
int               gToggleCacheCount = 0;

static int ToggleCacheIndexOf(const char* id) {
    for (int i = 0; i < gToggleCacheCount; i++)
        if (memcmp(gToggleCache[i].id, id, 37) == 0) return i;
    return -1;
}

static void ToggleCacheDropAt(int idx) {
    if (gToggleCache[idx].rows) DisposeHandle(gToggleCache[idx].rows);
    memmove(&gToggleCache[idx], &gToggleCache[idx + 1],
            sizeof(ToggleCacheEntry) * (gToggleCacheCount - idx - 1));
    gToggleCacheCount--;
}

/* Store toggle id's rows (replacing any earlier copy); the oldest entry
   makes room when all slots are taken.  nil rows: nothing is cached. */
static void ToggleCacheStore(const char* id, Handle rows, int count) {
    int idx = ToggleCacheIndexOf(id);
    if (idx >= 0) ToggleCacheDropAt(idx);
    if (!rows) return;
    if (gToggleCacheCount >= kToggleCacheSlots) ToggleCacheDropAt(kToggleCacheSlots - 1);
    memmove(&gToggleCache[1], &gToggleCache[0], sizeof(ToggleCacheEntry) * gToggleCacheCount);
    gToggleCacheCount++;
    setId(gToggleCache[0].id, id);
    gToggleCache[0].rows  = rows;
    gToggleCache[0].count = count;
}
ToggleCacheEntry* ToggleCacheFind(const char* id) {
    int idx = ToggleCacheIndexOf(id);
    return idx >= 0 ? &gToggleCache[idx] : nil;
}
void ToggleCacheEvict(const char* id) {
    int idx = ToggleCacheIndexOf(id);
    if (idx >= 0) ToggleCacheDropAt(idx);
}
void ToggleCacheClear() {
    while (gToggleCacheCount > 0) ToggleCacheDropAt(gToggleCacheCount - 1);
}
bool ToggleCacheHasRecreate() {
    static RowHdr hd;   /* static: fields re-read from memory (see PackRows) */
    for (int e = 0; e < gToggleCacheCount; e++) {
        if (!gToggleCache[e].rows) continue;
        const char* p = *gToggleCache[e].rows;   /* nothing here moves memory */
        for (int i = 0; i < gToggleCache[e].count; i++) {
            memcpy(&hd, p, sizeof hd);
            if (hd.recreate && hd.id[0]) return true;
            p += sizeof hd + hd.textLen + hd.runCount * sizeof(TextRun) + hd.emojiCount * 32;
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Shared micro-helpers                                                */
/* ------------------------------------------------------------------ */

bool HasSel() {
    return blockSelect >= 0 && blockSelect < pageElmCount;
}

/* syncTextActive — sync textActive to blockSelect without activating */
void syncTextActive() {
    textActive = (HasSel())
               ? pageElm[blockSelect].textEdit : nil;
}

/* activateTextActive — sync + TEActivate + cursor to position 0 */
static inline void activateTextActive() {
    syncTextActive();
    if (textActive) { TEActivate(textActive); TESetSelect(0, 0, textActive); }
}

/* reindexBlocks — update every Block.pos to match its array position */
void reindexBlocks() {
    for (int i = 0; i < pageElmCount; i++) pageElm[i].pos = i;
}

/* eraseBlockAt — remove one element from pageElm[], shifting tail left */
void eraseBlockAt(int idx) {
    if (idx < pageElmCount) {
        memmove(&pageElm[idx], &pageElm[idx+1], sizeof(Block)*(pageElmCount-idx-1));
        pageElmCount--;
    }
}
/* eraseBlocks — n rows in ONE tail shift.  A Block is
   ~4 KB: shifting the tail once per row costs n × tail × 4 KB (seconds
   on a 68030 for a toggle with children), once costs tail × 4 KB.       */
int FindSerial(uint32_t serial) {
    if (serial == 0) return -1;
    for (int k = 0; k < pageElmCount; k++)
        if (pageElm[k].serial == serial) return k;
    return -1;
}

static uint32_t sUndoSerial = 0;       /* the block the snapshot belongs to */
static bool     sUndoTyping = false;   /* the step is typing that may go on */

void ClearUndo() { sUndoSerial = 0; sUndoTyping = false; }

int UndoBlockIdx() { return FindSerial(sUndoSerial); }

void UndoBegin(int idx, bool typing) {
    if (idx < 0 || idx >= pageElmCount) return;
    const Block& b = pageElm[idx];
    if (typing && sUndoTyping && sUndoSerial == b.serial) return;   /* same step */
    memcpy(gUndo.text, b.text, b.textLen + 1); gUndo.textLen = b.textLen;
    memcpy(gUndo.emojiSeqs, b.emojiSeqs, 32 * b.emojiCount); gUndo.emojiCount = b.emojiCount;
    memcpy(gUndo.runs, b.runs, sizeof(TextRun) * b.runCount); gUndo.runCount = b.runCount;
    sUndoSerial = b.serial;
    sUndoTyping = typing;
}

void UndoSwap(int idx) {
    /* Swapped in place, a field at a time: no second ~4 KB copy */
    Block& b = pageElm[idx];
    int n = (b.textLen > gUndo.textLen ? b.textLen : gUndo.textLen) + 1;
    for (int i = 0; i < n; i++) { char t = b.text[i]; b.text[i] = gUndo.text[i]; gUndo.text[i] = t; }
    short tl = b.textLen; b.textLen = gUndo.textLen; gUndo.textLen = tl;
    int ne = b.emojiCount > gUndo.emojiCount ? b.emojiCount : gUndo.emojiCount;
    for (int i = 0; i < ne; i++) {
        char t[32];
        memcpy(t, b.emojiSeqs[i], 32);
        memcpy(b.emojiSeqs[i], gUndo.emojiSeqs[i], 32);
        memcpy(gUndo.emojiSeqs[i], t, 32);
    }
    uint8_t ec = b.emojiCount; b.emojiCount = gUndo.emojiCount; gUndo.emojiCount = ec;
    int nr = b.runCount > gUndo.runCount ? b.runCount : gUndo.runCount;
    for (int i = 0; i < nr; i++) std::swap(b.runs[i], gUndo.runs[i]);
    uint8_t rc = b.runCount; b.runCount = gUndo.runCount; gUndo.runCount = rc;
    sUndoTyping = false;   /* typing after this is a new step */
}

void eraseBlocks(int first, int n) {
    if (n <= 0 || first < 0 || first + n > pageElmCount) return;
    memmove(&pageElm[first], &pageElm[first + n], sizeof(Block) * (pageElmCount - first - n));
    pageElmCount -= n;
}
/* insertBlockAt — insert blk at pos, shifting tail right; no-op if full */
void insertBlockAt(int pos, const Block& blk) {
    if (pageElmCount < gMaxBlocks) {
        memmove(&pageElm[pos+1], &pageElm[pos], sizeof(Block)*(pageElmCount-pos));
        pageElm[pos] = blk;
        pageElmCount++;
    }
}

/* commitUIUpdate — updateBlocks + activate selected block (pattern 31) */
void commitUIUpdate() {
    updateBlocks();
    activateTextActive();
}

/* reactivateAt — after updateBlocks(), reacquire textActive and restore cursor to sel */
void reactivateAt(short sel) {
    textActive = (HasSel())
               ? pageElm[blockSelect].textEdit : nil;
    if (textActive) { TEActivate(textActive); TESetSelect(sel, sel, textActive); }
}


/* DirectChildrenEnd — index of the first element that is NOT a direct child
   of pageElm[parentIdx] (one past the last child).  Scans forward while
   isChild is true and parentId matches.  Returns parentIdx+1 if no children
   or parentIdx is out of range. */
int DirectChildrenEnd(int parentIdx) {
    if (parentIdx < 0 || parentIdx >= pageElmCount) return parentIdx + 1;
    const char* pid = pageElm[parentIdx].id;
    int pos = parentIdx + 1;
    while (pos < pageElmCount
           && pageElm[pos].isChild
           && strcmp(pageElm[pos].parentId, pid) == 0)
        pos++;
    return pos;
}

/* handle_write_error — check for non-200 write responses and clear save state.
   403 → read-only mode.  401 → connection dialog (needsReload).
   Returns true if error was handled; caller must return immediately.  */
bool handle_write_error(const HttpResponse& r, uint32_t* recreateSerial) {
    if (!r.Success || r.StatusCode == 200) return false;
    if (r.StatusCode == 403) {
        gAppState.readOnly = true; gAppState.isSaving = false; gAppState.hasDirtyBlocks = false;
        gSaveQueue.clear();
        if (recreateSerial) *recreateSerial = 0;
        return true;
    }
    if (r.StatusCode == 401) {
        gAppState.isSaving = false; gAppState.hasDirtyBlocks = false;
        gSaveQueue.clear();
        if (recreateSerial) *recreateSerial = 0;
        gAppState.reloadIsNetworkError = false;
        gAppState.needsReload = true;
        return true;
    }
    return false;
}

/* Show a two-line label where Save sits for a few seconds.  Only sets
   state — the main loop's updateSaveButton draws it — so it is safe from
   the save thread too. */
void Flash(uint8_t kind, bool beep) {
    if (beep) SysBeep(1);
    gAppState.flashKind  = kind;
    gAppState.flashUntil = TickCount() + kSavedDisplayTicks;
    gAppState.lastSaveButtonTick = 0;   /* draw on the next updateSaveButton */
}
void FlashNotSaved(bool beep)   { Flash(kFlashNotSaved, beep); }
void FlashTooLarge()            { Flash(kFlashTooLarge, true); }
void FlashTooLong(bool readOnly) { Flash(readOnly ? kFlashReadOnly : kFlashTooLong, true); }

int FindBlockIdx(const char* id) {
    if (!id || !id[0]) return -1;
    for (int k = 0; k < pageElmCount; k++)
        if (!pageElm[k].isCont && strcmp(pageElm[k].id, id) == 0) return k;
    return -1;
}

int GroupHead(int idx) {
    while (idx > 0 && idx < pageElmCount && pageElm[idx].isCont) idx--;
    return idx;
}

int GroupEnd(int head) {
    int j = head + 1;
    while (j < pageElmCount && pageElm[j].isCont) j++;
    return j;
}

bool InGroup(int idx) {
    if (idx < 0 || idx >= pageElmCount) return false;
    return pageElm[idx].isCont || (idx + 1 < pageElmCount && pageElm[idx + 1].isCont);
}

void MarkGroupCutShort(int idx, uint8_t why) {
    if (idx < 0 || idx >= pageElmCount) return;
    int h = GroupHead(idx), e = GroupEnd(h);
    for (int i = h; i < e; i++) if (pageElm[i].cutShort > why) why = pageElm[i].cutShort;
    for (int i = h; i < e; i++) pageElm[i].cutShort = why;
}

/* A block that didn't fully load would lose the part we never loaded if
   it were saved: refuse (after the flash) when pageElm[idx] is cutShort. */
static bool RefuseCutShortEdit(int idx) {
    if (!pageElm[idx].cutShort) return false;
    FlashTooLong(pageElm[idx].cutShort == kCutMention);
    return true;
}

bool AllowEdit(int idx, EditKind kind) {
    if (idx < 0 || idx >= pageElmCount) return false;
    if (gAppState.readOnly) { Flash(kFlashReadOnly); return false; }
    bool group = InGroup(idx);
    switch (kind) {
    case kEditMove:
        return true;   /* moveBlock/MoveUnit carry what can't be re-created */
    case kEditDelete:
        /* A whole block goes to the trash as is; one piece of a split
           block re-saves the rest, which must then be all there */
        return !(group && RefuseCutShortEdit(idx));
    case kEditSplit:
        if (RefuseCutShortEdit(idx)) return false;
        /* A new block can only follow a split block's last piece */
        if (idx + 1 < pageElmCount && pageElm[idx + 1].isCont) { FlashTooLong(); return false; }
        return true;
    case kEditToToggle:
        /* A toggle's children load right below its first row, i.e.
           between the pieces of a split block */
        if (group) { FlashTooLong(); return false; }
        /* fall through */
    case kEditRetype:
    case kEditIndent: {
        if (RefuseCutShortEdit(idx)) return false;
        const Block& h = pageElm[GroupHead(idx)];
        if (!isToggleType(h.type) && h.hasChildren) { FlashTooLarge(); return false; }
        return true;
    }
    default:
        return !RefuseCutShortEdit(idx);
    }
}

/* parse_inserted_block_id — read results[0].id from a /children POST
   response and write it into pageElm[elmIdx].id.                      */
void parse_inserted_block_id(HttpResponse& resp, int elmIdx) {
    if (elmIdx < 0 || elmIdx >= pageElmCount) return;
    if (!resp.Success || resp.StatusCode != 200 || resp.Content.empty()) return;
    gason::JsonAllocator alloc; gason::JsonValue root;
    if (gason::jsonParse(&resp.Content[0], root, alloc) != gason::JSON_PARSE_OK) return;
    bool _ok;
    const char* newId = root("results").at(0)("id").toString(&_ok);
    if (newId && newId[0]) setId(pageElm[elmIdx].id, newId);
}

/* ------------------------------------------------------------------ */
/* closeToggleAt — recursively close all open inner toggles, then close
   the toggle at togglePos.  After the call the toggle itself stays in
   pageElm (only its children are removed and cached).                 */
void closeToggleAt(int togglePos) {
    const char* pid = pageElm[togglePos].id;
    /* Close any open child toggles first (innermost first) */
    int scanPos = togglePos + 1;
    while (scanPos < pageElmCount
           && pageElm[scanPos].isChild
           && strcmp(pageElm[scanPos].parentId, pid) == 0) {
        if (pageElm[scanPos].open)
            closeToggleAt(scanPos);   /* recurse; scanPos still valid after (toggle itself not erased) */
        scanPos++;
    }
    /* Remove and cache direct children */
    pageElm[togglePos].open = false;
    int first = togglePos + 1;
    int last  = DirectChildrenEnd(togglePos);
    if (last > first) {
        /* All children, packed; if there's no room for that, nothing is
           cached and reopening reloads them from Notion. */
        ToggleCacheStore(pid, PackRows(first, last - first), last - first);
        for (int i = first; i < last; i++) {
            if (pageElm[i].textEdit) {
                if (textActive == pageElm[i].textEdit) textActive = nil;
                TEDispose(pageElm[i].textEdit); pageElm[i].textEdit = nil;
            }
        }
        // Single memmove instead of per-element erase — §1
        eraseBlocks(first, last - first);
        for (int i = first; i < pageElmCount; i++) pageElm[i].pos = i;
    }
}


/* ------------------------------------------------------------------ */
/* OpenToggleAt                                                        */
/* ------------------------------------------------------------------ */

/* Toggles opened only so a move/retype/indent could load their children:
   closed again once that is saved (RecloseOpenedToggles).  By ID; a
   re-created toggle's entry follows it (RecloseRenameId). */
static char sReclose[16][37];
static int  sRecloseCount = 0;
static bool sRecordOpens  = false;

int OpenToggleAt(int idx) {
    Block* p = &pageElm[idx];
    if (p->open) return 0;
    if (sRecordOpens && sRecloseCount < 16) setId(sReclose[sRecloseCount++], p->id);
    ToggleCacheEntry* entry = ToggleCacheFind(p->id);
    if (entry && pageElmCount + entry->count <= gMaxBlocks) {
        p->open = true;
        int insertPos  = idx + 1;
        UnpackRows(entry->rows, entry->count, insertPos, p->depth + 1);
        for (int j = insertPos; j < pageElmCount; j++) pageElm[j].pos = j;
        return 0;
    }
    if (entry) ToggleCacheEvict(p->id);   /* no room for all of them: load instead */
    if (gAppState.togglePending || gAppState.apiPending || p->id[0] == '\0') return -1;
    p->open = true;
    gAppState.togglePending = true;
    gToggleSerial = p->serial;
    gHttpClient.Get(BlocksURL(p->id) + "/children?page_size=100", afterToggleLoad);
    return 1;
}

/* ------------------------------------------------------------------ */
/* RetypeBlock / FinishPendingRetype                                   */
/* ------------------------------------------------------------------ */
static uint32_t  sPendingRetypeSerial = 0;   /* toggle to retype once loaded */
static BlockType sPendingRetypeType;

int SubtreeEnd(int idx) {
    int end = idx + 1;
    while (end < pageElmCount && pageElm[end].isChild && pageElm[end].depth > pageElm[idx].depth) end++;
    return end;
}

/* One past the sibling unit starting at k: its split pieces and subtree */
static int UnitEnd(int k) {
    int end = k + 1;
    while (end < pageElmCount &&
           (pageElm[end].isCont ||
            (pageElm[end].isChild && pageElm[end].depth > pageElm[k].depth))) end++;
    return end;
}

static bool SameParent(const Block& a, const Block& b) {
    return a.isChild == b.isChild && (!a.isChild || strcmp(a.parentId, b.parentId) == 0);
}

/* Load every level under toggle idx (opening closed nested toggles from
   the cache or Notion) and check all of it can be re-created.  Returns
   one past the subtree, or -1 after a beep (load failed) or a flash
   (content we can't carry: nested blocks under a non-toggle, a toggle
   whose children didn't all load, a block that didn't fully load).     */
static int PrepareSubtree(int idx, bool quiet = false) {
    sRecordOpens = true;
    /* Each load would relay out the whole page; the caller does that once
       when it is done (every path out of here and of the callers does). */
    gRelayoutHold++;
    for (int i = idx; i < SubtreeEnd(idx); i++) {
        Block& c = pageElm[i];
        if (isToggleType(c.type) && !c.open && (c.hasChildren || ToggleCacheFind(c.id))) {
            if (OpenToggleAt(i) < 0) {   /* the load completes in here */
                gRelayoutHold--;
                sRecordOpens = false;
                if (!quiet) SysBeep(1);
                updateBlocks(); return -1;
            }
        }
    }
    gRelayoutHold--;
    sRecordOpens = false;
    int end = SubtreeEnd(idx);
    for (int i = idx; i < end; i++) {
        Block& c = pageElm[i];
        /* The root too: a block that didn't fully load is never re-created
           (doSave skips it), so a move that chose it would never reach Notion */
        if (c.cutShort) {
            if (!quiet) RefuseCutShortEdit(i);
            updateBlocks(); return -1;
        }
        if (isToggleType(c.type)) {
            bool kids = i + 1 < pageElmCount && pageElm[i + 1].isChild && pageElm[i + 1].depth > c.depth;
            if (c.hasChildren && !kids) { c.open = false; if (!quiet) SysBeep(1); updateBlocks(); return -1; } /* load failed */
            if (c.kidsPartial) { if (!quiet) FlashTooLarge(); updateBlocks(); return -1; }
        } else if (c.hasChildren) {
            if (!quiet) FlashTooLarge();   /* nested blocks we never load */
            updateBlocks(); return -1;
        }
    }
    return end;
}

/* The unit [s, e) is in pageElm as a whole and can be re-created as is,
   without loading anything first. */
static bool UnitLoaded(int s, int e) {
    for (int i = s; i < e; i++) {
        const Block& c = pageElm[i];
        if (c.cutShort || c.kidsPartial) return false;
        if (isToggleType(c.type)) {
            if (!c.open && (c.hasChildren || ToggleCacheFind(c.id))) return false;
        } else if (c.hasChildren) {
            return false;
        }
    }
    return true;
}

/* Re-home the subtree of toggle idx for its change to newType: out one
   level (toggle → other, saved first) or kept inside (toggle → toggle).
   Every block in it is re-created; descendants follow their parent's new
   ID.  Returns false after a beep/flash when it can't all come along.  */
static bool MoveChildrenForRetype(int idx, BlockType newType) {
    int end = PrepareSubtree(idx);
    if (end < 0) return false;
    const Block& t = pageElm[idx];
    bool toToggle = isToggleType(newType);
    for (int i = idx + 1; i < end; i++) {
        Block& c = pageElm[i];
        if (!toToggle) {
            if (strcmp(c.parentId, t.id) == 0) {   /* direct child: to t's own parent */
                c.isChild = t.isChild;
                setId(c.parentId, t.parentId);
                c.leftParent = true;
            }
            c.depth--;
            c.invalidateTE();
        }
        if (c.isCont) continue;   /* a split block saves as one, via its head */
        c.recreate = true;     /* re-created under its (new) parent */
        c.saveFirst   = !toToggle;
        c.dirty       = true;
    }
    if (!toToggle) { pageElm[idx].hasChildren = false; pageElm[idx].kidsLeft = true; }
    return true;
}

void RetypeBlock(int idx, BlockType newType) {
    if (idx <= 0 || idx >= pageElmCount) return;
    /* A split block changes type as a whole: its head is re-created with
       all pieces' text */
    if (pageElm[GroupHead(idx)].type == newType) return;
    if (!AllowEdit(idx, isToggleType(newType) ? kEditToToggle : kEditRetype)) return;
    idx = GroupHead(idx);
    Block& b = pageElm[idx];

    if (isToggleType(b.type)) {
        /* Its children must be loaded before they can be moved */
        if (!b.open && (b.hasChildren || ToggleCacheFind(b.id))) {
            /* Remember the change BEFORE opening: the load may complete
               inside OpenToggleAt (Get runs the request to the end), and
               afterToggleLoad → FinishPendingRetype then applies it there. */
            sPendingRetypeSerial = b.serial;
            sPendingRetypeType   = newType;
            sRecordOpens = true;
            int r = OpenToggleAt(idx);
            sRecordOpens = false;
            if (r > 0) { updateBlocks(); return; }   /* applied already, or once loaded */
            sPendingRetypeSerial = 0;                 /* from the cache: carry on here */
            if (r < 0) { SysBeep(1); return; }
        }
        if (gAppState.togglePending) { SysBeep(1); return; }
        if (!MoveChildrenForRetype(idx, newType)) { updateBlocks(); return; }
    }

    Block& nb      = pageElm[idx];
    nb.type        = newType;
    nb.open        = isToggleType(newType);
    nb.dirty       = true;
    nb.recreate = true;
    nb.runsDirty   = true;
    int groupEnd = GroupEnd(idx);
    for (int k = idx + 1; k < groupEnd; k++) pageElm[k].type = newType;   /* its pieces */
    MarkDirty();
    updateBlocks();
    if (HasSel())
        textActive = pageElm[blockSelect].textEdit;
    if (sRecloseCount > 0) doSave();   /* the sooner it's saved, the sooner they close */
}

void RecloseRenameId(const char* oldId, const char* newId) {
    for (int r = 0; r < sRecloseCount; r++)
        if (strcmp(sReclose[r], oldId) == 0) setId(sReclose[r], newId);
}

void RecloseOpenedToggles() {
    if (sRecloseCount == 0) return;
    if (gAppState.busy() || gAppState.hasDirtyBlocks) return;   /* not saved yet */
    bool any = false;
    for (int r = 0; r < sRecloseCount; r++) {
        int i = FindBlockIdx(sReclose[r]);
        if (i < 0 || !isToggleType(pageElm[i].type) || !pageElm[i].open) continue;
        int end = SubtreeEnd(i);
        if (blockSelect > i && blockSelect < end) continue;   /* working inside: leave it open */
        bool dirty = false;
        for (int k = i; k < end; k++) if (pageElm[k].dirty || pageElm[k].recreate) dirty = true;
        if (dirty) continue;
        closeToggleAt(i);
        if (blockSelect >= end) blockSelect -= end - (i + 1);   /* rows above it went */
        any = true;
    }
    sRecloseCount = 0;
    /* (closeToggleAt clears undo itself when the undo block shifts) */
    if (any) { updateBlocks(); syncTextActive(); }
}

bool ToggleHasChildren(int idx) {
    const Block& t = pageElm[idx];
    if (!isToggleType(t.type)) return false;
    if (t.hasChildren || SubtreeEnd(idx) > idx + 1) return true;
    return !t.open && ToggleCacheFind(t.id) != nullptr;
}

void MoveUnit(int idx, int dir, MoveHook beforeMove) {
    int end = UnitEnd(idx);   /* the block, its pieces and any loaded subtree */
    int nS, nE;               /* the sibling unit it moves past */
    if (dir < 0) {
        const Block& t = pageElm[idx];
        int j = idx - 1;
        while (j >= 1 && !(pageElm[j].depth == t.depth && SameParent(pageElm[j], t) && !pageElm[j].isCont)) {
            if (pageElm[j].depth < t.depth) { j = 0; break; }   /* reached the parent */
            j--;
        }
        if (j < 1) { SysBeep(1); return; }
        nS = j; nE = idx;
    } else {
        if (end >= pageElmCount || pageElm[end].depth != pageElm[idx].depth ||
            !SameParent(pageElm[end], pageElm[idx])) {
            SysBeep(1); return;   /* last of its parent: no sibling below */
        }
        nS = end; nE = UnitEnd(end);
    }

    /* A swap only needs ONE side re-created in Notion.  Prefer the
       neighbour: then this block (a closed toggle, however big) stays
       put and nothing has to load.  Loading is the last resort. */
    bool recreateNeighbour;
    bool loaded = false;   /* rows were loaded: rects are stale until relayout */
    if (UnitLoaded(nS, nE)) {
        recreateNeighbour = true;
    } else if (UnitLoaded(idx, end)) {
        recreateNeighbour = false;
    } else {
        loaded = true;
        /* Loads insert rows: re-find both units (nS / idx stay put) */
        bool ok = false;
        recreateNeighbour = true;
        if (isToggleType(pageElm[nS].type)) ok = PrepareSubtree(nS, true) >= 0;
        if (dir < 0) { nE = UnitEnd(nS); idx = nE; end = UnitEnd(idx); }
        else         { end = UnitEnd(idx); nS = end; nE = UnitEnd(nS); }
        if (!ok && isToggleType(pageElm[idx].type)) {
            ok = PrepareSubtree(idx, true) >= 0;
            recreateNeighbour = false;
            if (dir < 0) { end = UnitEnd(idx); }
            else         { end = UnitEnd(idx); nS = end; nE = UnitEnd(nS); }
        }
        if (!ok) {   /* neither side can be carried */
            /* Loading the neighbour above may have put rows above this block:
               keep the selection on it (idx was re-found after the loads) */
            blockSelect = idx;
            FlashTooLarge();
            updateBlocks();
            syncTextActive();
            return;
        }
    }

    if (beforeMove && !loaded) beforeMove(idx, end, nS, nE, dir);
    int lenT = end - idx, lenN = nE - nS;
    int newIdx, rS, rE;   /* new position; range to re-create */
    if (dir < 0) {
        std::rotate(pageElm + nS, pageElm + idx, pageElm + end);   /* this unit, then the neighbour */
        newIdx = nS;
        rS = recreateNeighbour ? nS + lenT : nS;
        rE = recreateNeighbour ? end       : nS + lenT;
    } else {
        std::rotate(pageElm + idx, pageElm + end, pageElm + nE);   /* the neighbour, then this unit */
        newIdx = idx + lenN;
        rS = recreateNeighbour ? idx        : newIdx;
        rE = recreateNeighbour ? idx + lenN : nE;
    }
    reindexBlocks();
    /* Re-created in index order: a parent before its children, which pick
       up its new ID in afterRecreateInsert. */
    for (int i = rS; i < rE; i++) {
        if (pageElm[i].isCont) continue;
        pageElm[i].recreate = true;
        pageElm[i].dirty       = true;
    }
    MarkDirty();
    blockSelect = newIdx;
    updateBlocks();
    syncTextActive();
    if (sRecloseCount > 0) doSave();   /* the sooner it's saved, the sooner they close */
}

void FinishPendingRetype(uint32_t toggleSerial, bool loaded) {
    if (sPendingRetypeSerial == 0) return;
    uint32_t serial = sPendingRetypeSerial;
    sPendingRetypeSerial = 0;
    if (serial != toggleSerial) return;
    int idx = FindSerial(serial);
    if (idx < 0) return;
    if (!loaded) { SysBeep(1); return; }
    RetypeBlock(idx, sPendingRetypeType);
}

/* ------------------------------------------------------------------ */
/* doAddBlock — insert a new empty block after blockSelect            */
/* ------------------------------------------------------------------ */
void doAddBlock(int typeIdx) {
    /* Only user-insertable body types 0..14 (BT_PARAGRAPH..BT_DIVIDER);
       rejects BT_TITLE(15) and BT_CHILD_PAGE(16), which aren't insertable. */
    if (typeIdx < 0 || typeIdx >= 15) return;
    if (gCurRequest < kPhaseFullyLoaded || (pageElmCount == 0)) return;
    if (gAppState.readOnly) return;
    if (gAppState.busy()) return;

    int insertPos = (HasSel())
                  ? blockSelect + 1 : pageElmCount;
    const char* afterId = "";
    bool atStart = false;
    if (HasSel() && pageElm[blockSelect].type == BT_TITLE) {
        /* Title selected: insert at position 1 using position:start */
        atStart = true;
    } else if (HasSel() && pageElm[blockSelect].type != BT_TITLE) {
        afterId = pageElm[blockSelect].id;
    }

    const char* parentBlockId = EffPageID();
    bool   newIsChild    = false;
    int    newDepth      = 0;
    char   newParentId[37]; newParentId[0] = '\0';

    if (HasSel()) {
        const Block& cur = pageElm[blockSelect];
        bool curIsToggle = isToggleType(cur.type);

        if (curIsToggle && cur.open) {
            /* Enter on an OPEN toggle header: create a child block at the
               top of the toggle's children using position:start. */
            newIsChild    = true;
            newDepth      = cur.depth + 1;
            SafeStrCopy(newParentId, 37, cur.id);
            parentBlockId = cur.id;
            insertPos     = blockSelect + 1;
            afterId       = "";
            atStart       = true;
        } else if (curIsToggle && !cur.open) {
            /* Enter on a CLOSED toggle: insert after the entire hidden subtree
               at the same level — do not make the new block a child. */
            insertPos = DirectChildrenEnd(blockSelect);
            afterId   = pageElm[blockSelect].id;
            atStart   = false;
        } else if (cur.isChild && cur.parentId[0] != '\0') {
            /* Inside a toggle: inherit the same parent so new block stays
               in the same toggle at the same depth. */
            newIsChild    = true;
            newDepth      = cur.depth;
            SafeStrCopy(newParentId, 37, cur.parentId);
            parentBlockId = cur.parentId;
        } else if (cur.type != BT_TITLE) {
            /* Page-level block: skip past any open children (e.g. an open
               toggle at page level) so the new block lands after them. */
            insertPos = DirectChildrenEnd(blockSelect);
        }
    }

    /* Never land between the pieces of a split block */
    while (insertPos < pageElmCount && pageElm[insertPos].isCont) insertPos++;

    Block b;
    b.pos      = insertPos;
    b.type     = (BlockType)typeIdx;
    b.isChild  = newIsChild;
    b.depth    = newDepth;
    b.open     = isToggleType(b.type);  /* new toggles start expanded */
    setId(b.parentId, newParentId);
    int prevCount = pageElmCount;
    insertBlockAt(insertPos, b);
    if (pageElmCount == prevCount) {
        DebugLog("doAddBlock", "array full, insert skipped");
        return;
    }
    for (int i = insertPos + 1; i < pageElmCount; i++)
        pageElm[i].pos = i;

    blockSelect  = insertPos;
    gAddSerial   = pageElm[insertPos].serial;
    gAppState.apiPending  = true;   /* lock out save until we have the block's ID */

    /* If adding at the very end while scrolled to 100%, stay at 100% after
       layout so the new block is always visible at the bottom. */
    bool addingAtEnd = (insertPos == pageElmCount - 1);
    bool wasAtBottom = addingAtEnd && gScrollBar != nil
                       && GetControlValue(gScrollBar) >= GetControlMaximum(gScrollBar);

    updateBlocks();

    if (wasAtBottom) {
        short winBottom = gWindow->portRect.bottom;
        short maxScroll = gTotalContentHeight - winBottom;
        if (maxScroll < 0) maxScroll = 0;
        if (gScrollOffset != maxScroll) {
            gScrollOffset = maxScroll;
            updateBlocks();
        }
    }

    activateTextActive();

    /* Demo/read-only: no API key configured — assign a local fake ID so
       nothing downstream treats the block as ID-less, then bail out. */
    if (!gSettings.apiKey[0]) {
        static int sLocalId = 0;
        char fakeId[37];
        snprintf(fakeId, 37, "local%031d", ++sLocalId);
        setId(pageElm[insertPos].id, fakeId);
        gAddSerial = 0;
        gAppState.apiPending = false;
        return;
    }

    BlockType type = (BlockType)typeIdx;
    string blockContent, body;
    build_block_json(type, nullptr, false, blockContent);
    build_children_body(blockContent, afterId, atStart, body);

    runApiThread(BlocksURL(parentBlockId) + "/children", body, afterAddInsert, true /* keepBody: need ID from response */);
}

/* ------------------------------------------------------------------ */
/* doDeleteBlock                                                       */
/* ------------------------------------------------------------------ */
void doDeleteBlock() {
    if (gCurRequest < kPhaseFullyLoaded || (pageElmCount == 0)) return;
    if (gAppState.busy()) return;
    if (blockSelect <= 0 || blockSelect >= pageElmCount) return;
    if (pageElm[blockSelect].type == BT_TITLE) return;
    if (!AllowEdit(blockSelect, kEditDelete)) return;

    /* One piece of a split block: drop just that text locally; the rest
       of the Notion block is re-saved without it (no trash call). */
    if (InGroup(blockSelect)) {
        bool wasHead = !pageElm[blockSelect].isCont;
        Block& pb = pageElm[blockSelect];
        if (pb.textEdit != nil) {
            if (textActive == pb.textEdit) textActive = nil;
            TEDispose(pb.textEdit); pb.textEdit = nil;
        }
        eraseBlockAt(blockSelect);
        /* The next piece takes over as head */
        if (wasHead) pageElm[blockSelect].isCont = false;
        int head = GroupHead(wasHead ? blockSelect : blockSelect - 1);
        pageElm[head].dirty = true;
        MarkDirty();
        for (int i = blockSelect; i < pageElmCount; i++)
            pageElm[i].pos = i;
        if (blockSelect >= pageElmCount)
            blockSelect = pageElmCount - 1;
        updateBlocks();
        if (HasSel()) {
            textActive = pageElm[blockSelect].textEdit;
            if (textActive != nil) TESetSelect(0, 0, textActive);
        }
        return;
    }

    /* The block and, for an open toggle, its loaded children: Notion trashes
       them together, so they leave the screen together. */
    int first = blockSelect;
    int last  = isToggleType(pageElm[first].type) ? SubtreeEnd(first) : first + 1;
    int n     = last - first;
    string blockId = pageElm[first].id;
    bool isChildPage = (pageElm[first].type == BT_CHILD_PAGE);
    /* Blocks added in demo mode carry a fake ID: nothing to trash in Notion */
    bool remote = !blockId.empty() && strncmp(blockId.c_str(), "local", 5) != 0;

    /* Keep a copy so a delete Notion doesn't confirm can be put back */
    if (remote) KeepDeletedCopy(first, n);

    /* Clear textActive before TEDispose so updateBlocks() never dereferences a freed handle */
    for (int i = first; i < last; i++) {
        Block& b = pageElm[i];
        if (b.textEdit != nil) {
            if (textActive == b.textEdit) textActive = nil;
            TEDispose(b.textEdit); b.textEdit = nil;
        }
    }

    eraseBlocks(first, n);
    for (int i = blockSelect; i < pageElmCount; i++)
        pageElm[i].pos = i;
    if (blockSelect >= pageElmCount)
        blockSelect = pageElmCount - 1;

    updateBlocks();
    if (HasSel()) {
        textActive = pageElm[blockSelect].textEdit;
        if (textActive != nil) {
            TESetSelect(0, 0, textActive);
        }
    }

    if (remote) {
        gAppState.apiPending = true;
        ToggleCacheEvict(blockId.c_str());
        /* child_page blocks: archive via Pages endpoint (not Blocks) */
        string url = isChildPage ? PagesURL(blockId.c_str()) : BlocksURL(blockId.c_str());
        runApiThread(url, "{\"in_trash\":true}", afterDeleteArchive);
        if (!gAppState.saveThreadActive) {   /* thread didn't start: nothing was sent */
            RestoreDeletedBlocks();
            FlashNotSaved();
        }
    }
}

/* ------------------------------------------------------------------ */
/* KeepDeletedCopy / RestoreDeletedBlocks — undo an unconfirmed delete */
/* ------------------------------------------------------------------ */
static Handle   sDelCopy  = nil;
static int      sDelCount = 0;
static int      sDelIdx   = -1;
static uint32_t sDelAfter = 0;   /* the row above them: they go back below it */

void KeepDeletedCopy(int first, int n) {
    DropDeletedCopy();
    sDelCopy = PackRows(first, n);
    if (!sDelCopy) return;   /* no room for a copy: delete still goes ahead */
    sDelCount = n;
    sDelIdx   = first;
    sDelAfter = (first > 0) ? pageElm[first - 1].serial : 0;
}

void DropDeletedCopy() {
    if (sDelCopy) DisposeHandle(sDelCopy);
    sDelCopy = nil; sDelCount = 0; sDelIdx = -1; sDelAfter = 0;
}

bool RestoreDeletedBlocks() {
    if (!sDelCopy || sDelCount == 0) return false;
    int above = FindSerial(sDelAfter);
    int at = (above >= 0) ? above + 1 : sDelIdx;
    if (at < 1) at = 1;
    if (at > pageElmCount) at = pageElmCount;
    int added = UnpackRows(sDelCopy, sDelCount, at, -1);
    reindexBlocks();
    DropDeletedCopy();
    /* The selection is an index: rows went back in at or above it, so it
       moves down with its block (textActive, a handle, already did). */
    if (blockSelect >= at) blockSelect += added;
    updateBlocks();
    syncTextActive();
    return true;
}

/* ------------------------------------------------------------------ */
/* doIndentBlock — make current block a child of the toggle above     */
/* ------------------------------------------------------------------ */
void doIndentBlock() {
    if (blockSelect <= 0 || blockSelect >= pageElmCount) return;
    if (gAppState.busy()) return;
    if (pageElm[blockSelect].type == BT_TITLE) return;
    if (!AllowEdit(blockSelect, kEditIndent)) return;
    /* A split block goes in whole: work from its head piece */
    int self = GroupHead(blockSelect);

    /* Find the toggle directly above: either the block itself is a toggle,
       or the block above is a child of a toggle (use that toggle as parent). */
    int parentIdx = -1;
    if (self > 0) {
        Block& above = pageElm[self - 1];
        BlockType at = above.type;
        if (isToggleType(at)) {
            parentIdx = self - 1;
        } else if (above.isChild && above.parentId[0] != '\0') {
            for (int i = self - 2; i >= 0; i--) {
                if (strcmp(pageElm[i].id, above.parentId) == 0) { parentIdx = i; break; }
            }
        }
    }
    if (parentIdx < 0 || pageElm[parentIdx].id[0] == '\0') return;

    /* Into a closed toggle: load its children first, so they show (and a
       later retype or move sees them all); the block then goes in last. */
    if (!pageElm[parentIdx].open &&
        (pageElm[parentIdx].hasChildren || ToggleCacheFind(pageElm[parentIdx].id))) {
        SyncActiveBlock();
        int before = pageElmCount;
        if (OpenToggleAt(parentIdx) < 0) { SysBeep(1); return; }
        blockSelect += pageElmCount - before;   /* children landed above the block */
        self        += pageElmCount - before;
        if (pageElm[parentIdx].hasChildren && pageElmCount == before) {
            pageElm[parentIdx].open = false;     /* load failed */
            SysBeep(1); updateBlocks(); return;
        }
    }

    /* §1: Capture parent data before modifying the array */
    char   parentId[37]; SafeStrCopy(parentId, 37, pageElm[parentIdx].id);
    int    parentDepth = pageElm[parentIdx].depth;

    /* A split block: every piece moves in with it; the head is re-created
       (with all pieces' text) under the toggle.  Like a toggle subtree it
       already sits right after the toggle's last descendant. */
    if (InGroup(self)) {
        SyncActiveBlock();
        int end = GroupEnd(self);
        for (int k = self; k < end; k++) {
            Block& c = pageElm[k];
            c.isChild = true;
            setId(c.parentId, parentId);
            c.depth   = parentDepth + 1;
            c.invalidateTE();
        }
        pageElm[self].recreate   = true;
        pageElm[self].leftParent = true;
        pageElm[self].dirty       = true;
        pageElm[parentIdx].open   = true;
        ToggleCacheEvict(parentId);
        MarkDirty();
        commitUIUpdate();
        if (sRecloseCount > 0) doSave();
        return;
    }

    /* A toggle with children goes in whole.  It already sits right after
       its new parent's last descendant, so only levels change. */
    if (ToggleHasChildren(blockSelect)) {
        SyncActiveBlock();
        int end = PrepareSubtree(blockSelect);
        if (end < 0) return;
        int delta = parentDepth + 1 - pageElm[blockSelect].depth;
        Block& root = pageElm[blockSelect];
        root.isChild = true;
        setId(root.parentId, parentId);
        root.leftParent = true;
        for (int i = blockSelect; i < end; i++) {
            Block& c = pageElm[i];
            c.depth += delta;
            c.invalidateTE();
            if (c.isCont) continue;
            c.recreate = true;   /* re-created on save, root first */
            c.dirty       = true;
        }
        pageElm[parentIdx].open = true;
        ToggleCacheEvict(parentId);
        MarkDirty();
        commitUIUpdate();
        if (sRecloseCount > 0) doSave();
        return;
    }
    pageElm[parentIdx].open = true;

    /* Sync TE text into the block before moving */
    Block& blk = pageElm[blockSelect];
    if (textActive && blk.textEdit == textActive) {
        SyncBlockText(blk);
        textActive = nil;
    }

    /* Find where to insert: after the last existing child of this toggle.
       Must be done BEFORE setting isChild=true, or the block counts itself. */
    int insertPos = SubtreeEnd(parentIdx);   /* after nested levels too */

    /* Update child attributes before copying the block */
    blk.isChild  = true;
    blk.depth    = parentDepth + 1;
    setId(blk.parentId, parentId);

    /* Move block locally — dispose old TE so addTE recreates it at the
       correct new indent position (fixes left-alignment after move). */
    Block moved = blk;
    moved.invalidateTE();
    eraseBlockAt(blockSelect);
    if (insertPos > blockSelect) insertPos--;
    insertBlockAt(insertPos, moved);
    reindexBlocks();
    blockSelect = insertPos;

    /* Re-created as a child on save */
    beginIndentOperation(blockSelect, parentId, parentDepth + 1);

    commitUIUpdate();
}

/* ------------------------------------------------------------------ */
/* doInsertPageLink — open page picker and insert a mention link run   */
/* at the current cursor position in the active text block.           */
/* ------------------------------------------------------------------ */
void doInsertPageLink() {
    if (!textActive || blockSelect < 0 || blockSelect >= pageElmCount) return;
    Block& blk = pageElm[blockSelect];
    if (blk.type == BT_DIVIDER || blk.type == BT_CHILD_PAGE) return;

    /* Title selected: move focus to first body block so the link lands below */
    if (blk.type == BT_TITLE) {
        int next = blockSelect + 1;
        if (next >= pageElmCount) return; /* no body blocks yet */
        BlockType nt = pageElm[next].type;
        if (nt == BT_DIVIDER || nt == BT_CHILD_PAGE) return;
        if (textActive) TEDeactivate(textActive);
        blockSelect = next;
        textActive  = pageElm[next].textEdit;
        if (!textActive) return;
        TEActivate(textActive);
    }

    /* Re-acquire after possible BT_TITLE redirect */
    Block& blk2 = pageElm[blockSelect];
    if (!AllowEdit(blockSelect, kEditText)) return;

    /* Snapshot cursor before the picker destroys/rebuilds TE handles */
    short caretPos = (*textActive)->selStart;

    /* Sync runs with current TE content so splits are accurate */
    blk2.runsDirty = true;
    RebuildRunsFromTE(blk2);

    prepareForPageList();
    NotionPageEntry picked; memset(&picked, 0, sizeof(picked));
    bool ok = ShowPagePickerForLink(EffAPIKey(), DoFetchPages, DoFetchPageChildren, picked, DoLibMovePage);
    SetListEmojiGWorld(nil);

    if (!ok || !picked.id[0]) { doPagePickerCancel(); return; }

    /* prepareForPageList may have wiped TE handles — restore first */
    doPagePickerCancel();

    /* Re-acquire block reference (doPagePickerCancel may have called updateBlocks) */
    if (blockSelect < 0 || blockSelect >= pageElmCount) return;
    Block& b2 = pageElm[blockSelect];

    /* §1: Map TE cursor position → char position in b2.text (emoji take
       kEmojiTECells TE cells) */
    int textPos = TextIndexForTE(b2.text, b2.textLen, caretPos);

    /* §1: Build the link TextRun with char array url */
    const char* linkTitle = picked.title[0] ? picked.title : picked.id;
    short linkTitleLen = (short)strlen(linkTitle);
    TextRun linkRun;
    linkRun.underline = false; /* ApplyRunStyles adds underline for non-empty url */
    strncpy(linkRun.url, "http://notioncl/page/", kMaxUrlLen-1);
    strncat(linkRun.url, picked.id, kMaxUrlLen-1-(int)strlen(linkRun.url));
    linkRun.url[kMaxUrlLen-1]='\0';

    /* §1: Find which run the insertion point falls in (and whether it
       must be split) before touching anything */
    int runCharPos = 0;
    int insertAt   = (int)b2.runCount; /* default: append after all runs */
    int splitRun   = -1, splitOff = 0;
    for (int ri = 0; ri < b2.runCount; ri++) {
        int rlen = (int)b2.runs[ri].textLen;
        if (textPos <= runCharPos + rlen) {
            int off = textPos - runCharPos;
            if (off == 0)         insertAt = ri;       /* before this run */
            else if (off == rlen) insertAt = ri + 1;   /* after this run */
            else if (IsMentionRun(b2.runs[ri])) {
                /* Inside a page mention: never split it — the link goes after it */
                textPos  = runCharPos + rlen;
                insertAt = ri + 1;
            }
            else { splitRun = ri; splitOff = off; insertAt = ri + 1; }
            break;
        }
        runCharPos += rlen;
    }

    /* Text and runs must both take the link, or the runs stop matching the
       text and the save falls back to plain text (all formatting lost).
       If it doesn't fit, the link goes into a new paragraph right below. */
    int needRuns = (splitRun >= 0) ? 2 : 1;
    if (b2.textLen + linkTitleLen >= kMaxTextLen || b2.runCount + needRuns > kMaxRuns) {
        int before = pageElmCount;
        if (textActive) TEDeactivate(textActive);
        doAddBlock(0);
        if (pageElmCount == before || !HasSel()) { FlashTooLong(); return; }
        Block& nb = pageElm[blockSelect];
        memcpy(nb.text, linkTitle, linkTitleLen); nb.text[linkTitleLen] = '\0';
        nb.textLen = linkTitleLen;
        linkRun.textOffset = 0; linkRun.textLen = linkTitleLen;
        nb.runs[0] = linkRun; nb.runCount = 1;
        nb.dirty = true;
        MarkDirty();
        nb.invalidateTE();
        updateBlocks();
        if (nb.textEdit != nil) {
            textActive = nb.textEdit;
            TEActivate(textActive);
            TESetSelect(linkTitleLen, linkTitleLen, textActive);
        }
        return;
    }

    if (splitRun >= 0) {
        /* §1: Split the run at splitOff — insert tail after current */
        int ri = splitRun, rlen = (int)b2.runs[ri].textLen;
        memmove(&b2.runs[ri+2], &b2.runs[ri+1],
                sizeof(TextRun)*(b2.runCount - ri - 1));
        TextRun tail = b2.runs[ri];
        b2.runs[ri].textLen = (short)splitOff;
        tail.textOffset    = b2.runs[ri].textOffset + (short)splitOff;
        tail.textLen       = (short)(rlen - splitOff);
        b2.runs[ri].textUrl = 0; tail.textUrl = 0;   /* neither half is the whole URL text */
        b2.runs[ri+1]      = tail;
        b2.runCount++;
    }
    /* §1: Insert linkRun at insertAt */
    if (insertAt < b2.runCount)
        memmove(&b2.runs[insertAt+1], &b2.runs[insertAt],
                sizeof(TextRun)*(b2.runCount - insertAt));
    linkRun.textOffset = (short)textPos;
    linkRun.textLen    = linkTitleLen;
    b2.runs[insertAt]  = linkRun;
    b2.runCount++;
    /* Adjust textOffsets of all runs after insertAt */
    for (int ri = insertAt+1; ri < b2.runCount; ri++)
        b2.runs[ri].textOffset += linkTitleLen;

    /* §1: Insert title text into block text char array (fits: checked above) */
    {
        int remain = b2.textLen - textPos;
        memmove(b2.text + textPos + linkTitleLen, b2.text + textPos, remain + 1);
        memcpy(b2.text + textPos, linkTitle, linkTitleLen);
        b2.textLen += linkTitleLen;
    }
    b2.dirty        = true;
    MarkDirty();
    b2.invalidateTE();
    updateBlocks();

    /* Restore cursor to just after the inserted link */
    if (HasSel() &&
        pageElm[blockSelect].textEdit != nil) {
        textActive = pageElm[blockSelect].textEdit;
        TEActivate(textActive);
        short newCaret = TELenForText(pageElm[blockSelect].text, 0, (short)(textPos + linkTitleLen));
        TESetSelect(newCaret, newCaret, textActive);
    }
}

void doNewPage(const char* parentIdOverride) {
    if (gAppState.apiPending || gCurRequest < kPhaseFullyLoaded) return;
    string body;
    /* parentIdOverride == ""      → workspace root (menu + Library root "+")
       parentIdOverride == "<id>"  → specific page (Library per-row "+") */
    if (parentIdOverride != nullptr && !parentIdOverride[0]) {
        /* Workspace root — only works for OAuth integrations, not internal */
        gNewPageParentIdNH[0] = '\0';
        body = "{\"parent\":{\"type\":\"workspace\",\"workspace\":true},\"properties\":{\"title\":{\"title\":[{\"type\":\"text\",\"text\":{\"content\":\"New Page\"}}]}}}";
    } else {
        const char* pid = nullptr;
        if (parentIdOverride && parentIdOverride[0])
            pid = parentIdOverride;
        else
            pid = gSettings.pageID[0] ? gSettings.pageID : EffPageID();
        if (!pid || !pid[0]) return;
        /* Record no-hyphen parent before hyphenation, so OnNewPageResponse can cache it */
        SafeStrCopy(gNewPageParentIdNH, sizeof(gNewPageParentIdNH), pid);
        /* Convert no-hyphen id to hyphenated UUID for the API */
        char pidH[37] = {0};
        if (parentIdOverride && parentIdOverride[0]) {
            addHyphensToId(parentIdOverride, pidH, sizeof(pidH));
            pid = pidH;
        }
        body = "{\"parent\":{\"type\":\"page_id\",\"page_id\":\""; body += pid;
        body += "\"},\"properties\":{\"title\":{\"title\":[{\"type\":\"text\",\"text\":{\"content\":\"New Page\"}}]}}}";
    }
    gAppState.apiPending = true;
    gHttpClient.Post("https://api.notion.com/v1/pages", body, OnNewPageResponse);
}

