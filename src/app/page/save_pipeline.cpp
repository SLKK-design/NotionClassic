/* save_pipeline.cpp — Block save/patch pipeline and API thread helpers.
   Extracted from main.cpp. */
#include <string.h>
#include <string>
#include <functional>
#include <Threads.h>

#include "save_pipeline.h"
#include "page_cache.h"   /* gPageCacheNeedsSave */
#include "blocks.h"
#include "app_state.h"
#include "helpers.h"
#include "HttpClient.h"
#include "rich_text_json.h"
#include "run_styles.h"
#include "navigation.h"
#include "gason.hpp"
#include "debug_con.h"
#include "picker.h"     /* FreeGraphicsAssets */
#include "response_parser.h" /* ParseRichTextPieces */
#include "block_ops.h"       /* GroupHead/GroupEnd, MarkGroupCutShort */
#include "page_fetch.h"      /* UpdatePageListCacheEntry */

using std::string;
using std::function;

/* ------------------------------------------------------------------ */
/* Externs — globals defined in main.cpp                               */
/* ------------------------------------------------------------------ */
extern Block*        pageElm;
extern int           pageElmCount;
extern int           gMaxBlocks;
extern AppState      gAppState;
extern char          gPageIconName[32];
extern char          gPageIconColor[32];
extern bool          gHasMore;
extern int           gCurRequest;
extern long          gRequestStartTick;
extern WindowPtr     gWindow;
extern HttpClient    gHttpClient;
extern AppSettings   gSettings;
extern SaveQueue     gSaveQueue;
extern uint32_t      gRecreateSerial;
extern uint32_t      gAddSerial;
extern uint32_t      gToggleSerial;
extern ThreadID      gSaveThreadID;
extern int           blockSelect;
extern TEHandle      textActive;
extern const long    kSavedDisplayTicks;

extern const char*   EffPageID();
extern void          updateBlocks();
extern void          syncTextActive();
extern void          eraseBlockAt(int idx);
extern void          insertBlockAt(int pos, const Block& blk);
extern bool          handle_write_error(const HttpResponse& r, uint32_t* recreateSerial);
extern void          parse_inserted_block_id(HttpResponse& resp, int elmIdx);
extern void          DrawStatusText(const char* msg);
extern string        BlocksURL(const char* id);
extern string        PagesURL(const char* id);
extern void          SyncActiveBlock();
extern void          MarkDirty();

/* ------------------------------------------------------------------ */
/* File-static save state (only accessed within save pipeline)        */
/* ------------------------------------------------------------------ */
/* Block(s) whose PATCH is in flight: dirty was cleared when it was sent,
   so a failed response must set it again or the edit is never retried. */
static int  g_patchIdx = -1, g_patchEnd = -1;
/* Beep once per run of failed saves, not on every autosave retry */
static bool g_failBeeped = false;
static void SaveFailed() { FlashNotSaved(!g_failBeeped); g_failBeeped = true; }

/* A run that can't go on now (network, 429, 5xx): stop it, re-show Save
   and let autosave retry one period later (MarkDirty restarts the timer).
   What wasn't saved is still dirty, or still in the deferred list.    */
/* Runs of the block at idx and its split pieces resynced from their TEs
   and marked clean, ready to be written as one block. */
static void PrepareGroup(int idx, int end) {
    for (int k = idx; k < end; k++) {
        RebuildRunsFromTE(pageElm[k]);
        pageElm[k].dirty = false;
    }
}

static void StopSaveForRetry() {
    gSaveQueue.clear();
    gAppState.isSaving = false;
    MarkDirty();
    SaveFailed();
}

/* A refusal that retrying can't fix (400 bad request, 404 gone, 413 too
   large …): that block is skipped so it can't hold up the rest of the
   queue.  Network errors, 408/409/429 and 5xx are worth a retry. */
static bool g_anyRejected = false;   /* this save run skipped a block */
static bool PermanentRefusal(const HttpResponse& r) {
    int sc = r.StatusCode;
    return r.Success && sc >= 400 && sc < 500 && sc != 408 && sc != 409 && sc != 429;
}

static bool g_iconPatchPending  = false;
static bool g_titlePatchPending = false;
static char g_pendingIconName[32];
static char g_pendingTitle[80];
static short g_pendingTitleEmoji[kTitleEmojiMax];

/* API thread state */
static string                        gApiThreadUrl;
static string                        gApiThreadBody;
static function<void(HttpResponse&)> gApiThreadCb;
static bool                          gApiKeepBody = false;

/* Save constants */
static const long kSaveThreadStack = 131072L; /* 128 KB */

/* ------------------------------------------------------------------ */
/* Internal forward declarations                                        */
/* ------------------------------------------------------------------ */
static void patchNext(HttpResponse& response);
void ContinueQueue();
static void ClearTogglePending();
static void afterRecreateInsert(HttpResponse& resp);
static void afterRecreateArchive(HttpResponse& resp);
static OSErr SpawnCoopThread(ThreadEntryUPP fn, long stackBytes, ThreadID* outID);

/* ------------------------------------------------------------------ */
/* ClearTogglePending                                                  */
/* ------------------------------------------------------------------ */
static void ClearTogglePending() {
    gAppState.togglePending = false;
    gToggleSerial = 0;
}

/* ------------------------------------------------------------------ */
/* ContinueQueue                                                       */
/* ------------------------------------------------------------------ */
void ContinueQueue() {
    HttpResponse dummy; dummy.Success = true; dummy.StatusCode = 200;
    patchNext(dummy);
}

/* ------------------------------------------------------------------ */
/* afterAddInsert                                                      */
/* ------------------------------------------------------------------ */
void afterAddInsert(HttpResponse& resp) {
    int addIdx = FindSerial(gAddSerial);
    gAddSerial = 0;
    if (handle_write_error(resp, nullptr)) {
        if (addIdx >= 0) {
            if (pageElm[addIdx].textEdit) {
                if (textActive == pageElm[addIdx].textEdit) textActive = nil;
                TEDispose(pageElm[addIdx].textEdit); pageElm[addIdx].textEdit = nil;
            }
            eraseBlockAt(addIdx);
            if (blockSelect >= pageElmCount) blockSelect = pageElmCount - 1;
            updateBlocks();
        }
        gAppState.apiPending = false;
        return;
    }
    if (addIdx >= 0) parse_inserted_block_id(resp, addIdx);
    gAppState.apiPending = false;
    ContinueQueue();
}

/* ------------------------------------------------------------------ */
/* afterToggleLoad                                                     */
/* ------------------------------------------------------------------ */
/* PieceSink for toggle children: insert at *ctx and advance it */
/* afterToggleLoad parks the rows below the toggle at the end of pageElm
   (one shift), appends the children up to sAppendLimit, then closes the
   gap (one shift) — instead of shifting the whole tail per child.      */
static int sAppendLimit = 0;

static bool InsertPiece(Block& piece, void* ctx) {
    int* at = (int*)ctx;
    if (pageElmCount >= sAppendLimit) return false;
    piece.text[piece.textLen] = '\0';
    piece.pos = pageElmCount;
    pageElm[pageElmCount++] = piece;
    (*at)++;
    return true;
}

void afterToggleLoad(HttpResponse& resp) {
    uint32_t toggleSerial = gToggleSerial;   /* ClearTogglePending resets it */
    int      toggleIdx    = FindSerial(toggleSerial);
    if (!resp.Success || resp.StatusCode != 200 || toggleIdx < 0) {
        if (resp.Success && resp.StatusCode == 401) {
            gAppState.reloadIsNetworkError = false;
            gAppState.needsReload = true;
        }
        ClearTogglePending();
        FinishPendingRetype(toggleSerial, false);
        return;
    }
    gason::JsonAllocator alloc;
    gason::JsonValue root;
    if (gason::jsonParse(&resp.Content[0], root, alloc) != gason::JSON_PARSE_OK) {
        ClearTogglePending();
        FinishPendingRetype(toggleSerial, false);
        return;
    }

    int    insertPos  = toggleIdx + 1;
    /* More than one page of children (we load one), or not all fit */
    bool   partial    = root("has_more").getTag() == gason::JSON_TRUE;
    int    childDepth = pageElm[toggleIdx].depth + 1;
    char   parentId[37]; SafeStrCopy(parentId, 37, pageElm[toggleIdx].id);

    /* Open a gap: park the rows below the toggle at the top of the array */
    int tailLen = pageElmCount - insertPos;
    int tailDst = gMaxBlocks - tailLen;   /* no tail: append up to gMaxBlocks */
    if (tailLen > 0) {
        if (tailDst > insertPos)
            memmove(&pageElm[tailDst], &pageElm[insertPos], sizeof(Block) * tailLen);
        else
            tailDst = insertPos;   /* array full: no room for children */
    }
    pageElmCount = insertPos;
    sAppendLimit = tailDst;

    gason::JsonIterator it = gason::begin(root("results"));
    while (it.isValid()) {
        gason::JsonValue blockNode = it->value;
        bool okLT = false;
        const char* listtype = blockNode.child("type").toString(&okLT);
        if (!okLT || !listtype || listtype[0] == '\0') { it++; continue; }
        Block c;
        ParseBlockMeta(c, blockNode, listtype);
        c.isChild  = true;
        c.depth    = childDepth;
        setId(c.parentId, parentId);
        /* Long blocks load as several pieces (see ParseRichTextPieces) */
        ParseBlockText(c, blockNode, listtype, InsertPiece, &insertPos);
        c.pos = insertPos;
        bool added = pageElmCount < sAppendLimit;
        if (added) { pageElm[pageElmCount++] = c; insertPos++; }
        else       partial = true;
        FinishLoadedBlock(c, added, pageElmCount - 1);
        it++;
    }

    /* Close the gap: bring the parked rows back right after the children */
    if (tailLen > 0 && tailDst != pageElmCount)
        memmove(&pageElm[pageElmCount], &pageElm[tailDst], sizeof(Block) * tailLen);
    pageElmCount += tailLen;

    /* Renumber all positions in one pass after all children are inserted */
    for (int i = toggleIdx + 1; i < pageElmCount; i++)
        pageElm[i].pos = i;
    pageElm[toggleIdx].kidsPartial = partial;

    ClearTogglePending();
    updateBlocks();
    syncTextActive();
    FinishPendingRetype(toggleSerial, true);   /* a retype waiting for these children */
}

/* ------------------------------------------------------------------ */
/* afterDeleteArchive                                                  */
/* ------------------------------------------------------------------ */
void afterDeleteArchive(HttpResponse& resp) {
    gAppState.apiPending = false;
    /* Not confirmed (network error, 4xx/5xx, 401/403): the block still
       exists in Notion, so put it back on screen. */
    if (!resp.Success || resp.StatusCode != 200) {
        DEBUG_LOGF("afterDeleteArchive", "delete failed status=%d", resp.StatusCode);
        handle_write_error(resp, nullptr);   /* 403 → read-only, 401 → reconnect */
        RestoreDeletedBlocks();
        if (!gAppState.readOnly) SaveFailed();
        return;
    }
    DropDeletedCopy();
    ContinueQueue();
}

/* ------------------------------------------------------------------ */
/* afterRecreateInsert / afterRecreateArchive                     */
/* ------------------------------------------------------------------ */
/* Type changes and page-level moves re-create the block (the API can't
   change a block's type in place).  Insert the new block FIRST, under the
   block's real parent, and only trash the old one once the insert is
   acknowledged — a failed insert then leaves Notion untouched instead of
   leaving the block in the trash with the UI saying "Saved".           */
static char g_recreateOldId[37];

/* Old copies of re-created blocks that can't go to the trash yet:
   trashing one takes its old children along, and a block not re-created
   yet (still to send, cut off by the network, or refused) may have its
   only copy in there.  Each waits until none can be (RecreatePendingIn),
   then goes at the end of a save run.  A refused block keeps them
   waiting: a duplicate in Notion at worst, never a block left only in
   the trash.  Cleared when the page unloads, for the same reason.     */
static const int kMaxDeferTrash = 32;
struct DeferredTrash {
    char oldId[37];   /* old copy to trash */
    char newId[37];   /* …and the block that replaced it */
    bool covered;     /* its old copy sits under another deferred old copy
                         and goes along with that one: never trashed itself */
    bool kidsLeft;    /* blocks moved out of it (Block::kidsLeft) */
};
static DeferredTrash g_defer[kMaxDeferTrash];
static int           g_deferTrashCount = 0;

void ClearDeferredTrash() { g_deferTrashCount = 0; }

/* Could the old copy of the block at row x still hold a block that has to
   be re-created?  One in x's subtree can; when blocks moved out of x
   (kidsLeft), so can any block that changed parent (where its copy is,
   we don't know); and for a toggle, one in a closed toggle's cache. */
static bool RecreatePendingIn(int x, bool kidsLeft) {
    int end = SubtreeEnd(x);
    for (int k = 0; k < pageElmCount; k++) {
        const Block& c = pageElm[k];
        if (c.recreate && c.id[0] && ((k > x && k < end) || (kidsLeft && c.leftParent)))
            return true;
    }
    return (end > x + 1 || isToggleType(pageElm[x].type)) && ToggleCacheHasRecreate();
}

/* A deferred old copy that may go now, or -1.  One whose block isn't on
   the page (inside a closed toggle) keeps waiting. */
static int ReadyDeferred() {
    for (int i = g_deferTrashCount - 1; i >= 0; i--) {
        int x = FindBlockIdx(g_defer[i].newId);
        if (x >= 0 && !RecreatePendingIn(x, g_defer[i].kidsLeft)) return i;
    }
    return -1;
}

static void afterDeferredTrash(HttpResponse& resp);

/* Parent and position for re-inserting pageElm[idx]: the nearest earlier
   sibling (same parent), or the start of the parent when there is none. */
static const char* RecreateAnchor(int idx, const char** afterId, bool* atStart) {
    const Block& b = pageElm[idx];
    *afterId = ""; *atStart = true;
    for (int j = idx - 1; j >= 1; j--) {
        const Block& p = pageElm[j];
        if (b.isChild) {
            if (strcmp(p.id, b.parentId) == 0) break;   /* reached the parent: first child */
            if (p.isChild && strcmp(p.parentId, b.parentId) == 0) { *afterId = p.id; *atStart = false; break; }
        } else if (!p.isChild) {
            *afterId = p.id; *atStart = false; break;
        }
    }
    return b.isChild ? b.parentId : EffPageID();
}

static void afterRecreateArchive(HttpResponse& resp) {
    if (handle_write_error(resp, &gRecreateSerial)) return;
    /* New block is already saved; a failed trash only leaves the old copy
       behind as a duplicate, so log it and carry on with the queue. */
    if (!resp.Success || resp.StatusCode != 200)
        DEBUG_LOGF("recreate", "trash old block failed status=%d", resp.StatusCode);
    gRecreateSerial = 0;
    ContinueQueue();
}

static void afterRecreateInsert(HttpResponse& resp) {
    if (handle_write_error(resp, &gRecreateSerial)) return;
    int idx = FindSerial(gRecreateSerial);
    if (PermanentRefusal(resp)) {
        /* Notion won't take this block as built: the old one is still live
           and unchanged.  Put recreate back (patchNext cleared it when
           sending) but leave it clean, so it is retried when the block is
           edited again — otherwise later edits would go out as plain
           PATCHes of the new type to the old block, refused forever. */
        DEBUG_LOGF("recreate", "insert refused status=%d, skipped", resp.StatusCode);
        if (idx >= 0 && idx < pageElmCount) {
            pageElm[idx].recreate = true;    /* patchNext cleared it on send */
            pageElm[idx].dirty       = false;   /* …retried on the next edit */
            pageElm[idx].saveFirst   = false;   /* (keeps old copies waiting) */
        }
        gRecreateSerial = 0;
        g_anyRejected  = true;
        SaveFailed();
        ContinueQueue();
        return;
    }
    if (!resp.Success || resp.StatusCode != 200) {
        /* Network drop, 429 or 5xx: the old block is still live in Notion.
           Keep the change pending and re-show Save. */
        DEBUG_LOGF("recreate", "insert failed status=%d", resp.StatusCode);
        if (idx >= 0 && idx < pageElmCount) {
            pageElm[idx].recreate = true;
            pageElm[idx].dirty       = true;
        }
        gRecreateSerial = 0;
        StopSaveForRetry();
        return;
    }
    parse_inserted_block_id(resp, idx);
    if (idx < 0 || idx >= pageElmCount || strcmp(pageElm[idx].id, g_recreateOldId) == 0) {
        /* No new ID parsed — don't trash the only block we can address. */
        gRecreateSerial = 0;
        ContinueQueue();
        return;
    }
    Block& nb = pageElm[idx];
    nb.saveFirst = false;
    /* Its old copy goes along with its parent's old copy (no trash request
       of its own) when it lay in there.  From now on its copy sits right
       under parentId. */
    bool covered   = nb.inOldParent;
    bool kidsLeft  = nb.kidsLeft;
    nb.inOldParent = false;
    nb.leftParent  = false;
    nb.kidsLeft    = false;
    /* Something may still have its only copy under the old one: keep that
       for later.  Listed even when covered, so the blocks under it are. */
    bool deferred = false;
    if (RecreatePendingIn(idx, kidsLeft)) {
        if (g_deferTrashCount < kMaxDeferTrash) {
            DEBUG_LOGF("recreate", "defer trash of old copy %d", idx);
            DeferredTrash& d = g_defer[g_deferTrashCount++];
            SafeStrCopy(d.oldId, 37, g_recreateOldId);
            SafeStrCopy(d.newId, 37, nb.id);
            d.covered  = covered;
            d.kidsLeft = kidsLeft;
            deferred   = true;
        } else {
            /* No room: leave the old copy (a duplicate) rather than trash
               it before everything in it is re-created. */
            DEBUG_LOGF("recreate", "defer list full: old copy kept %d", idx);
            covered = true;
        }
    }
    /* The new ID also belongs to the block's other pieces, and is the
       parent of anything still pointing at the old one (the children of
       a toggle retyped to another toggle, re-created after it) — whose
       copies lie in the deferred old one, unless they moved in. */
    for (int k = idx + 1; k < GroupEnd(idx); k++) setId(pageElm[k].id, nb.id);
    for (int k = 0; k < pageElmCount; k++)
        if (strcmp(pageElm[k].parentId, g_recreateOldId) == 0) {
            setId(pageElm[k].parentId, pageElm[idx].id);
            if (deferred && !pageElm[k].leftParent) pageElm[k].inOldParent = true;
        }
    RecloseRenameId(g_recreateOldId, pageElm[idx].id);

    if (deferred || covered) {
        gRecreateSerial = 0;
        ContinueQueue();
        return;
    }
    gHttpClient.Patch(BlocksURL(g_recreateOldId), "{\"in_trash\":true}", afterRecreateArchive);
}

/* One deferred trash done (always the last entry): the next, or finish
   the run (patchNext). */
static void afterDeferredTrash(HttpResponse& resp) {
    if (handle_write_error(resp, nullptr)) return;
    if (!resp.Success) {
        /* Network: keep the rest (this one is still at the end) for the
           next run; the old copies stay in Notion meanwhile. */
        StopSaveForRetry();
        return;
    }
    if (resp.StatusCode != 200)
        DEBUG_LOGF("recreate", "deferred trash status=%d (old copy left)", resp.StatusCode);
    /* Drop the one just sent (always the last) */
    if (g_deferTrashCount > 0) g_deferTrashCount--;
    ContinueQueue();
}


/* ------------------------------------------------------------------ */
/* patchNext — send the next queued block; called serially via callback */
/* ------------------------------------------------------------------ */
static void patchNext(HttpResponse& response) {
    /* 403 from Notion API: integration token lacks write permission.
       Success=true, StatusCode=403 is the normal Notion API error shape. */
    if (handle_write_error(response, nullptr)) return;
    bool ok = response.Success && response.StatusCode == 200;
    if (PermanentRefusal(response)) {
        /* Retrying would fail again and stop every block queued after it:
           leave the in-flight block (or icon) unsaved, flash, carry on. */
        DEBUG_LOGF("patchNext", "save refused status=%d, skipped", response.StatusCode);
        g_patchIdx = -1;
        g_iconPatchPending  = false;
        g_titlePatchPending = false;
        g_anyRejected = true;
        SaveFailed();
        ok = true;   /* handled: continue with the queue below */
    }
    if (!ok) {
        /* Network error, or Notion refused it (400 bad request, 429 rate
           limit, 5xx): it is NOT saved.  Put the in-flight block back to
           dirty (icon too), stop, re-show Save; MarkDirty restarts the
           autosave countdown so the retry comes one period later. */
        DEBUG_LOGF("patchNext", "save failed status=%d", response.StatusCode);
        if (g_patchIdx >= 0 && g_patchIdx < pageElmCount)
            for (int k = g_patchIdx; k < g_patchEnd && k < pageElmCount; k++)
                pageElm[k].dirty = true;
        if (g_iconPatchPending) gAppState.iconDirty = true;
        g_patchIdx = -1;
        g_iconPatchPending  = false;
        g_titlePatchPending = false;
        StopSaveForRetry();
        return;
    }
    g_patchIdx = -1;   /* the previous PATCH (if any) went through */

    /* Shadow-update caches for any just-completed PATCH */
    if (g_iconPatchPending) {
        g_iconPatchPending = false;
        UpdatePageListCacheEntry(EffPageID(), nullptr, g_pendingIconName);
    }
    if (g_titlePatchPending) {
        g_titlePatchPending = false;
        UpdatePageListCacheEntry(EffPageID(), g_pendingTitle, nullptr, g_pendingTitleEmoji);
    }

    /* Icon change takes priority */
    if (gAppState.iconDirty) {
        gAppState.iconDirty = false;
        string body;
        if (gPageIconName[0] == '\0') {
            body = "{\"icon\":null}";
            g_pendingIconName[0] = '\0';
        } else {
            body = "{\"icon\":{\"type\":\"icon\",\"icon\":{\"name\":\"";
            body += gPageIconName; body += "\",\"color\":\"";
            body += gPageIconColor; body += "\"}}}";
            SafeStrCopy(g_pendingIconName, sizeof(g_pendingIconName), gPageIconName);
        }
        g_iconPatchPending = true;
        { gHttpClient.Patch(PagesURL(EffPageID()), body, patchNext); }
        return;
    }

    /* Drain any queue entries that were cleared (e.g. block became clean) */
    while (!gSaveQueue.empty() && !pageElm[gSaveQueue.front()].dirty)
        gSaveQueue.pop_front();

    /* Queue done: now trash the old copies nothing is waiting on */
    if (gSaveQueue.empty() && g_deferTrashCount > 0) {
        int i = ReadyDeferred();
        if (i >= 0) {
            DEBUG_LOGF("patchNext", "deferred trash %d left", g_deferTrashCount);
            /* To the end of the list, where afterDeferredTrash drops it */
            int last = g_deferTrashCount - 1;
            if (i != last) {
                static DeferredTrash t;   /* static: no struct copy on the stack */
                memcpy(&t, &g_defer[i], sizeof t);
                memcpy(&g_defer[i], &g_defer[last], sizeof t);
                memcpy(&g_defer[last], &t, sizeof t);
            }
            if (g_defer[last].covered) {   /* goes along with an ancestor's old copy */
                g_deferTrashCount--;
                ContinueQueue();
                return;
            }
            gHttpClient.Patch(BlocksURL(g_defer[last].oldId), "{\"in_trash\":true}", afterDeferredTrash);
            return;
        }
    }

    if (gSaveQueue.empty()) {
        if (gAppState.isSaving && !g_anyRejected)   /* not "Saved" if a block was refused */
            gAppState.savedUntil   = TickCount() + kSavedDisplayTicks;
        gAppState.isSaving         = false;
        gAppState.lastSaveLabelIdx = -1;
        /* Chain finished with every PATCH acknowledged — only now is the
           in-memory page a faithful copy of Notion, safe to flush to the
           disk cache.  Error paths (network failure, 401/403) return
           before reaching here, so a failed save never gets cached. */
        gPageCacheNeedsSave        = !g_anyRejected;   /* don't cache text Notion refused */
        if (!g_anyRejected) g_failBeeped = false;
        g_anyRejected              = false;
        return;
    }

    int idx = gSaveQueue.front();
    gSaveQueue.pop_front();

    Block& b = pageElm[idx];
    b.dirty = false;
    int groupEnd = GroupEnd(idx);   /* idx+1 unless b is the head of a split block */

    string body;

    if (b.type == BT_TITLE) {
        /* AppendRichArray splits titles over Notion's 2,000-char item limit */
        body.reserve(RichJsonEstimate(&b, 1));
        body = "{\"properties\":{\"title\":{\"title\":";
        AppendRichArray(body, &b, 1); body += "}}}";
        SyncCurrentPageInHistory();
        /* Shadow-cache the new title (displayed immediately in Library) */
        SafeStrCopy(g_pendingTitle, sizeof(g_pendingTitle), b.text);
        TitleEmojiFromSeqs(b.emojiSeqs, b.emojiCount, g_pendingTitleEmoji);
        g_titlePatchPending = true;
        g_patchIdx = idx; g_patchEnd = idx + 1;
        { gHttpClient.Patch(PagesURL(b.id), body, patchNext); }
    } else if (b.recreate) {
        /* Type change / page-level move: insert the new block, then trash
           the old one (see afterRecreateInsert).  Rebuild runs NOW while
           the TE is fresh and guaranteed valid. */
        b.recreate  = false;
        gRecreateSerial = b.serial;
        SafeStrCopy(g_recreateOldId, sizeof(g_recreateOldId), b.id);
        const char* afterId; bool atStart;
        const char* parentId = RecreateAnchor(idx, &afterId, &atStart);
        /* A split block (moved out of a toggle) is re-created whole */
        PrepareGroup(idx, groupEnd);
        body.reserve(RichJsonEstimate(&pageElm[idx], groupEnd - idx));
        AppendChildrenBody(b.type, b.check, &pageElm[idx], groupEnd - idx, afterId, atStart, body);
        gHttpClient.PatchWithBody(BlocksURL(parentId) + "/children", body, afterRecreateInsert);
    } else if (b.type == BT_DIVIDER || b.type == BT_CHILD_PAGE) {
        /* These blocks have no editable content — skip PATCH, continue queue */
        ContinueQueue();
    } else {
        /* Resync runs from TE style scrap — typing updates TE but not blk.runs */
        PrepareGroup(idx, groupEnd);
        body.reserve(RichJsonEstimate(&pageElm[idx], groupEnd - idx));
        AppendBlockJson(b.type, b.check, &pageElm[idx], groupEnd - idx, body);
        g_patchIdx = idx; g_patchEnd = groupEnd;
        gHttpClient.Patch(BlocksURL(b.id), body, patchNext);
    }
}

/* ------------------------------------------------------------------ */
/* SaveThreadFunc — runs patchNext chain in a cooperative thread       */
/* ------------------------------------------------------------------ */
static pascal void* SaveThreadFunc(void*) {
    ContinueQueue();
    /* If Patch failed without calling the callback, gAppState.isSaving is still true — clear it */
    gAppState.isSaving         = false;
    gAppState.saveThreadActive = false;
    /* gPageCacheNeedsSave is set by patchNext's successful queue-empty exit,
       NOT here — this epilogue also runs after failed saves, and flagging the
       cache then would persist content Notion never received. */
    return nil;
}

/* ------------------------------------------------------------------ */
/* ApiThreadFunc / SpawnCoopThread / runApiThread                     */
/* ------------------------------------------------------------------ */
static pascal void* ApiThreadFunc(void*) {
    if (gApiKeepBody)
        gHttpClient.PatchWithBody(gApiThreadUrl, gApiThreadBody, gApiThreadCb);
    else
        gHttpClient.Patch(gApiThreadUrl, gApiThreadBody, gApiThreadCb);
    gApiKeepBody              = false;
    gAppState.apiPending      = false;
    gAppState.isSaving        = false;
    gAppState.saveThreadActive = false;
    /* gPageCacheNeedsSave: see SaveThreadFunc — only patchNext's successful
       queue-empty exit flags the cache, so failed API ops aren't persisted. */
    return nil;
}

static OSErr SpawnCoopThread(ThreadEntryUPP fn, long stackBytes, ThreadID* outID) {
    OSErr err = NewThread(kCooperativeThread, fn, nil,
                          stackBytes, kNewSuspend, nil, outID);
    if (err == noErr) SetThreadState(*outID, kReadyThreadState, kNoThreadID);
    return err;
}

void runApiThread(const string& url, const string& body,
                  function<void(HttpResponse&)> cb,
                  bool keepBody) {
    gApiThreadUrl  = url;
    gApiThreadBody = body;
    gApiThreadCb   = cb;
    gApiKeepBody   = keepBody;
    gAppState.saveThreadActive = true;
    OSErr err = SpawnCoopThread((ThreadEntryUPP)ApiThreadFunc, kSaveThreadStack, &gSaveThreadID);
    if (err != noErr) {
        /* NewThread failed (e.g. -108 on a fragmented heap): the epilogue in
           ApiThreadFunc never runs, so clear its flags here or every later
           save, Back/Forward and the watch cursor stay blocked for good. */
        DEBUG_LOGF("runApiThread", "NewThread err=%d", err);
        gApiKeepBody               = false;
        gAppState.apiPending       = false;
        gAppState.isSaving         = false;
        gAppState.saveThreadActive = false;
    }
}

/* ------------------------------------------------------------------ */
/* doSave — queue every dirty block then spin up the save thread       */
/* ------------------------------------------------------------------ */
void doSave() {
    if (gAppState.isSaving || gAppState.saveThreadActive ||
        (!gAppState.hasDirtyBlocks && !gAppState.iconDirty && ReadyDeferred() < 0)) return;
    gAppState.isSaving = true;
    gAppState.saveStartTick = TickCount();
    g_anyRejected = false;
    gAppState.hasDirtyBlocks = false;

    /* Sync the currently active TE handle into b.text before patching */
    SyncActiveBlock();

    /* A split block saves as one: an edited piece makes its head dirty */
    for (int i = 0; i < pageElmCount; i++)
        if (pageElm[i].isCont && pageElm[i].dirty) {
            pageElm[GroupHead(i)].dirty = true;
            pageElm[i].dirty = false;
        }

    /* Build queue of all dirty blocks.  Pass 0 takes saveFirst blocks —
       children moving out of a retyped toggle must leave it before the
       toggle is re-created and its old copy (with any children) trashed. */
    gSaveQueue.clear();
    for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < pageElmCount; i++) {
        if (!pageElm[i].dirty || pageElm[i].id[0] == '\0' || pageElm[i].isCont) continue;
        if (pageElm[i].saveFirst != (pass == 0)) continue;
        if (pageElm[i].cutShort) {
            /* Safety net behind AllowEdit: writing a block that
               didn't fully load would delete the unloaded part in Notion. */
            DEBUG_LOGF("doSave", "skip cut-short block %d", i);
            pageElm[i].dirty = false; pageElm[i].recreate = false;
            continue;
        }
        gSaveQueue.push_back(i);
    }

    if (gSaveQueue.empty() && !gAppState.iconDirty && ReadyDeferred() < 0) {
        gAppState.isSaving = false;
        return;
    }

    /* Spawn a cooperative thread so the event loop stays responsive.
     * 128 KB stack: patchNext chains recursively; ~2 KB/block frame, ~30 KB
     * for TLS precompute on first reconnect; 64 KB overflowed at ~11 blocks.
     * Free sprite sheet first (locked, large) so MaxMem can give 128 KB. */
    FreeGraphicsAssets();
    /* Compact heap so the freed space is contiguous. */
    Size grow;
    MaxMem(&grow);

    gAppState.saveThreadActive = true;
    OSErr err = SpawnCoopThread((ThreadEntryUPP)SaveThreadFunc, kSaveThreadStack, &gSaveThreadID);
    if (err != noErr) {
        /* No thread, no patchNext: blocks keep their dirty bits, so just
           drop the busy flags and re-show Save for a retry. */
        DEBUG_LOGF("doSave", "NewThread err=%d", err);
        gSaveQueue.clear();
        gAppState.isSaving         = false;
        gAppState.saveThreadActive = false;
        gAppState.hasDirtyBlocks   = true;
        return;
    }
    /* Thread runs the next time the event loop calls YieldToAnyThread() */
}
