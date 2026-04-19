#include <vector>
#include <string>
#include <algorithm>
#include <map>

#include <Menus.h>
#include <Fonts.h>
#include <Dialogs.h>
#include <Windows.h>
#include <Controls.h>
#include <Resources.h>
#include <TextEdit.h>
#include <TextUtils.h>
#include <Devices.h>
#include <Events.h>
#include <Quickdraw.h>
#include <Threads.h>
#include <Gestalt.h>
#include <Processes.h>
#include <Scrap.h>
#include <Files.h>

#include <stdio.h>
#include <stddef.h>

#include "gason.hpp"
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl_ciphersuites.h>
#include "HttpClient.h"
#include "Settings.h"
extern "C" {
#include "emoji_sprite.h"
}

using namespace std;

AppSettings gSettings;

/* Demo page shown on first launch (no prefs file).  Never stored in gSettings
   so the settings dialog fields stay empty until the user enters their own. */
static const char kDemoPageID[] = "34605fe4e8c78048bb61c8888e370aea";
static const char kDemoAPIKey[] = "ntn_c1608837319ba4BKLkkgJ9ninUwfzTt4UWReZSecgB754w";
static inline const char* EffPageID() { return gSettings.pageID[0] ? gSettings.pageID : kDemoPageID; }
static inline const char* EffAPIKey() { return gSettings.apiKey[0] ? gSettings.apiKey : kDemoAPIKey; }

extern pascal long MyWindowDefProc(short varCode, WindowRef window, short message, long param);
void pageRequest();
void updateBlocks();

WindowPtr _window;
WindowRef _aboutBox;
static Rect _initialWindowRect;
int blockSelect;

/* Scroll state */
ControlHandle   gScrollBar          = nil;
int             gScrollOffset       = 0;
int             gTotalContentHeight = 0;
ControlActionUPP gScrollActionUPP   = nil;

/* Save button state */
ControlHandle   gSaveButton         = nil;
long            gLastEditTick       = 0;
bool            gHasDirtyBlocks     = false;
struct UndoState { string text; vector<string> emojiSeqs; string type; };
static const int kUndoMax       = 3;
static const int kUndoCharStep  = 20;  /* chars typed between snapshots */
UndoState       gUndoStack[kUndoMax];
int             gUndoStackSize      = 0;
int             gUndoBlock          = -1;
int             gUndoCharCount      = 0; /* chars since last push */
bool            gIsSaving           = false;
int             gLastSaveLabelIdx   = -1;  /* tracks last drawn label to avoid flicker */
long            gSavedUntil         = 0;   /* TickCount deadline for "Saved" display */
short           gLastBarFilled      = -1;  /* last drawn progress bar fill width */
vector<int>     gSaveQueue;               /* indices of blocks waiting to be PATCHed */
int             gTypeChangeIdx      = -1; /* block index being archived for type change */
int             gIndentIdx          = -1; /* block index being indented into a toggle */
string          gIndentParentId;          /* parent toggle ID for indent operation */
string          gIndentAfter;             /* after=ID prepends; empty=append at end   */
int             gAddIdx             = -1; /* index of newly added block awaiting API-assigned ID */
bool            gApiPending         = false; /* add/delete API call in flight; blocks saves but does not show Save button */
bool            gTogglePending      = false; /* children fetch in flight */
int             gToggleIdx          = -1;   /* pageElm index of toggle being opened */
bool            gNeedsReload        = false; /* set on 401/bad-ID to restart from main loop */
bool            gReadOnly           = false; /* set on 403 PATCH → integration lacks write permission */
bool            gTimedOut           = false; /* set when request exceeds timeout; auto-retry */
long            gRequestStartTick   = 0;     /* TickCount() when current request was issued */
static const long kRequestTimeoutTicks = 30L * 60L; /* 30 s × 60 ticks/s */
bool            gHasMore            = false; /* Notion returned has_more:true for blocks */
string          gNextCursor;                 /* next_cursor from last paginated response */
bool            gScrolling          = false; /* set during scrollActionProc to skip full EraseRect */
static RgnHandle gScrollClipRgn     = nil;   /* exposed-strip clip region; non-nil during scroll draw */
ThreadID        gSaveThreadID       = kNoThreadID;
bool            gSaveThreadActive   = false;
Rect            gMoveUpArrowRect    = {0,0,0,0};
Rect            gMoveDownArrowRect  = {0,0,0,0};
Rect            gAddBlockRect       = {0,0,0,0};
bool            gArrowsVisible      = false;
static int      gHoverBlockIdx      = -1;

enum SidebarPending { kSPNone = 0, kSPPlus, kSPUp, kSPDown };
static SidebarPending gSidebarPending      = kSPNone;
static long           gSidebarPendingTick  = 0;
static int            gSidebarPendingBlock = -1;
static const long     kSidebarPendingTimeout = 180L; /* 3 s then auto-clear  */

const short kScrollBarWidth   = 16;
const short kLineScrollAmount = 28;
const short kSidebarWidth     = 56;   /* left sidebar panel width */

enum {
	kMenuApple = 128,
	kMenuFile,
	kMenuEdit,
	kMenuAdd,
	kItemAbout    = 1,
	kItemReload   = 1,
	kItemSave     = 2,
	kItemSettings  = 4,
	kItemFullWidth = 6,
	kItemQuit      = 8
};

struct TextRun {
    string text;
    bool bold          = false;
    bool italic        = false;
    bool underline     = false;
    bool strikethrough = false;
    string url;        /* href from Notion rich_text; "http://notioncl/..." for in-app links */
};

struct Block {
  int pos;
  string id;
  string type;
  string text;
  vector<TextRun>   runs;          /* styled runs from Notion API */
  vector<string> emojiSeqs;  /* original UTF-8 sequences replaced by sentinel in TE */
  bool check;
  bool dirty;
  bool typeChanged;
  bool open;       /* toggle: currently expanded */
  bool isChild;    /* child of a toggle block */
  int  depth;      /* indent level (0 = top-level) */
  string parentId; /* id of parent toggle (for children) */
  Rect rect, vrect;
  TEHandle textEdit;
  string   teCache;           /* last text sent to TE via TESetText */
  short    teCachedWidth;     /* column width at last TENew (0 = never created) */
  string   teCachedType;      /* block type at last TENew (font differs per type) */

  Block() : pos(0), check(false), dirty(false), typeChanged(false),
            open(false), isChild(false), depth(0), textEdit(nil),
            teCachedWidth(0) {}

  /* Returns true if TE was (re)created, false if only rects were updated in place.
     Only recreates when the column width or block type changes (e.g. window resize
     or user-initiated type change). During normal scrolling, just repositions. */
  bool addTE(Rect _rect, Rect _vrect, const string& curType) {
      short newWidth = _rect.right - _rect.left;
      if (textEdit != nil && teCachedWidth == newWidth && teCachedType == curType) {
          /* Same geometry — reposition destRect/viewRect in place, no TENew needed */
          rect  = _rect;
          vrect = _vrect;
          (*textEdit)->viewRect = _vrect;
          (*textEdit)->destRect = _rect;
          return false;
      }
      /* Width or type changed, or first call — (re)create */
      if (textEdit != nil) { TEDispose(textEdit); textEdit = nil; }
      teCache.clear();
      teCachedWidth = 0;
      teCachedType.clear();
      rect   = _rect;
      vrect  = _vrect;
      textEdit = TEStyleNew(&rect, &vrect);
      if (textEdit == nil) return false; /* out of heap — skip this block */
      /* TEStyleNew sets lineHeight=-1 (variable); override with real font metrics
         so the +2 line-spacing adjustment in updateBlocks has a correct baseline. */
      { FontInfo fi; GetFontInfo(&fi);
        (*textEdit)->lineHeight = fi.ascent + fi.descent + fi.leading;
        (*textEdit)->fontAscent = fi.ascent; }
      teCachedWidth = newWidth;
      teCachedType  = curType;
      return true;
  }
};
vector<Block> pageElm;
map<string, vector<Block>> gToggleChildCache; /* cached children by toggle ID */

int _curRequest = 0;
HttpClient _httpClient;
TEHandle textActive;
string gPageIconName;      /* native Notion icon slug, e.g. "house"; empty if none */
string gPageIconColor = "gray"; /* icon color from API */
short  gAppVRefNum = 0;    /* volume ref of folder containing the app */
long   gAppDirID   = 0;    /* directory ID of folder containing the app */
long   gIconsDirID = 0;    /* cached dirID of the icon/ subfolder */
Rect   gIconRect   = {0,0,0,0}; /* drawn rect of page icon (for click detection) */
short  gColLeft    = kSidebarWidth; /* left edge of centred text column; updated each updateBlocks() */
bool   gFullWidth  = false;        /* full-screen window mode */
Rect   gSavedWindowRect = {42, 6, 375, 505}; /* window rect before entering full-screen */
bool   gIconDirty       = false; /* icon was changed, needs API PATCH */
vector<string> gIconList;       /* sorted list of all icon slugs from icon/ folder */
vector<string> gIconListLower;  /* same, pre-lowercased for case-insensitive search */
static Handle  gSpriteHandle      = nil;
static BitMap  gSpriteBM;
static Handle    gEmojiSpriteHandle = nil; /* non-nil = sprite loaded */
static GWorldPtr gEmojiGWorld       = nil; /* offscreen 1-bit GWorld for sprite */
static SInt16  gFontHelvetica = 0;  /* cached font number — looked up once at startup */
static SInt16  gFontGeneva    = 0;
static SInt16  gFontMonaco    = 0;


static void drawSaveButtonFrame(short, short, short, short);
static void drawProgressBar(short filled);
static void drawSidebarControls();
void updateSaveButton();
static void resizeControls();

/* ------------------------------------------------------------------ */
/* Scroll action proc — fires repeatedly while mouse held on arrows    */
/* ------------------------------------------------------------------ */
pascal void scrollActionProc(ControlHandle control, short part)
{
    if (part == 0) return;

    short visibleHeight = _window->portRect.bottom - _window->portRect.top;
    short pageAmount    = visibleHeight - kLineScrollAmount;
    short delta = 0;

    switch (part) {
        case 20: delta = -kLineScrollAmount; break;  /* inUpButton   */
        case 21: delta =  kLineScrollAmount; break;  /* inDownButton */
        case 22: delta = -pageAmount;        break;  /* inPageUp     */
        case 23: delta =  pageAmount;        break;  /* inPageDown   */
        default: return;
    }

    short newVal = GetControlValue(control) + delta;
    short minVal = GetControlMinimum(control);
    short maxVal = GetControlMaximum(control);
    if (newVal < minVal) newVal = minVal;
    if (newVal > maxVal) newVal = maxVal;

    if (newVal == GetControlValue(control)) return;

    short scrollDelta = newVal - GetControlValue(control);

    /* Shift existing pixels, then redraw only the newly-exposed strip.
       We keep updateRgn alive and set it as the QuickDraw clip before
       calling updateBlocks(), so TEUpdate/DrawBlockEmojis only paint
       inside the strip — already-visible pixels are left untouched.   */
    Rect contentRect = _window->portRect;
    contentRect.left   = gColLeft;
    contentRect.right -= kScrollBarWidth;
    RgnHandle updateRgn = NewRgn();
    ScrollRect(&contentRect, 0, -scrollDelta, updateRgn);
    EraseRgn(updateRgn);

    SetControlValue(control, newVal);
    gScrollOffset = newVal;
    gScrolling      = true;
    gScrollClipRgn  = updateRgn;
    updateBlocks();
    gScrollClipRgn  = nil;
    gScrolling      = false;
    DisposeRgn(updateRgn);
    if (blockSelect >= 0 && blockSelect < (int)pageElm.size())
        textActive = pageElm[blockSelect].textEdit;
    /* Repaint the gray progress bar — ScrollRect can wipe it */
    gLastBarFilled = -1;
    updateSaveButton();
}

/* ------------------------------------------------------------------ */
/* updateScrollRange — called after layout to sync the scroll bar      */
/* ------------------------------------------------------------------ */
static void updateScrollRange()
{
    if (gScrollBar == nil) return;

    short visibleHeight = _window->portRect.bottom - _window->portRect.top;
    short maxScroll     = gTotalContentHeight - visibleHeight;
    if (maxScroll < 0) maxScroll = 0;

    SetControlMaximum(gScrollBar, maxScroll);
    SetControlValue(gScrollBar, gScrollOffset);
}

/* ------------------------------------------------------------------ */
/* drawArrow — filled triangle in the sidebar                         */
/* ------------------------------------------------------------------ */
static void drawArrow(short cx, short cy, bool pointUp) {
    PolyHandle poly = OpenPoly();
    if (pointUp) {
        MoveTo(cx,     cy);
        LineTo(cx - 5, cy + 8);
        LineTo(cx + 5, cy + 8);
        LineTo(cx,     cy);
    } else {
        MoveTo(cx,     cy + 8);
        LineTo(cx - 5, cy);
        LineTo(cx + 5, cy);
        LineTo(cx,     cy + 8);
    }
    ClosePoly();
    FillPoly(poly, &qd.black);
    KillPoly(poly);
}

/* drawGrayArrow — small filled triangle for sidebar controls         */
static void drawGrayArrow(short cx, short cy, bool pointUp, bool black = false) {
    PolyHandle poly = OpenPoly();
    if (pointUp) {
        MoveTo(cx,     cy);
        LineTo(cx - 5, cy + 6);
        LineTo(cx + 5, cy + 6);
        LineTo(cx,     cy);
    } else {
        MoveTo(cx,     cy + 6);
        LineTo(cx - 5, cy);
        LineTo(cx + 5, cy);
        LineTo(cx,     cy + 6);
    }
    ClosePoly();
    FillPoly(poly, black ? &qd.black : &qd.gray);
    KillPoly(poly);
}

static string StripEmojiPad(const string& s); /* forward decl */
static string TextToTE(const string& text, vector<string>& emojiSeqs); /* forward decl */
static void   DrawBlockEmojis(TEHandle teh, const string& teText, const vector<string>& emojiSeqs); /* forward decl */
static void   LoadEmojiSprite(); /* forward decl */
static void DrawStrikethrough(TEHandle te, const vector<TextRun>& runs); /* forward decl */
static void afterIndentArchive(HttpResponse&); /* forward decl */

/* ------------------------------------------------------------------ */
/* moveBlock — swap current block up or down; queue for API reorder   */
/* ------------------------------------------------------------------ */
static void moveBlock(int dir) {
    if (blockSelect <= 0) return;
    int dest = blockSelect + dir;
    if (dest <= 0 || dest >= (int)pageElm.size()) return;

    /* Sync active TE text before swapping */
    if (textActive != nil && pageElm[blockSelect].textEdit == textActive) {
        CharsHandle ch = TEGetText(textActive);
        short len = (*textActive)->teLength;
        HLock((Handle)ch);
        pageElm[blockSelect].text = StripEmojiPad(string(*ch, (size_t)len));
        HUnlock((Handle)ch);
    }

    /* Remember screen Y of the block we're moving so we can keep it in place */
    short screenYBefore = pageElm[blockSelect].rect.top;

    /* Special case: moving up when dest IS a toggle — block would land before
       the toggle, but we want it inside. Adopt child status in place (no swap). */
    if (dir == -1 && !pageElm[blockSelect].isChild &&
        !gIsSaving && !gSaveThreadActive && !gApiPending && !gTogglePending) {
        string dt = pageElm[dest].type;
        if ((dt=="toggle"||dt=="toggle_heading_1"||dt=="toggle_heading_2"||dt=="toggle_heading_3")
            && !pageElm[dest].id.empty() && pageElm[dest].open) {
            Block& blk    = pageElm[blockSelect];
            Block& toggle = pageElm[dest];
            toggle.open  = true;
            blk.isChild  = true;
            blk.depth    = toggle.depth + 1;
            blk.parentId = toggle.id;
            if (blk.textEdit) { TEDispose(blk.textEdit); blk.textEdit = nil; }
            blk.teCachedWidth = 0; blk.teCachedType.clear(); blk.teCache.clear();
            blk.dirty    = false; /* saved via archive+re-insert, not the save queue */
            { bool any = false; for (auto& e : pageElm) if (e.dirty) { any = true; break; }
              gHasDirtyBlocks = any; }
            gIndentIdx      = blockSelect;
            gIndentParentId = toggle.id;
            gIndentAfter    = "";
            gApiPending     = true;
            gToggleChildCache.erase(toggle.id);
            _httpClient.Patch("https://api.notion.com/v1/blocks/" + blk.id,
                              "{\"archived\":true}", afterIndentArchive);
            updateBlocks();
            if (blockSelect >= 0 && blockSelect < (int)pageElm.size())
                textActive = pageElm[blockSelect].textEdit;
            if (textActive) { TEActivate(textActive); TESetSelect(0, 0, textActive); }
            return;
        }
    }

    /* Moving DOWN into an open toggle: enter as first child locally; appended
       last in Notion (API has no prepend — local order is authoritative). */
    if (dir == +1 && !pageElm[blockSelect].isChild &&
        !gIsSaving && !gSaveThreadActive && !gApiPending && !gTogglePending) {
        const Block& togBlock = pageElm[dest];
        string tt = togBlock.type;
        if ((tt=="toggle"||tt=="toggle_heading_1"||tt=="toggle_heading_2"||tt=="toggle_heading_3")
            && togBlock.open && !togBlock.id.empty()) {
            string toggleId = togBlock.id;
            string blkId    = pageElm[blockSelect].id;
            /* Find last existing child before any mutations. */
            int lastChild = dest;
            while (lastChild + 1 < (int)pageElm.size()
                   && pageElm[lastChild + 1].isChild
                   && pageElm[lastChild + 1].parentId == toggleId)
                lastChild++;
            /* Set up child attrs on A */
            pageElm[blockSelect].isChild  = true;
            pageElm[blockSelect].depth    = togBlock.depth + 1;
            pageElm[blockSelect].parentId = toggleId;
            if (pageElm[blockSelect].textEdit) {
                TEDispose(pageElm[blockSelect].textEdit);
                pageElm[blockSelect].textEdit = nil;
            }
            pageElm[blockSelect].teCachedWidth = 0;
            pageElm[blockSelect].teCachedType.clear();
            pageElm[blockSelect].teCache.clear();
            pageElm[blockSelect].dirty = false;
            { bool any = false; for (auto& e : pageElm) if (e.dirty) { any = true; break; }
              gHasDirtyBlocks = any; }
            /* Move A to last child slot (matches Notion append order).
               After erase(blockSelect), everything shifts down by 1, so
               original lastChild index lands A right after the last child. */
            Block blkCopy = pageElm[blockSelect];
            pageElm.erase(pageElm.begin() + blockSelect);
            int insertAt = lastChild;
            pageElm.insert(pageElm.begin() + insertAt, blkCopy);
            for (int i = 0; i < (int)pageElm.size(); i++) pageElm[i].pos = i;
            blockSelect     = insertAt;
            gIndentIdx      = blockSelect;
            gIndentParentId = toggleId;
            gIndentAfter    = "";
            gApiPending     = true;
            gToggleChildCache.erase(toggleId);
            _httpClient.Patch("https://api.notion.com/v1/blocks/" + blkId,
                              "{\"archived\":true}", afterIndentArchive);
            updateBlocks();
            if (blockSelect >= 0 && blockSelect < (int)pageElm.size())
                textActive = pageElm[blockSelect].textEdit;
            if (textActive) { TEActivate(textActive); TESetSelect(0, 0, textActive); }
            return;
        }
    }

    /* Moving UP just below a toggle's last child: adopt as last child in
       place (no swap) so the block lands AFTER the existing children. */
    if (dir == -1 && !pageElm[blockSelect].isChild &&
        !gIsSaving && !gSaveThreadActive && !gApiPending && !gTogglePending &&
        pageElm[dest].isChild && !pageElm[dest].parentId.empty()) {
        string newParentId = pageElm[dest].parentId;
        int    newDepth    = pageElm[dest].depth;
        string blkId       = pageElm[blockSelect].id;
        pageElm[blockSelect].isChild  = true;
        pageElm[blockSelect].depth    = newDepth;
        pageElm[blockSelect].parentId = newParentId;
        if (pageElm[blockSelect].textEdit) {
            TEDispose(pageElm[blockSelect].textEdit);
            pageElm[blockSelect].textEdit = nil;
        }
        pageElm[blockSelect].teCachedWidth = 0;
        pageElm[blockSelect].teCachedType.clear();
        pageElm[blockSelect].teCache.clear();
        pageElm[blockSelect].dirty = false;
        { bool any = false; for (auto& e : pageElm) if (e.dirty) { any = true; break; }
          gHasDirtyBlocks = any; }
        gIndentIdx      = blockSelect;
        gIndentParentId = newParentId;
        gIndentAfter    = "";  /* append at end = last child */
        gApiPending     = true;
        gToggleChildCache.erase(newParentId);
        _httpClient.Patch("https://api.notion.com/v1/blocks/" + blkId,
                          "{\"archived\":true}", afterIndentArchive);
        updateBlocks();
        if (blockSelect >= 0 && blockSelect < (int)pageElm.size())
            textActive = pageElm[blockSelect].textEdit;
        if (textActive) { TEActivate(textActive); TESetSelect(0, 0, textActive); }
        return;
    }

    swap(pageElm[blockSelect], pageElm[dest]);
    pageElm[blockSelect].pos = blockSelect;
    pageElm[dest].pos        = dest;

    /* When moving up past a toggle child: adopt child status and use the
       indent API path (archive + re-insert into toggle children). */
    bool adoptedChild = false;
    if (dir == -1 && dest > 1 && !pageElm[dest].isChild &&
        !gIsSaving && !gSaveThreadActive && !gApiPending && !gTogglePending) {
        Block& moved = pageElm[dest];
        Block& above = pageElm[dest - 1];
        string newParentId;
        int    newDepth = 0;
        string at = above.type;
        if ((at=="toggle"||at=="toggle_heading_1"||at=="toggle_heading_2"||at=="toggle_heading_3") && above.open) {
            newParentId = above.id;
            newDepth    = above.depth + 1;
        } else if (above.isChild && !above.parentId.empty()) {
            newParentId = above.parentId;
            newDepth    = above.depth;
        }
        if (!newParentId.empty() && !moved.id.empty()) {
            moved.isChild  = true;
            moved.depth    = newDepth;
            moved.parentId = newParentId;
            if (moved.textEdit) { TEDispose(moved.textEdit); moved.textEdit = nil; }
            moved.teCachedWidth = 0; moved.teCachedType.clear(); moved.teCache.clear();
            moved.dirty    = false; /* saved via archive+re-insert, not the save queue */
            { bool any = false; for (auto& e : pageElm) if (e.dirty) { any = true; break; }
              gHasDirtyBlocks = any; }
            gIndentIdx      = dest;
            gIndentParentId = newParentId;
            gIndentAfter    = "";
            gApiPending     = true;
            gToggleChildCache.erase(newParentId);
            _httpClient.Patch("https://api.notion.com/v1/blocks/" + moved.id,
                              "{\"archived\":true}", afterIndentArchive);
            adoptedChild = true;
        }
    }

    /* If a child block moved out of its toggle's consecutive range, clear
       the child attributes so it renders without indent.  The block above
       (dest-1) must be either the parent toggle or a sibling; if not, the
       block has exited the toggle. */
    bool leftToggle = false;
    if (!adoptedChild && pageElm[dest].isChild && !pageElm[dest].parentId.empty()) {
        const string& oldParentId = pageElm[dest].parentId;
        bool stillInRange = false;
        if (dest > 0) {
            const Block& above = pageElm[dest - 1];
            stillInRange = (above.id == oldParentId) ||
                           (above.isChild && above.parentId == oldParentId);
        }
        if (!stillInRange) {
            Block& moved   = pageElm[dest];
            moved.isChild  = false;
            moved.depth    = 0;
            moved.parentId = "";
            if (moved.textEdit) { TEDispose(moved.textEdit); moved.textEdit = nil; }
            moved.teCachedWidth = 0; moved.teCachedType.clear(); moved.teCache.clear();
            leftToggle = true;
        }
    }

    if (!adoptedChild && !leftToggle && !pageElm[dest].isChild) {
        /* Normal page-level reorder: archive + re-insert */
        pageElm[dest].typeChanged = true;
        pageElm[dest].dirty       = true;
        gHasDirtyBlocks           = true;
        gLastEditTick             = TickCount();
    }

    blockSelect = dest;
    updateBlocks();
    if (blockSelect >= 0 && blockSelect < (int)pageElm.size())
        textActive = pageElm[blockSelect].textEdit;

    /* Adjust scroll so the moved block stays at the same screen position */
    if (blockSelect >= 0 && blockSelect < (int)pageElm.size()) {
        short screenYAfter = pageElm[blockSelect].rect.top;
        int   delta        = screenYAfter - screenYBefore;
        if (delta != 0) {
            gScrollOffset += delta;
            if (gScrollOffset < 0) gScrollOffset = 0;
            short winH     = _window->portRect.bottom - _window->portRect.top;
            short maxScroll = gTotalContentHeight - winH;
            if (maxScroll < 0) maxScroll = 0;
            if (gScrollOffset > maxScroll) gScrollOffset = maxScroll;
            updateScrollRange();
            updateBlocks();
            if (blockSelect >= 0 && blockSelect < (int)pageElm.size())
                textActive = pageElm[blockSelect].textEdit;
        }
    }
}

/* ------------------------------------------------------------------ */
/* drawSidebarControls — repaint +  ▴ ▾ for hovered block            */
/* Controls sit to the left of the text column, aligned to block top  */
/* ------------------------------------------------------------------ */
static void drawSidebarControls() {
    if (!gArrowsVisible || _window == nil || pageElm.empty()) return;
    SetPort(_window);

    /* Erase the full sidebar strip */
    Rect sideBar;
    SetRect(&sideBar, 0, 0, gColLeft - 1, _window->portRect.bottom - 28);
    EraseRect(&sideBar);

    SetRect(&gAddBlockRect,     0, 0, 0, 0);
    SetRect(&gMoveUpArrowRect,  0, 0, 0, 0);
    SetRect(&gMoveDownArrowRect,0, 0, 0, 0);

    /* Right-aligned: constant gap between controls and text column */
    const short downX = gColLeft - 16;
    const short upX   = downX    - 14;
    const short plusX = upX      - 14;

    /* --- Pending state: show only the clicked control in black --- */
    if (gSidebarPending != kSPNone) {
        int pidx = gSidebarPendingBlock;
        if (pidx <= 0 || pidx >= (int)pageElm.size()) return;
        Block& pb = pageElm[pidx];
        if (pb.type == "divider") return;
        short pcy = (pb.textEdit != nil ? pb.vrect.top : pb.rect.top) + 6;
        PenNormal();
        if (gSidebarPending == kSPPlus) {
            PenSize(2, 2);
            MoveTo(plusX - 3, pcy - 1); LineTo(plusX + 3, pcy - 1);
            MoveTo(plusX, pcy - 4);     LineTo(plusX, pcy + 2);
            PenNormal();
        } else if (gSidebarPending == kSPUp) {
            drawGrayArrow(upX, pcy - 3, true, true);
        } else if (gSidebarPending == kSPDown) {
            drawGrayArrow(downX, pcy - 3, false, true);
        }
        gLastSaveLabelIdx = -1; gLastBarFilled = -1;
        updateSaveButton();
        return;
    }

    /* --- Normal state: draw all three controls in gray --- */
    int idx = gHoverBlockIdx;
    if (idx <= 0 || idx >= (int)pageElm.size()) return;

    Block& b = pageElm[idx];
    if (b.type == "divider") return;

    short cy = (b.textEdit != nil ? b.vrect.top : b.rect.top) + 6;

    /* + button: gray */
    PenSize(2, 2);
    PenPat(&qd.gray);
    MoveTo(plusX - 3, cy - 1); LineTo(plusX + 3, cy - 1);
    MoveTo(plusX, cy - 4);     LineTo(plusX, cy + 2);
    PenNormal();
    SetRect(&gAddBlockRect, plusX - 6, cy - 6, plusX + 6, cy + 6);

    /* ▴ up arrow — gray */
    if (idx > 1) {
        drawGrayArrow(upX, cy - 3, true);
        SetRect(&gMoveUpArrowRect, upX - 6, cy - 5, upX + 6, cy + 5);
    }

    /* ▾ down arrow — gray */
    if (idx < (int)pageElm.size() - 1) {
        drawGrayArrow(downX, cy - 3, false);
        SetRect(&gMoveDownArrowRect, downX - 6, cy - 5, downX + 6, cy + 5);
    }
    gLastSaveLabelIdx = -1; gLastBarFilled = -1;
    updateSaveButton();
}

/* ------------------------------------------------------------------ */
/* updateArrowVisibility — show/hide sidebar arrows based on mouse    */
/* ------------------------------------------------------------------ */
static long gLastMouseCheckTick  = 0;   /* throttle GetMouse polling */

static void updateArrowVisibility() {
    if (_window == nil || _curRequest < 2) return;
    long now = TickCount();
    if (now - gLastMouseCheckTick < 3) return;  /* ~50ms throttle */
    gLastMouseCheckTick = now;

    SetPort(_window);
    Point pt;
    GetMouse(&pt);
    bool inSidebar = (pt.h >= 0 && pt.h < gColLeft - 1 &&
                      pt.v >= 0 && pt.v < _window->portRect.bottom - 28);

    if (inSidebar) {
        /* Find which block row the mouse Y falls in */
        int hoverIdx = -1;
        for (int i = 1; i < (int)pageElm.size(); i++) {
            if (pageElm[i].type == "divider") continue;
            if (pt.v >= pageElm[i].rect.top && pt.v < pageElm[i].rect.bottom) {
                hoverIdx = i;
                break;
            }
        }
        /* If between blocks, snap to nearest block above */
        if (hoverIdx == -1) {
            for (int i = (int)pageElm.size() - 1; i >= 1; i--) {
                if (pageElm[i].type == "divider") continue;
                if (pt.v >= pageElm[i].rect.top) { hoverIdx = i; break; }
            }
        }
        if (hoverIdx != -1) {
            /* Clear pending on different block hover or 3 s timeout */
            if (gSidebarPending != kSPNone) {
                bool diffBlock = (hoverIdx != gSidebarPendingBlock);
                bool timedOut  = (now - gSidebarPendingTick > kSidebarPendingTimeout);
                if (diffBlock || timedOut) {
                    gSidebarPending      = kSPNone;
                    gSidebarPendingBlock = -1;
                }
            }
            if (!gArrowsVisible || gHoverBlockIdx != hoverIdx) {
                gArrowsVisible  = true;
                gHoverBlockIdx  = hoverIdx;
                drawSidebarControls();
            }
        } else if (gArrowsVisible) {
            gArrowsVisible = false;
            gHoverBlockIdx = -1;
            SetRect(&gAddBlockRect,     0, 0, 0, 0);
            SetRect(&gMoveUpArrowRect,  0, 0, 0, 0);
            SetRect(&gMoveDownArrowRect,0, 0, 0, 0);
            Rect sideBar;
            SetRect(&sideBar, 0, 0, gColLeft - 1, _window->portRect.bottom - 28);
            EraseRect(&sideBar);
        }
    } else if (gArrowsVisible) {
        gSidebarPending      = kSPNone;
        gSidebarPendingBlock = -1;
        gArrowsVisible = false;
        gHoverBlockIdx = -1;
        SetRect(&gAddBlockRect,     0, 0, 0, 0);
        SetRect(&gMoveUpArrowRect,  0, 0, 0, 0);
        SetRect(&gMoveDownArrowRect,0, 0, 0, 0);
        Rect sideBar;
        SetRect(&sideBar, 0, 0, gColLeft - 1, _window->portRect.bottom - 28);
        EraseRect(&sideBar);
    }
}

static void DrawStatusText(const char *msg); /* forward declaration */


/* ------------------------------------------------------------------ */
/* DrawNotionIcon — load a .pict file from the icons folder and draw  */
/* 1-slot cache: keeps the last-used PICT in memory so repeated draws */
/* during scroll never hit the disk.                                  */
/* ------------------------------------------------------------------ */
static string  gIconCacheName;
static Handle  gIconCacheHandle = nil;

static void DrawNotionIcon(const string& name, short x, short y, short size = 40) {
    if (gAppVRefNum == 0) return;

    /* Cache icon/ folder dirID on first call */
    if (gIconsDirID == 0) {
        CInfoPBRec pb;
        Str255 folderName = "\passets";
        memset(&pb, 0, sizeof(pb));
        pb.dirInfo.ioNamePtr   = folderName;
        pb.dirInfo.ioVRefNum   = gAppVRefNum;
        pb.dirInfo.ioDrDirID   = gAppDirID;
        pb.dirInfo.ioFDirIndex = 0;
        if (PBGetCatInfoSync(&pb) != noErr) return;
        gIconsDirID = pb.dirInfo.ioDrDirID;
    }

    if (name != gIconCacheName) {
        /* Cache miss — dispose old, load new */
        if (gIconCacheHandle != nil) { DisposeHandle(gIconCacheHandle); gIconCacheHandle = nil; }
        gIconCacheName.clear();

        string base = (name.length() <= 26) ? name : name.substr(0, 26);
        string filename = base + ".pict";
        Str255 pfname;
        pfname[0] = (unsigned char)filename.length();
        memcpy(pfname + 1, filename.c_str(), filename.length());

        FSSpec spec;
        if (FSMakeFSSpec(gAppVRefNum, gIconsDirID, pfname, &spec) != noErr) {
            DrawStatusText(("Icon not found: " + name).c_str());
            return;
        }

        short refNum;
        if (FSpOpenDF(&spec, fsRdPerm, &refNum) != noErr) return;

        long fileSize = 0;
        if (GetEOF(refNum, &fileSize) != noErr || fileSize <= 512) { FSClose(refNum); return; }
        if (SetFPos(refNum, fsFromStart, 512)  != noErr)           { FSClose(refNum); return; }

        long pictSize = fileSize - 512;
        Handle h = NewHandle(pictSize);
        if (h == nil) { FSClose(refNum); return; }

        HLock(h);
        OSErr readErr = FSRead(refNum, &pictSize, *h);
        HUnlock(h);
        FSClose(refNum);
        if (readErr != noErr) { DisposeHandle(h); return; }

        gIconCacheHandle = h;
        gIconCacheName   = name;
    }

    Rect destRect;
    SetRect(&destRect, x, y, x + size, y + size);
    DrawPicture((PicHandle)gIconCacheHandle, &destRect);
}

/* ------------------------------------------------------------------ */
/* Icon picker constants and state                                     */
/* ------------------------------------------------------------------ */
static const int kPSCell  = 20;                      /* sprite cell size (240px / 12 cols) */
static const int kSprCols = 12;                      /* columns in sprite sheet (fixed) */
static const int kPCell   = kPSCell;                 /* display 1:1, sprite already has spacing */
static const int kPCols   = 12;                      /* display columns */
static const int kPVisR   = 12;                      /* visible rows */
static const int kPNameH  = 30;                      /* name label height */
static const int kPSBW    = 16;                      /* scroll bar width */
static const int kPSBGap  = 8;                       /* gap between grid and scroll bar */
static const int kPGridW  = kPCols * kPCell;         /* 240 */
static const int kPGridH  = kPVisR  * kPCell;        /* 240 */
static const int kPWinH   = kPGridH + kPNameH;       /* 258 */
static const int kPWinW   = kPGridW + kPSBGap + kPSBW; /* 264 */

static WindowPtr        sPickerWin       = nil;
static ControlHandle    sPickerSB        = nil;
static int              sPickerScrollRow = 0;
static string           sPickerSelected;
static int              sPickerHoverIdx  = -1;
static ControlActionUPP sPickerScrollUPP = nil;
static bool             sPickerSearchMode = false;
static string           sPickerQuery;
static vector<string>   sPickerFiltered;   /* icon names in display order */
static vector<int>      sPickerSpriteIdx;  /* corresponding sprite indices */
static void DrawPickerName();

static void BuildPickerFilter() {
    sPickerFiltered.clear();
    sPickerSpriteIdx.clear();
    /* Lowercase the query once so per-entry comparison is a plain find() */
    string lq = sPickerQuery;
    for (int j = 0; j < (int)lq.size(); j++)
        lq[j] = (char)tolower((unsigned char)lq[j]);
    for (int i = 0; i < (int)gIconList.size(); i++) {
        if (lq.empty() || gIconListLower[i].find(lq) != string::npos) {
            sPickerFiltered.push_back(gIconList[i]);
            sPickerSpriteIdx.push_back(i);
        }
    }
}

/* ------------------------------------------------------------------ */
/* LoadIconList — read icon/iconorder.txt for sprite-order icon names  */
/* ------------------------------------------------------------------ */
static void LoadIconList() {
    gIconList.clear();
    /* Ensure gIconsDirID is resolved */
    if (gIconsDirID == 0) {
        CInfoPBRec pb;
        Str255 fn = "\passets";
        memset(&pb, 0, sizeof(pb));
        pb.dirInfo.ioNamePtr   = fn;
        pb.dirInfo.ioVRefNum   = gAppVRefNum;
        pb.dirInfo.ioDrDirID   = gAppDirID;
        pb.dirInfo.ioFDirIndex = 0;
        if (PBGetCatInfoSync(&pb) != noErr) return;
        gIconsDirID = pb.dirInfo.ioDrDirID;
    }
    FSSpec spec;
    if (FSMakeFSSpec(gAppVRefNum, gIconsDirID, (ConstStr255Param)"\piconorder.txt", &spec) != noErr) return;
    short refNum;
    if (FSpOpenDF(&spec, fsRdPerm, &refNum) != noErr) return;
    /* Read entire file then parse lines */
    long fileSize;
    GetEOF(refNum, &fileSize);
    Handle h = NewHandle(fileSize + 1);
    if (!h) { FSClose(refNum); return; }
    HLock(h);
    long count = fileSize;
    FSRead(refNum, &count, *h);
    FSClose(refNum);
    (*h)[fileSize] = '\0';
    /* Parse newline-separated names */
    char *p = *h, *end = *h + fileSize;
    while (p < end) {
        char *nl = p;
        while (nl < end && *nl != '\n' && *nl != '\r') nl++;
        if (nl > p) {
            /* strip trailing \r if CRLF */
            char *e = nl;
            if (e > p && *(e-1) == '\r') e--;
            gIconList.push_back(string(p, e));
        }
        p = nl + 1;
    }
    HUnlock(h);
    DisposeHandle(h);

    /* Build pre-lowercased copy for case-insensitive search */
    gIconListLower.resize(gIconList.size());
    for (int i = 0; i < (int)gIconList.size(); i++) {
        gIconListLower[i] = gIconList[i];
        for (int j = 0; j < (int)gIconListLower[i].size(); j++)
            gIconListLower[i][j] = (char)tolower((unsigned char)gIconListLower[i][j]);
    }
}

/* ------------------------------------------------------------------ */
/* LoadSprite — load iconsprite.pict into a locked Handle + BitMap     */
/* No GWorld needed: point the BitMap directly at the pixel data.      */
/* ------------------------------------------------------------------ */
static void LoadSprite() {
    if (gSpriteHandle != nil) return;  /* already loaded */

    /* Ensure gIconsDirID is resolved */
    if (gIconsDirID == 0) {
        CInfoPBRec pb;
        Str255 fn = "\passets";
        memset(&pb, 0, sizeof(pb));
        pb.dirInfo.ioNamePtr   = fn;
        pb.dirInfo.ioVRefNum   = gAppVRefNum;
        pb.dirInfo.ioDrDirID   = gAppDirID;
        pb.dirInfo.ioFDirIndex = 0;
        if (PBGetCatInfoSync(&pb) != noErr) return;
        gIconsDirID = pb.dirInfo.ioDrDirID;
    }

    Str255 pfname = "\piconsprite.pict";
    FSSpec spec;
    if (FSMakeFSSpec(gAppVRefNum, gIconsDirID, pfname, &spec) != noErr) return;

    short refNum;
    if (FSpOpenDF(&spec, fsRdPerm, &refNum) != noErr) return;

    long fileSize = 0;
    if (GetEOF(refNum, &fileSize) != noErr || fileSize <= 512) { FSClose(refNum); return; }
    if (SetFPos(refNum, fsFromStart, 512) != noErr)            { FSClose(refNum); return; }

    long pictSize = fileSize - 512;
    Handle h = NewHandle(pictSize);
    if (!h) { FSClose(refNum); return; }

    HLock(h);
    OSErr err = FSRead(refNum, &pictSize, *h);
    FSClose(refNum);
    if (err != noErr) { HUnlock(h); DisposeHandle(h); return; }

    /* Read picFrame (PICT v1: picSize(2) + picFrame(8) starting at byte 2) */
    Rect picFrame;
    memcpy(&picFrame, *h + 2, sizeof(Rect));
    short sprW = picFrame.right  - picFrame.left;
    short sprH = picFrame.bottom - picFrame.top;
    if (sprW <= 0 || sprH <= 0 || sprW > 2048 || sprH > 8192) {
        HUnlock(h); DisposeHandle(h); return;
    }

    /* Pixel data starts at offset 39 in our PICT v1 BitsRect:
       picSize(2)+picFrame(8)+opcode(1)+rowBytes(2)+bounds(8)+srcRect(8)+dstRect(8)+mode(2)=39 */
    short rowBytes = ((sprW + 15) / 16) * 2;
    gSpriteBM.baseAddr = *h + 39;
    gSpriteBM.rowBytes = rowBytes;
    SetRect(&gSpriteBM.bounds, 0, 0, sprW, sprH);

    gSpriteHandle = h;   /* keep locked — baseAddr must stay valid */
}

/* ------------------------------------------------------------------ */
/* DrawPickerGrid — render visible icon cells + name label             */
/* ------------------------------------------------------------------ */
static void DrawPickerGrid() {
    if (!sPickerWin) return;
    SetPort(sPickerWin);

    int total      = (int)sPickerFiltered.size();
    int totalSlots = total + 1;                            /* +1 for "no icon" cell */
    int realRows   = (total      + kPCols - 1) / kPCols;  /* rows backed by sprite */
    int totalRows  = (totalSlots + kPCols - 1) / kPCols;  /* rows incl. none cell  */

    /* Get destination bitmap — window may be CGrafPort on Color QD systems */
    BitMap*      dstBM = nil;
    PixMapHandle dstPM = nil;
    if (((CGrafPtr)sPickerWin)->portVersion & (short)0xC000) {
        dstPM = ((CGrafPtr)sPickerWin)->portPixMap;
        LockPixels(dstPM);
        dstBM = (BitMap*)(*dstPM);
    } else {
        dstBM = &sPickerWin->portBits;
    }

    /* Clear grid first so border artifacts from prior selection are gone.
       Add 4px margin to catch the outer selection border that can overflow. */
    { Rect gridR; SetRect(&gridR, 0, 0, kPGridW + 4, kPGridH + 4); EraseRect(&gridR); }

    /* Per-cell blit: use sPickerSpriteIdx to map display index → sprite position */
    if (gSpriteHandle != nil && dstBM != nil) {
        for (int dr = 0; dr < kPVisR; dr++) {
            for (int dc = 0; dc < kPCols; dc++) {
                int idx = (dr + sPickerScrollRow) * kPCols + dc;
                if (idx >= total) break;
                int sprIdx = sPickerSpriteIdx[idx];
                int sc = sprIdx % kSprCols;
                int sr = sprIdx / kSprCols;
                Rect srcR; SetRect(&srcR, sc * kPSCell, sr * kPSCell,
                                   (sc+1) * kPSCell, (sr+1) * kPSCell);
                Rect dstR; SetRect(&dstR, dc * kPCell, dr * kPCell,
                                   (dc+1) * kPCell, (dr+1) * kPCell);
                CopyBits(&gSpriteBM, dstBM, &srcR, &dstR, srcCopy, nil);
            }
        }
    }

    /* Draw "no icon" cell — spans rest of its row so text has room */
    {
        int noneAbsRow = total / kPCols;
        int noneCol    = total % kPCols;
        int noneRow    = noneAbsRow - sPickerScrollRow;
        if (noneRow >= 0 && noneRow < kPVisR) {
            /* Erase from noneCol to end of row */
            Rect nr; SetRect(&nr,
                             noneCol * kPCell, noneRow * kPCell,
                             kPGridW,          (noneRow + 1) * kPCell);
            EraseRect(&nr);
            TextFont(gFontHelvetica); TextFace(0); TextSize(12);
            const char* noIconLabel = "no Icon";
            short tw = TextWidth(noIconLabel, 0, 7);
            short midX = nr.left + (nr.right - nr.left - tw) / 2;
            short midY = nr.top + (kPCell + 9) / 2;  /* vertically centered */
            MoveTo(midX, midY);
            DrawText(noIconLabel, 0, 7);
        }
    }

    /* Draw selection frame on top of blitted sprite */
    {
        int selIdx = -1;
        if (!sPickerSelected.empty()) {
            for (int i = 0; i < total; i++) {
                if (sPickerFiltered[i] == sPickerSelected) { selIdx = i; break; }
            }
        } else {
            selIdx = total;  /* "no icon" cell selected */
        }
        if (selIdx >= 0) {
            int absRow = selIdx / kPCols;
            int col    = selIdx % kPCols;
            int row    = absRow - sPickerScrollRow;
            if (row >= 0 && row < kPVisR) {
                bool isNone = (selIdx == total);
                Rect hr; SetRect(&hr,
                                 col * kPCell + 3, row * kPCell + 3,
                                 isNone ? kPGridW + 1 : col * kPCell + kPCell + 1,
                                 row * kPCell + kPCell + 1);
                InvertRect(&hr);
                FrameRect(&hr);
                InsetRect(&hr, -3, -3);
                PenSize(2, 2);
                FrameRect(&hr);
                PenSize(1, 1);
            }
        }
    }

    if (dstPM) UnlockPixels(dstPM);

    /* Name label */
    DrawPickerName();
}

/* ------------------------------------------------------------------ */
/* DrawPickerName — redraw just the name label (hover or selected)     */
/* ------------------------------------------------------------------ */
static void DrawPickerName() {
    if (!sPickerWin) return;
    SetPort(sPickerWin);
    Rect nameR = {kPGridH, 0, kPWinH, kPGridW};
    EraseRect(&nameR);
    TextFont(gFontHelvetica); TextFace(0); TextSize(12);

    /* Magnifying glass: 8×8 oval + diagonal handle */
    {
        short gy = kPGridH + 10;
        Rect  lens; SetRect(&lens, 3, gy, 11, gy + 8);
        FrameOval(&lens);
        MoveTo(10, gy + 7); LineTo(13, gy + 10);
    }

    int total = (int)sPickerFiltered.size();
    static const string sNoneLabel = "(none)";

    /* Hover takes priority: show icon name while mouse is over a cell */
    if (sPickerHoverIdx >= 0 && sPickerHoverIdx <= total) {
        const string& label = (sPickerHoverIdx < total)
                              ? sPickerFiltered[sPickerHoverIdx]
                              : sNoneLabel;
        if (!label.empty()) {
            MoveTo(17, kPGridH + 20);
            DrawText(label.c_str(), 0, (short)label.length());
        }
    } else if (sPickerSearchMode) {
        /* Not hovering: show framed search input */
        Rect boxR; SetRect(&boxR, 16, kPGridH + 8, kPGridW - 2, kPWinH - 2);
        FrameRect(&boxR);
        string display = sPickerQuery + "|";
        MoveTo(19, kPGridH + 20);
        DrawText(display.c_str(), 0, (short)display.length());
    } else {
        /* Not hovering, not searching: show current selection */
        if (!sPickerSelected.empty()) {
            MoveTo(17, kPGridH + 20);
            DrawText(sPickerSelected.c_str(), 0, (short)sPickerSelected.length());
        }
    }
}

/* ------------------------------------------------------------------ */
/* PickerScrollAction — live scrolling for picker arrow/page clicks    */
/* ------------------------------------------------------------------ */
static pascal void PickerScrollAction(ControlHandle ctl, short part) {
    if (part == 0) return;
    short delta = 0;
    switch (part) {
        case 20: delta = -1;          break;  /* inUpButton   */
        case 21: delta =  1;          break;  /* inDownButton */
        case 22: delta = -(kPVisR-1); break;  /* inPageUp     */
        case 23: delta =  (kPVisR-1); break;  /* inPageDown   */
        default: return;
    }
    short v = GetControlValue(ctl) + delta;
    if (v < GetControlMinimum(ctl)) v = GetControlMinimum(ctl);
    if (v > GetControlMaximum(ctl)) v = GetControlMaximum(ctl);
    if (v == GetControlValue(ctl)) return;
    SetControlValue(ctl, v);
    sPickerScrollRow = v;
    DrawPickerGrid();
    DrawControls(sPickerWin);
}

/* Redraw only the page icon area in the main window (no block relayout) */
static void RedrawPageIcon() {
    SetPort(_window);
    short ix = gColLeft + 2;
    short iy = 48;  /* scroll is 0 while picker is open */
    Rect area; SetRect(&area, ix - 2, iy - 2, ix + 44, iy + 44);
    EraseRect(&area);
    SetRect(&gIconRect, 0, 0, 0, 0);
    if (!gPageIconName.empty()) {
        DrawNotionIcon(gPageIconName, ix, iy);
        SetRect(&gIconRect, ix, iy, ix + 40, iy + 40);
    }
}

/* ------------------------------------------------------------------ */
/* ShowIconPicker — modal icon grid; click to apply, Esc to cancel     */
/* ------------------------------------------------------------------ */
static void ShowIconPicker() {
    SetCursor(*GetCursor(watchCursor));
    if (gIconList.empty()) LoadIconList();
    if (gIconList.empty()) { InitCursor(); return; }
    LoadSprite();
    InitCursor();

    sPickerSearchMode = false;
    sPickerQuery.clear();
    BuildPickerFilter();

    int total      = (int)sPickerFiltered.size();
    int totalSlots = total + 1;  /* +1 for "no icon" cell */
    int totalRows  = (totalSlots + kPCols - 1) / kPCols;
    int maxScroll  = (totalRows > kPVisR) ? totalRows - kPVisR : 0;

    sPickerSelected  = gPageIconName;
    sPickerScrollRow = 0;
    sPickerHoverIdx  = -1;
    string originalIcon = gPageIconName;  /* saved for Esc cancel */

    /* Pre-scroll to show current icon (or "no icon" cell if empty) */
    if (sPickerSelected.empty()) {
        sPickerHoverIdx = total;  /* keyboard starts at "no icon" */
        int row = total / kPCols;
        sPickerScrollRow = row - kPVisR / 2;
        if (sPickerScrollRow < 0) sPickerScrollRow = 0;
        if (sPickerScrollRow > maxScroll) sPickerScrollRow = maxScroll;
    } else {
        for (int i = 0; i < total; i++) {
            if (sPickerFiltered[i] == sPickerSelected) {
                sPickerHoverIdx = i;  /* keyboard starts at current icon */
                int row = i / kPCols;
                sPickerScrollRow = row - kPVisR / 2;
                if (sPickerScrollRow < 0) sPickerScrollRow = 0;
                if (sPickerScrollRow > maxScroll) sPickerScrollRow = maxScroll;
                break;
            }
        }
    }

    /* Scroll the main window to the top so the page icon is visible for live preview */
    if (gScrollOffset != 0) {
        gScrollOffset = 0;
        updateBlocks();
    }

    /* Center on screen */
    short wx = (qd.screenBits.bounds.right  - kPWinW) / 2;
    short wy = (qd.screenBits.bounds.bottom - kPWinH) / 2;
    Rect  winRect; SetRect(&winRect, wx, wy, wx + kPWinW, wy + kPWinH);

    sPickerWin = NewWindow(nil, &winRect, "\pSelect Icon", true,
                           dBoxProc, (WindowPtr)-1, false, 0L);
    Rect sbRect; SetRect(&sbRect, kPGridW + kPSBGap, -1, kPWinW + 1, kPGridH + 1);
    sPickerSB = NewControl(sPickerWin, &sbRect, "\p", true,
                           sPickerScrollRow, 0, maxScroll, 16 /* scrollBarProc */, 0L);

    if (!sPickerScrollUPP)
        sPickerScrollUPP = NewControlActionUPP(PickerScrollAction);

    SetPort(sPickerWin);
    DrawPickerGrid();
    DrawControls(sPickerWin);

    bool done = false;
    while (!done) {
        EventRecord ev = {};
        GetNextEvent(everyEvent, &ev);
        switch (ev.what) {
            case mouseDown: {
                WindowPtr hitWin;
                short wPart = FindWindow(ev.where, &hitWin);
                if (hitWin != sPickerWin) { done = true; break; }
                SetPort(sPickerWin);
                Point lp = ev.where; GlobalToLocal(&lp);
                if (wPart == inContent) {
                    ControlHandle hitCtl;
                    short cPart = FindControl(lp, sPickerWin, &hitCtl);
                    if (hitCtl == sPickerSB) {
                        if (cPart == kControlIndicatorPart) {
                            TrackControl(sPickerSB, lp, nil);
                            sPickerScrollRow = GetControlValue(sPickerSB);
                            DrawPickerGrid(); DrawControls(sPickerWin);
                        } else {
                            TrackControl(sPickerSB, lp, sPickerScrollUPP);
                        }
                    } else if (lp.v >= kPGridH) {
                        /* Click on name bar — enter search mode */
                        sPickerSearchMode = true;
                        DrawPickerName();
                    } else if (lp.v < kPGridH && lp.h < kPGridW) {
                        int col = lp.h / kPCell;
                        int row = lp.v / kPCell;
                        int idx = (row + sPickerScrollRow) * kPCols + col;
                        if (col < kPCols && idx >= 0 && idx < total) {
                            gPageIconName   = sPickerFiltered[idx];
                            gIconDirty      = true;
                            gHasDirtyBlocks = true;
                            gLastEditTick   = TickCount();
                            done            = true;
                        } else if (col < kPCols && idx >= total) {
                            /* any cell at or after the "no icon" slot = clear icon */
                            gPageIconName   = "";
                            gIconDirty      = true;
                            gHasDirtyBlocks = true;
                            gLastEditTick   = TickCount();
                            done            = true;
                        }
                    }
                } else if (wPart == inDrag) {
                    DragWindow(sPickerWin, ev.where, &qd.screenBits.bounds);
                }
                break;
            }
            case updateEvt: {
                if ((WindowPtr)ev.message == sPickerWin) {
                    BeginUpdate(sPickerWin);
                    DrawPickerGrid(); DrawControls(sPickerWin);
                    EndUpdate(sPickerWin);
                }
                break;
            }
            case autoKey:
            case keyDown: {
                char c = (char)(ev.message & charCodeMask);
                if (sPickerSearchMode && c != 0x1C && c != 0x1D && c != 0x1E && c != 0x1F
                                     && c != 0x0D && c != 0x03) {
                    /* Search input */
                    if (c == 0x1B) {
                        /* Esc exits search mode, clears query */
                        sPickerSearchMode = false;
                        sPickerQuery.clear();
                        BuildPickerFilter();
                    } else if ((c == 0x08 || c == 0x7F) && !sPickerQuery.empty()) {
                        sPickerQuery.erase(sPickerQuery.size() - 1, 1);
                        BuildPickerFilter();
                    } else if ((unsigned char)c >= 0x20) {
                        sPickerQuery += c;
                        BuildPickerFilter();
                    }
                    /* Reset scroll and scrollbar for new result set */
                    sPickerScrollRow = 0;
                    int ft = (int)sPickerFiltered.size();
                    int fRows = (ft + 1 + kPCols - 1) / kPCols;
                    int fMax  = (fRows > kPVisR) ? fRows - kPVisR : 0;
                    SetControlValue(sPickerSB, 0);
                    SetControlMaximum(sPickerSB, fMax);
                    SetPort(sPickerWin);
                    DrawPickerGrid();
                    DrawControls(sPickerWin);
                } else if (c == 0x1B) {
                    /* Esc — restore original, no save */
                    gPageIconName = originalIcon;
                    done = true;
                } else if (c == 0x0D || c == 0x03) {
                    /* Enter/Return — confirm keyboard selection */
                    gPageIconName   = sPickerSelected;
                    gIconDirty      = true;
                    gHasDirtyBlocks = true;
                    gLastEditTick   = TickCount();
                    done = true;
                } else {
                    /* Arrow keys */
                    int total2 = (int)sPickerFiltered.size();
                    int curIdx = total2;  /* default: "no icon" cell */
                    if (!sPickerSelected.empty()) {
                        for (int i = 0; i < total2; i++) {
                            if (sPickerFiltered[i] == sPickerSelected) { curIdx = i; break; }
                        }
                    }
                    int newIdx = curIdx;
                    if      (c == 0x1C) newIdx = curIdx - 1;
                    else if (c == 0x1D) newIdx = curIdx + 1;
                    else if (c == 0x1E) newIdx = curIdx - kPCols;
                    else if (c == 0x1F) newIdx = curIdx + kPCols;
                    if (newIdx < 0) newIdx = 0;
                    if (newIdx > total2) newIdx = total2;
                    if (newIdx != curIdx) {
                        sPickerSelected = (newIdx < total2) ? sPickerFiltered[newIdx] : "";
                        int selRow = newIdx / kPCols;
                        if (selRow < sPickerScrollRow) {
                            sPickerScrollRow = selRow;
                            SetControlValue(sPickerSB, sPickerScrollRow);
                        } else if (selRow >= sPickerScrollRow + kPVisR) {
                            sPickerScrollRow = selRow - kPVisR + 1;
                            SetControlValue(sPickerSB, sPickerScrollRow);
                        }
                        gPageIconName = sPickerSelected;
                        RedrawPageIcon();
                        SetPort(sPickerWin);
                        DrawPickerGrid();
                        DrawControls(sPickerWin);
                        DrawPickerName();
                    }
                }
                break;
            }
            case nullEvent: {
                Point mp = ev.where;
                SetPort(sPickerWin);
                GlobalToLocal(&mp);
                int total2 = (int)sPickerFiltered.size();
                int newHover = -1;
                if (mp.v >= 0 && mp.v < kPGridH && mp.h >= 0 && mp.h < kPGridW) {
                    int col = mp.h / kPCell;
                    int row = mp.v / kPCell;
                    int idx = (row + sPickerScrollRow) * kPCols + col;
                    if (col < kPCols && idx >= 0 && idx < total2)
                        newHover = idx;
                    else if (col < kPCols && idx >= total2)
                        newHover = total2;
                }
                if (newHover != sPickerHoverIdx) {
                    sPickerHoverIdx = newHover;
                    DrawPickerName();
                }
                break;
            }
        }
    }

    sPickerHoverIdx = -1;
    DisposeControl(sPickerSB);
    DisposeWindow(sPickerWin);
    sPickerWin = nil; sPickerSB = nil;

    /* Always redraw main window — live preview may have changed it */
    SetPort(_window);
    updateBlocks();
}

/* ------------------------------------------------------------------ */
/* Emoji picker — select an emoji to insert into the active text field */
/* ------------------------------------------------------------------ */
static const int kEPPad   = 3;                /* padding around each 9×9 tile */
static const int kEPCell  = EMOJI_TILE_W + kEPPad * 2;  /* 9 + 6 = 15px per cell */
static const int kEPCols  = 16;
static const int kEPVisR  = 14;               /* visible rows */
static const int kEPBotH  = 30;               /* hint bar height */
static const int kEPSBW   = 16;
static const int kEPSBGap = 8;
static const int kEPGridW = kEPCols * kEPCell;              /* 240 */
static const int kEPGridH = kEPVisR  * kEPCell;              /* 210 */
static const int kEPWinW  = kEPGridW + kEPSBGap + kEPSBW;   /* 264 */
static const int kEPWinH  = kEPGridH + kEPBotH;              /* 228 */

static WindowPtr        sEPWin         = nil;
static int              sEPScrollRow   = 0;
static int              sEPSelIdx      = 0;   /* index into sEPFiltered */
static ControlActionUPP sEPScrollUPP   = nil;
static string           sEPQuery;
static vector<int>      sEPFiltered;           /* EMOJI_TABLE indices matching current query */
static bool             sEPSearchActive = false; /* true when search bar is focused */

static void BuildEmojiFilter() {
    sEPFiltered.clear();
    if (sEPQuery.empty()) {
        sEPFiltered.reserve(EMOJI_COUNT);
        /* Use Mac/Unicode category order (Smileys first) instead of UTF-8 sort order */
        for (int i = 0; i < EMOJI_COUNT; i++) sEPFiltered.push_back(EMOJI_DISPLAY_ORDER[i]);
        return;
    }
    /* Lowercase query for case-insensitive match */
    string lq = sEPQuery;
    for (size_t i = 0; i < lq.size(); i++)
        lq[i] = (char)tolower((unsigned char)lq[i]);
    for (int i = 0; i < EMOJI_COUNT; i++)
        if (strstr(EMOJI_NAMES[i], lq.c_str())) sEPFiltered.push_back(i);
}

static void DrawEmojiPickerGrid(int hoverIdx);  /* forward */

static pascal void EmojiPickerScrollAction(ControlHandle ctl, short part) {
    if (part == 0) return;
    short delta = 0;
    switch (part) {
        case 20: delta = -1;           break;
        case 21: delta =  1;           break;
        case 22: delta = -(kEPVisR-1); break;
        case 23: delta =  (kEPVisR-1); break;
        default: return;
    }
    short v = GetControlValue(ctl) + delta;
    if (v < GetControlMinimum(ctl)) v = GetControlMinimum(ctl);
    if (v > GetControlMaximum(ctl)) v = GetControlMaximum(ctl);
    if (v == GetControlValue(ctl)) return;
    SetControlValue(ctl, v);
    sEPScrollRow = v;
    DrawEmojiPickerGrid(-1);
    DrawControls(sEPWin);
}

static void DrawEmojiSearchBar(const string& query, int hoverIdx = -1) {
    if (!sEPWin) return;
    SetPort(sEPWin);
    Rect bar; SetRect(&bar, 0, kEPGridH, kEPGridW, kEPWinH);
    EraseRect(&bar);
    TextFont(gFontHelvetica); TextFace(0); TextSize(12);

    /* Magnifying glass: identical to icon picker */
    {
        short gy = kEPGridH + 10;
        Rect lens; SetRect(&lens, 3, gy, 11, gy + 8);
        FrameOval(&lens);
        MoveTo(10, gy + 7); LineTo(13, gy + 10);
    }

    /* Hovering an emoji: show its name */
    if (hoverIdx >= 0 && hoverIdx < (int)sEPFiltered.size()) {
        int ti = sEPFiltered[hoverIdx];
        const char* ename = EMOJI_NAMES[ti];
        if (ename && *ename) {
            MoveTo(17, kEPGridH + 20);
            DrawText(ename, 0, (short)strlen(ename));
        }
    } else {
        /* Not hovering: show search input; border when focused */
        if (sEPSearchActive) {
            Rect boxR; SetRect(&boxR, 16, kEPGridH + 8, kEPGridW - 2, kEPWinH - 2);
            FrameRect(&boxR);
        }
        MoveTo(17, kEPGridH + 20);
        if (!query.empty()) DrawText(query.c_str(), 0, (short)query.size());
        /* Blinking cursor */
        if ((TickCount() / 30) % 2 == 0) {
            short cx = 19 + (query.empty() ? 0 : TextWidth(query.c_str(), 0, (short)query.size()));
            MoveTo(cx, kEPGridH + 11); LineTo(cx, kEPGridH + 21);
        }
    }
}

/* Redraw a single cell in-place — used for cheap hover transitions.
   Avoids the full EraseRect+redraw that causes the grid to flash.     */
static void UpdateEmojiCell(int fi, bool isHover,
                             BitMap* srcBM, BitMap* dstBM) {
    if (fi < 0 || fi >= (int)sEPFiltered.size()) return;
    int row = fi / kEPCols - sEPScrollRow;
    int col = fi % kEPCols;
    if (row < 0 || row >= kEPVisR) return;

    Rect cellR;
    SetRect(&cellR, col*kEPCell, row*kEPCell, (col+1)*kEPCell, (row+1)*kEPCell);
    EraseRect(&cellR);

    if (srcBM && dstBM) {
        int ti = sEPFiltered[fi];
        short sx = EMOJI_TABLE[ti].x, sy = EMOJI_TABLE[ti].y;
        Rect srcR; SetRect(&srcR, sx, sy, sx+EMOJI_TILE_W, sy+EMOJI_TILE_H);
        short dx = col*kEPCell + kEPPad, dy = row*kEPCell + kEPPad;
        Rect dstR; SetRect(&dstR, dx, dy, dx+EMOJI_TILE_W, dy+EMOJI_TILE_H);
        CopyBits(srcBM, dstBM, &srcR, &dstR, srcCopy, nil);
    }

    bool isSel = (fi == sEPSelIdx);
    Rect hr; SetRect(&hr, col*kEPCell+2, row*kEPCell+2,
                          col*kEPCell+kEPCell, row*kEPCell+kEPCell);
    if (isSel) { InvertRect(&hr); FrameRect(&hr); InsetRect(&hr,-2,-2); }
    if (isHover) { PenSize(2,2); FrameRect(&hr); PenSize(1,1); }
}

static void DrawEmojiPickerGrid(int hoverIdx) {
    if (!sEPWin || !gEmojiGWorld) return;
    SetPort(sEPWin);

    BitMap* dstBM = nil; PixMapHandle dstPM = nil;
    if (((CGrafPtr)sEPWin)->portVersion & (short)0xC000) {
        dstPM = ((CGrafPtr)sEPWin)->portPixMap;
        LockPixels(dstPM); dstBM = (BitMap*)(*dstPM);
    } else dstBM = &sEPWin->portBits;

    BitMap* srcBM = (BitMap*)*GetGWorldPixMap(gEmojiGWorld);
    int total = (int)sEPFiltered.size();

    { Rect gr; SetRect(&gr, 0, 0, kEPGridW + 4, kEPGridH + 4); EraseRect(&gr); }

    if (srcBM && dstBM) {
        for (int dr = 0; dr < kEPVisR; dr++) {
            for (int dc = 0; dc < kEPCols; dc++) {
                int displayIdx = (dr + sEPScrollRow) * kEPCols + dc;
                if (displayIdx >= total) break;
                int ti = sEPFiltered[displayIdx]; /* actual table index */
                short sx = EMOJI_TABLE[ti].x, sy = EMOJI_TABLE[ti].y;
                Rect srcR; SetRect(&srcR, sx, sy, sx + EMOJI_TILE_W, sy + EMOJI_TILE_H);
                short dx = (short)(dc * kEPCell + kEPPad);
                short dy = (short)(dr * kEPCell + kEPPad);
                Rect dstR; SetRect(&dstR, dx, dy, dx + EMOJI_TILE_W, dy + EMOJI_TILE_H);
                CopyBits(srcBM, dstBM, &srcR, &dstR, srcCopy, nil);
            }
        }
    }

    /* Show "no results" text if filter is empty */
    if (total == 0) {
        TextFont(gFontHelvetica); TextFace(0); TextSize(10);
        const char* msg = "No results";
        MoveTo((kEPGridW - TextWidth(msg,0,10))/2, kEPGridH/2);
        DrawText(msg, 0, 10); TextSize(12);
    }

    /* Selection + hover frames — fi is a displayIdx */
    auto drawFrame = [&](int fi, bool isHover) {
        if (fi < 0 || fi >= total) return;
        int row = fi / kEPCols - sEPScrollRow;
        int col = fi % kEPCols;
        if (row < 0 || row >= kEPVisR) return;
        Rect hr; SetRect(&hr, col*kEPCell+2, row*kEPCell+2,
                              col*kEPCell+kEPCell, row*kEPCell+kEPCell);
        if (!isHover) { InvertRect(&hr); FrameRect(&hr); InsetRect(&hr,-2,-2); }
        PenSize(2,2); FrameRect(&hr); PenSize(1,1);
    };
    drawFrame(sEPSelIdx, false);
    if (hoverIdx >= 0 && hoverIdx != sEPSelIdx) drawFrame(hoverIdx, true);

    if (dstPM) UnlockPixels(dstPM);

    DrawEmojiSearchBar(sEPQuery, hoverIdx);
}

/* Insert the emoji at EMOJI_TABLE[tableIdx] at the active TE cursor */
static void InsertPickedEmoji(int tableIdx) {
    if (!textActive || tableIdx < 0 || tableIdx >= EMOJI_COUNT) return;
    if (blockSelect < 0 || blockSelect >= (int)pageElm.size()) return;
    Block& b = pageElm[blockSelect];

    short teLen = (*textActive)->teLength;
    string rawTE;
    if (teLen > 0) rawTE.assign(*(*textActive)->hText, (size_t)teLen);
    int cursorPos = (*textActive)->selStart;
    int insertIdx = 0;
    for (int j = 0; j < cursorPos && j < (int)rawTE.size(); j++)
        if ((unsigned char)rawTE[j] == (unsigned char)'\x01') insertIdx++;

    if ((*textActive)->selStart != (*textActive)->selEnd) TEDelete(textActive);
    const char kIns[3] = { ' ', '\x01', ' ' };
    TEInsert(kIns, 3, textActive);

    string emojiUTF8(EMOJI_TABLE[tableIdx].utf8);
    if (insertIdx <= (int)b.emojiSeqs.size())
        b.emojiSeqs.insert(b.emojiSeqs.begin() + insertIdx, emojiUTF8);
    else
        b.emojiSeqs.push_back(emojiUTF8);

    short newLen = (*textActive)->teLength;
    string newRawTE;
    if (newLen > 0) newRawTE.assign(*(*textActive)->hText, (size_t)newLen);
    b.text = StripEmojiPad(newRawTE);
    b.teCachedWidth = 0; b.teCachedType.clear(); b.teCache.clear();
    b.dirty = true; gHasDirtyBlocks = true; gLastEditTick = TickCount();

    SetPort(_window);
    string teText2 = TextToTE(b.text, b.emojiSeqs);
    DrawBlockEmojis(textActive, teText2, b.emojiSeqs);
}

static void ShowEmojiPicker() {
    if (!textActive) return;
    LoadEmojiSprite();
    if (!gEmojiGWorld) return;

    sEPQuery.clear();
    sEPSearchActive = false;
    BuildEmojiFilter();
    sEPScrollRow = 0; sEPSelIdx = 0;

    auto calcMaxScroll = [&]() -> int {
        int total = (int)sEPFiltered.size();
        int rows  = (total + kEPCols - 1) / kEPCols;
        return (rows > kEPVisR) ? rows - kEPVisR : 0;
    };

    short wx = (qd.screenBits.bounds.right  - kEPWinW) / 2;
    short wy = (qd.screenBits.bounds.bottom - kEPWinH) / 2;
    Rect winRect; SetRect(&winRect, wx, wy, wx + kEPWinW, wy + kEPWinH);

    sEPWin = NewWindow(nil, &winRect, "\pSelect Emoji", true,
                       dBoxProc, (WindowPtr)-1, false, 0L);
    if (!sEPWin) return;

    Rect sbRect; SetRect(&sbRect, kEPGridW + kEPSBGap, -1, kEPWinW + 1, kEPGridH + 1);
    ControlHandle epSB = NewControl(sEPWin, &sbRect, "\p", true,
                                    0, 0, calcMaxScroll(), 16, 0L);
    if (!sEPScrollUPP) sEPScrollUPP = NewControlActionUPP(EmojiPickerScrollAction);

    SetPort(sEPWin);
    DrawEmojiPickerGrid(-1);
    DrawControls(sEPWin);

    int  resultIdx = -1;
    int  lastHover = -1;
    bool done      = false;
    while (!done) {
        EventRecord ev = {}; GetNextEvent(everyEvent, &ev);
        switch (ev.what) {
            case mouseDown: {
                WindowPtr hitWin; FindWindow(ev.where, &hitWin);
                if (hitWin != sEPWin) { done = true; break; }
                SetPort(sEPWin);
                Point lp = ev.where; GlobalToLocal(&lp);
                ControlHandle hitCtl; short cPart = FindControl(lp, sEPWin, &hitCtl);
                if (hitCtl == epSB) {
                    if (cPart == kControlIndicatorPart) {
                        TrackControl(epSB, lp, nil);
                        sEPScrollRow = GetControlValue(epSB);
                    } else {
                        TrackControl(epSB, lp, sEPScrollUPP);
                    }
                    DrawEmojiPickerGrid(-1); DrawControls(sEPWin);
                } else if (lp.v >= 0 && lp.v < kEPGridH && lp.h >= 0 && lp.h < kEPGridW) {
                    int col = lp.h / kEPCell, row = lp.v / kEPCell;
                    int displayIdx = (row + sEPScrollRow) * kEPCols + col;
                    if (displayIdx >= 0 && displayIdx < (int)sEPFiltered.size())
                        { resultIdx = sEPFiltered[displayIdx]; done = true; }
                } else if (lp.v >= kEPGridH) {
                    /* Click on search bar — activate input border */
                    sEPSearchActive = true;
                    DrawEmojiSearchBar(sEPQuery, lastHover);
                }
                break;
            }
            case updateEvt:
                if ((WindowPtr)ev.message == sEPWin) {
                    BeginUpdate(sEPWin); DrawEmojiPickerGrid(lastHover);
                    DrawControls(sEPWin); EndUpdate(sEPWin);
                }
                break;
            case keyDown: case autoKey: {
                char c = (char)(ev.message & charCodeMask);
                /* Esc: clear query first, then close */
                if (c == 0x1B) {
                    if (!sEPQuery.empty()) {
                        sEPQuery.clear(); BuildEmojiFilter();
                        sEPScrollRow = 0; sEPSelIdx = 0;
                        SetControlMaximum(epSB, calcMaxScroll());
                        SetControlValue(epSB, 0);
                        DrawEmojiPickerGrid(-1); DrawControls(sEPWin);
                    } else { done = true; }
                    break;
                }
                /* Enter/Return: confirm */
                if (c == 0x0D || c == 0x03) {
                    if (sEPSelIdx < (int)sEPFiltered.size())
                        resultIdx = sEPFiltered[sEPSelIdx];
                    done = true; break;
                }
                /* Arrow keys: navigate grid */
                if (c == 0x1C || c == 0x1D || c == 0x1E || c == 0x1F) {
                    int total = (int)sEPFiltered.size();
                    int newSel = sEPSelIdx;
                    if      (c == 0x1C) newSel--;
                    else if (c == 0x1D) newSel++;
                    else if (c == 0x1E) newSel -= kEPCols;
                    else if (c == 0x1F) newSel += kEPCols;
                    if (newSel < 0) newSel = 0;
                    if (newSel >= total) newSel = total > 0 ? total - 1 : 0;
                    if (newSel != sEPSelIdx) {
                        sEPSelIdx = newSel;
                        int selRow = sEPSelIdx / kEPCols;
                        if (selRow < sEPScrollRow) {
                            sEPScrollRow = selRow; SetControlValue(epSB, sEPScrollRow);
                        } else if (selRow >= sEPScrollRow + kEPVisR) {
                            sEPScrollRow = selRow - kEPVisR + 1;
                            SetControlValue(epSB, sEPScrollRow);
                        }
                        DrawEmojiPickerGrid(-1); DrawControls(sEPWin);
                    }
                    break;
                }
                /* Backspace: remove last char from query */
                if ((c == 0x08 || c == 0x7F) && !sEPQuery.empty()) {
                    sEPQuery.erase(sEPQuery.size() - 1, 1);
                    BuildEmojiFilter(); sEPScrollRow = 0; sEPSelIdx = 0;
                    SetControlMaximum(epSB, calcMaxScroll());
                    SetControlValue(epSB, 0);
                    DrawEmojiPickerGrid(-1); DrawControls(sEPWin); break;
                }
                /* Printable: add to query */
                if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7F) {
                    sEPSearchActive = true;
                    sEPQuery += (char)tolower((unsigned char)c);
                    BuildEmojiFilter(); sEPScrollRow = 0; sEPSelIdx = 0;
                    SetControlMaximum(epSB, calcMaxScroll());
                    SetControlValue(epSB, 0);
                    DrawEmojiPickerGrid(-1); DrawControls(sEPWin);
                }
                break;
            }
            case nullEvent: {
                /* Cursor blink: redraw search bar every ~15 ticks */
                static long lastBlink = 0;
                long now = TickCount();
                if (now - lastBlink > 15) { lastBlink = now; DrawEmojiSearchBar(sEPQuery, lastHover); }

                SetPort(sEPWin);
                Point mp = ev.where; GlobalToLocal(&mp);
                int newHover = -1;
                if (mp.v >= 0 && mp.v < kEPGridH && mp.h >= 0 && mp.h < kEPGridW) {
                    int col = mp.h / kEPCell, row = mp.v / kEPCell;
                    int idx = (row + sEPScrollRow) * kEPCols + col;
                    if (col < kEPCols && idx >= 0 && idx < (int)sEPFiltered.size())
                        newHover = idx;
                }
                if (newHover != lastHover) {
                    SetPort(sEPWin);
                    BitMap* dstBM = nil;
                    PixMapHandle dstPM = nil;
                    if (((CGrafPtr)sEPWin)->portVersion & (short)0xC000) {
                        dstPM = ((CGrafPtr)sEPWin)->portPixMap;
                        LockPixels(dstPM);
                        dstBM = (BitMap*)(*dstPM);
                    } else {
                        dstBM = &sEPWin->portBits;
                    }
                    BitMap* srcBM = (BitMap*)*GetGWorldPixMap(gEmojiGWorld);
                    int oldHover = lastHover;
                    lastHover = newHover;
                    UpdateEmojiCell(oldHover, false, srcBM, dstBM);
                    UpdateEmojiCell(newHover, true,  srcBM, dstBM);
                    if (dstPM) UnlockPixels(dstPM);
                    DrawEmojiSearchBar(sEPQuery, lastHover);
                }
                break;
            }
        }
    }

    DisposeControl(epSB);
    DisposeWindow(sEPWin);
    sEPWin = nil; sEPQuery.clear(); sEPFiltered.clear();

    SetPort(_window);
    updateBlocks();

    if (resultIdx >= 0)
        InsertPickedEmoji(resultIdx);
}

/* ------------------------------------------------------------------ */
/* Emoji sentinel helpers                                              */
/* \x01 is used as a single-char placeholder for multi-byte sequences */
/* ------------------------------------------------------------------ */
#define kEmojiSentinel '\x01'

/* TextToTE — wrap each sentinel with spaces so the emoji has breathing room */
static string TextToTE(const string& text, vector<string>& /*emojiSeqs*/) {
    if (text.find(kEmojiSentinel) == string::npos) return text;
    string out; out.reserve(text.size() + 4);
    for (size_t i = 0; i < text.size(); i++) {
        if ((unsigned char)text[i] == (unsigned char)kEmojiSentinel)
            { out += ' '; out += text[i]; out += ' '; }
        else
            out += text[i];
    }
    return out;
}

/* StripEmojiPad — remove the spaces TextToTE added around sentinels.
   Call this whenever TE content is read back into b.text. */
static string StripEmojiPad(const string& s) {
    if (s.find(kEmojiSentinel) == string::npos) return s;
    string out; out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == ' ') {
            bool adj = (i > 0 && (unsigned char)s[i-1] == (unsigned char)kEmojiSentinel)
                    || (i+1 < s.size() && (unsigned char)s[i+1] == (unsigned char)kEmojiSentinel);
            if (adj) continue;
        }
        out += s[i];
    }
    return out;
}

/* StripEmojiPadPos — map a cursor position in padded TE text to its
   position in the stripped (b.text) equivalent. */
static int StripEmojiPadPos(const string& padded, int tePos) {
    int out = 0;
    for (int i = 0; i < tePos && i < (int)padded.size(); i++) {
        if (padded[i] == ' ') {
            bool adj = (i > 0 && (unsigned char)padded[i-1] == (unsigned char)kEmojiSentinel)
                    || (i+1 < (int)padded.size() && (unsigned char)padded[i+1] == (unsigned char)kEmojiSentinel);
            if (adj) continue;
        }
        out++;
    }
    return out;
}

/* Reconstruct full UTF-8 text: each \x01 → next emoji sequence. */
static string TEToText(const string& teText, const vector<string>& emojiSeqs) {
    string result;
    size_t eidx = 0;
    for (size_t i = 0; i < teText.size(); ++i) {
        if ((unsigned char)teText[i] == (unsigned char)kEmojiSentinel && eidx < emojiSeqs.size())
            result += emojiSeqs[eidx++];
        else
            result += teText[i];
    }
    return result;
}

/* ------------------------------------------------------------------ */
/* ApplyRunStyles — apply bold/italic/underline to a styled TE handle  */
/* Positions are in teText space (spaces around sentinels already in). */
/* ------------------------------------------------------------------ */
static void ApplyRunStyles(TEHandle te, const vector<TextRun>& runs) {
    if (runs.empty()) return;
    bool anyStyle = false;
    for (const TextRun& r : runs)
        if (r.bold || r.italic || r.underline || !r.url.empty()) { anyStyle = true; break; }
    if (!anyStyle) return;

    short savedStart = (*te)->selStart;
    short savedEnd   = (*te)->selEnd;
    short tePos = 0;
    for (const TextRun& run : runs) {
        short runStart = tePos;
        for (size_t i = 0; i < run.text.size(); i++)
            tePos += ((unsigned char)run.text[i] == (unsigned char)kEmojiSentinel) ? 3 : 1;
        short runEnd = tePos;
        TESetSelect(runStart, runEnd, te);
        TextStyle ts;
        ts.tsFont  = 0; ts.tsSize = 0;
        ts.tsFace  = (Style)((run.bold           ? bold      : 0)
                           | (run.italic         ? italic    : 0)
                           | (run.underline       ? underline : 0)
                           | (!run.url.empty()    ? underline : 0));
        TESetStyle(doFace, &ts, false, te);
    }
    TESetSelect(savedStart, savedEnd, te);
}

/* ------------------------------------------------------------------ */
/* RebuildRunsFromTE — resync blk.runs from the TE style scrap.       */
/* Must be called after blk.text has been updated from the TE.        */
/* Bold/italic/underline come from TE; strikethrough is carried over  */
/* from existing runs on a best-effort positional basis.              */
/* ------------------------------------------------------------------ */
static void RebuildRunsFromTE(Block& blk) {
    TEHandle te = blk.textEdit;
    if (!te) return;

    const string& btext = blk.text;
    int bn = (int)btext.size();
    if (bn == 0) {
        blk.runs.clear();
        TextRun r; r.text = ""; blk.runs.push_back(r);
        return;
    }

    /* Carry forward strikethrough from old runs (positional best-effort) */
    vector<bool> oldStrike(bn, false);
    { int ci = 0;
      for (const TextRun& r : blk.runs)
          for (size_t ri = 0; ri < r.text.size() && ci < bn; ri++, ci++)
              oldStrike[ci] = r.strikethrough;
    }

    /* Read bold/italic/underline per TE position from style scrap.
       TEGetStyleScrapHandle returns positions relative to the SELECTION start,
       so select all text first to get absolute positions covering the full text. */
    short teLen = (*te)->teLength;
    struct TStyle { bool b, i, u; };
    vector<TStyle> teS(teLen, {false,false,false});
    short selS = (*te)->selStart, selE = (*te)->selEnd;
    TESetSelect(0, teLen, te);
    StScrpHandle scrap = TEGetStyleScrapHandle(te);
    TESetSelect(selS, selE, te);
    if (scrap && *scrap) {
        HLock((Handle)scrap);
        short nRuns = (*scrap)->scrpNStyles;
        for (short s = 0; s < nRuns; s++) {
            long start = (*scrap)->scrpStyleTab[s].scrpStartChar;
            long end   = (s + 1 < nRuns)
                         ? (*scrap)->scrpStyleTab[s+1].scrpStartChar
                         : (long)teLen;
            Style face = (*scrap)->scrpStyleTab[s].scrpFace;
            bool b = (face & bold)      != 0;
            bool i = (face & italic)    != 0;
            bool u = (face & underline) != 0;
            for (long p = start; p < end && p < teLen; p++)
                teS[p] = {b, i, u};
        }
        HUnlock((Handle)scrap);
        DisposeHandle((Handle)scrap);
    }

    /* Walk blk.text, map TE positions → per-char style, build new runs */
    blk.runs.clear();
    TextRun cur;
    bool curB = false, curI = false, curU = false, curS = false;
    bool first = true;
    short tep = 0;

    for (int ci = 0; ci < bn; ci++) {
        bool b = (tep < teLen) ? teS[tep].b : false;
        bool i = (tep < teLen) ? teS[tep].i : false;
        bool u = (tep < teLen) ? teS[tep].u : false;
        bool s = oldStrike[ci];

        if (first || b != curB || i != curI || u != curU || s != curS) {
            if (!first && !cur.text.empty()) {
                cur.bold = curB; cur.italic = curI;
                cur.underline = curU; cur.strikethrough = curS;
                blk.runs.push_back(cur);
                cur = TextRun();
            }
            curB = b; curI = i; curU = u; curS = s;
            first = false;
        }
        cur.text += btext[ci];
        tep += ((unsigned char)btext[ci] == (unsigned char)kEmojiSentinel) ? 3 : 1;
    }
    if (!cur.text.empty()) {
        cur.bold = curB; cur.italic = curI;
        cur.underline = curU; cur.strikethrough = curS;
        blk.runs.push_back(cur);
    }
    if (blk.runs.empty()) {
        TextRun r; r.text = blk.text; blk.runs.push_back(r);
    }
}

/* ------------------------------------------------------------------ */
/* ToggleRunStyleBit — toggle one formatting flag on the current TE   */
/* selection, updating Block.runs and the TE style scrap in sync.     */
/* ------------------------------------------------------------------ */
enum { kStyleBold=0, kStyleItalic, kStyleUnderline, kStyleStrikethrough };

static void ToggleRunStyleBit(Block& blk, int which) {
    TEHandle te = blk.textEdit;
    if (!te) return;
    short selStart = (*te)->selStart;
    short selEnd   = (*te)->selEnd;
    if (selStart >= selEnd) return;

    const string& text = blk.text;
    int n = (int)text.size();
    if (n == 0) return;

    /* Ensure runs is populated with at least one default run */
    if (blk.runs.empty()) {
        TextRun r; r.text = text; blk.runs.push_back(r);
    }

    /* 1. Expand runs to a per-character style array */
    struct CS { bool b,i,u,s; };
    vector<CS> cs(n, {false,false,false,false});
    { int ci = 0;
      for (const TextRun& r : blk.runs)
          for (size_t ri = 0; ri < r.text.size() && ci < n; ri++, ci++)
              cs[ci] = {r.bold, r.italic, r.underline, r.strikethrough};
    }

    /* 2. Map TE selection to blk.text indices (emoji sentinel = 3 TE chars) */
    int txtStart = n, txtEnd = n;
    { short tep = 0;
      for (int ci = 0; ci < n; ci++) {
          short cw = ((unsigned char)text[ci] == (unsigned char)kEmojiSentinel) ? 3 : 1;
          if (txtStart == n && tep + cw > selStart) txtStart = ci;
          if (txtEnd   == n && tep >= selEnd)       { txtEnd = ci; break; }
          tep += cw;
      }
    }
    if (txtStart >= n || txtStart >= txtEnd) return;

    /* 3. Toggle direction: remove if all selected chars have the flag, else add */
    bool allHave = true;
    for (int ci = txtStart; ci < txtEnd; ci++) {
        bool has = (which==kStyleBold ? cs[ci].b : which==kStyleItalic ? cs[ci].i :
                    which==kStyleUnderline ? cs[ci].u : cs[ci].s);
        if (!has) { allHave = false; break; }
    }
    bool adding = !allHave;

    /* 4. Apply toggle to selected range */
    for (int ci = txtStart; ci < txtEnd; ci++) {
        switch (which) {
            case kStyleBold:          cs[ci].b = adding; break;
            case kStyleItalic:        cs[ci].i = adding; break;
            case kStyleUnderline:     cs[ci].u = adding; break;
            case kStyleStrikethrough: cs[ci].s = adding; break;
        }
    }

    /* 5. Reconstruct runs from per-character style array */
    blk.runs.clear();
    { TextRun cur; CS curStyle = cs[0];
      for (int ci = 0; ci < n; ci++) {
          CS& c = cs[ci];
          bool same = (c.b==curStyle.b && c.i==curStyle.i &&
                       c.u==curStyle.u && c.s==curStyle.s);
          if (!same && !cur.text.empty()) {
              cur.bold=curStyle.b; cur.italic=curStyle.i;
              cur.underline=curStyle.u; cur.strikethrough=curStyle.s;
              blk.runs.push_back(cur);
              cur=TextRun(); curStyle=c;
          }
          cur.text += text[ci];
      }
      if (!cur.text.empty()) {
          cur.bold=curStyle.b; cur.italic=curStyle.i;
          cur.underline=curStyle.u; cur.strikethrough=curStyle.s;
          blk.runs.push_back(cur);
      }
    }

    /* 6. Re-apply all run styles to TE (ApplyRunStyles restores the selection) */
    if (which != kStyleStrikethrough)
        ApplyRunStyles(te, blk.runs);

    /* 7. Redraw block and mark dirty */
    TEUpdate(&blk.vrect, te);
    DrawStrikethrough(te, blk.runs);
    blk.dirty        = true;
    gHasDirtyBlocks  = true;
    gLastEditTick    = TickCount();
}

/* ------------------------------------------------------------------ */
/* DrawStrikethrough — draw lines through strikethrough runs           */
/* ------------------------------------------------------------------ */
static void DrawStrikethrough(TEHandle te, const vector<TextRun>& runs) {
    bool any = false;
    for (const TextRun& r : runs) if (r.strikethrough) { any = true; break; }
    if (!any) return;

    static const Pattern kStrike = {{0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA}}; /* ~50% but shifted → ~30% on 1px line */
    /* 30% gray on a 1px horizontal line: use every-other-column pattern (0x55 or 0xAA).
       On a single-pixel-tall stroke PenPat repeats horizontally, giving ~50% coverage;
       alternating rows cancel to ~25–30% visual weight. */
    static const Pattern k30Gray = {{0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55}};

    short lineHt = (*te)->lineHeight;
    short strike = lineHt / 2 + 1;   /* pixels above baseline (moved 2px up from lineHt/2-1) */
    short tePos  = 0;
    PenPat(&k30Gray);
    for (const TextRun& run : runs) {
        short runStart = tePos;
        for (size_t i = 0; i < run.text.size(); i++)
            tePos += ((unsigned char)run.text[i] == (unsigned char)kEmojiSentinel) ? 3 : 1;
        short runEnd = tePos;
        if (!run.strikethrough) continue;
        short segStart = runStart;
        while (segStart < runEnd) {
            Point ptA = TEGetPoint(segStart, te);
            short segEnd = segStart + 1;
            while (segEnd < runEnd && TEGetPoint(segEnd, te).v == ptA.v) segEnd++;
            Point ptB = TEGetPoint(segEnd, te);
            short y = ptA.v - strike;
            MoveTo(ptA.h, y);
            LineTo(ptB.h - 1, y);
            segStart = segEnd;
        }
    }
    PenPat(&qd.black);
}

/* Load emoji_sprite.pict once into a locked BitMap (same approach as LoadSprite). */
static void LoadEmojiSprite() {
    if (gEmojiSpriteHandle != nil) return;
    if (gIconsDirID == 0) {
        CInfoPBRec pb; Str255 fn = "\passets";
        memset(&pb, 0, sizeof(pb));
        pb.dirInfo.ioNamePtr = fn; pb.dirInfo.ioVRefNum = gAppVRefNum;
        pb.dirInfo.ioDrDirID = gAppDirID; pb.dirInfo.ioFDirIndex = 0;
        if (PBGetCatInfoSync(&pb) != noErr) return;
        gIconsDirID = pb.dirInfo.ioDrDirID;
    }
    FSSpec spec;
    if (FSMakeFSSpec(gAppVRefNum, gIconsDirID,
                     (ConstStr255Param)"\pemoji_sprite.pict", &spec) != noErr) return;
    short refNum;
    if (FSpOpenDF(&spec, fsRdPerm, &refNum) != noErr) return;
    long fileSize = 0;
    if (GetEOF(refNum, &fileSize) != noErr || fileSize <= 512) { FSClose(refNum); return; }
    if (SetFPos(refNum, fsFromStart, 512) != noErr)             { FSClose(refNum); return; }
    long pictSize = fileSize - 512;
    Handle h = NewHandle(pictSize);
    if (!h) { FSClose(refNum); return; }
    HLock(h);
    OSErr ferr = FSRead(refNum, &pictSize, *h);
    FSClose(refNum);
    if (ferr != noErr) { HUnlock(h); DisposeHandle(h); return; }

    /* Read bounding rect from picFrame (PICT offset 2) */
    Rect picFrame;
    memcpy(&picFrame, *h + 2, sizeof(Rect));
    short sprW = picFrame.right  - picFrame.left;
    short sprH = picFrame.bottom - picFrame.top;
    if (sprW <= 0 || sprH <= 0) { HUnlock(h); DisposeHandle(h); return; }

    /* Create a 1-bit offscreen GWorld and render the PICT into it.
       This correctly handles any PICT version / compression (v2, PackBits,
       8-bit PixMap, etc.) without us having to parse the opcodes manually. */
    GWorldPtr gw = nil;
    if (NewGWorld(&gw, 1, &picFrame, nil, nil, 0) != noErr || !gw) {
        HUnlock(h); DisposeHandle(h); return;
    }
    PixMapHandle pm = GetGWorldPixMap(gw);
    LockPixels(pm);

    GWorldPtr savedGW; GDHandle savedGD;
    GetGWorld(&savedGW, &savedGD);
    SetGWorld(gw, nil);
    EraseRect(&picFrame);                /* clear to white */
    HUnlock(h);                          /* QD may move memory during DrawPicture */
    DrawPicture((PicHandle)h, &picFrame);
    SetGWorld(savedGW, savedGD);

    DisposeHandle(h);   /* PICT data no longer needed — pixels live in GWorld */
    gEmojiGWorld       = gw;
    gEmojiSpriteHandle = (Handle)1; /* non-nil sentinel: sprite is ready */
}

/* Strip U+FE0F variation selector bytes (\xef\xb8\x8f) from a UTF-8 string. */
static string StripVariationSelectors(const string& s) {
    string out;
    for (size_t i = 0; i < s.size(); ) {
        if (i + 2 < s.size() &&
            (unsigned char)s[i]   == 0xef &&
            (unsigned char)s[i+1] == 0xb8 &&
            (unsigned char)s[i+2] == 0x8f) { i += 3; }
        else { out += s[i++]; }
    }
    return out;
}

/* After TEUpdate, draw the correct emoji tile for every sentinel position. */
static void DrawBlockEmojis(TEHandle teh, const string& teText,
                             const vector<string>& emojiSeqs) {
    if (teText.find(kEmojiSentinel) == string::npos) return;
    LoadEmojiSprite();
    if (!gEmojiGWorld) return;

    /* Get destination BitMap (handle both b&w and colour QD ports) */
    BitMap*      dstBM = nil;
    PixMapHandle dstPM = nil;
    if (((CGrafPtr)_window)->portVersion & (short)0xC000) {
        dstPM = ((CGrafPtr)_window)->portPixMap;
        LockPixels(dstPM);
        dstBM = (BitMap*)(*dstPM);
    } else {
        dstBM = &(_window->portBits);
    }
    if (!dstBM) { if (dstPM) UnlockPixels(dstPM); return; }

    static const short kEmojiSrc  = EMOJI_TILE_W; /* 9 — source sprite size */
    static const short kEmojiDst  = 13;           /* rendered size          */
    short lineHt = (*teh)->lineHeight;
    if (lineHt < 8) lineHt = 12;
    short teLen   = (short)teText.size();
    int   emojiIdx = 0;

    PixMapHandle spritePM = GetGWorldPixMap(gEmojiGWorld);
    LockPixels(spritePM);

    for (short i = 0; i < teLen; ++i) {
        if ((unsigned char)teText[i] != (unsigned char)kEmojiSentinel) continue;

        /* Measure slot bounds */
        short baseline = TEGetPoint(i, teh).v;
        short left  = (i >= 1)         ? TEGetPoint(i - 1, teh).h : TEGetPoint(i, teh).h;
        short right = (i + 2 <= teLen) ? TEGetPoint(i + 2, teh).h : TEGetPoint(i, teh).h + lineHt;
        Rect slotR; SetRect(&slotR, left, baseline - lineHt, right, baseline);
        EraseRect(&slotR);

        /* Look up sprite coordinates for this specific emoji */
        const EmojiSprite* es = nullptr;
        if (emojiIdx < (int)emojiSeqs.size()) {
            string stripped = StripVariationSelectors(emojiSeqs[emojiIdx]);
            es = emoji_find(stripped.c_str());
        }
        emojiIdx++;

        if (!es) continue; /* unknown sentinel — slot already erased, skip draw */

        /* Scale 9→13, centred in the slot */
        short slotW   = right - left;
        short imgLeft = left + (slotW - kEmojiDst) / 2;
        short centerV = baseline - lineHt / 2;
        short imgTop  = centerV - kEmojiDst / 2 - 2;
        Rect srcR; SetRect(&srcR, es->x, es->y, es->x + kEmojiSrc, es->y + kEmojiSrc);
        Rect dstR; SetRect(&dstR, imgLeft, imgTop, imgLeft + kEmojiDst, imgTop + kEmojiDst);
        CopyBits((BitMap*)*spritePM, dstBM, &srcR, &dstR, srcCopy, nil);
    }

    UnlockPixels(spritePM);

    if (dstPM) UnlockPixels(dstPM);
}

/* ------------------------------------------------------------------ */
/* updateBlocks                                                        */
/* ------------------------------------------------------------------ */
void updateBlocks() {
    SetPort(_window);

    /* Content area rect — used for clip and (when not scrolling) erase */
    Rect contentRect = _window->portRect;
    contentRect.left   = kSidebarWidth;
    contentRect.right -= kScrollBarWidth;

    /* During scrolling, ScrollRect already shifted existing pixels and erased the
       exposed strip — skip the expensive full-area EraseRect to avoid flicker and
       wasted work.  For all other updates (resize, reload, type-change) we still
       need to erase everything so stale content doesn't show through. */
    if (!gScrolling) {
        Rect sideRect = _window->portRect;
        sideRect.right  = kSidebarWidth;
        sideRect.bottom = _window->portRect.bottom - 28;
        EraseRect(&sideRect);
        EraseRect(&contentRect);
    }

    /* Clip text drawing to content area; during scrolling restrict further to
       the newly-exposed strip so already-visible pixels aren't repainted.    */
    if (gScrollClipRgn)
        SetClip(gScrollClipRgn);
    else
        ClipRect(&contentRect);

    /* Centre a max-640px column in the content area — shared by icon and all blocks */
    short contentW  = (_window->portRect.right - kScrollBarWidth) - kSidebarWidth;
    short colWidth  = contentW < 640 ? contentW : 640;
    short centerOff = (contentW - colWidth) / 2;
    short colLeft   = kSidebarWidth + centerOff;
    short colRight  = colLeft + colWidth;
    gColLeft        = colLeft;

    /* Draw the page icon and store its rect for click detection */
    SetRect(&gIconRect, 0, 0, 0, 0);
    if (!gPageIconName.empty()) {
        short ix = colLeft + 2;
        short iy = 48 - gScrollOffset;
        DrawNotionIcon(gPageIconName, ix, iy);
        SetRect(&gIconRect, ix, iy, ix + 40, iy + 40);
    }

    SInt16 fontNum   = gFontHelvetica;
    SInt16 monacoNum = gFontGeneva;
    int top;
    int listCounter = 0;  /* counter for consecutive numbered_list_item blocks */

    for (vector<Block>::iterator p = pageElm.begin(); p != pageElm.end(); ++p) {
        Rect r, rect;

        if (p->pos > 0) {
            top = pageElm[(p->pos - 1)].rect.bottom;
        } else {
            top = 48 - gScrollOffset;   /* apply scroll offset at the origin */
            if (!gPageIconName.empty()) top += 64; /* 40px icon + 24px gap */
        }

        int blockTop = top; /* save original top before any type-specific adjustment */

        /* ---- Divider: draw a horizontal rule, no TE ---- */
        if (p->type == "divider") {
            if (p->textEdit != nil) { TEDispose(p->textEdit); p->textEdit = nil; }
            short divTop = top + 4;
            short divBot = divTop + 11;
            SetRect(&p->rect, colLeft, divTop - 4, colRight, divBot + 4 + 10); /* layout: 4px above + 11px + 4px below + 10px gap */
            p->vrect = p->rect;
            short midY = (divTop + divBot) / 2;
            short rightPad = (_window->portRect.right - _window->portRect.left) * 5 / 100;
            static const Pattern kDivider = {{0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88}}; /* 25% gray, pixel at col 0+4 every row */
            /* Selection highlight — 2px padding each side */
            bool divSel = ((int)(p - pageElm.begin()) == blockSelect);
            { Rect selR; SetRect(&selR, colLeft, divTop - 2, colRight - rightPad, divBot + 2);
              if (divSel) {
                  EraseRect(&selR);
                  PenPat(&qd.gray);
                  FrameRect(&selR);
                  PenPat(&qd.black);
              } else {
                  EraseRect(&selR);
              }
            }
            if (divSel) {
                MoveTo(colLeft + 2, midY);
                LineTo(colRight - rightPad, midY);
            } else {
                PenPat(&kDivider);
                MoveTo(colLeft + 2, midY);
                LineTo(colRight - rightPad, midY);
                PenPat(&qd.black);
            }
            { Rect gapR; SetRect(&gapR, colLeft, divBot + 2, colRight - rightPad, p->rect.bottom);
              EraseRect(&gapR); }
            continue;
        }

        int   offset = 0;
        float muli   = 1;
        /* effectiveDepth: child text/decorations align with parent's text edge (depth-1).
           toggleDepth: toggle triangles and offsets use actual depth so nested
           toggle headers are visually indented relative to their siblings. */
        int effectiveDepth = (p->isChild && p->depth > 0) ? p->depth - 1 : p->depth;
        int toggleDepth    = p->depth;

        if (p->type == "title") {
            TextFace(1);
            TextSize(26);
            muli = 1.6;
        } else if (p->type == "heading_1") {
            TextFace(1);
            TextSize(20);
            muli =  1.3;
            top  = top + 16;
        } else if (p->type == "heading_2") {
            TextFace(1);
            TextSize(16);
            muli = 1.2;
            top  = top + 12;
        } else if (p->type == "heading_3") {
            TextFace(1);
            TextSize(14);
            top = top + 6;
        } else if (p->type == "to_do") {
            TextFace(0);
            TextSize(12);
            offset = 19;
            SetRect(&r, colLeft + 4, top + 1, colLeft + 16, (top + 13));
            if (p->check) {
                PenSize(2, 2);
                FrameRect(&r);
                PenSize(1, 1);
                FillRect(&r, &qd.gray);
                /* White tick mark over the gray fill */
                PenPat(&qd.white);
                PenSize(2, 2);
                MoveTo(colLeft + 6,  top + 7);
                LineTo(colLeft + 8,  top + 10);
                LineTo(colLeft + 13, top + 4);
                PenPat(&qd.black);
                PenSize(1, 1);
            } else {
                FrameRect(&r);
            }
        } else if (p->type == "bulleted_list_item") {
            TextFace(0);
            TextSize(12);
            offset = 18 + effectiveDepth * 20;
            SetRect(&r, colLeft + 7 + effectiveDepth*20, (top + 6), colLeft + 11 + effectiveDepth*20, (top + 10));
            FrameOval(&r);
            FillOval(&r, &qd.black);
            listCounter = 0;
        } else if (p->type == "numbered_list_item") {
            TextFace(0);
            TextSize(12);
            offset = 18 + effectiveDepth * 20;
            listCounter++;
            {
                char numStr[8];
                numStr[0] = (char)(listCounter + '0');
                numStr[1] = '.';
                numStr[2] = '\0';
                TextFont(gFontGeneva);
                TextSize(12);
                TextFace(0);
                MoveTo(colLeft + 4 + effectiveDepth*20, top + 12);
                DrawText(numStr, 0, 2);
            }
        } else if (p->type == "toggle") {
            TextFace(0);
            TextSize(12);
            offset = 18 + toggleDepth * 20;
            listCounter = 0;
            short tx = colLeft + 6 + toggleDepth * 20;
            PolyHandle poly = OpenPoly();
            if (p->open) {
                /* ▼ down-pointing triangle */
                MoveTo(tx,     top + 3);
                LineTo(tx + 8, top + 3);
                LineTo(tx + 4, top + 11);
                LineTo(tx,     top + 3);
            } else {
                /* ▶ right-pointing triangle */
                MoveTo(tx, top + 3);
                LineTo(tx, top + 11);
                LineTo(tx + 6, top + 7);
                LineTo(tx, top + 3);
            }
            ClosePoly();
            FillPoly(poly, &qd.black);
            KillPoly(poly);
        } else if (p->type == "code") {
            listCounter = 0;
            TextFace(0);
            TextSize(12);
            offset = 3;
        } else if (p->type == "quote") {
            TextFace(0);
            TextSize(12);
            offset = 11;
            listCounter = 0;
            top = top + 8;
            SetRect(&r, colLeft + 3, (top - 1), colLeft + 5, (top + 15));
            FillRect(&r, &qd.black);
        } else if (p->type == "callout") {
            TextFace(0);
            TextSize(12);
            offset = 21;
            listCounter = 0;
            muli   = 0;
            top = blockTop + 12;   /* 4px border + 8px internal top padding */
        } else if (p->type == "toggle_heading_1" || p->type == "toggle_heading_2" || p->type == "toggle_heading_3") {
            listCounter = 0;
            offset = 18 + toggleDepth * 20;
            if (p->type == "toggle_heading_1") {
                TextFace(1); TextSize(20); muli = 1.3; top = top + 16;
            } else if (p->type == "toggle_heading_2") {
                TextFace(1); TextSize(16); muli = 1.2; top = top + 12;
            } else {
                TextFace(1); TextSize(14); top = top + 6;
            }
            { short tx = colLeft + 6 + toggleDepth * 20;
              PolyHandle poly = OpenPoly();
              if (p->open) {
                  MoveTo(tx,     top + 3); LineTo(tx + 8, top + 3);
                  LineTo(tx + 4, top + 11); LineTo(tx, top + 3);
              } else {
                  MoveTo(tx, top + 3); LineTo(tx, top + 11);
                  LineTo(tx + 6, top + 7); LineTo(tx, top + 3);
              }
              ClosePoly();
              FillPoly(poly, &qd.black);
              KillPoly(poly);
            }
        } else {
            TextFace(0);
            TextSize(12);
            listCounter = 0;
            /* Child paragraphs (plain text under a toggle) get the same indent
               as the parent toggle's text edge. */
            if (p->isChild) offset = 18 + effectiveDepth * 20;
        }

        /* Create TE with generous height so all text wraps correctly */
        short rightPad = (_window->portRect.right - _window->portRect.left) * 5 / 100;
        short teRight  = (p->type == "callout") ? colRight - rightPad - 8
                       : (p->type == "code")    ? colRight - rightPad - 4
                                                 : colRight - rightPad;
        short teLeft   = colLeft + 2 + offset;
        SetRect(&rect, teLeft, (p->type == "code") ? top + 8 : top, teRight, top + 4000);
        if (p->type == "code") {
            TextFont(gFontGeneva);
            TextFace(0);
        } else if (p->type == "title"    || p->type == "heading_1" ||
                   p->type == "heading_2" || p->type == "heading_3" ||
                   p->type == "toggle_heading_1" || p->type == "toggle_heading_2" ||
                   p->type == "toggle_heading_3") {
            TextFont(fontNum);   /* Helvetica for headings */
        } else {
            TextFont(gFontGeneva); /* Geneva for body text */
        }
        bool teNew = p->addTE(rect, rect, p->type);
        if (p->textEdit == nil) {
            /* TEStyleNew failed (out of heap) — give block a minimal rect and skip drawing */
            p->rect.bottom = top + 20;
            p->vrect = p->rect;
            continue;
        }
        /* lineHeight adjustment is persistent inside the TE struct — only apply
           once when the TE is freshly created, not on subsequent scroll updates. */
        if (teNew && (p->type == "paragraph" || p->type == "to_do" ||
            p->type == "bulleted_list_item" || p->type == "numbered_list_item" ||
            p->type == "toggle" || p->type == "quote" ||
            p->type == "callout"))
            (*p->textEdit)->lineHeight += 2;
        /* Compute teText once — reused for TESetText, cache update, and DrawBlockEmojis */
        string teText = TextToTE(p->text, p->emojiSeqs);
        {
          /* Skip TESetText when text is unchanged — avoids full re-layout.
             Also skip for the active TE when not freshly created: TEKey already
             updated its content; calling TESetText would reset the cursor and
             cause a visible flash on the first keystroke or last-char delete. */
          bool isActive = (p->textEdit != nil && p->textEdit == textActive);
          if (teNew || (teText != p->teCache && !isActive)) {
              TESetText(teText.c_str(), teText.length(), p->textEdit);
              ApplyRunStyles(p->textEdit, p->runs);
              /* TESetStyle uses redraw=false so line breaks aren't recalculated
                 with the styled (bold/italic) character widths. Call TECalText
                 now so the layout matches the actual drawn glyph widths. */
              TECalText(p->textEdit);
          }
          p->teCache = teText;
        }

        /* Use actual TE line layout for height instead of character-count estimate.
           Clamp to 1 so an empty block has the same height as a single-line block —
           this prevents nLines 0↔1 transitions from triggering full-page reflows. */
        short nLines     = (*p->textEdit)->nLines;
        if (nLines < 1) nLines = 1;
        short textHeight = TEGetHeight(nLines, 0, p->textEdit);
        int   padding    = (int)(8 * muli);
        p->rect.bottom = top + textHeight + padding;
        /* The lineHeight+2 types carry an extra 2px below the last line's
           descenders — trim it from the bottom border so it stays tight. */
        if (p->type == "paragraph" || p->type == "to_do" ||
            p->type == "bulleted_list_item" || p->type == "numbered_list_item" ||
            p->type == "toggle" || p->type == "quote" || p->type == "callout")
            p->rect.bottom -= 2;
        p->vrect                    = p->rect;

        (*p->textEdit)->viewRect    = p->vrect;
        (*p->textEdit)->destRect    = p->vrect;

        /* ---- Callout: simple 4px gray border ---- */
        if (p->type == "callout") {
            p->rect.top    = blockTop;
            p->rect.bottom = p->vrect.bottom + 24;  /* box bottom (8) + 16px gap */

            static const Pattern kBorder = {{0x88,0x00,0x22,0x00,0x88,0x00,0x22,0x00}}; /* 25% gray */
            Rect boxR; SetRect(&boxR, colLeft, blockTop + 4, colRight - rightPad, p->vrect.bottom + 8);
            FillRect(&boxR, &kBorder);
            InsetRect(&boxR, 4, 4);
            FillRect(&boxR, &qd.white);
            BackPat(&qd.white);
            { Rect gapR; SetRect(&gapR, colLeft, p->vrect.bottom + 8, colRight - rightPad, p->rect.bottom);
              EraseRect(&gapR); }
        }

        /* ---- Code: 12.5% gray background + 1px border ---- */
        if (p->type == "code") {
            short boxBottom = p->vrect.bottom + 4;  /* 4px bottom padding inside box */
            p->rect.top    = blockTop;
            p->rect.bottom = boxBottom + 16;  /* 16px gap below box */

            static const Pattern kCodeBg = {{0x80,0x00,0x00,0x00,0x08,0x00,0x00,0x00}}; /* 12.5% gray */
            Rect boxR; SetRect(&boxR, colLeft, blockTop + 4, colRight - rightPad, boxBottom);
            FillRect(&boxR, &kCodeBg);
            PenPat(&qd.gray);
            PenSize(1, 1);
            FrameRect(&boxR);
            PenPat(&qd.black);
            { Rect innerR = boxR; InsetRect(&innerR, 1, 1); EraseRect(&innerR); }
            { Rect gapR; SetRect(&gapR, colLeft, boxBottom, colRight - rightPad, p->rect.bottom);
              EraseRect(&gapR); }  /* clear 20px gap */
            /* Update vrect to match box interior — override inset top to match TE (top+4) */
            p->vrect = boxR;
            InsetRect(&p->vrect, 4, 4);
            p->vrect.top = blockTop + 8;
            (*p->textEdit)->viewRect = p->vrect;
            (*p->textEdit)->destRect = p->vrect;
        }

        /* Skip TEUpdate and emoji drawing for blocks entirely outside the
           visible port — saves time for long pages with many off-screen blocks. */
        { Rect portR = _window->portRect;
          if (p->vrect.bottom >= portR.top && p->vrect.top <= portR.bottom) {
              TEUpdate(&p->vrect, p->textEdit);
              DrawBlockEmojis(p->textEdit, teText, p->emojiSeqs);  /* reuse teText computed above */
              DrawStrikethrough(p->textEdit, p->runs);
              /* Dim checked to-do text: draw white lines on every other row */
              if (p->type == "to_do" && p->check) {
                  PenPat(&qd.white);
                  PenSize(1, 1);
                  for (short gy = p->vrect.top; gy < p->vrect.bottom; gy += 2) {
                      MoveTo(p->vrect.left, gy);
                      LineTo(p->vrect.right, gy);
                  }
                  PenPat(&qd.black);
              }
          }

          /* Loading indicator: shown while the first child-fetch for this toggle
             is in flight.  Reserves layout space and draws "Loading…" in the
             child-indent position; disappears when afterToggleLoad() calls
             updateBlocks() with gTogglePending already cleared.              */
          bool isLoadingToggle = gTogglePending && (gToggleIdx == p->pos) &&
              (p->type == "toggle" || p->type == "toggle_heading_1" ||
               p->type == "toggle_heading_2" || p->type == "toggle_heading_3");
          if (isLoadingToggle) {
              short lx = colLeft + 2 + 18 + effectiveDepth * 20;
              short ly = p->rect.bottom + 2;
              p->rect.bottom = ly + 20;   /* reserve space so the next block doesn't overlap */
              if (ly + 16 >= portR.top && ly < portR.bottom) {
                  TextFont(fontNum); TextSize(10); TextFace(italic);
                  MoveTo(lx, ly + 10);
                  DrawString("\pLoading\xC9"); /* "Loading…" — 0xC9 = ellipsis in Mac Roman */
                  TextFace(0); TextSize(12);
              }
          }
        }

        /* 4px gap after the last child of an open toggle */
        if (p->isChild) {
            vector<Block>::iterator next = p + 1;
            bool isLastChild = (next == pageElm.end())
                            || (!next->isChild)
                            || (next->depth < p->depth);
            if (isLastChild) p->rect.bottom += 6;
        }
    }

    /* Track unscrolled total height from last block + 20% bottom padding */
    if (!pageElm.empty()) {
        short winH = _window->portRect.bottom - _window->portRect.top;
        gTotalContentHeight = pageElm.back().rect.bottom + gScrollOffset
                              + winH / 5;
    }


    /* Show "Loading page…" below the title while blocks are still in flight */
    if (_curRequest == 1 && pageElm.size() == 1) {
        short ly = pageElm[0].rect.bottom + 8;
        Rect portR = _window->portRect;
        if (ly > portR.top && ly + 16 < portR.bottom - 28) {
            TextFont(gFontGeneva); TextFace(0); TextSize(12);
            MoveTo(colLeft + 2, ly + 10);
            DrawString("\pLoading more\xC9");
        }
    }

    /* Restore clip, update scroll bar */
    ClipRect(&_window->portRect);

    drawSidebarControls();

    updateScrollRange();
    if (gScrollBar != nil) DrawControls(_window);
}

/* ------------------------------------------------------------------ */
/* MakeNewWindow                                                       */
/* ------------------------------------------------------------------ */
static void DrawStatusText(const char *msg)
{
    if (_window == nil) return;
    SetPort(_window);
    EraseRect(&_window->portRect);
    TextFont(gFontGeneva); TextFace(0); TextSize(12);
    /* Word-wrap at ~60 chars per line, 14px line height.
       Vertically: start at 1/3 from top; horizontally: each line centered. */
    const int lineW = 60;
    const int lineH = 14;
    short winW = _window->portRect.right  - _window->portRect.left;
    short winH = _window->portRect.bottom - _window->portRect.top;
    int y = winH / 3;
    int len = strlen(msg);
    int i = 0;
    while (i < len) {
        int end = i + lineW;
        if (end >= len) { end = len; }
        else {
            /* break at last space before lineW */
            int sp = end;
            while (sp > i && msg[sp] != ' ') sp--;
            if (sp > i) end = sp;
        }
        short linePixW = TextWidth(msg, i, end - i);
        short x = (winW - linePixW) / 2;
        if (x < 4) x = 4;
        MoveTo(x, y);
        DrawText(msg, i, end - i);
        i = end;
        if (i < len && msg[i] == ' ') i++; /* skip space */
        y += lineH;
    }
}

void MakeNewWindow(short procID) {
    if (_window == nil) {
        SetRect(&_initialWindowRect, 6, 42, 505, 375);
        _window = NewWindow(NULL, &_initialWindowRect, "\pNotion classic",
                            true, procID, (WindowPtr)-1, true, 0);
    }
    /* Ensure the window is visible — it may have been hidden while the
       settings panel was shown (first-launch or HTTP error path). */
    ShowWindow(_window);

    /* Update title to page name — str[0] is the leading space, leaving
       253 chars for the title + null; SetWTitle expects a Pascal string
       so the buffer is treated as length-prefixed, max 254 content bytes. */
    char str[255];
    str[0] = ' ';
    strncpy(str + 1, pageElm[0].text.c_str(), 253);
    str[254] = '\0';
    SetWTitle(_window, (ConstStr255Param)str);

    /* Create vertical scroll bar */
    gScrollOffset = 0;
    gTotalContentHeight = 0;

    if (gScrollBar == nil) {
        Rect sbRect;
        SetRect(&sbRect,
                _window->portRect.right - kScrollBarWidth,
                _window->portRect.top    - 1,
                _window->portRect.right  + 1,
                _window->portRect.bottom - 14);   /* leave room for grow box */
        gScrollBar = NewControl(_window, &sbRect, "\p", true,
                                0, 0, 0, 16 /* scrollBarProc */, 0L);
    }

    if (gSaveButton == nil) {
        Rect btnRect;
        SetRect(&btnRect, 3, _window->portRect.bottom - 22,
                kSidebarWidth - 4, _window->portRect.bottom - 6);
        gSaveButton = NewControl(_window, &btnRect, (ConstStr255Param)"\pSave...",
                                 false /* hidden until dirty */, 0, 0, 1, 0, 0L);
    }

    updateBlocks();
    /* Mark the window contents as valid so the OS doesn't queue a redundant
       updateEvt that would cause a second full redraw/blink on page load. */
    ValidRect(&_window->portRect);
    if (blockSelect >= 0 && blockSelect < (int)pageElm.size())
        textActive = pageElm[blockSelect].textEdit;
}

/* ------------------------------------------------------------------ */
/* utf8_to_macroman                                                    */
/* ------------------------------------------------------------------ */
static string utf8_to_macroman(const string& utf8, vector<string>* emojiSeqs = nullptr) {
    /* Mac Roman non-ASCII characters, sorted by Unicode codepoint for binary search.
     * Binary search reduces average comparisons from ~64 (linear) to ~7. */
    static const struct { unsigned short cp; unsigned char mr; } kTable[] = {
        {0x00A0,0xCA},{0x00A1,0xC1},{0x00A2,0xA2},{0x00A3,0xA3},
        {0x00A5,0xB4},{0x00A7,0xA4},{0x00A8,0xAC},{0x00A9,0xA9},
        {0x00AA,0xBB},{0x00AB,0xC7},{0x00AC,0xC2},{0x00AE,0xA8},
        {0x00AF,0xF8},{0x00B0,0xA1},{0x00B1,0xB1},{0x00B4,0xAB},
        {0x00B5,0xB5},{0x00B6,0xA6},{0x00B7,0xE1},{0x00B8,0xFC},
        {0x00BA,0xBC},{0x00BB,0xC8},{0x00BF,0xC0},{0x00C0,0xCB},
        {0x00C1,0xE7},{0x00C2,0xE5},{0x00C3,0xCC},{0x00C4,0x80},
        {0x00C5,0x81},{0x00C6,0xAE},{0x00C7,0x82},{0x00C8,0xE9},
        {0x00C9,0x83},{0x00CA,0xE6},{0x00CB,0xE8},{0x00CC,0xED},
        {0x00CD,0xEA},{0x00CE,0xEB},{0x00CF,0xEC},{0x00D1,0x84},
        {0x00D2,0xF1},{0x00D3,0xEE},{0x00D4,0xEF},{0x00D5,0xCD},
        {0x00D6,0x85},{0x00D8,0xAF},{0x00D9,0xF4},{0x00DA,0xF2},
        {0x00DB,0xF3},{0x00DC,0x86},{0x00DF,0xA7},{0x00E0,0x88},
        {0x00E1,0x87},{0x00E2,0x89},{0x00E3,0x8B},{0x00E4,0x8A},
        {0x00E5,0x8C},{0x00E6,0xBE},{0x00E7,0x8D},{0x00E8,0x8F},
        {0x00E9,0x8E},{0x00EA,0x90},{0x00EB,0x91},{0x00EC,0x93},
        {0x00ED,0x92},{0x00EE,0x94},{0x00EF,0x95},{0x00F1,0x96},
        {0x00F2,0x98},{0x00F3,0x97},{0x00F4,0x99},{0x00F5,0x9B},
        {0x00F6,0x9A},{0x00F7,0xD6},{0x00F8,0xBF},{0x00F9,0x9D},
        {0x00FA,0x9C},{0x00FB,0x9E},{0x00FC,0x9F},{0x00FF,0xD8},
        {0x0131,0xF5},{0x0152,0xCE},{0x0153,0xCF},{0x0178,0xD9},
        {0x0192,0xC4},{0x02C6,0xF6},{0x02C7,0xFF},{0x02D8,0xF9},
        {0x02D9,0xFA},{0x02DA,0xFB},{0x02DB,0xFE},{0x02DC,0xF7},
        {0x02DD,0xFD},{0x03A9,0xBD},{0x03C0,0xB9},{0x2013,0xD0},
        {0x2014,0xD1},{0x2018,0xD4},{0x2019,0xD5},{0x201A,0xE2},
        {0x201C,0xD2},{0x201D,0xD3},{0x201E,0xE3},{0x2020,0xA0},
        {0x2021,0xE0},{0x2022,0xA5},{0x2026,0xC9},{0x2030,0xE4},
        {0x2039,0xDC},{0x203A,0xDD},{0x2044,0xDA},{0x20AC,0xDB},
        {0x2122,0xAA},{0x2192,0x3E},{0x2202,0xB6},{0x2206,0xC6},{0x220F,0xB8},
        {0x2211,0xB7},{0x221A,0xC3},{0x221E,0xB0},{0x222B,0xBA},
        {0x2248,0xC5},{0x2260,0xAD},{0x2264,0xB2},{0x2265,0xB3},
        {0x25CA,0xD7},{0xFB01,0xDE},{0xFB02,0xDF}
    };
    static const int kTableSize = (int)(sizeof(kTable)/sizeof(kTable[0]));

    string out;
    out.reserve(utf8.size());
    size_t i = 0;
    bool afterZWJ = false; /* true after ZWJ so next 4-byte merges into same sentinel */
    while (i < utf8.size()) {
        unsigned char b = (unsigned char)utf8[i];
        unsigned int cp;
        if (b < 0x80) {
            out += (char)b; i++; afterZWJ = false; continue;
        } else if (b < 0xC2) {
            i++; continue;  /* invalid/continuation byte */
        } else if (b < 0xE0) {
            if (i + 1 >= utf8.size()) break;
            cp = ((b & 0x1F) << 6) | ((unsigned char)utf8[i+1] & 0x3F);
            i += 2;
        } else if (b < 0xF0) {
            if (i + 2 >= utf8.size()) break;
            cp = ((b & 0x0F) << 12) | (((unsigned char)utf8[i+1] & 0x3F) << 6) | ((unsigned char)utf8[i+2] & 0x3F);
            i += 3;
        } else {
            /* 4-byte sequence: outside BMP — emoji */
            size_t seqLen = (i + 4 <= utf8.size()) ? 4 : utf8.size() - i;
            if (emojiSeqs) {
                /* If we just saw a ZWJ and there is already a sentinel in out,
                   merge this codepoint into the preceding entry (no extra sentinel). */
                bool merge = afterZWJ && !emojiSeqs->empty() &&
                             !out.empty() && (unsigned char)out.back() == (unsigned char)kEmojiSentinel;
                if (merge)
                    emojiSeqs->back() += utf8.substr(i, seqLen);
                else {
                    emojiSeqs->push_back(utf8.substr(i, seqLen));
                    out += kEmojiSentinel;
                }
            } else {
                out += kEmojiSentinel;
            }
            afterZWJ = false;
            i += seqLen; continue;
        }
        /* Binary search for codepoint in sorted Mac Roman table */
        unsigned char mr = 0;
        {
            int lo = 0, hi = kTableSize - 1;
            while (lo <= hi) {
                int mid = (lo + hi) >> 1;
                if      (kTable[mid].cp == (unsigned short)cp) { mr = kTable[mid].mr; break; }
                else if (kTable[mid].cp  < (unsigned short)cp)  lo = mid + 1;
                else                                             hi = mid - 1;
            }
        }
        if (mr != 0) {
            out += (char)mr; afterZWJ = false;
        } else if (cp == 0x200D ||                    /* ZWJ */
                   (cp >= 0xFE00 && cp <= 0xFE0F) ||  /* variation selectors */
                   cp == 0x20E3) {                     /* combining enclosing keycap */
            /* Modifier: append raw bytes to preceding emoji entry only if the
               immediately preceding output was a sentinel (guards against VS16
               after Mac-Roman chars like © that aren't sentinels). */
            size_t seqLen = (b < 0xE0) ? 2 : 3;
            bool prevIsSentinel = !out.empty() &&
                                  (unsigned char)out.back() == (unsigned char)kEmojiSentinel;
            if (emojiSeqs && !emojiSeqs->empty() && prevIsSentinel)
                emojiSeqs->back() += utf8.substr(i - seqLen, seqLen);
            /* No sentinel, no output char — just absorb the modifier */
            afterZWJ = (cp == 0x200D);
        } else {
            /* BMP codepoint not in Mac Roman — treat as emoji/special symbol */
            size_t seqLen = (b < 0xE0) ? 2 : 3;
            if (emojiSeqs) emojiSeqs->push_back(utf8.substr(i - seqLen, seqLen));
            out += kEmojiSentinel;
            afterZWJ = false;
        }
    }
    return out;
}

/* Convert b.text (Mac Roman + sentinels) back to UTF-8 for Notion API. */
static string block_to_utf8(const string& text, const vector<string>& emojiSeqs);

/* ------------------------------------------------------------------ */
/* macroman_to_utf8                                                    */
/* ------------------------------------------------------------------ */
static string macroman_to_utf8(const string& mac) {
    /* Direct lookup: index = byte - 0x80, value = Unicode codepoint (0 = unassigned).
     * Eliminates the O(n) linear scan — each byte is now a single array access. */
    static const unsigned short kMrToUnicode[128] = {
        /* 0x80 */ 0x00C4,0x00C5,0x00C7,0x00C9,0x00D1,0x00D6,0x00DC,0x00E1,
        /* 0x88 */ 0x00E0,0x00E2,0x00E4,0x00E3,0x00E5,0x00E7,0x00E9,0x00E8,
        /* 0x90 */ 0x00EA,0x00EB,0x00ED,0x00EC,0x00EE,0x00EF,0x00F1,0x00F3,
        /* 0x98 */ 0x00F2,0x00F4,0x00F6,0x00F5,0x00FA,0x00F9,0x00FB,0x00FC,
        /* 0xA0 */ 0x2020,0x00B0,0x00A2,0x00A3,0x00A7,0x2022,0x00B6,0x00DF,
        /* 0xA8 */ 0x00AE,0x00A9,0x2122,0x00B4,0x00A8,0x2260,0x00C6,0x00D8,
        /* 0xB0 */ 0x221E,0x00B1,0x2264,0x2265,0x00A5,0x00B5,0x2202,0x2211,
        /* 0xB8 */ 0x220F,0x03C0,0x222B,0x00AA,0x00BA,0x03A9,0x00E6,0x00F8,
        /* 0xC0 */ 0x00BF,0x00A1,0x00AC,0x221A,0x0192,0x2248,0x2206,0x00AB,
        /* 0xC8 */ 0x00BB,0x2026,0x00A0,0x00C0,0x00C3,0x00D5,0x0152,0x0153,
        /* 0xD0 */ 0x2013,0x2014,0x201C,0x201D,0x2018,0x2019,0x00F7,0x25CA,
        /* 0xD8 */ 0x00FF,0x0178,0x2044,0x20AC,0x2039,0x203A,0xFB01,0xFB02,
        /* 0xE0 */ 0x2021,0x00B7,0x201A,0x201E,0x2030,0x00C2,0x00CA,0x00C1,
        /* 0xE8 */ 0x00CB,0x00C8,0x00CD,0x00CE,0x00CF,0x00CC,0x00D3,0x00D4,
        /* 0xF0 */ 0x0000,0x00D2,0x00DA,0x00DB,0x00D9,0x0131,0x02C6,0x02DC,
        /* 0xF8 */ 0x00AF,0x02D8,0x02D9,0x02DA,0x00B8,0x02DD,0x02DB,0x02C7
    };
    string out;
    out.reserve(mac.size() * 2);
    for (size_t i = 0; i < mac.size(); i++) {
        unsigned char b = (unsigned char)mac[i];
        if (b < 0x80) { out += (char)b; continue; }
        unsigned int cp = kMrToUnicode[b - 0x80];
        if (cp == 0) { out += '?'; continue; }
        if (cp < 0x800) {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* json_escape                                                         */
/* ------------------------------------------------------------------ */
static string json_escape(const string& s) {
    string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c < 0x20)  { /* skip other control chars */ }
        else                out += (char)c;
    }
    return out;
}

/* Expand sentinels back to original UTF-8, then convert Mac Roman → UTF-8. */
static string block_to_utf8(const string& text, const vector<string>& emojiSeqs) {
    string result;
    size_t ei = 0;
    string macPart;
    for (size_t i = 0; i <= text.size(); i++) {
        if (i == text.size() || (unsigned char)text[i] == (unsigned char)kEmojiSentinel) {
            result += macroman_to_utf8(macPart);
            macPart.clear();
            if (i < text.size() && ei < emojiSeqs.size())
                result += emojiSeqs[ei++];
        } else {
            macPart += text[i];
        }
    }
    return result;
}

/* ------------------------------------------------------------------ */
/* build_rich_text_json — serialise Block.runs as a Notion rich_text   */
/* JSON array with per-run annotations.  Falls back to a single plain  */
/* element if runs are absent or their text doesn't match b.text       */
/* (e.g. the user typed new characters that haven't been split yet).   */
/* ------------------------------------------------------------------ */
static string build_rich_text_json(const Block& b) {
    /* Validate: concatenated run text must equal b.text — no allocation needed */
    bool valid = !b.runs.empty();
    if (valid) {
        size_t total = 0;
        for (const TextRun& r : b.runs) total += r.text.size();
        if (total != b.text.size()) {
            valid = false;
        } else {
            size_t off = 0;
            for (const TextRun& r : b.runs) {
                if (b.text.compare(off, r.text.size(), r.text) != 0) { valid = false; break; }
                off += r.text.size();
            }
        }
    }

    if (!valid) {
        /* Plain fallback */
        string esc = json_escape(block_to_utf8(b.text, b.emojiSeqs));
        return "[{\"type\":\"text\",\"text\":{\"content\":\"" + esc + "\"}}]";
    }

    string result;
    result.reserve(2 + b.runs.size() * 180);  /* ~180 bytes per run: type+annotations */
    result = "[";
    size_t emojiIdx = 0;
    bool first = true;
    for (const TextRun& run : b.runs) {
        if (!first) result += ",";
        first = false;

        /* Collect emoji sequences that belong to this run */
        vector<string> runEmojis;
        for (size_t i = 0; i < run.text.size(); i++)
            if ((unsigned char)run.text[i] == (unsigned char)kEmojiSentinel
                    && emojiIdx < b.emojiSeqs.size())
                runEmojis.push_back(b.emojiSeqs[emojiIdx++]);

        string esc = json_escape(block_to_utf8(run.text, runEmojis));
        result += "{\"type\":\"text\",\"text\":{\"content\":\"" + esc + "\"}";
        result += ",\"annotations\":{";
        result += "\"bold\":"          + string(run.bold          ? "true" : "false") + ",";
        result += "\"italic\":"        + string(run.italic        ? "true" : "false") + ",";
        result += "\"underline\":"     + string(run.underline     ? "true" : "false") + ",";
        result += "\"strikethrough\":" + string(run.strikethrough ? "true" : "false") + ",";
        result += "\"code\":false,\"color\":\"default\"}}";
    }
    result += "]";
    return result;
}

/* ------------------------------------------------------------------ */
/* patchNext — send the next queued block; called serially via callback */
/* ------------------------------------------------------------------ */
static void patchNext(HttpResponse&);   /* forward declaration */
static void runApiThread(const string&, const string&, function<void(HttpResponse&)>, bool keepBody = false); /* forward declaration */

/* ------------------------------------------------------------------ */
/* afterAddInsert — capture ID of the new block returned by the API   */
/* ------------------------------------------------------------------ */
static void afterAddInsert(HttpResponse& resp) {
    if (gAddIdx >= 0 && gAddIdx < (int)pageElm.size()) {
        if (resp.Success && !resp.Content.empty()) {
            string content = resp.Content;
            gason::JsonAllocator alloc;
            gason::JsonValue root;
            if (gason::jsonParse((char*)content.c_str(), root, alloc) == gason::JSON_PARSE_OK) {
                gason::JsonValue first = root("results").at(0);
                if (first) {
                    const char* newId = first("id").toString();
                    if (newId && newId[0] != '\0')
                        pageElm[gAddIdx].id = string(newId);
                }
            }
        }
    }
    gAddIdx     = -1;
    gApiPending = false;
    HttpResponse dummy; dummy.Success = true;
    patchNext(dummy);
}

/* ------------------------------------------------------------------ */
/* afterToggleLoad — insert children returned by the API              */
/* ------------------------------------------------------------------ */
static void afterToggleLoad(HttpResponse& resp) {
    if (!resp.Success || gToggleIdx < 0 || gToggleIdx >= (int)pageElm.size()) {
        gTogglePending = false;
        gToggleIdx     = -1;
        return;
    }
    string content = resp.Content;
    gason::JsonAllocator alloc;
    gason::JsonValue root;
    if (gason::jsonParse((char*)content.c_str(), root, alloc) != gason::JSON_PARSE_OK) {
        gTogglePending = false;
        gToggleIdx     = -1;
        return;
    }

    int    insertPos  = gToggleIdx + 1;
    int    childDepth = pageElm[gToggleIdx].depth + 1;
    string parentId   = pageElm[gToggleIdx].id;

    gason::JsonIterator it = gason::begin(root("results"));
    while (it.isValid()) {
        gason::JsonValue father = it->value;
        const char* listtype = father.child("type").toString();
        if (!listtype || listtype[0] == '\0') { it++; continue; }
        string text = "";
        Block c;
        for (size_t i = 0; i < 50; i++) {
            gason::JsonValue item = father.child(listtype).child("rich_text").at(i);
            if (!item) break;
            TextRun run;
            const char* pt = item("plain_text").toString();
            run.text = utf8_to_macroman(pt ? pt : "", &c.emojiSeqs);
            gason::JsonValue ann = item("annotations");
            run.bold          = (ann("bold").getTag()          == gason::JSON_TRUE);
            run.italic        = (ann("italic").getTag()        == gason::JSON_TRUE);
            run.underline     = (ann("underline").getTag()     == gason::JSON_TRUE);
            run.strikethrough = (ann("strikethrough").getTag() == gason::JSON_TRUE);
            text += run.text;
            c.runs.push_back(run);
        }
        if (listtype[0] != '\0') {
            if (listtype == string("to_do"))
                c.check = (father.child(listtype).child("checked").getTag() == gason::JSON_TRUE);
            c.type = listtype;
            if ((c.type == "heading_1" || c.type == "heading_2" || c.type == "heading_3") &&
                father.child(listtype).child("is_toggleable").getTag() == gason::JSON_TRUE)
                c.type = "toggle_" + c.type;
            c.text     = text;
            c.id       = father.child("id").toString();
            c.isChild  = true;
            c.depth    = childDepth;
            c.parentId = parentId;
            c.pos      = insertPos;
            pageElm.insert(pageElm.begin() + insertPos, c);
            insertPos++;
        }
        it++;
    }

    /* Renumber all positions in one pass after all children are inserted,
     * instead of O(n) per-insert inside the loop above. */
    for (int i = gToggleIdx + 1; i < (int)pageElm.size(); i++)
        pageElm[i].pos = i;

    gTogglePending = false;
    gToggleIdx     = -1;
    updateBlocks();
    if (blockSelect >= 0 && blockSelect < (int)pageElm.size())
        textActive = pageElm[blockSelect].textEdit;
}

/* ------------------------------------------------------------------ */
/* doAddBlock — insert a new empty block after blockSelect            */
/* ------------------------------------------------------------------ */
static void doAddBlock(int typeIdx) {
    static const char* kTypes[] = {
        "paragraph", "heading_1", "heading_2", "heading_3",
        "bulleted_list_item", "numbered_list_item", "to_do", "toggle",
        "code", "quote", "callout",
        "toggle_heading_1", "toggle_heading_2", "toggle_heading_3",
        "divider"
    };
    if (typeIdx < 0 || typeIdx >= 15) return;
    if (_curRequest < 2 || pageElm.empty()) return;
    if (gIsSaving || gSaveThreadActive || gApiPending) return;

    int insertPos = (blockSelect >= 0 && blockSelect < (int)pageElm.size())
                  ? blockSelect + 1 : (int)pageElm.size();
    string afterId = (blockSelect >= 0 && blockSelect < (int)pageElm.size())
                   ? pageElm[blockSelect].id : "";

    string parentBlockId = string(EffPageID());
    bool   newIsChild    = false;
    int    newDepth      = 0;
    string newParentId;

    if (blockSelect >= 0 && blockSelect < (int)pageElm.size()) {
        const Block& cur = pageElm[blockSelect];
        bool curIsToggle = (cur.type == "toggle" ||
                            cur.type == "toggle_heading_1" ||
                            cur.type == "toggle_heading_2" ||
                            cur.type == "toggle_heading_3");

        if (curIsToggle) {
            /* Enter on a toggle header: create a child block inside the toggle,
               appended after any existing children. */
            newIsChild    = true;
            newDepth      = cur.depth + 1;
            newParentId   = cur.id;
            parentBlockId = cur.id;
            /* Advance insertPos past all existing children of this toggle */
            while (insertPos < (int)pageElm.size()
                   && pageElm[insertPos].isChild
                   && pageElm[insertPos].parentId == cur.id)
                insertPos++;
            /* afterId = last existing child's ID, or "" to append at start */
            afterId = (insertPos > blockSelect + 1)
                      ? pageElm[insertPos - 1].id : "";
        } else if (cur.isChild && !cur.parentId.empty()) {
            /* Inside a toggle: inherit the same parent so new block stays
               in the same toggle at the same depth. */
            newIsChild    = true;
            newDepth      = cur.depth;
            newParentId   = cur.parentId;
            parentBlockId = cur.parentId;
        } else {
            /* Page-level block: skip past any open children (e.g. an open
               toggle at page level) so the new block lands after them. */
            const string& curId = cur.id;
            while (insertPos < (int)pageElm.size()
                   && pageElm[insertPos].isChild
                   && pageElm[insertPos].parentId == curId)
                insertPos++;
        }
    }

    Block b;
    b.pos      = insertPos;
    b.type     = kTypes[typeIdx];
    b.isChild  = newIsChild;
    b.depth    = newDepth;
    b.parentId = newParentId;
    pageElm.insert(pageElm.begin() + insertPos, b);
    for (int i = insertPos + 1; i < (int)pageElm.size(); i++)
        pageElm[i].pos = i;

    blockSelect  = insertPos;
    gAddIdx      = insertPos;
    gApiPending  = true;   /* lock out save until we have the block's ID */

    /* If adding at the very end while scrolled to 100%, stay at 100% after
       layout so the new block is always visible at the bottom. */
    bool addingAtEnd = (insertPos == (int)pageElm.size() - 1);
    bool wasAtBottom = addingAtEnd && gScrollBar != nil
                       && GetControlValue(gScrollBar) >= GetControlMaximum(gScrollBar);

    updateBlocks();

    if (wasAtBottom) {
        short winBottom = _window->portRect.bottom;
        short maxScroll = gTotalContentHeight - winBottom;
        if (maxScroll < 0) maxScroll = 0;
        if (gScrollOffset != maxScroll) {
            gScrollOffset = maxScroll;
            updateBlocks();
        }
    }

    if (blockSelect >= 0 && blockSelect < (int)pageElm.size()) {
        textActive = pageElm[blockSelect].textEdit;
        if (textActive) { TEActivate(textActive); TESetSelect(0, 0, textActive); }
    }

    string type = string(kTypes[typeIdx]);
    string blockContent;
    if (type == "to_do") {
        blockContent = "{\"type\":\"to_do\",\"to_do\":{\"rich_text\":[{\"text\":{\"content\":\"\"}}],\"checked\":false}}";
    } else if (type == "divider") {
        blockContent = "{\"type\":\"divider\",\"divider\":{}}";
    } else if (type == "callout") {
        blockContent = "{\"type\":\"callout\",\"callout\":{\"rich_text\":[{\"text\":{\"content\":\"\"}}]}}";
    } else if (type == "code") {
        blockContent = "{\"type\":\"code\",\"code\":{\"rich_text\":[{\"text\":{\"content\":\"\"}}],\"language\":\"plain text\"}}";
    } else if (type == "toggle_heading_1" || type == "toggle_heading_2" || type == "toggle_heading_3") {
        string apiType = type.substr(7); /* "heading_1" etc. */
        blockContent = "{\"type\":\"" + apiType + "\",\"" + apiType
                     + "\":{\"rich_text\":[{\"text\":{\"content\":\"\"}}],\"is_toggleable\":true}}";
    } else {
        blockContent = "{\"type\":\"" + type + "\",\"" + type
                     + "\":{\"rich_text\":[{\"text\":{\"content\":\"\"}}]}}";
    }
    string body = "{\"children\":[" + blockContent + "]";
    if (!afterId.empty())
        body += ",\"after\":\"" + afterId + "\"";
    body += "}";

    runApiThread(
        "https://api.notion.com/v1/blocks/" + parentBlockId + "/children",
        body, afterAddInsert, true /* keepBody: need ID from response */);
}

/* --- helpers for type-change: archive old block, then insert new --- */

static void afterDeleteArchive(HttpResponse&) {
    gApiPending = false;
    HttpResponse dummy; dummy.Success = true;
    patchNext(dummy);
}

static void doDeleteBlock() {
    if (_curRequest < 2 || pageElm.empty()) return;
    if (gIsSaving || gSaveThreadActive || gApiPending) return;
    if (blockSelect <= 0 || blockSelect >= (int)pageElm.size()) return;
    if (pageElm[blockSelect].type == "title") return;

    Block& b = pageElm[blockSelect];
    if (b.textEdit != nil) { TEDispose(b.textEdit); b.textEdit = nil; }
    string blockId = b.id;

    pageElm.erase(pageElm.begin() + blockSelect);
    for (int i = blockSelect; i < (int)pageElm.size(); i++)
        pageElm[i].pos = i;
    if (blockSelect >= (int)pageElm.size())
        blockSelect = (int)pageElm.size() - 1;

    updateBlocks();
    if (blockSelect >= 0 && blockSelect < (int)pageElm.size()) {
        textActive = pageElm[blockSelect].textEdit;
        if (textActive != nil) {
            TESetSelect(0, 0, textActive);
        }
    }

    if (!blockId.empty()) {
        gApiPending = true;
        runApiThread("https://api.notion.com/v1/blocks/" + blockId,
                     "{\"archived\":true}", afterDeleteArchive);
    }
}

static void afterTypeChangeInsert(HttpResponse& resp) {
    if (resp.Success && resp.StatusCode == 403) {
        gReadOnly = true; gIsSaving = false; gHasDirtyBlocks = false;
        gSaveQueue.clear(); gTypeChangeIdx = -1; return;
    }
    /* Parse the new block ID from the children response so future saves work */
    if (gTypeChangeIdx >= 0 && gTypeChangeIdx < (int)pageElm.size()) {
        if (resp.Success && !resp.Content.empty()) {
            string content = resp.Content;
            gason::JsonAllocator alloc;
            gason::JsonValue root;
            if (gason::jsonParse((char*)content.c_str(), root, alloc) == gason::JSON_PARSE_OK) {
                gason::JsonValue first = root("results").at(0);
                if (first) {
                    const char* newId = first("id").toString();
                    if (newId && newId[0] != '\0')
                        pageElm[gTypeChangeIdx].id = string(newId);
                }
            }
        }
    }
    gTypeChangeIdx = -1;
    HttpResponse dummy; dummy.Success = true;
    patchNext(dummy);
}

static void afterTypeChangeArchive(HttpResponse& response) {
    if (response.Success && response.StatusCode == 403) {
        gReadOnly = true; gIsSaving = false; gHasDirtyBlocks = false;
        gSaveQueue.clear(); gTypeChangeIdx = -1; return;
    }
    if (gTypeChangeIdx < 0) { HttpResponse d; d.Success = true; patchNext(d); return; }
    Block& b = pageElm[gTypeChangeIdx];

    /* b.runs was synced by RebuildRunsFromTE in patchNext before the archive
       request — use it directly rather than re-reading the TE after async I/O. */
    string richText   = "\"rich_text\":" + build_rich_text_json(b);
    string blockContent;
    if (b.type == "to_do") {
        blockContent = "{\"type\":\"to_do\",\"to_do\":{" + richText + ",\"checked\":"
                     + (b.check ? "true" : "false") + "}}";
    } else if (b.type == "divider") {
        blockContent = "{\"type\":\"divider\",\"divider\":{}}";
    } else if (b.type == "callout") {
        blockContent = "{\"type\":\"callout\",\"callout\":{" + richText + "}}";
    } else if (b.type == "code") {
        blockContent = "{\"type\":\"code\",\"code\":{" + richText + ",\"language\":\"plain text\"}}";
    } else if (b.type == "toggle_heading_1" || b.type == "toggle_heading_2" || b.type == "toggle_heading_3") {
        string apiType = b.type.substr(7);
        blockContent = "{\"type\":\"" + apiType + "\",\"" + apiType + "\":{" + richText + ",\"is_toggleable\":true}}";
    } else {
        blockContent = "{\"type\":\"" + b.type + "\",\""
                     + b.type + "\":{" + richText + "}}";
    }

    string body = "{\"children\":[" + blockContent + "]";
    /* Insert after the previous block so position is preserved */
    if (gTypeChangeIdx > 0)
        body += ",\"after\":\"" + pageElm[gTypeChangeIdx - 1].id + "\"";
    body += "}";

    _httpClient.PatchWithBody(
        "https://api.notion.com/v1/blocks/" + string(EffPageID()) + "/children",
        body, afterTypeChangeInsert);
}

/* ------------------------------------------------------------------ */
/* Indent block — make current block a child of the toggle above      */
/* ------------------------------------------------------------------ */
static void afterIndentInsert(HttpResponse& resp) {
    if (gIndentIdx >= 0 && gIndentIdx < (int)pageElm.size()) {
        if (resp.Success && !resp.Content.empty()) {
            string content = resp.Content;
            gason::JsonAllocator alloc; gason::JsonValue root;
            if (gason::jsonParse((char*)content.c_str(), root, alloc) == gason::JSON_PARSE_OK) {
                gason::JsonValue first = root("results").at(0);
                if (first) {
                    const char* newId = first("id").toString();
                    if (newId && newId[0] != '\0')
                        pageElm[gIndentIdx].id = string(newId);
                }
            }
        }
    }
    gIndentIdx  = -1; gIndentParentId.clear(); gIndentAfter.clear();
    gApiPending = false;
    /* Show "Saved" briefly when the indent was the only pending operation */
    if (!gHasDirtyBlocks && !gIconDirty && !gIsSaving)
        gSavedUntil = TickCount() + 360;
    HttpResponse dummy; dummy.Success = true; patchNext(dummy);
}

static void afterIndentArchive(HttpResponse&) {
    if (gIndentIdx < 0 || gIndentParentId.empty()) { HttpResponse d; d.Success = true; patchNext(d); return; }
    Block& b = pageElm[gIndentIdx];
    RebuildRunsFromTE(b);
    string richText = "\"rich_text\":" + build_rich_text_json(b);
    string blockContent;
    if (b.type == "to_do")
        blockContent = "{\"type\":\"to_do\",\"to_do\":{" + richText + ",\"checked\":" + (b.check?"true":"false") + "}}";
    else if (b.type == "divider")
        blockContent = "{\"type\":\"divider\",\"divider\":{}}";
    else if (b.type == "callout")
        blockContent = "{\"type\":\"callout\",\"callout\":{" + richText + "}}";
    else if (b.type == "code")
        blockContent = "{\"type\":\"code\",\"code\":{" + richText + ",\"language\":\"plain text\"}}";
    else if (b.type == "toggle_heading_1" || b.type == "toggle_heading_2" || b.type == "toggle_heading_3") {
        string at = b.type.substr(7);
        blockContent = "{\"type\":\"" + at + "\",\"" + at + "\":{" + richText + ",\"is_toggleable\":true}}";
    } else
        blockContent = "{\"type\":\"" + b.type + "\",\"" + b.type + "\":{" + richText + "}}";

    string indBody = "{\"children\":[" + blockContent + "]";
    if (!gIndentAfter.empty())
        indBody += ",\"after\":\"" + gIndentAfter + "\"";
    indBody += "}";
    _httpClient.PatchWithBody("https://api.notion.com/v1/blocks/" + gIndentParentId + "/children",
                             indBody, afterIndentInsert);
}

static void doIndentBlock() {
    if (blockSelect <= 0 || blockSelect >= (int)pageElm.size()) return;
    if (gIsSaving || gSaveThreadActive || gApiPending || gTogglePending) return;
    if (pageElm[blockSelect].type == "title") return;

    /* Find the toggle directly above: either the block itself is a toggle,
       or the block above is a child of a toggle (use that toggle as parent). */
    int parentIdx = -1;
    if (blockSelect > 0) {
        Block& above = pageElm[blockSelect - 1];
        string at = above.type;
        if (at == "toggle" || at == "toggle_heading_1" ||
            at == "toggle_heading_2" || at == "toggle_heading_3") {
            parentIdx = blockSelect - 1;
        } else if (above.isChild && !above.parentId.empty()) {
            for (int i = blockSelect - 2; i >= 0; i--) {
                if (pageElm[i].id == above.parentId) { parentIdx = i; break; }
            }
        }
    }
    if (parentIdx < 0 || pageElm[parentIdx].id.empty()) return;

    /* Capture parent data before modifying the vector */
    string parentId    = pageElm[parentIdx].id;
    int    parentDepth = pageElm[parentIdx].depth;
    pageElm[parentIdx].open = true;

    /* Sync TE text into the block before moving */
    Block& blk = pageElm[blockSelect];
    if (textActive && blk.textEdit == textActive) {
        CharsHandle ch = TEGetText(textActive);
        short len = (*textActive)->teLength;
        HLock((Handle)ch);
        blk.text = StripEmojiPad(string(*ch, (size_t)len));
        HUnlock((Handle)ch);
        textActive = nil;
    }

    /* Find where to insert: after the last existing child of this toggle.
       Must be done BEFORE setting isChild=true, or the block counts itself. */
    int insertAfter = parentIdx;
    for (int i = parentIdx + 1; i < (int)pageElm.size(); i++) {
        if (pageElm[i].isChild && pageElm[i].parentId == parentId) insertAfter = i;
        else break;
    }
    int insertPos = insertAfter + 1;

    /* Update child attributes */
    blk.isChild  = true;
    blk.depth    = parentDepth + 1;
    blk.parentId = parentId;
    blk.dirty    = false; /* saved via archive+re-insert, not the save queue */

    /* Move block locally — dispose old TE so addTE recreates it at the
       correct new indent position (fixes left-alignment after move). */
    Block moved = blk;
    if (moved.textEdit) { TEDispose(moved.textEdit); moved.textEdit = nil; }
    moved.teCachedWidth = 0;
    moved.teCachedType.clear();
    moved.teCache.clear();
    pageElm.erase(pageElm.begin() + blockSelect);
    if (insertPos > blockSelect) insertPos--;
    pageElm.insert(pageElm.begin() + insertPos, moved);
    for (int i = 0; i < (int)pageElm.size(); i++) pageElm[i].pos = i;
    blockSelect = insertPos;

    /* Fire archive → re-insert as child */
    gIndentIdx      = blockSelect;
    gIndentParentId = parentId;
    gIndentAfter    = "";
    gApiPending     = true;
    /* Invalidate cache for this toggle — its child list just changed */
    gToggleChildCache.erase(parentId);
    _httpClient.Patch("https://api.notion.com/v1/blocks/" + pageElm[gIndentIdx].id,
                      "{\"archived\":true}", afterIndentArchive);

    updateBlocks();
    if (blockSelect >= 0 && blockSelect < (int)pageElm.size())
        textActive = pageElm[blockSelect].textEdit;
    if (textActive) { TEActivate(textActive); TESetSelect(0, 0, textActive); }
}

static void patchNext(HttpResponse& response) {
    /* 403 from Notion API: integration token lacks write permission.
       Success=true, StatusCode=403 is the normal Notion API error shape. */
    if (response.Success && response.StatusCode == 403) {
        gReadOnly       = true;
        gIsSaving       = false;
        gHasDirtyBlocks = false;
        gSaveQueue.clear();
        return;
    }
    if (!response.Success) {
        gIsSaving       = false;
        gHasDirtyBlocks = true;  /* network error — re-show Save so user can retry */
        return;
    }

    /* Icon change takes priority */
    if (gIconDirty) {
        gIconDirty = false;
        string body;
        if (gPageIconName.empty()) {
            body = "{\"icon\":null}";
        } else {
            body = "{\"icon\":{\"type\":\"icon\",\"icon\":{\"name\":\""
                 + gPageIconName + "\",\"color\":\"" + gPageIconColor + "\"}}}";
        }
        _httpClient.Patch("https://api.notion.com/v1/pages/" + string(EffPageID()),
                         body, patchNext);
        return;
    }

    /* Drain any queue entries that were cleared (e.g. block became clean) */
    while (!gSaveQueue.empty() && !pageElm[gSaveQueue.front()].dirty)
        gSaveQueue.erase(gSaveQueue.begin());

    if (gSaveQueue.empty()) {
        if (gIsSaving)
            gSavedUntil   = TickCount() + 360;  /* show "Saved" only after real user-edit save */
        gIsSaving         = false;
        gLastSaveLabelIdx = -1;
        return;
    }

    int idx = gSaveQueue.front();
    gSaveQueue.erase(gSaveQueue.begin());

    Block& b = pageElm[idx];
    b.dirty = false;

    string escaped = json_escape(block_to_utf8(b.text, b.emojiSeqs));
    string body;

    if (b.type == "title") {
        body = "{\"properties\":{\"title\":{\"title\":[{\"text\":{\"content\":\""
             + escaped + "\"}}]}}}";
        _httpClient.Patch("https://api.notion.com/v1/pages/" + b.id, body, patchNext);
    } else if (b.typeChanged) {
        /* Type change: archive the old block, then re-insert with new type.
           Rebuild runs NOW while the TE is fresh and guaranteed valid — not
           after the async archive round-trip where the TE state may differ. */
        b.typeChanged  = false;
        gTypeChangeIdx = idx;
        RebuildRunsFromTE(b);   /* sync runs → b.runs matches current TE styles */
        _httpClient.Patch("https://api.notion.com/v1/blocks/" + b.id,
                          "{\"archived\":true}", afterTypeChangeArchive);
    } else if (b.type == "divider") {
        /* Dividers have no content — nothing to PATCH, just continue queue */
        HttpResponse dummy; dummy.Success = true; patchNext(dummy);
    } else {
        /* Resync runs from TE style scrap — typing updates TE but not blk.runs */
        RebuildRunsFromTE(b);
        string richText = "\"rich_text\":" + build_rich_text_json(b);
        if (b.type == "to_do") {
            body = "{\"type\":\"to_do\",\"to_do\":{" + richText + ",\"checked\":"
                 + (b.check ? "true" : "false") + "}}";
        } else if (b.type == "code") {
            body = "{\"type\":\"code\",\"code\":{" + richText + ",\"language\":\"plain text\"}}";
        } else if (b.type == "toggle_heading_1" || b.type == "toggle_heading_2" || b.type == "toggle_heading_3") {
            string apiType = b.type.substr(7);
            body = "{\"type\":\"" + apiType + "\",\"" + apiType + "\":{" + richText + ",\"is_toggleable\":true}}";
        } else {
            body = "{\"type\":\"" + b.type + "\",\""
                 + b.type + "\":{" + richText + "}}";
        }
        _httpClient.Patch("https://api.notion.com/v1/blocks/" + b.id, body, patchNext);
    }
}

/* ------------------------------------------------------------------ */
/* SaveThreadFunc — runs patchNext chain in a cooperative thread       */
/* ------------------------------------------------------------------ */
static pascal void* SaveThreadFunc(void*) {
    HttpResponse dummy; dummy.Success = true;
    patchNext(dummy);
    /* If Patch failed without calling the callback, gIsSaving is still true — clear it */
    gIsSaving         = false;
    gSaveThreadActive = false;
    return nil;
}

/* ------------------------------------------------------------------ */
/* ApiThreadFunc — fires a single Patch (add/delete) in a thread so   */
/* the event loop stays responsive via MacYield during TCP ops        */
/* ------------------------------------------------------------------ */
static string                        gApiThreadUrl;
static string                        gApiThreadBody;
static function<void(HttpResponse&)> gApiThreadCb;
static bool                          gApiKeepBody = false; /* true = need response body (block creation) */

static pascal void* ApiThreadFunc(void*) {
    if (gApiKeepBody)
        _httpClient.PatchWithBody(gApiThreadUrl, gApiThreadBody, gApiThreadCb);
    else
        _httpClient.Patch(gApiThreadUrl, gApiThreadBody, gApiThreadCb);
    gApiKeepBody      = false;
    /* If Patch failed without calling the callback, gApiPending/gIsSaving may still be set */
    gApiPending       = false;
    gIsSaving         = false;
    gSaveThreadActive = false;
    return nil;
}

static void runApiThread(const string& url, const string& body,
                         function<void(HttpResponse&)> cb,
                         bool keepBody) {
    gApiThreadUrl  = url;
    gApiThreadBody = body;
    gApiThreadCb   = cb;
    gApiKeepBody   = keepBody;
    gSaveThreadActive = true;
    NewThread(kCooperativeThread,
              (ThreadEntryUPP)ApiThreadFunc,
              nil, 128 * 1024L, kNewSuspend, nil, &gSaveThreadID);
    SetThreadState(gSaveThreadID, kReadyThreadState, kNoThreadID);
}

/* ------------------------------------------------------------------ */
/* doSave — queue every dirty block then spin up the save thread       */
/* ------------------------------------------------------------------ */
void doSave() {
    if (gIsSaving || gSaveThreadActive || (!gHasDirtyBlocks && !gIconDirty)) return;
    gIsSaving = true;
    gHasDirtyBlocks = false;

    /* Sync the currently active TE handle into b.text before patching */
    if (textActive != nil && blockSelect >= 0 && blockSelect < (int)pageElm.size()) {
        Block& cur = pageElm[blockSelect];
        if (cur.textEdit == textActive) {
            CharsHandle ch = TEGetText(textActive);
            short len = (*textActive)->teLength;
            HLock((Handle)ch);
            cur.text = StripEmojiPad(string(*ch, (size_t)len));
            HUnlock((Handle)ch);
        }
    }

    /* Build queue of all dirty blocks */
    gSaveQueue.clear();
    for (int i = 0; i < (int)pageElm.size(); i++) {
        if (pageElm[i].dirty && !pageElm[i].id.empty())
            gSaveQueue.push_back(i);
    }

    if (gSaveQueue.empty() && !gIconDirty) {
        gIsSaving = false;
        return;
    }

    /* Spawn a cooperative thread so the event loop stays responsive.
     * 128 KB stack: patchNext chains recursively; ~2 KB/block frame, ~30 KB
     * for TLS precompute on first reconnect; 64 KB overflowed at ~11 blocks.
     * Free sprite sheet first (locked, large) so MaxMem can give 128 KB. */
    /* Free the icon sprite sheet (locked, large) so the thread stack fits.
       LoadSprite() reloads it on the next picker open. */
    if (gSpriteHandle) {
        HUnlock(gSpriteHandle);
        DisposeHandle(gSpriteHandle);
        gSpriteHandle = nil;
    }
    if (gIconCacheHandle) {
        DisposeHandle(gIconCacheHandle);
        gIconCacheHandle = nil;
        gIconCacheName.clear();
    }
    /* Compact heap so the freed space is contiguous. */
    Size grow;
    MaxMem(&grow);

    gSaveThreadActive = true;
    NewThread(kCooperativeThread,
              (ThreadEntryUPP)SaveThreadFunc,
              nil, 128 * 1024L, kNewSuspend, nil, &gSaveThreadID);
    SetThreadState(gSaveThreadID, kReadyThreadState, kNoThreadID);
    /* Thread runs the next time the event loop calls YieldToAnyThread() */
}

/* ------------------------------------------------------------------ */
/* drawSaveButtonFrame — custom button look: 1px black top/left/right  */
/* No system button control is ever shown; we draw the frame ourselves */
/* ------------------------------------------------------------------ */
static void drawSaveButtonFrame(short btnLeft, short btnTop,
                                short btnRight, short btnBottom) {
    SetPort(_window);
    /* Erase interior (above the 2 px bar row) */
    Rect inner;
    SetRect(&inner, btnLeft + 1, btnTop + 1, btnRight - 1, btnBottom - 2);
    EraseRect(&inner);
    /* 1 px black border: top, left, right only (bottom handled by bar) */
    /* Rounded top corners: skip corner pixel, side starts one row below top */
    PenNormal();
    MoveTo(btnLeft + 1,  btnTop - 3); LineTo(btnRight - 2, btnTop - 3);     /* top   */
    MoveTo(btnLeft,      btnTop - 2); LineTo(btnLeft,      btnBottom - 3);   /* left  */
    MoveTo(btnRight - 1, btnTop - 2); LineTo(btnRight - 1, btnBottom - 3);   /* right */
    /* "Save" label centered */
    TextFont(0); TextFace(0); TextSize(0);
    short tw = StringWidth((ConstStr255Param)"\pSave");
    short tx = btnLeft + (btnRight - btnLeft - tw) / 2;
    if (tx < btnLeft) tx = btnLeft;
    MoveTo(tx, btnTop + 10);
    DrawString((ConstStr255Param)"\pSave");
}

/* ------------------------------------------------------------------ */
/* drawProgressBar — 2 px bar at the bottom edge of the Save button   */
/* filled = pixels to paint black (0..barInnerWidth); rest is ltGray  */
/* ------------------------------------------------------------------ */
static void drawProgressBar(short filled) {
    if (gSaveButton == nil || _window == nil) return;
    if (filled == gLastBarFilled) return;
    gLastBarFilled = filled;

    SetPort(_window);
    const short btnWidth  = kSidebarWidth - 8;
    const short btnLeft   = (gColLeft - btnWidth) / 2 + 1;
    const short btnRight  = btnLeft + btnWidth;
    const short btnBottom = _window->portRect.bottom - 7;
    const short barTop    = btnBottom - 2;
    const short barLeft   = btnLeft  + 1;   /* 1 px inset each side */
    const short barRight  = btnRight - 1;

    Rect barRect;
    /* Gray background across full inner width */
    SetRect(&barRect, barLeft, barTop, barRight, btnBottom);
    FillRect(&barRect, &qd.ltGray);

    /* Black filled portion */
    if (filled > 0) {
        SetRect(&barRect, barLeft, barTop, barLeft + filled, btnBottom);
        FillRect(&barRect, &qd.black);
    }
}

/* ------------------------------------------------------------------ */
/* updateSaveButton — show/hide button + progress bar, fire auto-save  */
/* ------------------------------------------------------------------ */
void updateSaveButton() {
    if (gSaveButton == nil || _window == nil) return;

    SetPort(_window);
    const short btnWidth  = kSidebarWidth - 8;
    const short btnLeft   = (gColLeft - btnWidth) / 2 + 1;
    const short btnRight  = btnLeft + btnWidth;
    const short btnTop    = _window->portRect.bottom - 23;
    const short btnBottom = _window->portRect.bottom - 7;
    /* Inner bar width (1 px inset each side) */
    const short barWidth  = btnRight - btnLeft - 2;

    /* --- "Read only": integration token lacks write permission --- */
    if (gReadOnly) {
        if (gLastSaveLabelIdx != 6) {
            gLastSaveLabelIdx = 6;
            HideControl(gSaveButton);
            gLastBarFilled = -1;
            drawProgressBar(0);
        }
        Rect r; SetRect(&r, btnLeft, btnTop - 3, btnRight, btnBottom);
        EraseRect(&r);
        TextFont(0); TextFace(0); TextSize(12);
        FontInfo fi; GetFontInfo(&fi);
        short lineH = fi.ascent + fi.descent + fi.leading;
        short midY  = btnTop + (btnBottom - btnTop) / 2 - 28;
        short tw;
        tw = StringWidth((ConstStr255Param)"\pRead");
        MoveTo(btnLeft + (btnRight - btnLeft - tw) / 2, midY);
        DrawString((ConstStr255Param)"\pRead");
        tw = StringWidth((ConstStr255Param)"\ponly");
        MoveTo(btnLeft + (btnRight - btnLeft - tw) / 2, midY + lineH);
        DrawString((ConstStr255Param)"\ponly");
        tw = StringWidth((ConstStr255Param)"\pfile");
        MoveTo(btnLeft + (btnRight - btnLeft - tw) / 2, midY + lineH * 2);
        DrawString((ConstStr255Param)"\pfile");
        return;
    }

    /* --- "Saved": hide border, draw plain text for 6 s --- */
    if (!gHasDirtyBlocks && !gIsSaving && gSavedUntil > 0) {
        if (TickCount() < gSavedUntil) {
            /* First entry: hide button control and erase bar */
            if (gLastSaveLabelIdx != 5) {
                gLastSaveLabelIdx = 5;
                HideControl(gSaveButton);
                gLastBarFilled = -1;
                drawProgressBar(0);   /* paint bar area gray so it's clean */
            }
            /* Always redraw — updateBlocks() may have erased the text */
            Rect r; SetRect(&r, btnLeft, btnTop - 3, btnRight, btnBottom);
            EraseRect(&r);
            TextFont(0); TextFace(0); TextSize(12);
            short tw = StringWidth((ConstStr255Param)"\pSaved");
            MoveTo(btnLeft + (btnRight - btnLeft - tw) / 2, btnTop + 11);
            DrawString((ConstStr255Param)"\pSaved");
            TextFace(0);
            return;
        }
        /* Timer expired — erase area, fall through to hide */
        gSavedUntil = 0;
        if (gLastSaveLabelIdx == 5) {
            Rect r; SetRect(&r, btnLeft, btnTop - 3, btnRight, btnBottom);
            EraseRect(&r);
            gLastSaveLabelIdx = -1;
        }
    }

    /* --- Hide when nothing pending --- */
    if (!gHasDirtyBlocks && !gIsSaving) {
        if (gLastSaveLabelIdx != 0) {
            gLastSaveLabelIdx = 0;
            HideControl(gSaveButton);
            gLastBarFilled = -1;
            Rect r; SetRect(&r, btnLeft, btnTop - 3, btnRight, btnBottom);
            EraseRect(&r);
        }
        return;
    }

    /* --- Show button with static "Save" label --- */
    if (gLastSaveLabelIdx != 1) {
        if (gLastSaveLabelIdx == 5) gSavedUntil = 0;
        gLastSaveLabelIdx = 1;
        gLastBarFilled = -1;
        drawSaveButtonFrame(btnLeft, btnTop, btnRight, btnBottom);
    }

    /* --- Progress bar fill --- */
    if (gIsSaving) {
        drawProgressBar(barWidth);   /* full bar while sending */
        return;
    }

    long elapsed = TickCount() - gLastEditTick;
    if (elapsed >= 2400L) {
        doSave();
        return;
    }
    short filled = (short)(elapsed * (long)barWidth / 2400L);
    drawProgressBar(filled);
}

/* ------------------------------------------------------------------ */
/* OnGetResponse                                                       */
/* ------------------------------------------------------------------ */
void OnGetResponse(HttpResponse& response) {
    /* A non-200 status means the request reached Notion but was rejected.
       Check this before touching the JSON body — the error body has a
       completely different shape and would crash the parsers below. */
    if (response.Success && response.StatusCode != 200) {
        if (response.StatusCode == 400) {
            DrawStatusText("Bad request (400): the Page ID looks invalid. Please check your settings.");
        } else if (response.StatusCode == 401 || response.StatusCode == 403) {
            DrawStatusText("Unauthorized: check your Integration Token and Page ID in Settings.");
        } else {
            DrawStatusText(("Notion error " + to_string(response.StatusCode)
                           + ": " + response.Content.substr(0, 100)).c_str());
        }
        gNeedsReload = true;
        return;
    }

    if (response.Success) {
        int pos = 0;
        gason::JsonAllocator allocator;
        gason::JsonValue root;
        gason::JsonParseStatus status = gason::jsonParse((char*)response.Content.c_str(), root, allocator);
        if (status == gason::JSON_PARSE_OK) {
            if (_curRequest == 0) {
                Block c; c.pos = pos; c.type = "title";
                c.text = utf8_to_macroman(root("properties").child("title").child("title").at(0).child("text").child("content").toString(), &c.emojiSeqs);
                c.id = string(EffPageID());
                pageElm.push_back(c);

                /* Parse native Notion icon: type=="icon", icon.name gives the slug */
                gPageIconName = "";
                {
                    gason::JsonValue iconNode = root("icon");
                    const char* iconType = iconNode("type").toString();
                    if (iconType && string(iconType) == "icon") {
                        const char* iconName  = iconNode("icon")("name").toString();
                        const char* iconColor = iconNode("icon")("color").toString();
                        if (iconName)  gPageIconName  = string(iconName);
                        if (iconColor) gPageIconColor = string(iconColor);
                    }
                }
            } else {
                /* Offset pos so appended pages continue numbering after existing blocks */
                pos = (int)pageElm.size() - 1;  /* title is at 0, blocks start at 1 */
                gason::JsonIterator it = gason::begin(root("results"));
                while (it.isValid()) {
                    gason::JsonValue father = it->value;
                    const char *listtype = father.child("type").toString();
                    string text = "";
                    Block c;
                    for (size_t i = 0; i < 50; i++) {
                        gason::JsonValue item = father.child(listtype).child("rich_text").at(i);
                        if (!item) break;
                        TextRun run;
                        const char* pt = item("plain_text").toString();
                        run.text = utf8_to_macroman(pt ? pt : "", &c.emojiSeqs);
                        gason::JsonValue ann = item("annotations");
                        run.bold          = (ann("bold").getTag()          == gason::JSON_TRUE);
                        run.italic        = (ann("italic").getTag()        == gason::JSON_TRUE);
                        run.underline     = (ann("underline").getTag()     == gason::JSON_TRUE);
                        run.strikethrough = (ann("strikethrough").getTag() == gason::JSON_TRUE);
                        /* href — top-level field on the rich_text item.
                           Notion returns "href": null for non-linked runs; only call
                           toString() when the value is actually a JSON string (not null). */
                        {
                            gason::JsonValue hrefVal = item("href");
                            if (hrefVal.getTag() == gason::JSON_STRING) {
                                const char* href = hrefVal.toString();
                                if (href && href[0] != '\0') run.url = href;
                            }
                        }
                        text += run.text;
                        c.runs.push_back(run);
                    }
                    /* Include all blocks with a recognised type — empty paragraphs
                       (rich_text:[]) are valid Notion spacing and must be shown. */
                    if (listtype[0] != '\0') {
                        pos++;
                        if (listtype == string("to_do"))
                            c.check = (father.child(listtype).child("checked").getTag() == gason::JSON_TRUE);
                        c.type = listtype;
                        if ((c.type == "heading_1" || c.type == "heading_2" || c.type == "heading_3") &&
                            father.child(listtype).child("is_toggleable").getTag() == gason::JSON_TRUE)
                            c.type = "toggle_" + c.type;
                        c.pos = pos; c.text = text;
                        c.id = father.child("id").toString();
                        pageElm.push_back(c);
                    }
                    it++;
                }
                /* Check for more pages */
                gHasMore = (root("has_more").getTag() == gason::JSON_TRUE);
                if (gHasMore) {
                    const char* cur = root("next_cursor").toString();
                    gNextCursor = cur ? string(cur) : "";
                    if (gNextCursor.empty()) gHasMore = false;
                } else {
                    gNextCursor.clear();
                }
            }
        } else {
            DrawStatusText(("JSON parse error req=" + to_string(_curRequest)
                          + " sc=" + to_string(response.StatusCode)
                          + " body=" + response.Content.substr(0, 120)).c_str());
            while (!Button()); while (Button());
        }
    } else {
        DrawStatusText(("HTTP error req=" + to_string(_curRequest)
                      + ": " + response.ErrorMsg).c_str());
        gNeedsReload = true;
        return;
    }

    gRequestStartTick = 0;  /* request completed — cancel timeout */
    _curRequest++;
    if (_curRequest == 1) {
        /* Show title + icon immediately — window opens before blocks arrive */
        MakeNewWindow(documentProc);
        pageRequest();
    } else if (_curRequest == 2) {
        if (gHasMore) {
            /* More block pages to fetch — re-render what we have, then continue */
            _curRequest = 1;
            updateBlocks();
            ValidRect(&_window->portRect);
            pageRequest();
            return;
        }
        if (pageElm.empty()) {
            DrawStatusText("No page content (empty pageElm)");
            while (!Button()); while (Button());
            return;
        }
        /* Final batch received — render complete page */
        updateBlocks();
        ValidRect(&_window->portRect);
    }
}

/* ------------------------------------------------------------------ */
/* pageRequest                                                         */
/* ------------------------------------------------------------------ */
void pageRequest() {
    gRequestStartTick = TickCount();
    string pid = string(EffPageID());
    string url;
    if (_curRequest == 0) {
        url = "https://api.notion.com/v1/pages/" + pid;
    } else {
        url = "https://api.notion.com/v1/blocks/" + pid + "/children?page_size=100";
        if (!gNextCursor.empty())
            url += "&start_cursor=" + gNextCursor;
    }
    _httpClient.Get(url, OnGetResponse);
}

/* ------------------------------------------------------------------ */
/* ShowAboutBox                                                        */
/* ------------------------------------------------------------------ */
void ShowAboutBox() {
    _aboutBox = GetNewWindow(128, NULL, (WindowPtr)-1);
    if (_aboutBox == nil) return;
    MoveWindow(_aboutBox,
               qd.screenBits.bounds.right  / 2 - _aboutBox->portRect.right  / 2,
               qd.screenBits.bounds.bottom / 2 - _aboutBox->portRect.bottom / 2,
               false);
    ShowWindow(_aboutBox);
    SetPort(_aboutBox);

    /* Draw NotionClassic.pict centered horizontally, 32px from top */
    {
        /* Resolve icon folder if needed */
        if (gIconsDirID == 0) {
            CInfoPBRec pb;
            Str255 fn = "\passets";
            memset(&pb, 0, sizeof(pb));
            pb.dirInfo.ioNamePtr   = fn;
            pb.dirInfo.ioVRefNum   = gAppVRefNum;
            pb.dirInfo.ioDrDirID   = gAppDirID;
            pb.dirInfo.ioFDirIndex = 0;
            if (PBGetCatInfoSync(&pb) == noErr)
                gIconsDirID = pb.dirInfo.ioDrDirID;
        }
        FSSpec spec;
        if (FSMakeFSSpec(gAppVRefNum, gIconsDirID, "\pNotionClassic.pict", &spec) == noErr) {
            short refNum;
            if (FSpOpenDF(&spec, fsRdPerm, &refNum) == noErr) {
                long fileSize = 0;
                GetEOF(refNum, &fileSize);
                long pictSize = fileSize - 512;
                if (pictSize > 0) {
                    SetFPos(refNum, fsFromStart, 512);
                    Handle ph = NewHandle(pictSize);
                    if (ph) {
                        HLock(ph);
                        FSRead(refNum, &pictSize, *ph);
                        HUnlock(ph);
                        PicHandle pic = (PicHandle)ph;
                        Rect pf = (*pic)->picFrame;
                        short pw = pf.right - pf.left;
                        short ph2 = pf.bottom - pf.top;
                        short cx = (_aboutBox->portRect.right - pw) / 2;
                        Rect dst = { 28, cx, (short)(28 + ph2), (short)(cx + pw) };
                        DrawPicture(pic, &dst);
                        DisposeHandle(ph);
                    }
                }
                FSClose(refNum);
            }
        }
    }

    /* Draw hey.pict at bottom, 16px from right */
    {
        FSSpec spec;
        if (FSMakeFSSpec(gAppVRefNum, gIconsDirID, "\phey.pict", &spec) == noErr) {
            short refNum;
            if (FSpOpenDF(&spec, fsRdPerm, &refNum) == noErr) {
                long fileSize = 0;
                GetEOF(refNum, &fileSize);
                long pictSize = fileSize - 512;
                if (pictSize > 0) {
                    SetFPos(refNum, fsFromStart, 512);
                    Handle ph = NewHandle(pictSize);
                    if (ph) {
                        HLock(ph);
                        FSRead(refNum, &pictSize, *ph);
                        HUnlock(ph);
                        PicHandle pic = (PicHandle)ph;
                        Rect pf = (*pic)->picFrame;
                        short pw = pf.right - pf.left;
                        short ph2 = pf.bottom - pf.top;
                        short r2 = _aboutBox->portRect.right - 12;
                        short b2 = _aboutBox->portRect.bottom;
                        Rect dst = { (short)(b2 - ph2), (short)(r2 - pw), b2, r2 };
                        DrawPicture(pic, &dst);
                        DisposeHandle(ph);
                    }
                }
                FSClose(refNum);
            }
        }
    }

    Handle h = GetResource('TEXT', 128);
    if (h) { ReleaseResource(h); }
    {
        TextFont(3); TextSize(10);
        FontInfo fi;
        GetFontInfo(&fi);
        short lineH = fi.ascent + fi.descent + fi.leading;

        /* measure widest line (treating the * line as one piece for width) */
        ConstStringPtr lines[] = {
            "\pVersion 0.1 beta",
            "\pFor Macintosh System 7 - 9",
            "\pMade with Retro68 * Claude code",
            "\p2026  by Lin van der Slikke"
        };
        short maxW = 0;
        for (int i = 0; i < 4; i++) {
            short w = StringWidth(lines[i]);
            if (w > maxW) maxW = w;
        }

        short x = 30;
        short yBase = _aboutBox->portRect.bottom - 20 - 88 + fi.ascent;

        /* line 0: Version 1.0 */
        MoveTo(x, yBase);
        DrawString("\pVersion 1.0");
        yBase += lineH;

        /* blank line */
        yBase += lineH;

        /* line 2: Made with Retro68 [big *] Claude code */
        MoveTo(x, yBase);
        DrawString("\pFor Macintosh System 7 - 9");
        yBase += lineH;

        /* line 3: Made with Retro68 * Claude code — * drawn bigger */
        MoveTo(x, yBase);
        DrawString("\pMade with Retro68 ");
        Point pen; GetPen(&pen);
        TextFont(23); TextSize(17);
        GetFontInfo(&fi);
        MoveTo(pen.h, yBase + (fi.descent / 2));
        DrawString("\p*");
        GetPen(&pen);
        TextFont(3); TextSize(10);
        MoveTo(pen.h, yBase);
        DrawString("\p Claude code");
        yBase += lineH;

        /* blank line */
        yBase += lineH;

        /* line 5: 2026 by Lin van der Slikke */
        MoveTo(x, yBase);
        DrawString("\p2026  by Lin van der Slikke");
    }
    EventRecord evt;
    while (true) {
        WaitNextEvent(everyEvent, &evt, 10, nil);
        if (evt.what == mouseDown || evt.what == keyDown)
            break;
    }
    FlushEvents(everyEvent, 0);
    DisposeWindow(_aboutBox);
    _aboutBox = nil;
}

/* ------------------------------------------------------------------ */
/* UpdateMenus / DoMenuCommand                                         */
/* ------------------------------------------------------------------ */
void UpdateMenus() {
    MenuHandle addMenu    = GetMenuHandle(kMenuAdd);
    MenuHandle editMenu   = GetMenuHandle(kMenuEdit);
    MenuHandle notionMenu = GetMenuHandle(kMenuFile);
    bool busy = (_curRequest < 2 || gIsSaving || gSaveThreadActive || gApiPending);
    bool canDelete = !busy && blockSelect > 0 && blockSelect < (int)pageElm.size()
                     && pageElm[blockSelect].type != "title";
    if (addMenu)    { if (busy)      DisableItem(addMenu,  0); else EnableItem(addMenu,  0); }
    if (editMenu)   { if (canDelete) EnableItem(editMenu,  1); else DisableItem(editMenu, 1); }
    if (notionMenu) { CheckItem(notionMenu, kItemFullWidth, gFullWidth ? true : false); }

    /* Check the Edit menu item matching the selected block's current type */
    if (editMenu) {
        static const char* kTypes[] = {
            "paragraph", "heading_1", "heading_2", "heading_3",
            "bulleted_list_item", "numbered_list_item", "to_do",
            "toggle", "code", "quote", "callout",
            "toggle_heading_1", "toggle_heading_2", "toggle_heading_3"
        };
        static const int kTypeCount = 14;
        static const int kFirstTypeItem = 14; /* Edit menu item offset */
        string curType = (blockSelect >= 0 && blockSelect < (int)pageElm.size())
                         ? pageElm[blockSelect].type : "";
        for (int i = 0; i < kTypeCount; i++) {
            short item = kFirstTypeItem + i;
            short mark = (curType == kTypes[i]) ? (short)checkMark : (short)noMark;
            SetItemMark(editMenu, item, mark);
        }
    }
}

/* ------------------------------------------------------------------ */
/* reloadPage — discard current content and re-fetch from Notion       */
/* ------------------------------------------------------------------ */
void reloadPage() {
    /* Dispose all TE handles before clearing the vector */
    for (vector<Block>::iterator p = pageElm.begin(); p != pageElm.end(); ++p) {
        if (p->textEdit != nil) { TEDispose(p->textEdit); p->textEdit = nil; }
    }
    pageElm.clear(); gToggleChildCache.clear();

    /* Invalidate save/add state so any in-flight callback becomes a no-op */
    gIsSaving         = false;
    gSaveThreadActive = false;
    gReadOnly         = false;
    gApiPending       = false;
    gTypeChangeIdx    = -1;
    gAddIdx           = -1;
    gSaveQueue.clear();

    _curRequest       = 0;
    gHasMore          = false;
    gNextCursor.clear();
    gRequestStartTick = 0;
    textActive        = nil;
    blockSelect       = 0;
    gUndoBlock        = -1;
    gUndoStackSize    = 0;
    gUndoCharCount    = 0;
    gScrollOffset     = 0;
    gHasDirtyBlocks   = false;
    gLastSaveLabelIdx = -1;
    gPageIconName     = "";
    gPageIconColor    = "gray";
    gIconDirty        = false;
    gIconList.clear();
    if (gIconCacheHandle) { DisposeHandle(gIconCacheHandle); gIconCacheHandle = nil; }
    gIconCacheName.clear();
    if (gSpriteHandle)      { HUnlock(gSpriteHandle);      DisposeHandle(gSpriteHandle);      gSpriteHandle      = nil; }
    if (gEmojiGWorld) { DisposeGWorld(gEmojiGWorld); gEmojiGWorld = nil; gEmojiSpriteHandle = nil; }
    gIconsDirID = 0;

    if (gScrollBar != nil) {
        SetControlMaximum(gScrollBar, 0);
        SetControlValue(gScrollBar, 0);
    }

    /* Force a fresh TCP connection — the server may have closed the idle
       connection since the last page load. */
    _httpClient.ForceReconnect();
    DrawStatusText("Reloading...");
    pageRequest();
}

void DoMenuCommand(long menuCommand) {
    Str255 str;
    short menuID   = menuCommand >> 16;
    short menuItem = menuCommand & 0xFFFF;
    if (menuID == kMenuApple) {
        if (menuItem == kItemAbout) ShowAboutBox();
        else { GetMenuItemText(GetMenu(128), menuItem, str); OpenDeskAcc(str); }
    } else if (menuID == kMenuFile) {
        switch (menuItem) {
            case kItemReload:   reloadPage();                     break;
            case kItemSave:     doSave();                         break;
            case kItemSettings:
                if (ShowSettingsDialog(gSettings)) {
                    _httpClient.SetAuthorization("Bearer " + string(EffAPIKey()));
                    reloadPage();
                }
                break;
            case kItemFullWidth:
                gFullWidth = !gFullWidth;
                SaveFullWidth(gFullWidth);
                if (_window != nil) {
                    SetPort(_window);
                    if (gFullWidth) {
                        /* MoveWindow moves the CONTENT area, not the structure.
                           Compute how far the content sits below the structure top
                           (title bar + border) so we can position the structure
                           flush with the menu bar while keeping the title bar visible. */
                        short contTop  = -_window->portBits.bounds.top;   /* content global top  */
                        short contLeft = -_window->portBits.bounds.left;  /* content global left */
                        short winW     = _window->portRect.right  - _window->portRect.left;
                        short winH     = _window->portRect.bottom - _window->portRect.top;
                        /* Save content-area global rect for restore */
                        SetRect(&gSavedWindowRect, contLeft, contTop,
                                contLeft + winW, contTop + winH);
                        /* Place content flush below menu bar — title bar slides
                           behind the menu bar, giving a true full-screen look */
                        short mbarH = *(short*)0x0BAA; /* MBarHeight low-mem global */
                        if (mbarH < 16 || mbarH > 40) mbarH = 20;
                        short screenW = qd.screenBits.bounds.right;
                        short screenH = qd.screenBits.bounds.bottom - mbarH;
                        MoveWindow(_window, 0, mbarH, true);
                        SizeWindow(_window, screenW, screenH, true);
                    } else {
                        /* Restore: MoveWindow takes content-area global coords */
                        MoveWindow(_window, gSavedWindowRect.left, gSavedWindowRect.top, true);
                        SizeWindow(_window,
                                   gSavedWindowRect.right  - gSavedWindowRect.left,
                                   gSavedWindowRect.bottom - gSavedWindowRect.top, true);
                    }
                    resizeControls();
                    updateBlocks();
                    gLastSaveLabelIdx = -1; gLastBarFilled = -1;
                    updateSaveButton();
                }
                break;
            case kItemQuit:     _httpClient.SslFreeAll(); ExitToShell(); break;
        }
    } else if (menuID == kMenuEdit) {
        if (menuItem == 1) {
            doDeleteBlock();
        } else if (menuItem >= 6 && menuItem <= 9 &&
                   textActive && blockSelect >= 0 && blockSelect < (int)pageElm.size()) {
            /* Bold(6) Italic(7) Underline(8) Strikethrough(9) */
            static const int kStyleMap[] = {kStyleBold,kStyleItalic,kStyleUnderline,kStyleStrikethrough};
            ToggleRunStyleBit(pageElm[blockSelect], kStyleMap[menuItem - 6]);
        } else if (menuItem == 11) {
            moveBlock(-1);
        } else if (menuItem == 12) {
            moveBlock(1);
        } else if (menuItem >= 14 && blockSelect >= 0 && blockSelect < (int)pageElm.size()
                          && pageElm[blockSelect].type != "title") {
            static const char* kTypes[] = {
                "paragraph", "heading_1", "heading_2", "heading_3",
                "bulleted_list_item", "numbered_list_item", "to_do",
                "toggle", "code", "quote", "callout",
                "toggle_heading_1", "toggle_heading_2", "toggle_heading_3"
            };
            int ti = menuItem - 14;
            if (ti >= 0 && ti < 14) {
                Block& b        = pageElm[blockSelect];
                b.type          = kTypes[ti];
                b.dirty         = true;
                b.typeChanged   = true;
                gHasDirtyBlocks = true;
                gLastEditTick   = TickCount();
                /* Update undo stack entries so Cmd+Z won't revert the type change */
                if (gUndoBlock == blockSelect) {
                    for (int ui = 0; ui < gUndoStackSize; ui++)
                        gUndoStack[ui].type = kTypes[ti];
                }
                updateBlocks();
                if (blockSelect >= 0 && blockSelect < (int)pageElm.size())
                    textActive = pageElm[blockSelect].textEdit;
            }
        } else {
            SystemEdit(menuItem - 1);
        }
    } else if (menuID == kMenuAdd) {
        if (menuItem == 18) {
            ShowIconPicker();                        /* Page Icon         */
        } else if (menuItem == 17) {
            ShowEmojiPicker();                       /* Emoji             */
        } else if (menuItem >= 1 && menuItem <= 15) {
            doAddBlock(menuItem - 1);               /* items 1-15 → kTypes[0-14] */
        }
    }
    HiliteMenu(0);
}

/* ------------------------------------------------------------------ */
/* resizeControls — reposition scroll bar and save button after grow   */
/* ------------------------------------------------------------------ */
static void resizeControls() {
    if (gScrollBar != nil) {
        HideControl(gScrollBar);
        MoveControl(gScrollBar,
                    _window->portRect.right - kScrollBarWidth,
                    _window->portRect.top - 1);
        SizeControl(gScrollBar,
                    kScrollBarWidth + 1,
                    (_window->portRect.bottom - 14) - (_window->portRect.top - 1));
        ShowControl(gScrollBar);
    }
    if (gSaveButton != nil)
        MoveControl(gSaveButton, 3, _window->portRect.bottom - 22);
}

/* ------------------------------------------------------------------ */
/* closeToggleAt — recursively close all open inner toggles, then close
   the toggle at togglePos.  After the call the toggle itself stays in
   pageElm (only its children are removed and cached).                 */
static void closeToggleAt(int togglePos) {
    string pid = pageElm[togglePos].id;
    /* Close any open child toggles first (innermost first) */
    int scanPos = togglePos + 1;
    while (scanPos < (int)pageElm.size()
           && pageElm[scanPos].isChild
           && pageElm[scanPos].parentId == pid) {
        if (pageElm[scanPos].open)
            closeToggleAt(scanPos);   /* recurse; scanPos still valid after (toggle itself not erased) */
        scanPos++;
    }
    /* Remove and cache direct children */
    pageElm[togglePos].open = false;
    int first = togglePos + 1;
    int last  = first;
    while (last < (int)pageElm.size()
           && pageElm[last].isChild
           && pageElm[last].parentId == pid)
        last++;
    if (last > first) {
        vector<Block> cached;
        for (int i = first; i < last; i++) {
            Block b = pageElm[i];
            if (b.textEdit != nil) {
                if (textActive == b.textEdit) textActive = nil;
                TEDispose(b.textEdit);
                b.textEdit = nil;
            }
            b.teCachedWidth = 0; b.teCachedType.clear(); b.teCache.clear();
            cached.push_back(b);
        }
        gToggleChildCache[pid] = cached;
        pageElm.erase(pageElm.begin() + first, pageElm.begin() + last);
        for (int i = first; i < (int)pageElm.size(); i++) pageElm[i].pos = i;
    }
}

/* doInContent                                                         */
/* ------------------------------------------------------------------ */
void doInContent(EventRecord *event, WindowPtr win) {
    if (_window != win) return;
    if (_curRequest < 2) return;

    ControlHandle control;
    SetPort(win);
    GlobalToLocal(&event->where);

    short part = FindControl(event->where, win, &control);

    /* Icon click — open picker */
    if (!gPageIconName.empty() && PtInRect(event->where, &gIconRect)) {
        ShowIconPicker();
        return;
    }

    /* Sidebar controls: + (add block), ▴ (move up), ▾ (move down) */
    if (event->where.h < gColLeft) {
        if (PtInRect(event->where, &gAddBlockRect)) {
            int addIdx = gHoverBlockIdx;
            if (addIdx > 0 && addIdx < (int)pageElm.size()) {
                blockSelect = addIdx;
                const string& ct = pageElm[addIdx].type;
                int typeIdx = 0;
                if (ct == "bulleted_list_item") typeIdx = 4;
                else if (ct == "numbered_list_item") typeIdx = 5;
                else if (ct == "to_do") typeIdx = 6;
                doAddBlock(typeIdx);
                if (blockSelect >= 0 && blockSelect < (int)pageElm.size())
                    textActive = pageElm[blockSelect].textEdit;
                gSidebarPending      = kSPPlus;
                gSidebarPendingTick  = TickCount();
                gSidebarPendingBlock = blockSelect;  /* new block position */
                gArrowsVisible       = true;
                drawSidebarControls();
            }
            return;
        }
        if (PtInRect(event->where, &gMoveUpArrowRect)) {
            if (gHoverBlockIdx > 0) blockSelect = gHoverBlockIdx;
            moveBlock(-1);
            gSidebarPending      = kSPUp;
            gSidebarPendingTick  = TickCount();
            gSidebarPendingBlock = blockSelect;
            gArrowsVisible       = true;
            drawSidebarControls();
            return;
        }
        if (PtInRect(event->where, &gMoveDownArrowRect)) {
            if (gHoverBlockIdx > 0) blockSelect = gHoverBlockIdx;
            moveBlock(1);
            gSidebarPending      = kSPDown;
            gSidebarPendingTick  = TickCount();
            gSidebarPendingBlock = blockSelect;
            gArrowsVisible       = true;
            drawSidebarControls();
            return;
        }
        /* Save button (kept hidden; detect via rect) */
        if (gHasDirtyBlocks || gIsSaving) {
            Rect btnR;
            { const short bw = kSidebarWidth - 8;
              const short bl = (gColLeft - bw) / 2 + 1;
              SetRect(&btnR, bl, _window->portRect.bottom - 23,
                      bl + bw, _window->portRect.bottom - 7); }
            if (PtInRect(event->where, &btnR)) {
                Rect inner;
                SetRect(&inner, btnR.left + 1, btnR.top + 1,
                        btnR.right - 1, btnR.bottom - 2);
                InvertRect(&inner);
                while (StillDown()) {}
                Point lp; GetMouse(&lp);
                InvertRect(&inner);
                gLastSaveLabelIdx = -1;
                updateSaveButton();
                if (PtInRect(lp, &btnR)) doSave();
                return;
            }
        }
    }

    if (part == 0) {
        /* Sync leaving block's TE content before changing selection */
        if (textActive != nil && blockSelect >= 0 && blockSelect < (int)pageElm.size()) {
            Block& cur = pageElm[blockSelect];
            if (cur.textEdit == textActive) {
                CharsHandle ch = TEGetText(textActive);
                short len = (*textActive)->teLength;
                HLock((Handle)ch);
                cur.text = StripEmojiPad(string(*ch, (size_t)len));
                HUnlock((Handle)ch);
            }
        }

        /* Deactivate and redraw old block to erase its cursor */
        if (textActive != nil) {
            TEDeactivate(textActive);
            EraseRect(&pageElm[blockSelect].vrect);
            TEUpdate(&pageElm[blockSelect].vrect, textActive);
            { string te = TextToTE(pageElm[blockSelect].text, pageElm[blockSelect].emojiSeqs);
              DrawBlockEmojis(textActive, te, pageElm[blockSelect].emojiSeqs);
              DrawStrikethrough(textActive, pageElm[blockSelect].runs); }
            textActive = nil;
        }

        /* Check for toggle triangle click */
        for (vector<Block>::iterator p = pageElm.begin(); p != pageElm.end(); ++p) {
            if (p->type != "toggle" && p->type != "toggle_heading_1" &&
                p->type != "toggle_heading_2" && p->type != "toggle_heading_3") continue;
            if (event->where.v <= p->rect.top || event->where.v >= p->rect.bottom) continue;
            short tx = gColLeft + 6 + p->depth * 20;
            if (event->where.h < tx || event->where.h > tx + 14) continue;
            /* Triangle hit */
            if (p->open) {
                /* Close: recursively close inner toggles first, then this one */
                int toggleIdx = (int)(p - pageElm.begin());
                closeToggleAt(toggleIdx);
                /* p is now invalid (erase may have reallocated); use index */
                blockSelect = toggleIdx;
                textActive  = nil;
                updateBlocks();
                return;
            } else {
                /* Open: restore from cache if available, otherwise fetch from API */
                p->open = true;
                string toggleId = p->id;
                if (gToggleChildCache.count(toggleId)) {
                    /* Cache hit — restore children directly */
                    vector<Block>& cached = gToggleChildCache[toggleId];
                    int insertPos  = p->pos + 1;
                    int childDepth = p->depth + 1;
                    for (int i = 0; i < (int)cached.size(); i++) {
                        Block b   = cached[i];
                        b.depth   = childDepth;
                        b.pos     = insertPos;
                        pageElm.insert(pageElm.begin() + insertPos, b);
                        for (int j = insertPos + 1; j < (int)pageElm.size(); j++)
                            pageElm[j].pos = j;
                        insertPos++;
                    }
                } else if (!gTogglePending && !gApiPending && !toggleId.empty()) {
                    gTogglePending = true;
                    gToggleIdx     = p->pos;
                    _httpClient.Get(
                        "https://api.notion.com/v1/blocks/" + toggleId + "/children?page_size=100",
                        afterToggleLoad);
                }
            }
            blockSelect = p->pos;
            textActive  = nil;
            updateBlocks();
            return;
        }

        /* Find clicked block — blocks are in layout order so first match is correct */
        int prevBlockSelect = blockSelect;
        for (vector<Block>::iterator p = pageElm.begin(); p != pageElm.end(); ++p) {
            if (event->where.v > p->rect.top && event->where.v < p->rect.bottom) {
                blockSelect = p->pos;
                break;
            }
        }

        /* If we moved away from a divider, redraw it to clear its selection highlight */
        if (prevBlockSelect != blockSelect &&
            prevBlockSelect >= 0 && prevBlockSelect < (int)pageElm.size() &&
            pageElm[prevBlockSelect].type == "divider") {
            updateBlocks();
        }

        if (pageElm[blockSelect].type == "to_do" &&
            event->where.h >= gColLeft + 2 && event->where.h <= gColLeft + 18) {
            pageElm[blockSelect].check = !pageElm[blockSelect].check;
            pageElm[blockSelect].dirty = true;
            gHasDirtyBlocks = true;
            gLastEditTick   = TickCount();
            updateBlocks();
        } else {
            textActive = pageElm[blockSelect].textEdit;
            if (textActive != nil) {
                TEActivate(textActive);
                TEClick(event->where, 0, textActive);
                /* --- notioncl:// link dispatch ---
                   After TEClick, selStart==selEnd means a simple click (not a drag).
                   Walk runs to find which one the caret landed in, then dispatch. */
                if ((*textActive)->selStart == (*textActive)->selEnd) {
                    short caretPos = (*textActive)->selStart;
                    short tePos = 0;
                    const vector<TextRun> runs = pageElm[blockSelect].runs; /* copy — pickers call updateBlocks */
                    bool dispatched = false;
                    for (size_t ri = 0; ri < runs.size(); ri++) {
                        const TextRun& run = runs[ri];
                        short runLen = 0;
                        for (size_t ci = 0; ci < run.text.size(); ci++)
                            runLen += ((unsigned char)run.text[ci] == (unsigned char)kEmojiSentinel) ? 3 : 1;
                        if (caretPos >= tePos && caretPos <= tePos + runLen && !run.url.empty()) {
                            if (run.url.find("http://notioncl/") == 0) {
                                string cmd = run.url.substr(16); /* strip "http://notioncl/" */
                                if (cmd == "icon-picker")        { ShowIconPicker();            dispatched = true; }
                                else if (cmd == "emoji-picker")  { ShowEmojiPicker();           dispatched = true; }
                                else if (cmd == "connection")    { ShowSettingsDialog(gSettings); dispatched = true; }
                            }
                            break;
                        }
                        tePos += runLen;
                    }
                    /* ShowIconPicker/ShowEmojiPicker call updateBlocks() which disposes and
                       recreates TE handles — textActive and pageElm[blockSelect] are stale.
                       Return immediately; the window was already redrawn by updateBlocks. */
                    if (dispatched) return;
                }
                TEUpdate(&pageElm[blockSelect].rect, textActive);
                { string te = TextToTE(pageElm[blockSelect].text, pageElm[blockSelect].emojiSeqs);
                  DrawBlockEmojis(textActive, te, pageElm[blockSelect].emojiSeqs);
                  DrawStrikethrough(textActive, pageElm[blockSelect].runs); }
            } else {
                updateBlocks();  /* divider: redraw to show selection highlight */
            }
            DrawControls(_window);
        }
    } else if (control == gScrollBar) {
        /* Scroll bar click */
        if (part == kControlIndicatorPart) {
            TrackControl(control, event->where, nil);
            gScrollOffset = GetControlValue(gScrollBar);
            updateBlocks();
        } else {
            TrackControl(control, event->where, gScrollActionUPP);
        }
    }
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main() {
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();

    /* Cache font numbers once — GetFNum is a slow system lookup */
    GetFNum((ConstStr255Param)"\pHelvetica", &gFontHelvetica);
    GetFNum((ConstStr255Param)"\pGeneva",    &gFontGeneva);
    GetFNum((ConstStr255Param)"\pMonaco",    &gFontMonaco);

    /* Cache the folder that contains this app so DrawNotionIcon can find icon/ */
    {
        ProcessSerialNumber psn = { 0, kCurrentProcess };
        ProcessInfoRec info;
        FSSpec appSpec;
        memset(&info, 0, sizeof(info));
        info.processInfoLength = sizeof(ProcessInfoRec);
        info.processName       = nil;
        info.processAppSpec    = &appSpec;
        if (GetProcessInformation(&psn, &info) == noErr) {
            gAppVRefNum = appSpec.vRefNum;
            gAppDirID   = appSpec.parID;
        }
    }
    InitMenus();
    TEInit();
    InitDialogs(NULL);

    SetMenuBar(GetNewMBar(128));
    AppendResMenu(GetMenu(128), 'DRVR');
    DrawMenuBar();

    /* Set arrow-key shortcuts for Move Up / Move Down in the Edit menu.
     * GetMenuHandle() returns the live menu already installed in the menu bar.
     * SetMenuItemKeyGlyph() needs Appearance Manager (Mac OS 8+); on System 7
     * it may not be available so we guard it with a Gestalt check. */
    {
        MenuHandle editMenu = GetMenuHandle(130);
        if (editMenu) {
            long gestaltResult = 0;
            if (Gestalt(gestaltAppearanceAttr, &gestaltResult) == noErr) {
                SetItemCmd(editMenu, 11, 0x1E);
                SetMenuItemKeyGlyph(editMenu, 11, kMenuUpArrowGlyph);
                SetItemCmd(editMenu, 12, 0x1F);
                SetMenuItemKeyGlyph(editMenu, 12, kMenuDownArrowGlyph);
            }
        }
    }

    InitCursor();

    gScrollActionUPP = NewControlActionUPP(scrollActionProc);

    /* Create window early so status can be shown in it */
    short initBottom = qd.screenBits.bounds.bottom - 4;
    if (initBottom > 570) initBottom = 570;
    SetRect(&_initialWindowRect, 6, 42, 505, initBottom);
    _window = NewWindow(NULL, &_initialWindowRect, "\pNotion classic",
                        true, documentProc, (WindowPtr)-1, true, 0);

    LoadSettings(gSettings);
    gFullWidth = LoadFullWidth();
    if (gFullWidth && _window != nil) {
        /* _initialWindowRect is the content-area rect passed to NewWindow */
        gSavedWindowRect = _initialWindowRect;
        short mbarH = *(short*)0x0BAA;
        if (mbarH < 16 || mbarH > 40) mbarH = 20;
        short screenW = qd.screenBits.bounds.right;
        short screenH = qd.screenBits.bounds.bottom - mbarH;
        MoveWindow(_window, 0, mbarH, true);
        SizeWindow(_window, screenW, screenH, true);
        /* resizeControls() is called later when MakeNewWindow runs */
    }
    /* Fresh install (no prefs): gSettings fields are empty; EffPageID()/EffAPIKey()
       fall back to the built-in demo page automatically.  No dialog on startup. */

    _httpClient.SetCipherSuite(MBEDTLS_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256);
    _httpClient.SetAuthorization("Bearer " + string(EffAPIKey()));

    /* Warm-up: complete the full TLS handshake with no data transfer so the
     * session is cached.  Both real requests (page + blocks) will then use
     * the fast abbreviated handshake — no ECDH computation. */
    /* Attempt 1: full ECDH handshake.  On slow hardware this may exceed the
     * server's timer and fail, but uECC_shared_secret() still completes
     * locally and caches the server's ephemeral key.
     * Attempt 2: Cloudflare reuses its ephemeral key within its rotation
     * window (~60 s), so the cache hits and the handshake finishes fast. */
    DrawStatusText("Notion classic is Dialing in\xC9");
    if (!_httpClient.WarmUp("api.notion.com")) {
        DrawStatusText("Teaching a 1989 chip about elliptic curves\xC9");
        if (!_httpClient.WarmUp("api.notion.com")) {
            DrawStatusText("One more try...");
            _httpClient.WarmUp("api.notion.com");
        }
    }
    DrawStatusText("Loading page\xC9");

    pageRequest();

    for (;;) {
        /* A non-200 / network error was flagged from inside a callback.
           Show settings so the user can correct credentials, then retry. */
        if (gNeedsReload) {
            gNeedsReload = false;
            HideWindow(_window);
            if (ShowSettingsDialog(gSettings, true, "Connection Error")) {
                ShowWindow(_window);
                _httpClient.SetAuthorization("Bearer " + string(EffAPIKey()));
                _curRequest = 0; gHasMore = false; gNextCursor.clear();
                pageElm.clear(); gToggleChildCache.clear(); blockSelect = 0; textActive = nil;
                pageRequest();
            } else {
                /* User chose Quit */
                _httpClient.SslFreeAll();
                ExitToShell();
            }
        }

        if (gTimedOut) {
            gTimedOut = false;
            _curRequest = 0; gHasMore = false; gNextCursor.clear();
            pageElm.clear(); gToggleChildCache.clear(); blockSelect = 0; textActive = nil;
            DrawStatusText("Request timed out. Retrying...");
            pageRequest();
        }

        /* Timeout watchdog: fires only during initial page load */
        if (_curRequest < 2 && gRequestStartTick > 0
            && TickCount() - gRequestStartTick > kRequestTimeoutTicks) {
            gRequestStartTick = 0;
            DrawStatusText("Request timed out.");
            gTimedOut = true;
        }

        if (_curRequest < 2 || gTogglePending) _httpClient.ProcessRequests();

        /* Give the save thread a slice each loop iteration */
        if (gSaveThreadActive) YieldToAnyThread();

        updateArrowVisibility();
        updateSaveButton();

        /* Watch cursor while loading/saving; arrow otherwise */
        { bool busy = (_curRequest < 2) || gSaveThreadActive || gIsSaving
                   || gTogglePending    || gApiPending;
          static bool sCursorIsWatch = false;
          if (busy && !sCursorIsWatch) {
              CursHandle wc = GetCursor(watchCursor);
              if (wc) SetCursor(*wc);
              sCursorIsWatch = true;
          } else if (!busy && sCursorIsWatch) {
              InitCursor();
              sCursorIsWatch = false;
          }
        }

        EventRecord e;
        WindowRef win;
        /* Spin during page load or active save; blink when editing; sleep when idle */
        long sleepTicks = (_curRequest < 2 || gSaveThreadActive || gTogglePending) ? 0
                        : (textActive != nil) ? GetCaretTime()
                        : 6;

        if (WaitNextEvent(everyEvent, &e, sleepTicks, nil)) {
            switch (e.what) {
                case nullEvent:
                    if (textActive) { SetPort(_window); TEIdle(textActive); }
                    break;
                case keyDown: {
                    char c = (char)(e.message & charCodeMask);
                    short vkey = (short)((e.message & keyCodeMask) >> 8);
                    /* --- Scroll keys: intercept by virtual key code before any TE handling ---
                       vkey 0x7E=Up  0x7D=Down  0x74=PageUp  0x79=PageDown
                            0x73=Home  0x77=End
                       These fire regardless of whether a TE is active so they never
                       fall through to TEKey and insert a character. */
                    if (!(e.modifiers & cmdKey) && gScrollBar != nil &&
                        (vkey==0x7E||vkey==0x7D||vkey==0x74||vkey==0x79||vkey==0x73||vkey==0x77)
                        && textActive == nil) {
                        short winH0    = _window->portRect.bottom - _window->portRect.top;
                        short pageAmt0 = winH0 - kLineScrollAmount;
                        short scrollBy0 = 0;
                        if      (vkey == 0x7E) scrollBy0 = -kLineScrollAmount;
                        else if (vkey == 0x7D) scrollBy0 =  kLineScrollAmount;
                        else if (vkey == 0x74) scrollBy0 = -pageAmt0;
                        else if (vkey == 0x79) scrollBy0 =  pageAmt0;
                        else if (vkey == 0x73) scrollBy0 = -32767;
                        else if (vkey == 0x77) scrollBy0 =  32767;
                        short cur0 = GetControlValue(gScrollBar);
                        short min0 = GetControlMinimum(gScrollBar);
                        short max0 = GetControlMaximum(gScrollBar);
                        short nv0  = cur0 + scrollBy0;
                        if (nv0 < min0) nv0 = min0;
                        if (nv0 > max0) nv0 = max0;
                        if (nv0 != cur0) {
                            short d0 = nv0 - cur0;
                            Rect cr0 = _window->portRect;
                            cr0.left = gColLeft; cr0.right -= kScrollBarWidth;
                            RgnHandle ur0 = NewRgn();
                            ScrollRect(&cr0, 0, -d0, ur0);
                            EraseRgn(ur0);
                            SetControlValue(gScrollBar, nv0);
                            gScrollOffset = nv0;
                            gScrolling = true; gScrollClipRgn = ur0;
                            updateBlocks();
                            gScrollClipRgn = nil; gScrolling = false;
                            DisposeRgn(ur0);
                        }
                        break;  /* consume the key — never reaches TEKey */
                    }
                    if (!(e.modifiers & cmdKey) && gScrollBar != nil &&
                        (vkey==0x74||vkey==0x79) && textActive != nil) {
                        /* Page Up/Down while editing: scroll without deactivating the field */
                        short winH0    = _window->portRect.bottom - _window->portRect.top;
                        short pageAmt0 = winH0 - kLineScrollAmount;
                        short scrollBy0 = (vkey == 0x74) ? -pageAmt0 : pageAmt0;
                        short cur0 = GetControlValue(gScrollBar);
                        short min0 = GetControlMinimum(gScrollBar);
                        short max0 = GetControlMaximum(gScrollBar);
                        short nv0  = cur0 + scrollBy0;
                        if (nv0 < min0) nv0 = min0;
                        if (nv0 > max0) nv0 = max0;
                        if (nv0 != cur0) {
                            short d0 = nv0 - cur0;
                            TEDeactivate(textActive);
                            Rect cr0 = _window->portRect;
                            cr0.left = gColLeft; cr0.right -= kScrollBarWidth;
                            RgnHandle ur0 = NewRgn();
                            ScrollRect(&cr0, 0, -d0, ur0);
                            EraseRgn(ur0);
                            SetControlValue(gScrollBar, nv0);
                            gScrollOffset = nv0;
                            gScrolling = true; gScrollClipRgn = ur0;
                            updateBlocks();
                            gScrollClipRgn = nil; gScrolling = false;
                            DisposeRgn(ur0);
                            if (blockSelect >= 0 && blockSelect < (int)pageElm.size()) {
                                textActive = pageElm[blockSelect].textEdit;
                                if (textActive) TEActivate(textActive);
                            }
                        }
                        break;  /* consume — never reaches TEKey */
                    }
                    if (e.modifiers & cmdKey) {
                        if (c == 0x1E || c == 0x1F) {
                            /* Cmd+Up/Down — move current block */
                            moveBlock(c == 0x1E ? -1 : 1);
                        } else if (c == 'z' && gUndoBlock == blockSelect
                                   && gUndoBlock >= 0 && gUndoBlock < (int)pageElm.size()
                                   && gUndoStackSize > 0) {
                            Block& ub = pageElm[gUndoBlock];
                            UndoState& us = gUndoStack[--gUndoStackSize];
                            ub.text      = us.text;
                            ub.emojiSeqs = us.emojiSeqs;
                            if (!us.type.empty() && us.type != ub.type) {
                                ub.type        = us.type;
                                ub.typeChanged = true;
                            }
                            /* Force TE recreation so updateBlocks sets text with the
                               correct type-based font (TESetText resets char styles to
                               the current GrafPort font if called outside updateBlocks) */
                            ub.teCachedWidth = 0;
                            ub.teCachedType.clear();
                            ub.teCache.clear();
                            ub.dirty        = true;
                            gHasDirtyBlocks = true;
                            gLastEditTick   = TickCount();
                            updateBlocks();
                            if (blockSelect >= 0 && blockSelect < (int)pageElm.size()) {
                                textActive = pageElm[blockSelect].textEdit;
                                short tlen = (*textActive)->teLength;
                                TESetSelect(tlen, tlen, textActive);
                                TEActivate(textActive);
                            }
                        } else if ((c == 'c' || c == 'x') && textActive) {
                            /* Copy / Cut */
                            if (c == 'x') TECut(textActive);
                            else          TECopy(textActive);
                            ZeroScrap();
                            TEToScrap();
                            if (c == 'x' && blockSelect >= 0 && blockSelect < (int)pageElm.size()) {
                                Block& b = pageElm[blockSelect];
                                CharsHandle ch = TEGetText(textActive);
                                short len = (*textActive)->teLength;
                                HLock((Handle)ch); b.text = StripEmojiPad(string(*ch, len)); HUnlock((Handle)ch);
                                b.dirty = true; gHasDirtyBlocks = true; gLastEditTick = TickCount();
                                short sel = (*textActive)->selStart;
                                updateBlocks();
                                textActive = pageElm[blockSelect].textEdit;
                                TEActivate(textActive);
                                TESetSelect(sel, sel, textActive);
                            }
                        } else if (c == 'v' && textActive
                                   && blockSelect >= 0 && blockSelect < (int)pageElm.size()) {
                            /* Paste */
                            TEFromScrap();
                            TEPaste(textActive);
                            Block& b = pageElm[blockSelect];
                            CharsHandle ch = TEGetText(textActive);
                            short len = (*textActive)->teLength;
                            HLock((Handle)ch); b.text = StripEmojiPad(string(*ch, len)); HUnlock((Handle)ch);
                            b.dirty = true; gHasDirtyBlocks = true; gLastEditTick = TickCount();
                            short sel = (*textActive)->selStart;
                            updateBlocks();
                            textActive = pageElm[blockSelect].textEdit;
                            TEActivate(textActive);
                            TESetSelect(sel, sel, textActive);
                        } else {
                            UpdateMenus();
                            DoMenuCommand(MenuKey(c));
                        }
                    } else if (textActive == nil
                               && blockSelect >= 0 && blockSelect < (int)pageElm.size()
                               && (c == 0x1E || c == 0x1F)) {
                        /* Arrow key from a divider (no TE) */
                        int dest = (c == 0x1E) ? blockSelect - 1 : blockSelect + 1;
                        if (dest >= 0 && dest < (int)pageElm.size()) {
                            blockSelect = dest;
                            textActive  = pageElm[dest].textEdit;
                            if (textActive != nil) {
                                if (c == 0x1E) {
                                    short len = (*textActive)->teLength;
                                    TESetSelect(len, len, textActive);
                                } else {
                                    TESetSelect(0, 0, textActive);
                                }
                            }
                            updateBlocks();
                            DrawControls(_window);
                        }
                    } else if (textActive != nil) {

                        /* ---- Tab: indent block into the toggle above ---- */
                        if (c == '\t') {
                            doIndentBlock();
                        } else

                        /* ---- Enter: split block and create new one below ---- */
                        if (c == 0x0D
                            && blockSelect >= 0 && blockSelect < (int)pageElm.size()
                            && !gApiPending && !gIsSaving && !gSaveThreadActive) {
                            /* Title: Enter moves focus to first body block */
                            if (pageElm[blockSelect].type == "title") {
                                int dest = blockSelect + 1;
                                if (dest < (int)pageElm.size()) {
                                    TEDeactivate(textActive);
                                    TEUpdate(&pageElm[blockSelect].rect, textActive);
                                    blockSelect = dest;
                                    textActive  = pageElm[dest].textEdit;
                                    if (textActive != nil) {
                                        TEActivate(textActive);
                                        TESetSelect(0, 0, textActive);
                                    }
                                    updateBlocks();
                                    DrawControls(_window);
                                }
                                break;
                            }
                            Block& cur = pageElm[blockSelect];
                            short  splitPos = (*textActive)->selEnd;   /* cut after selection */

                            /* Grab raw TE text and split at cursor */
                            CharsHandle ch = TEGetText(textActive);
                            short teLen    = (*textActive)->teLength;
                            HLock((Handle)ch);
                            string rawTE   = string(*ch, (size_t)teLen);
                            HUnlock((Handle)ch);
                            string before  = rawTE.substr(0, splitPos);
                            string after   = (splitPos < teLen) ? rawTE.substr(splitPos) : "";

                            /* Split emojiSeqs at the sentinel boundary */
                            int beforeEmoji = 0;
                            for (int si = 0; si < splitPos; si++)
                                if ((unsigned char)rawTE[si] == (unsigned char)kEmojiSentinel)
                                    beforeEmoji++;
                            vector<string> afterSeqs(cur.emojiSeqs.begin() + beforeEmoji,
                                                     cur.emojiSeqs.end());
                            cur.emojiSeqs.resize(beforeEmoji);

                            /* Update current block with text before cursor */
                            cur.text  = StripEmojiPad(before);
                            cur.dirty = true;
                            gHasDirtyBlocks = true;
                            gLastEditTick   = TickCount();

                            /* New block type: continue lists, else paragraph.
                               Toggle headers always produce a paragraph child. */
                            const string& ct = cur.type;
                            string newType = (ct == "bulleted_list_item" ||
                                             ct == "numbered_list_item" ||
                                             ct == "to_do") ? ct : "paragraph";
                            static const char* kNTypes[] = {
                                "paragraph","heading_1","heading_2","heading_3",
                                "bulleted_list_item","numbered_list_item","to_do","toggle",
                                "code","quote","callout",
                                "toggle_heading_1","toggle_heading_2","toggle_heading_3",
                                "divider"
                            };
                            int typeIdx = 0;
                            for (int ti = 0; ti < 15; ti++)
                                if (newType == kNTypes[ti]) { typeIdx = ti; break; }

                            /* Deactivate old block to erase its cursor before inserting */
                            {
                                Rect oldRect = cur.rect;
                                SetPort(_window);
                                TEDeactivate(textActive);
                                TEUpdate(&oldRect, textActive);
                            }

                            doAddBlock(typeIdx);   /* inserts block, updates blockSelect */

                            /* Populate new block with text after cursor */
                            if (!after.empty() && blockSelect >= 0
                                && blockSelect < (int)pageElm.size()) {
                                Block& nb  = pageElm[blockSelect];
                                nb.text    = StripEmojiPad(after);
                                nb.emojiSeqs = afterSeqs;
                                nb.dirty   = true;
                                /* Refresh TE to show the after-text */
                                string teAfter = TextToTE(nb.text, nb.emojiSeqs);
                                if (nb.textEdit != nil)
                                    TESetText(teAfter.c_str(), teAfter.length(), nb.textEdit);
                                updateBlocks();
                                textActive = pageElm[blockSelect].textEdit;
                            }

                            if (textActive != nil) {
                                TEActivate(textActive);
                                TESetSelect(0, 0, textActive);
                                TEUpdate(&pageElm[blockSelect].rect, textActive);
                            }
                            break;
                        }

                        /* ---- up/down arrow: jump between blocks at boundary ---- */
                        if ((c == 0x1E || c == 0x1F)
                            && blockSelect >= 0 && blockSelect < (int)pageElm.size()) {
                            short sel    = (*textActive)->selStart;
                            short nLines = (*textActive)->nLines;
                            bool  jump   = false;
                            int   dest   = blockSelect;

                            if (c == 0x1E) {  /* up arrow */
                                bool onFirstLine = (nLines <= 1)
                                    || (sel <= (*textActive)->lineStarts[1]);
                                if (onFirstLine && blockSelect > 0) {
                                    dest = blockSelect - 1;
                                    jump = true;
                                }
                            } else {           /* down arrow */
                                bool onLastLine = (nLines <= 1)
                                    || (sel >= (*textActive)->lineStarts[nLines - 1]);
                                if (onLastLine && blockSelect < (int)pageElm.size() - 1) {
                                    dest = blockSelect + 1;
                                    jump = true;
                                }
                            }

                            if (jump) {
                                /* Deactivate old block to erase cursor/selection */
                                SetPort(_window);
                                TEDeactivate(textActive);
                                TEUpdate(&pageElm[blockSelect].rect, textActive);
                                { string te = TextToTE(pageElm[blockSelect].text, pageElm[blockSelect].emojiSeqs);
                                  DrawBlockEmojis(textActive, te, pageElm[blockSelect].emojiSeqs);
                                  DrawStrikethrough(textActive, pageElm[blockSelect].runs); }

                                blockSelect = dest;
                                textActive  = pageElm[dest].textEdit;

                                if (textActive != nil) {
                                    TEActivate(textActive);
                                    /* Place cursor: end when coming from below, start from above */
                                    if (c == 0x1E) {
                                        short len = (*textActive)->teLength;
                                        TESetSelect(len, len, textActive);
                                    } else {
                                        TESetSelect(0, 0, textActive);
                                    }
                                    TEUpdate(&pageElm[dest].rect, textActive);
                                    { string te = TextToTE(pageElm[dest].text, pageElm[dest].emojiSeqs);
                                      DrawBlockEmojis(textActive, te, pageElm[blockSelect].emojiSeqs);
                                      DrawStrikethrough(textActive, pageElm[blockSelect].runs); }
                                } else {
                                    /* Landed on a divider — redraw to show selection */
                                    updateBlocks();
                                }
                                DrawControls(_window);
                                break;   /* skip normal TEKey handling */
                            }
                        }

                        /* Backspace on empty block — same as Cmd+Delete */
                        if (c == 0x08
                            && (*textActive)->teLength == 0
                            && blockSelect > 0 && blockSelect < (int)pageElm.size()
                            && pageElm[blockSelect].type != "title"
                            && !gApiPending && !gIsSaving && !gSaveThreadActive) {
                            doDeleteBlock();
                            break;
                        }

                        short linesBefore = (*textActive)->nLines;
                        if (linesBefore < 1) linesBefore = 1;
                        TEKey(c, textActive);
                        /* Sync edited content back to block so updateBlocks preserves it */
                        Block& b = pageElm[blockSelect];
                        if (b.textEdit == textActive) {
                            CharsHandle ch    = TEGetText(textActive);
                            short len         = (*textActive)->teLength;
                            short selStart    = (*textActive)->selStart;
                            short selEnd      = (*textActive)->selEnd;
                            short linesAfter  = (*textActive)->nLines;
                            if (linesAfter < 1) linesAfter = 1;
                            HLock((Handle)ch);
                            string rawTE    = string(*ch, (size_t)len);
                            HUnlock((Handle)ch);
                            string strippedTE = StripEmojiPad(rawTE);
                            /* Sync emojiSeqs with surviving sentinels in rawTE */
                            { vector<string> ns; size_t ei = 0;
                              for (size_t j = 0; j < rawTE.size(); j++)
                                  if ((unsigned char)rawTE[j] == (unsigned char)kEmojiSentinel
                                      && ei < b.emojiSeqs.size())
                                      ns.push_back(b.emojiSeqs[ei++]);
                              b.emojiSeqs = ns; }
                            if (strippedTE != b.text) {
                                if (gUndoBlock != blockSelect) {
                                    /* new block — reset stack and push pre-edit state */
                                    gUndoStackSize = 0;
                                    gUndoCharCount = 0;
                                    gUndoBlock     = blockSelect;
                                    gUndoStack[gUndoStackSize].text      = b.text;
                                    gUndoStack[gUndoStackSize].emojiSeqs = b.emojiSeqs;
                                    gUndoStack[gUndoStackSize].type      = b.type;
                                    gUndoStackSize++;
                                } else {
                                    gUndoCharCount++;
                                    if (gUndoCharCount >= kUndoCharStep && gUndoStackSize < kUndoMax) {
                                        gUndoStack[gUndoStackSize].text      = b.text;
                                        gUndoStack[gUndoStackSize].emojiSeqs = b.emojiSeqs;
                                        gUndoStack[gUndoStackSize].type      = b.type;
                                        gUndoStackSize++;
                                        gUndoCharCount = 0;
                                    }
                                }
                                b.text          = strippedTE;
                                b.dirty         = true;
                                gHasDirtyBlocks = true;
                                gLastEditTick   = TickCount();
                                /* Keep window title bar in sync with page title */
                                if (b.type == "title") {
                                    char wtitle[256] = " ";
                                    strncat(wtitle, b.text.c_str(), 253);
                                    SetWTitle(_window, (ConstStr255Param)wtitle);
                                }
                            } else {
                                b.text = strippedTE; /* keep in sync without dirtying */
                            }
                            if (linesAfter != linesBefore) {
                                /* Line wrap changed — re-layout so blocks below shift */
                                updateBlocks();
                                textActive = pageElm[blockSelect].textEdit;
                                TESetSelect(selStart, selEnd, textActive);
                            } else {
                                /* Same line count — redraw only this block, no flicker */
                                SetPort(_window);
                                TEUpdate(&b.rect, textActive);
                                DrawBlockEmojis(textActive, rawTE, pageElm[blockSelect].emojiSeqs);
                                DrawStrikethrough(textActive, pageElm[blockSelect].runs);
                            }
                        }
                    }
                    break;
                }  /* end case keyDown */

                case mouseDown:
                    switch (FindWindow(e.where, &win)) {
                        case inGoAway:
                            if (TrackGoAway(win, e.where)) { _httpClient.SslFreeAll(); ExitToShell(); }
                            break;
                        case inDrag:
                            DragWindow(win, e.where, &qd.screenBits.bounds);
                            break;
                        case inMenuBar:
                            UpdateMenus();
                            DoMenuCommand(MenuSelect(e.where));
                            break;
                        case inContent:
                            doInContent(&e, win);
                            break;
                        case inGrow: {
                            Rect sizeRect;
                            SetRect(&sizeRect, 200, 100,
                                    qd.screenBits.bounds.right  - 2,
                                    qd.screenBits.bounds.bottom - 2);
                            long newSize = GrowWindow(win, e.where, &sizeRect);
                            if (newSize != 0) {
                                SizeWindow(win, (short)(newSize & 0xFFFF), (short)(newSize >> 16), true);
                                resizeControls();
                                updateBlocks();
                                if (blockSelect >= 0 && blockSelect < (int)pageElm.size())
                                    textActive = pageElm[blockSelect].textEdit;
                                gLastSaveLabelIdx = -1; gLastBarFilled = -1;
                                updateSaveButton();
                            }
                            break;
                        }
                        case inSysWindow:
                            SystemClick(&e, win);
                            break;
                    }
                    break;

                case updateEvt:
                    _window = (WindowRef)e.message;
                    BeginUpdate(_window);
                    updateBlocks();
                    if (blockSelect >= 0 && blockSelect < (int)pageElm.size())
                        textActive = pageElm[blockSelect].textEdit;
                    gLastSaveLabelIdx = -1; gLastBarFilled = -1;
                    updateSaveButton();
                    EndUpdate(_window);
                    break;
            }
        } else {
            /* WaitNextEvent returned false (no event) — tick caret */
            if (textActive) { SetPort(_window); TEIdle(textActive); }
        }
    }
    return 0;
}
