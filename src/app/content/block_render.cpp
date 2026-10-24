/* block_render.cpp — Block rendering and UI functions extracted from main.cpp.
   Contains: updateScrollRange, RedrawBlock, SyncActiveBlock,
             beginIndentOperation, moveBlock, ClearSidebar,
             drawSidebarControls, updateArrowVisibility,
             updateBlocks, DrawStatusText, MakeNewWindow,
             drawProgressBar, updateSaveButton, InvalidateSaveButton. */

#include <Memory.h>
#include <Windows.h>
#include <Controls.h>
#include <Events.h>
#include <TextEdit.h>
#include <Quickdraw.h>
#include <Sound.h>

#include "block_render.h"
#include "block_ops.h"      /* block array, toggles, groups, flashes */
#include "app_state.h"
#include "helpers.h"
#include "blocks.h"
#include "run_styles.h"
#include "emoji_text.h"
#include "picker.h"
#include "save_pipeline.h"
#include "navigation.h"
#include "debug_con.h"
#include "Settings.h"

#include <string>
#include <string.h>
using std::string;

/* ------------------------------------------------------------------ */
/* Sidebar pending-click state — defined here, declared extern in .h  */
/* ------------------------------------------------------------------ */
SidebarPending  gSidebarPending      = kSPNone;
long            gSidebarPendingTick  = 0;
int             gSidebarPendingBlock = -1;
int             gHoverBlockIdx       = -1;
static long     sHoverLockUntil      = 0;   /* TickCount deadline — lock hover to gHoverBlockIdx */

/* One-shot flag: set by the HTTP response handler before calling
   updateBlocks() or MakeNewWindow().  Tells updateBlocks() to skip
   erasing the already-rendered title+icon area so stable header pixels
   are not whited-out and redrawn when the same (or updated) content
   arrives from the network.  Consumed (cleared) inside updateBlocks(). */
bool gSkipHeaderErase = false;

/* One-shot flag: set before updateBlocks() when the whole window must be
   repainted, not just the sidebar+content rects (e.g. after a screen-centered
   picker window closes over the scrollbar column / bottom margins, which the
   normal targeted erase never touches → ghost pixels). Consumed in updateBlocks(). */
bool gFullEraseOnce = false;

static const long kSidebarPendingTimeout = 60L;  /* TickCount() ticks at ~16ms; 60 ≈ 1 second */

/* ------------------------------------------------------------------ */
/* Local constants (values match main.cpp; kept here to avoid extern   */
/* on static const — C++ static const is TU-local by default)         */
/* ------------------------------------------------------------------ */
static const short kBottomMargin  = 28;
static const long  kAutoSaveTicks = 2400L;
extern "C" volatile unsigned long g_net_activity_tick;   /* net_sockets_mac.c */

/* Mouse-check throttle for updateArrowVisibility */
static long gLastMouseCheckTick = 0;

/* ------------------------------------------------------------------ */
/* Extern declarations — globals defined in main.cpp                   */
/* ------------------------------------------------------------------ */

extern WindowPtr        gWindow;
extern Rect             gInitialWindowRect;
extern ControlHandle    gScrollBar;
extern ControlHandle    gSaveButton;
extern int              gScrollOffset;
extern int              gTotalContentHeight;
extern short            gColLeft;
extern AppState         gAppState;
extern char             gPageIconName[32];
extern char             gPageIconColor[32];
extern int              blockSelect;
extern TEHandle         textActive;
extern Block*           pageElm;
extern int              pageElmCount;
extern bool             gArrowsVisible;
extern RgnHandle        gScrollClipRgn;
extern bool             gScrolling;
extern Rect             gMoveUpArrowRect;
extern Rect             gMoveDownArrowRect;
extern Rect             gAddBlockRect;
extern Rect             gIconRect;
extern bool             gFullWidth;
extern SInt16           gFontHelvetica;
extern SInt16           gFontGeneva;
extern int              gCurRequest;
extern uint32_t         gToggleSerial;

extern bool             g_titleIsPlaceholder;

/* Constants from main.cpp */
extern const short      kScrollBarWidth;
extern const short      kLineScrollAmount;
extern const short      kSidebarWidth;

/* #define from main.cpp — memory-mapped hardware register */
#define kMBarHeight (*(short*)0x0BAA)

/* ------------------------------------------------------------------ */
/* Extern function declarations — defined in main.cpp                  */
/* ------------------------------------------------------------------ */

void   MarkDirty();

/* ------------------------------------------------------------------ */
/* Block type descriptor table — one row per BlockType (0–16).        */
/* Field order matches struct BlockDesc in blocks.h:                  */
/*  fontSize  useHelvetica  bold  leftBase  useActualDepth            */
/*  topMargin  lineHeightPct  teRightInset  teTopInset                */
/*  hasBox  boxFill  extraLineH  hasChecked  useTitle                 */
/*  decor  counter                                                    */
/* ------------------------------------------------------------------ */
const BlockDesc kBlockDesc[17] = {
/* 0  PARAGRAPH  */ { 12, false, false,   0, false,   0,  100,   0,  0, false, false,  true, false, false, DECOR_NONE,     CTR_RESET     },
/* 1  HEADING_1  */ { 20,  true,  true,   0, false,  16,  130,   0,  0, false, false, false, false, false, DECOR_NONE,     CTR_IGNORE    },
/* 2  HEADING_2  */ { 16,  true,  true,   0, false,  12,  120,   0,  0, false, false, false, false, false, DECOR_NONE,     CTR_IGNORE    },
/* 3  HEADING_3  */ { 14,  true,  true,   0, false,   6,  100,   0,  0, false, false, false, false, false, DECOR_NONE,     CTR_IGNORE    },
/* 4  BULLETED   */ { 12, false, false,  20,  true,   0,  100,   0,  0, false, false,  true, false, false, DECOR_BULLET,   CTR_RESET     }, /* leftBase=20: click detection uses TEClick on TE rect, not leftBase */
/* 5  NUMBERED   */ { 12, false, false,  20,  true,   0,  100,   0,  0, false, false,  true, false, false, DECOR_NUMBER,   CTR_INCREMENT }, /* leftBase=20: same */
/* 6  TODO       */ { 12, false, false,  19,  true,   0,  100,   0,  0, false, false,  true,  true, false, DECOR_CHECKBOX, CTR_IGNORE    },
/* 7  TOGGLE     */ { 12, false,  true,  18,  true,   0,  100,   0,  0, false, false,  true, false, false, DECOR_TRIANGLE, CTR_RESET     },
/* 8  CODE       */ { 12, false, false,   3, false,   0,  100,   4,  8,  true,  true, false, false, false, DECOR_NONE,     CTR_RESET     },
/* 9  QUOTE      */ { 12, false, false,  11, false,   8,  100,   0,  0, false, false,  true, false, false, DECOR_QUOTEBAR, CTR_RESET     },
/* 10 CALLOUT    */ { 12, false, false,  21, false,  12,    0,   8,  0,  true, false,  true, false, false, DECOR_NONE,     CTR_RESET     },
/* 11 TOGGLE_H1  */ { 20,  true,  true,  18,  true,  16,  130,   0,  0, false, false, false, false, false, DECOR_TRIANGLE, CTR_RESET     },
/* 12 TOGGLE_H2  */ { 16,  true,  true,  18,  true,  12,  120,   0,  0, false, false, false, false, false, DECOR_TRIANGLE, CTR_RESET     },
/* 13 TOGGLE_H3  */ { 14,  true,  true,  18,  true,   6,  100,   0,  0, false, false, false, false, false, DECOR_TRIANGLE, CTR_RESET     },
/* 14 DIVIDER    */ { 12, false, false,   0, false,   0,  100,   0,  0, false, false, false, false, false, DECOR_NONE,     CTR_IGNORE    },
/* 15 TITLE      */ { 26,  true,  true,   0, false,   0,  160,   0,  0, false, false, false, false, false, DECOR_NONE,     CTR_IGNORE    },
/* 16 CHILD_PAGE */ { 12, false, false,   0, false,   0,  100,   0,  0, false, false, false, false,  true, DECOR_NONE,     CTR_IGNORE    },
};

/* ------------------------------------------------------------------ */
/* updateScrollRange — sync scroll bar to content height               */
/* ------------------------------------------------------------------ */
static void updateScrollRange()
{
    if (gScrollBar == nil) return;

    short visibleHeight = gWindow->portRect.bottom - gWindow->portRect.top;
    short maxScroll     = gTotalContentHeight - visibleHeight;
    if (maxScroll < 0) maxScroll = 0;

    SetControlMaximum(gScrollBar, maxScroll);
    SetControlValue(gScrollBar, gScrollOffset);
}

/* ------------------------------------------------------------------ */
/* RedrawBlock — TEUpdate + emoji + strikethrough for one block        */
/* ------------------------------------------------------------------ */
void RedrawBlock(Block& b, TEHandle te) {
    static char _rdbuf[kTEBufLen]; short _rdlen;
    TextToTE(b.text, b.textLen, _rdbuf, sizeof(_rdbuf), _rdlen);
    EraseRect(&b.vrect);
    TEUpdate(&b.vrect, te);
    DrawBlockEmojis(te, _rdbuf, _rdlen, b);
    DrawStrikethrough(te, b);
}

/* ------------------------------------------------------------------ */
/* SyncActiveBlock — sync textActive into blockSelect.text             */
/* ------------------------------------------------------------------ */
void SyncActiveBlock() {
    if (textActive != nil && HasSel()) {
        Block& cur = pageElm[blockSelect];
        if (cur.textEdit == textActive) SyncBlockText(cur);
    }
}

/* ------------------------------------------------------------------ */
/* beginIndentOperation — re-parent a block locally and flag it to be
   re-created under its new parent on save (insert first, then trash). */
/* ------------------------------------------------------------------ */
void beginIndentOperation(int blockIdx, const char* parentId, int depth) {
    /* Local re-parent only.  The save re-creates the block under its new
       parent (insert first, then trash the old copy — see patchNext's
       type-change path), so a failed insert can't lose it. */
    Block& blk   = pageElm[blockIdx];
    blk.isChild  = true;
    blk.depth    = depth;
    setId(blk.parentId, parentId);
    blk.leftParent = true;
    blk.invalidateTE();
    blk.recreate = true;
    blk.dirty       = true;
    MarkDirty();
    ToggleCacheEvict(parentId);
}

/* ------------------------------------------------------------------ */
/* Move animation — fade source, scroll only the two-block region    */
/* ------------------------------------------------------------------ */
/* Fade rows [top, top+h): 50% gray, pause, then very light */
static void FadeRows(short left, short top, short h)
{
    Rect portR = gWindow->portRect;
    short fadeBot = (top + h < portR.bottom) ? top + h : portR.bottom;
    Rect grayR;
    SetRect(&grayR, left, top, portR.right - kScrollBarWidth, fadeBot);
    PenPat(&qd.gray); PenMode(patBic);
    PaintRect(&grayR);
    PenNormal();
    { unsigned long d; Delay(4, &d); }
    PenPat(&qd.dkGray); PenMode(patBic);
    PaintRect(&grayR);
    PenNormal();
}

static void AnimateMove(short srcTop, short srcH, short destTop, short destH, int dir)
{
    if (!gWindow || srcH <= 4) return;
    SetPort(gWindow);
    Rect portR = gWindow->portRect;
    short clipRight = portR.right - kScrollBarWidth;

    FadeRows(portR.left, srcTop, srcH);

    /* Scroll only the two-block region — D, E, F stay still.
       Move DOWN: scroll region up by srcH (C slides up, gap opens below C).
       Move UP:   scroll region down by srcH (A slides down, gap opens above A). */
    RgnHandle rgn = NewRgn();
    if (!rgn) return;

    short regionTop = (dir > 0) ? srcTop : destTop;
    short regionBot = regionTop + srcH + destH;
    if (regionBot > portR.bottom) regionBot = portR.bottom;
    short scrollDir = (dir > 0) ? -1 : 1;

    int steps = 4;
    short remaining = srcH;
    for (int i = 0; i < steps; i++) {
        short dv = (i < steps - 1) ? srcH / steps : remaining;
        remaining -= dv;
        Rect r;
        SetRect(&r, portR.left, regionTop, clipRight, regionBot);
        ScrollRect(&r, 0, scrollDir * dv, rgn);
        EraseRgn(rgn);
        unsigned long dummy; Delay(2, &dummy);
    }
    DisposeRgn(rgn);

    /* Brief pause to see the gap before updateBlocks fills it */
    { unsigned long d; Delay(5, &d); }
}

/* ------------------------------------------------------------------ */
/* AnimateUnitMove — MoveUnit's hook, run while the old layout is still */
/* on screen: unit [s, e) is about to swap with its neighbour [nS, nE). */
/* ------------------------------------------------------------------ */
static void AnimateUnitMove(int s, int e, int nS, int nE, int dir) {
    short srcTop = pageElm[s].rect.top,  srcH = pageElm[e - 1].rect.bottom - srcTop;
    short nTop   = pageElm[nS].rect.top, nH   = pageElm[nE - 1].rect.bottom - nTop;
    int   newIdx = (dir < 0) ? nS : s + (nE - nS);
    bool  doAnim = gWindow && srcH > 4 &&
                   srcTop < gWindow->portRect.bottom && srcTop + srcH > gWindow->portRect.top;
    if (doAnim) {
        AnimateMove(srcTop, srcH, nTop, nH, dir);
        /* Keep the sidebar controls on it at the new position; lock hover
           so mouse tracking doesn't immediately override. */
        gHoverBlockIdx  = newIdx;
        gArrowsVisible  = true;
        SetSidebarPending((dir > 0) ? kSPDown : kSPUp, newIdx);
        sHoverLockUntil = gSidebarPendingTick + 40;
        return;
    }
    /* Off screen: scroll along so it stays at the same screen Y */
    int delta = (dir < 0) ? nTop - srcTop
                          : pageElm[nE - 1].rect.bottom - pageElm[e - 1].rect.bottom;
    if (delta == 0) return;
    gScrollOffset += delta;
    if (gScrollOffset < 0) gScrollOffset = 0;
    short winH      = gWindow->portRect.bottom - gWindow->portRect.top;
    short maxScroll = gTotalContentHeight - winH;
    if (maxScroll < 0) maxScroll = 0;
    if (gScrollOffset > maxScroll) gScrollOffset = maxScroll;
    updateScrollRange();
}

/* Left edge of a block's content at this depth (see the layout's childIndent) */
static short IndentPx(int depth) { return depth > 0 ? (short)(18 + (depth - 1) * 20) : 0; }

/* AnimateIndent — block idx changes level in place: fade it, then slide
   it sideways to its new indent.  Sets the sidebar hover like a move. */
static void AnimateIndent(int idx, int newDepth, int dir)
{
    const Block& b = pageElm[idx];
    short top = b.rect.top, h = b.rect.bottom - top;
    if (!gWindow || h <= 4 || top >= gWindow->portRect.bottom || b.rect.bottom <= gWindow->portRect.top)
        return;
    SetPort(gWindow);
    FadeRows(gColLeft, top, h);
    short dx = IndentPx(newDepth) - IndentPx(b.depth);
    RgnHandle rgn = NewRgn();
    if (rgn && dx != 0) {
        Rect r; SetRect(&r, gColLeft, top, gWindow->portRect.right - kScrollBarWidth,
                        b.rect.bottom < gWindow->portRect.bottom ? b.rect.bottom : gWindow->portRect.bottom);
        short remaining = dx;
        for (int i = 0; i < 4; i++) {
            short step = (i < 3) ? dx / 4 : remaining;
            remaining -= step;
            ScrollRect(&r, step, 0, rgn);
            EraseRgn(rgn);
            unsigned long d; Delay(2, &d);
        }
    }
    if (rgn) DisposeRgn(rgn);
    { unsigned long d; Delay(5, &d); }
    gHoverBlockIdx  = idx;
    gArrowsVisible  = true;
    SetSidebarPending((dir > 0) ? kSPDown : kSPUp, idx);
    sHoverLockUntil = gSidebarPendingTick + 40;
}

/* ------------------------------------------------------------------ */
/* moveBlock — move the selected block one step up or down.  Past a     */
/* sibling (with its pieces and children) it is MoveUnit; a plain block */
/* also steps into and out of toggles, one level per press.            */
/* ------------------------------------------------------------------ */
void moveBlock(int dir) {
    if (blockSelect <= 0) return;
    /* Indices in the save queue must not shift under a running save */
    if (gAppState.busy()) return;
    if (!AllowEdit(blockSelect, kEditMove)) return;
    /* A block that can't be re-created itself — children (loaded or not),
       a split block, one that didn't fully load — only moves past its
       siblings, with the neighbour re-created on the other side instead. */
    { int head = GroupHead(blockSelect);
      if (ToggleHasChildren(head) || pageElm[head].hasChildren ||
          InGroup(head) || pageElm[head].cutShort) {
          SyncActiveBlock();
          MoveUnit(head, dir, AnimateUnitMove);
          return;
      } }
    Block& b     = pageElm[blockSelect];
    bool   child = b.isChild && b.parentId[0] != '\0';
    int    dest  = blockSelect + dir;
    if (dest <= 0) return;
    /* Past the end only for a last child moving out of its toggle */
    if (dest >= pageElmCount && !(dir > 0 && child)) return;

    SyncActiveBlock();

    if (dir < 0) {
        Block& d = pageElm[dest];
        /* Up past its own toggle: out of it, to the toggle's level */
        if (child && strcmp(d.id, b.parentId) == 0) {
            AnimateUnitMove(blockSelect, blockSelect + 1, dest, dest + 1, dir);
            swap(pageElm[blockSelect], pageElm[dest]);
            pageElm[blockSelect].pos = blockSelect;
            pageElm[dest].pos        = dest;
            pageElm[blockSelect].kidsLeft = true;
            const Block& t = pageElm[blockSelect];   /* the toggle, now below */
            Block& moved   = pageElm[dest];
            moved.isChild  = t.isChild;
            moved.depth    = t.depth;
            setId(moved.parentId, t.parentId);
            moved.leftParent = true;
            moved.invalidateTE();
            moved.recreate = true;   /* re-created at the new level on save */
            moved.dirty       = true;
            blockSelect = dest;
            MarkDirty();
            commitUIUpdate();
            return;
        }
        /* Up into a toggle right above — an open one with nothing in it,
           or one whose last child is right above: its last child, in place */
        if (isToggleType(d.type) && d.open && d.id[0] != '\0') {
            AnimateIndent(blockSelect, d.depth + 1, dir);
            beginIndentOperation(blockSelect, d.id, d.depth + 1);
            commitUIUpdate();
            return;
        }
        if (d.isChild && d.depth > b.depth && d.parentId[0] != '\0') {
            char pid[37]; setId(pid, d.parentId);
            AnimateIndent(blockSelect, d.depth, dir);
            beginIndentOperation(blockSelect, pid, d.depth);
            commitUIUpdate();
            return;
        }
    } else {
        /* Down past its toggle's last child: out of it, to the toggle's
           level, in place (it already sits right after the subtree) */
        bool destIsSibling = dest < pageElmCount && pageElm[dest].isChild &&
                             strcmp(pageElm[dest].parentId, b.parentId) == 0;
        if (child && !destIsSibling) {
            int parentIdx = -1;
            for (int i = blockSelect - 1; i >= 0; i--)
                if (strcmp(pageElm[i].id, b.parentId) == 0) { parentIdx = i; break; }
            AnimateIndent(blockSelect, parentIdx >= 0 ? pageElm[parentIdx].depth : 0, dir);
            if (parentIdx >= 0) {
                pageElm[parentIdx].kidsLeft = true;
                b.isChild = pageElm[parentIdx].isChild;
                b.depth   = pageElm[parentIdx].depth;
                setId(b.parentId, pageElm[parentIdx].parentId);
            } else {
                b.isChild = false; b.depth = 0; b.parentId[0] = '\0';
            }
            b.leftParent = true;
            b.invalidateTE();
            b.recreate = true;   /* re-created at the new level on save */
            b.dirty       = true;
            MarkDirty();
            commitUIUpdate();
            return;
        }
        /* Down into an open toggle right below: its first child */
        const Block& d = pageElm[dest];
        if (isToggleType(d.type) && d.open && d.id[0] != '\0') {
            char togId[37]; setId(togId, d.id);
            int  togDepth = d.depth;
            AnimateUnitMove(blockSelect, blockSelect + 1, dest, dest + 1, dir);
            swap(pageElm[blockSelect], pageElm[dest]);
            pageElm[blockSelect].pos = blockSelect;
            pageElm[dest].pos        = dest;
            blockSelect = dest;
            beginIndentOperation(blockSelect, togId, togDepth + 1);
            commitUIUpdate();
            return;
        }
    }

    /* Past a sibling at its own level — a plain block, a closed toggle,
       a split block or a toggle with all its children */
    MoveUnit(blockSelect, dir, AnimateUnitMove);
}

/* ------------------------------------------------------------------ */
/* ClearSidebar — erase sidebar panel and reset hit rects             */
/* ------------------------------------------------------------------ */
static void ClearSidebar() {
    SetRect(&gAddBlockRect,      0,0,0,0);
    SetRect(&gMoveUpArrowRect,   0,0,0,0);
    SetRect(&gMoveDownArrowRect, 0,0,0,0);
    Rect r; SetRect(&r, 0, 0, gColLeft-1, gWindow->portRect.bottom - kBottomMargin);
    EraseRect(&r);
}

/* ------------------------------------------------------------------ */
/* drawSidebarControls — repaint + ▴ ▾ for hovered block             */
/* Controls sit to the left of the text column, aligned to block top  */
/* ------------------------------------------------------------------ */
void drawSidebarControls() {
    if (!gArrowsVisible || gWindow == nil || (pageElmCount == 0)) return;
    SetPort(gWindow);

    ClearSidebar();

    /* Right-aligned: ▲ ▼ + (matches library order) */
    const short plusX = gColLeft - 14;
    const short downX = plusX    - 15;
    const short upX   = downX   - 15;

    int idx = gHoverBlockIdx;
    if (idx <= 0 || idx >= pageElmCount) return;

    Block& b = pageElm[idx];
    if (b.type == BT_DIVIDER || b.type == BT_CHILD_PAGE) return;

    short cy = (b.textEdit != nil ? b.vrect.top : b.rect.top) + 6;

    /* Which control (if any) was last clicked — black; others dim to ltGray */
    bool plusBlack = (gSidebarPending == kSPPlus);
    bool upBlack   = (gSidebarPending == kSPUp);
    bool downBlack = (gSidebarPending == kSPDown);
    bool anyPending = (gSidebarPending != kSPNone);
    const Pattern& idlePat = anyPending ? qd.ltGray : qd.gray;

    /* ▴ up arrow */
    if (idx > 1) {
        const Pattern* upPat = upBlack ? &qd.black : &idlePat;
        DrawArrowTriangle(upX, cy - 3, true, 6, upPat);
        SetRect(&gMoveUpArrowRect, upX - 6, cy - 5, upX + 6, cy + 5);
    }

    /* ▾ down arrow — also shown for the last element when it's a toggle child
       (can still move down to leave the toggle). */
    if (idx < pageElmCount - 1 ||
        (b.isChild && b.parentId[0] != '\0')) {
        const Pattern* downPat = downBlack ? &qd.black : &idlePat;
        DrawArrowTriangle(downX, cy - 3, false, 6, downPat);
        SetRect(&gMoveDownArrowRect, downX - 6, cy - 5, downX + 6, cy + 5);
    }

    /* + button */
    DrawPlusCross(plusX, cy, plusBlack ? &qd.black : &idlePat);
    SetRect(&gAddBlockRect, plusX - 6, cy - 6, plusX + 6, cy + 6);
    InvalidateSaveButton();
}

/* ------------------------------------------------------------------ */
/* updateArrowVisibility — show/hide sidebar arrows based on mouse    */
/* ------------------------------------------------------------------ */
void updateArrowVisibility() {
    if (gWindow == nil || gCurRequest < kPhaseFullyLoaded) return;
    long now = TickCount();
    if (now - gLastMouseCheckTick < 3) return;  /* ~50ms throttle */
    gLastMouseCheckTick = now;

    /* After a move, keep controls locked to the destination block */
    if (sHoverLockUntil && now < sHoverLockUntil) return;
    sHoverLockUntil = 0;

    SetPort(gWindow);
    Point pt;
    GetMouse(&pt);
    bool inSidebar = (pt.h >= 0 && pt.h < gColLeft - 1 &&
                      pt.v >= 0 && pt.v < gWindow->portRect.bottom - kBottomMargin);

    if (inSidebar) {
        /* §8: binary search for block containing mouse Y — O(log N) vs O(N) */
        int hoverIdx = -1;
        int lo = 1, hi = pageElmCount - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            if      (pageElm[mid].rect.bottom <= pt.v) lo = mid + 1;
            else if (pageElm[mid].rect.top    >  pt.v) hi = mid - 1;
            else { hoverIdx = mid; break; }
        }
        /* If between blocks (binary search missed), snap to nearest above */
        if (hoverIdx == -1 && lo > 1) {
            for (int i = lo - 1; i >= 1; i--) {
                if (pageElm[i].type == BT_DIVIDER || pageElm[i].type == BT_CHILD_PAGE) continue;
                if (pt.v >= pageElm[i].rect.top) { hoverIdx = i; break; }
            }
        }
        if (hoverIdx != -1 && (pageElm[hoverIdx].type == BT_DIVIDER ||
                                pageElm[hoverIdx].type == BT_CHILD_PAGE)) hoverIdx = -1;
        if (hoverIdx != -1) {
            /* Clear pending on different block hover or 1 s timeout */
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
            ClearSidebar();
        }
    } else if (gArrowsVisible) {
        gSidebarPending      = kSPNone;
        gSidebarPendingBlock = -1;
        gArrowsVisible = false;
        gHoverBlockIdx = -1;
        ClearSidebar();
    }

    /* ---- Cursor management ---- */
    /* In content area: iBeam for text, hand cursor only when hovering over
       a URL run (pixel-accurate via TEGetPoint) or a child-page block.
       Busy/watch cursor is handled by the event loop.                      */
    {
        bool inContent = (pt.h >= gColLeft &&
                          pt.h < gWindow->portRect.right - kScrollBarWidth &&
                          pt.v >= 0 &&
                          pt.v < gWindow->portRect.bottom - kBottomMargin);
        if (inContent) {
            bool overLink    = false;
            bool overTextEdit = false;
            for (int i = 0; i < pageElmCount; i++) {
                const Block& b = pageElm[i];
                if (pt.v < b.rect.top || pt.v >= b.rect.bottom) continue;
                if (b.type == BT_CHILD_PAGE) {
                    short rowTop  = b.rect.top + 3;
                    short rowBot  = b.rect.top + 25;
                    short hitRight = (b.titleRight > 0) ? b.titleRight + 4 : b.rect.right;
                    if (pt.h >= gColLeft + 6 && pt.h <= hitRight &&
                        pt.v >= rowTop && pt.v <= rowBot)
                        overLink = true;
                } else if (b.textEdit) {
                    if (pt.h >= b.vrect.left)
                        overTextEdit = true;
                    /* Use TEGetPoint to find if mouse is over a URL run */
                    TEHandle te = b.textEdit;
                    short lineH = (*te)->lineHeight;
                    if (lineH < 8) lineH = 12;
                    short teOff = 0;
                    for (int _ri = 0; _ri < b.runCount; _ri++) {
                        const TextRun& r = b.runs[_ri];
                        int rLen = TELenForText(b.text, r.textOffset, r.textLen);
                        if (r.url[0] && rLen > 0) {
                            Point ps = TEGetPoint(teOff, te);
                            Point pe = TEGetPoint(teOff + rLen, te);
                            if (ps.v == pe.v) {
                                if (pt.h >= ps.h && pt.h <= pe.h &&
                                    pt.v >= ps.v - lineH && pt.v <= ps.v + 2)
                                    overLink = true;
                            } else {
                                if (pt.v >= ps.v - lineH && pt.v <= pe.v + 2)
                                    overLink = true;
                            }
                        }
                        if (overLink) break;
                        teOff += rLen;
                    }
                }
                break;
            }
            if (overLink) {
                CursHandle hand = GetCursor(128);
                if (hand) SetCursor(*hand); else InitCursor();
            } else if (overTextEdit) {
                CursHandle ih = GetCursor(iBeamCursor);
                if (ih) SetCursor(*ih);
            } else {
                InitCursor(); /* arrow over non-editable block or gap */
            }
        } else {
            InitCursor(); /* arrow outside content area */
        }
    }
}

/* ------------------------------------------------------------------ */
/* DrawPageIcon — draw (or erase) just the page icon area             */
/* Safe to call independently of updateBlocks() to update the icon   */
/* without a full block re-render.                                    */
/* ------------------------------------------------------------------ */
static void DrawPageIcon() {
    if (!gWindow) return;
    SetPort(gWindow);
    short contentW  = (gWindow->portRect.right - kScrollBarWidth) - kSidebarWidth;
    short colWidth  = contentW < 640 ? contentW : 640;
    short centerOff = (contentW - colWidth) / 2;
    short colLeft   = kSidebarWidth + centerOff;
    short ix = colLeft + 2;
    short iy = 48 - gScrollOffset;
    /* Erase previous icon area before redrawing */
    Rect iconArea; SetRect(&iconArea, ix, iy, ix + 40, iy + 40);
    EraseRect(&iconArea);
    SetRect(&gIconRect, 0, 0, 0, 0);
    if (gPageIconName[0] != '\0') {
        DrawNotionIcon(gPageIconName, ix, iy);
        SetRect(&gIconRect, ix, iy, ix + 40, iy + 40);
    }
}

/* ------------------------------------------------------------------ */
/* updateBlocks — full layout + redraw of all visible blocks           */
/* ------------------------------------------------------------------ */
/* While > 0, updateBlocks is skipped: a run of loads (PrepareSubtree
   opening nested toggles) whose caller relays out once at the end. */
int gRelayoutHold = 0;

void updateBlocks() {
    if (gRelayoutHold > 0) return;
    SetPort(gWindow);

    /* Content area rect — used for clip and (when not scrolling) erase */
    Rect contentRect = gWindow->portRect;
    contentRect.left   = kSidebarWidth;
    contentRect.right -= kScrollBarWidth;

    /* During scrolling, ScrollRect already shifted pixels — only the newly-
       exposed strip needs painting; skip the full erase to avoid flicker.
       Otherwise erase everything so stale pixels don't show through.
       Exception: when gSkipHeaderErase is set (HTTP data arriving after a
       cached-header placeholder was shown), preserve the already-rendered
       title+icon area to avoid a white-flash of content that hasn't changed. */
    bool headerStable = gSkipHeaderErase &&
                        !g_titleIsPlaceholder &&
                        pageElmCount > 0;
    gSkipHeaderErase = false; /* one-shot: consumed here */

    if (!gScrolling) {
        /* Exposed-region repaint (picker close, window uncover): clear the
           whole port so the scrollbar column and bottom margins — which the
           targeted erases below never cover — don't keep ghost pixels.
           Controls are redrawn at the end of this function. */
        if (gFullEraseOnce) {
            EraseRect(&gWindow->portRect);
            gFullEraseOnce = false;
            /* The erase wiped the Save button border too; clear its label cache
               so the updateSaveButton() at the end redraws the border instead of
               skipping it (the idempotency guard would otherwise leave only the
               progress bar visible on an already-dirty page). */
            gAppState.lastSaveLabelIdx = -1;
        }
        Rect sideRect = gWindow->portRect;
        sideRect.right  = kSidebarWidth;
        sideRect.bottom = gWindow->portRect.bottom - kBottomMargin;
        EraseRect(&sideRect);
        if (headerStable && pageElm[0].rect.bottom > contentRect.top) {
            /* Only erase the area below the title so the header stays stable.
               Guard: if rect.bottom is 0 (fresh Block before first layout),
               fall through to the full erase so stale status-text pixels clear. */
            Rect belowTitle = contentRect;
            belowTitle.top = pageElm[0].rect.bottom;
            EraseRect(&belowTitle);
        } else {
            EraseRect(&contentRect);
        }
    }

    /* Clip text drawing to content area; during scrolling restrict further to
       the newly-exposed strip so already-visible pixels aren't repainted.    */
    if (gScrollClipRgn)
        SetClip(gScrollClipRgn);
    else
        ClipRect(&contentRect);


    static RgnHandle clipRgn = NewRgn();
    GetClip(clipRgn);

    /* Centre a max-640px column in the content area — shared by icon and all blocks */
    short contentW  = (gWindow->portRect.right - kScrollBarWidth) - kSidebarWidth;
    short colWidth  = contentW < 640 ? contentW : 640;
    short centerOff = (contentW - colWidth) / 2;
    short colLeft   = kSidebarWidth + centerOff;
    short colRight  = colLeft + colWidth;
    gColLeft        = colLeft;
    /* Loop-invariant: window width is fixed for the whole pass. */
    short rightPad  = (gWindow->portRect.right - gWindow->portRect.left) * 5 / 100;

    /* Draw the page icon — delegates to DrawPageIcon() for DRY icon logic */
    DrawPageIcon();

    SInt16 fontNum   = gFontHelvetica;
    int top;
    int listCounter = 0;  /* counter for consecutive numbered_list_item blocks */
    /* §1: teScratch string removed — TextToTE now uses static char buffer */

    for (int _pi = 0; _pi < pageElmCount; _pi++) { Block* p = &pageElm[_pi];
        Rect r, rect;

        if (p->pos > 0) {
            top = pageElm[(p->pos - 1)].rect.bottom;
        } else {
            top = 48 - gScrollOffset;   /* apply scroll offset at the origin */
            if (gPageIconName[0] != '\0') top += 64; /* 40px icon + 24px gap */
        }

        int blockTop = top; /* save original top before any type-specific adjustment */

        /* ---- Child page link: page icon + bold title, click to navigate ---- */
        if (p->type == BT_CHILD_PAGE) {
            if (p->textEdit != nil) { TEDispose(p->textEdit); p->textEdit = nil; }
            short rowH   = 20;
            short rowTop = top + 4;
            short rowBot = rowTop + rowH;
            SetRect(&p->rect,  colLeft, top, colRight, rowBot + 6);
            p->vrect = p->rect;
            bool sel = (_pi == blockSelect);
            /* Measure title first so bgR fits the content */
            TextFont(gFontGeneva); TextFace(1); TextSize(12);
            FontInfo cfi; GetFontInfo(&cfi);
            short tx = colLeft + 24;  /* icon at colLeft+6, 16px wide → end at +22, +2px gap */
            const char* title = p->text;
            int   titleLen = (int)p->textLen;
            short maxW = (colRight - 6) - tx;
            while (titleLen > 1 && TextWidth(title, 0, titleLen) > maxW) titleLen--;
            p->titleRight = tx + TextWidth(title, 0, titleLen);
            /* Selection box fits icon + title only */
            Rect bgR; SetRect(&bgR, colLeft + 6, rowTop - 1, p->titleRight + 4, rowBot + 1);
            EraseRect(&bgR);
            if (sel) { FrameRect(&bgR); }
            /* Page icon: native Notion slug, emoji sprite, or fallback document */
            if (p->iconName[0]) {
                DrawListPageIcon(p->iconName, colLeft + 4, rowTop);
            } else if (p->iconSprX >= 0 && gEmojiGWorld) {
                Rect srcR; SetRect(&srcR, p->iconSprX, p->iconSprY,
                                   p->iconSprX + 16, p->iconSprY + 16);
                Rect dstR; SetRect(&dstR, colLeft + 4, rowTop,
                                   colLeft + 20, rowTop + 16);
                PixMapHandle pm = GetGWorldPixMap(gEmojiGWorld);
                LockPixels(pm);
                PixMapHandle dstPM = nil;
                BitMap* dstBM = (BitMap*)&gWindow->portBits;
                if (((CGrafPtr)gWindow)->portVersion & (short)0xC000) {
                    dstPM = ((CGrafPtr)gWindow)->portPixMap;
                    LockPixels(dstPM);
                    dstBM = (BitMap*)(*dstPM);
                }
                CopyBits((BitMap*)(*pm), dstBM, &srcR, &dstR, srcCopy, nil);
                if (dstPM) UnlockPixels(dstPM);
                UnlockPixels(pm);
            } else {
                DrawPageIcon(colLeft + 4, rowTop);
            }
            /* Draw title */
            short ty = rowTop + cfi.ascent + 1;
            MoveTo(tx, ty); DrawText(title, 0, titleLen);
            TextFace(0);
            /* Clear area to the right of the title box and below the row */
            Rect rightR; SetRect(&rightR, p->titleRight + 5, rowTop - 1, colRight, rowBot + 1);
            EraseRect(&rightR);
            Rect gapR; SetRect(&gapR, colLeft, rowBot + 1, colRight, p->rect.bottom);
            EraseRect(&gapR);
            continue;
        }

        /* ---- Divider: draw a horizontal rule, no TE ---- */
        if (p->type == BT_DIVIDER) {
            if (p->textEdit != nil) { TEDispose(p->textEdit); p->textEdit = nil; }
            short divTop = top + 4;
            short divBot = divTop + 11;
            short divEff = (p->isChild && p->depth > 0) ? p->depth - 1 : p->depth;
            short divOff = (p->isChild && p->depth > 0) ? (short)(18 + divEff * 20) : 0;
            SetRect(&p->rect, colLeft, divTop - 4, colRight, divBot + 4 + 10); /* layout: 4px above + 11px + 4px below + 10px gap */
            p->vrect = p->rect;
            short midY = (divTop + divBot) / 2;
            static const Pattern kDivider = {{0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88}}; /* 25% gray, pixel at col 0+4 every row */
            /* Selection highlight — 2px padding each side */
            bool divSel = (_pi == blockSelect);
            { Rect selR; SetRect(&selR, colLeft + divOff, divTop - 2, colRight - rightPad, divBot + 2);
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
                MoveTo(colLeft + divOff + 2, midY);
                LineTo(colRight - rightPad, midY);
            } else {
                PenPat(&kDivider);
                MoveTo(colLeft + divOff + 2, midY);
                LineTo(colRight - rightPad, midY);
                PenPat(&qd.black);
            }
            { Rect gapR; SetRect(&gapR, colLeft + divOff, divBot + 2, colRight - rightPad, p->rect.bottom);
              EraseRect(&gapR); }
            continue;
        }

        /* effectiveDepth: child text/decorations align with parent's text edge (depth-1).
           toggleDepth: toggle triangles and offsets use actual depth so nested
           toggle headers are visually indented relative to their siblings. */
        int effectiveDepth = (p->isChild && p->depth > 0) ? p->depth - 1 : p->depth;
        int toggleDepth    = p->depth;

        const BlockDesc& desc = blockDesc(p->type);

        /* Font + face */
        TextFace(desc.bold ? 1 : 0);
        TextSize(desc.fontSize);

        /* Top margin */
        top += desc.topMargin;

        /* Left indent: base + depth scaling.
           useActualDepth=true  (lists, toggles): scale by full depth so decorations
                                                  stay visually indented per level.
           useActualDepth=false (paragraph, headings, code, quote, callout, …):
                                at page level use leftBase only (no scaling);
                                as a child, add childIndent (18 + effectiveDepth*20)
                                so the block's text aligns with the parent toggle's
                                text edge, plus 20px per additional nesting level. */
        int depthUnits  = desc.useActualDepth ? toggleDepth : effectiveDepth;
        int childIndent = (!desc.useActualDepth && p->isChild && p->depth > 0)
                        ? (18 + effectiveDepth * 20) : 0;
        int offset = desc.useActualDepth
                   ? desc.leftBase + toggleDepth * 20
                   : desc.leftBase + childIndent;
        const short depthPx = (short)(depthUnits * 20);  /* per-block, reused below */

        /* List counter */
        if      (desc.counter == CTR_RESET)     listCounter = 0;
        else if (desc.counter == CTR_INCREMENT && !p->isCont) listCounter++;

        /* Left decorator — a continuation piece only keeps the quote bar */
        switch ((p->isCont && desc.decor != DECOR_QUOTEBAR) ? DECOR_NONE : desc.decor) {
            case DECOR_BULLET:
                SetRect(&r, colLeft + 9 + depthPx, top + 6,
                            colLeft + 13 + depthPx, top + 10);
                FrameOval(&r);
                FillOval(&r, &qd.black);
                break;
            case DECOR_NUMBER: {
                char numStr[8];
                snprintf(numStr, sizeof(numStr), "%d.", listCounter);
                TextFont(gFontGeneva); TextSize(12); TextFace(0);
                MoveTo(colLeft + 6 + depthPx, top + 12);
                DrawText(numStr, 0, (short)strlen(numStr));
                break;
            }
            case DECOR_CHECKBOX: {
                short cbX = colLeft + 4 + depthPx;
                SetRect(&r, cbX, top + 2, cbX + 12, top + 14);
                if (p->check) {
                    DrawCheckedBox(cbX - 2, top);
                } else {
                    FrameRect(&r);
                }
                break;
            }
            case DECOR_TRIANGLE:
                DrawDisclosureTriangle(colLeft + 6 + depthPx + 4,
                                       top + 7, p->open, &qd.black);
                break;
            case DECOR_QUOTEBAR:
                SetRect(&r, colLeft + childIndent + 3, top - 1, colLeft + childIndent + 5, top + 15);
                FillRect(&r, &qd.black);
                break;
            case DECOR_NONE:
                break;
        }

        /* Create TE with generous height so all text wraps correctly */
        short teRight  = colRight - rightPad - desc.teRightInset;
        short teLeft   = colLeft + 2 + offset;
        SetRect(&rect, teLeft, top + desc.teTopInset, teRight, top + 4000);
        TextFont(desc.useHelvetica ? fontNum : gFontGeneva);
        bool teNew = p->addTE(rect, rect, p->type);
        if (p->textEdit == nil) {
            /* TEStyleNew failed (out of heap) — give block a minimal rect and skip drawing */
            p->rect.bottom = top + 20;
            p->vrect = p->rect;
            continue;
        }
        /* lineHeight adjustment is persistent inside the TE struct — only apply
           once when the TE is freshly created, not on subsequent scroll updates. */
        if (teNew && isLineHeightType(p->type))
            (*p->textEdit)->lineHeight += 2;
        /* Compute teText once — reused for TESetText, cache update, and DrawBlockEmojis.
           §1: TextToTE writes into static char buffer; returns via teTextBuf/teTextLen */
        static char teTextBuf[kTEBufLen]; short teTextLen = 0;
        TextToTE(p->text, p->textLen, teTextBuf, sizeof(teTextBuf), teTextLen);
        {
          /* Skip TESetText when text is unchanged — avoids full re-layout.
             Compared against the TE's own text (no shadow copy per block;
             memcmp can't move the handle).  Also skip for the active TE when
             not freshly created: TEKey already updated its content; calling
             TESetText would reset the cursor and cause a visible flash on the
             first keystroke or last-char delete. */
          bool isActive = (p->textEdit != nil && p->textEdit == textActive);
          bool teChanged = (teTextLen != (*p->textEdit)->teLength ||
                            (teTextLen > 0 &&
                             memcmp(teTextBuf, *TEGetText(p->textEdit), teTextLen) != 0));
          if (teNew || (teChanged && !isActive)) {
              TESetText(teTextBuf, teTextLen, p->textEdit);
              bool styled = ApplyRunStyles(p->textEdit, *p);
              if (ShrinkEmojiPads(p->textEdit, teTextBuf, teTextLen, p->type)) styled = true;
              if (styled) TECalText(p->textEdit);
              /* Bold-default blocks (toggles, headings) with no text: set the TE's
                 null/typing style to bold so the first keystroke comes in bold.
                 TESetText("", 0) may clear the style that TEStyleNew inherited
                 from the port face. Only needed for fresh empty blocks. */
              if (teNew && desc.bold && teTextLen == 0) {
                  TextStyle ts; ts.tsFont = 0; ts.tsSize = 0; ts.tsFace = bold;
                  TESetStyle(doFace, &ts, false, p->textEdit);
              }
              p->invalidateEmojiPos();
          }

        }

        /* Use actual TE line layout for height instead of character-count estimate.
           Clamp to 1 so an empty block has the same height as a single-line block —
           this prevents nLines 0↔1 transitions from triggering full-page reflows. */
        short nLines     = (*p->textEdit)->nLines;
        if (nLines < 1) nLines = 1;
        short textHeight = TEGetHeight(nLines, 0, p->textEdit);
        int   padding    = (8 * desc.lineHeightPct) / 100;
        p->rect.bottom = top + textHeight + padding;
        /* The lineHeight+2 types carry an extra 2px below the last line's
           descenders — trim it from the bottom border so it stays tight. */
        if (isLineHeightType(p->type))
            p->rect.bottom -= 2;
        p->vrect                    = p->rect;

        (*p->textEdit)->viewRect    = p->vrect;
        (*p->textEdit)->destRect    = p->vrect;

        /* ---- Callout: simple 4px gray border ---- */
        if (desc.hasBox && !desc.boxFill) {
            p->rect.top    = blockTop;
            p->rect.bottom = p->vrect.bottom + 24;  /* box bottom (8) + 16px gap */

            static const Pattern kBorder = {{0x88,0x00,0x22,0x00,0x88,0x00,0x22,0x00}}; /* 25% gray */
            Rect boxR; SetRect(&boxR, colLeft + childIndent, blockTop + 4, colRight - rightPad, p->vrect.bottom + 8);
            FillRect(&boxR, &kBorder);
            InsetRect(&boxR, 4, 4);
            FillRect(&boxR, &qd.white);
            BackPat(&qd.white);
            { Rect gapR; SetRect(&gapR, colLeft + childIndent, p->vrect.bottom + 8, colRight - rightPad, p->rect.bottom);
              EraseRect(&gapR); }
        }

        /* ---- Code: 12.5% gray background + 1px border ---- */
        if (desc.hasBox && desc.boxFill) {
            short boxBottom = p->vrect.bottom + 4;  /* 4px bottom padding inside box */
            p->rect.top    = blockTop;
            p->rect.bottom = boxBottom + 16;  /* 16px gap below box */

            static const Pattern kCodeBg = {{0x80,0x00,0x00,0x00,0x08,0x00,0x00,0x00}}; /* 12.5% gray */
            Rect boxR; SetRect(&boxR, colLeft + childIndent, blockTop + 4, colRight - rightPad, boxBottom);
            FillRect(&boxR, &kCodeBg);
            PenPat(&qd.gray);
            PenSize(1, 1);
            FrameRect(&boxR);
            PenPat(&qd.black);
            { Rect innerR = boxR; InsetRect(&innerR, 1, 1); EraseRect(&innerR); }
            { Rect gapR; SetRect(&gapR, colLeft + childIndent, boxBottom, colRight - rightPad, p->rect.bottom);
              EraseRect(&gapR); }  /* clear 20px gap */
            /* Update vrect to match box interior — override inset top to match TE (top+4) */
            p->vrect = boxR;
            InsetRect(&p->vrect, 4, 4);
            p->vrect.top = blockTop + 8;
            (*p->textEdit)->viewRect = p->vrect;
            (*p->textEdit)->destRect = p->vrect;
        }

        /* §2: Skip TEUpdate and emoji drawing for blocks outside the current
           clip region — zero cost for off-screen blocks; caller restricts clip
           to a single block's vrect for single-block keypress redraws. */
        { Rect portR = gWindow->portRect;
          if (RectInRgn(&p->vrect, clipRgn)) {
              TEUpdate(&p->vrect, p->textEdit);
              /* Gray placeholder for empty or default "New Page" title.
                 Draw regardless of textActive — syncTextActive() sets textActive=title
                 after load, which would otherwise permanently suppress the gray. */
              if (p->type == BT_TITLE) {
                  bool isEmpty   = (p->textLen == 0);
                  bool isDefault = (!isEmpty && strcmp(p->text, "New Page") == 0);
                  if (isEmpty || isDefault) {
                      EraseRect(&p->vrect);   /* erase black TE text (also clears cursor) */
                      TextFont(fontNum); TextSize(26); TextFace(1);
                      FontInfo fi; GetFontInfo(&fi);
                      MoveTo(p->vrect.left, p->vrect.top + fi.ascent);
                      TextMode(grayishTextOr);   /* stippled gray text */
                      DrawString("\pNew page");
                      TextMode(srcOr);
                  }
              }
              DrawBlockEmojis(p->textEdit, teTextBuf, teTextLen, *p);
              DrawStrikethrough(p->textEdit, *p);
              /* Dim checked to-do text: draw white lines on every other row */
              if (p->type == BT_TODO && p->check) {
                  PenPat(&qd.white);
                  PenSize(1, 1);
                  for (int gy = p->vrect.top; gy < p->vrect.bottom; gy += 2) {
                      MoveTo(p->vrect.left, gy);
                      LineTo(p->vrect.right, gy);
                  }
                  PenPat(&qd.black);
              }
          }

          /* Loading indicator: shown while the first child-fetch for this toggle
             is in flight.  Reserves layout space and draws "Loading…" in the
             child-indent position; disappears when afterToggleLoad() calls
             updateBlocks() with gAppState.togglePending already cleared.              */
          bool isLoadingToggle = gAppState.togglePending && (gToggleSerial == p->serial) &&
              isToggleType(p->type);
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

            bool isLastChild = (_pi + 1 >= pageElmCount)
                            || (!pageElm[_pi+1].isChild)
                            || (pageElm[_pi+1].depth < p->depth);
            if (isLastChild) p->rect.bottom += 6;
        }
    }

    /* Track unscrolled total height from last block + 20% bottom padding */
    if (!(pageElmCount == 0)) {
        short winH = gWindow->portRect.bottom - gWindow->portRect.top;
        gTotalContentHeight = pageElm[pageElmCount-1].rect.bottom + gScrollOffset
                              + winH / 5;
    }


    /* Show "Loading…" below the title while blocks are still in flight */
    if (gCurRequest == kPhaseTitleDone && pageElmCount == 1) {
        short ly = pageElm[0].rect.bottom + 8;
        Rect portR = gWindow->portRect;
        if (ly > portR.top && ly + 16 < portR.bottom - kBottomMargin) {
            TextFont(gFontGeneva); TextFace(0); TextSize(12);
            MoveTo(colLeft + 2, ly + 10);
            DrawString("\pLoading\xC9");
        }
    }

    /* Restore clip, update scroll bar */
    ClipRect(&gWindow->portRect);

    drawSidebarControls();
    updateSaveButton();  /* always redraw — content erase may overlap button in wide/full-screen layouts */

    updateScrollRange();
    if (gScrollBar != nil) DrawControls(gWindow);
}

/* ------------------------------------------------------------------ */
/* DrawStatusText — erase window and draw centred status message       */
/* ------------------------------------------------------------------ */
void DrawStatusText(const char *msg)
{
    if (gWindow == nil) return;
    SetPort(gWindow);
    EraseRect(&gWindow->portRect);
    DrawCenteredText(gWindow, gWindow->portRect, msg);
}

/* ------------------------------------------------------------------ */
/* DrawStatusBelowHeader — status line under the placeholder title     */
/* without erasing the header (DrawStatusText wipes the whole window). */
/* Mirrors the "Loading more…" style used by updateBlocks.             */
/* ------------------------------------------------------------------ */
void DrawStatusBelowHeader(const char *msg)
{
    if (gWindow == nil) return;
    if (pageElmCount < 1 || pageElm[0].rect.bottom <= 0) {
        DrawStatusText(msg);  /* no header on screen — fall back */
        return;
    }
    SetPort(gWindow);
    Rect r = gWindow->portRect;
    r.left   = kSidebarWidth;
    r.right -= kScrollBarWidth;
    r.top    = pageElm[0].rect.bottom + 8;
    r.bottom = r.top + 20;
    if (r.bottom >= gWindow->portRect.bottom - kBottomMargin) {
        /* A tall multi-line title (small screen) pushes the line off the
           window — don't silently drop the only loading feedback; erase
           the whole window and show it centred instead. */
        DrawStatusText(msg);
        return;
    }
    EraseRect(&r);
    TextFont(gFontGeneva); TextFace(0); TextSize(12);
    MoveTo(gColLeft + 2, r.top + 14);
    DrawText(msg, 0, (short)strlen(msg));
}

/* ------------------------------------------------------------------ */
/* EnsureControls — create scroll bar + save button if they don't     */
/* exist yet.  Called from paths that skip MakeNewWindow (e.g. when   */
/* the cached preview header already matches the HTTP title).         */
/* ------------------------------------------------------------------ */
void EnsureControls() {
    if (gWindow == nil) return;
    gScrollOffset       = 0;
    gTotalContentHeight = 0;
    if (gScrollBar == nil) {
        Rect sbRect;
        SetRect(&sbRect,
                gWindow->portRect.right - kScrollBarWidth,
                gWindow->portRect.top    - 1,
                gWindow->portRect.right  + 1,
                gWindow->portRect.bottom - 14);
        gScrollBar = MakeScrollBar(gWindow, sbRect);
    }
    if (gSaveButton == nil) {
        Rect btnRect;
        SetRect(&btnRect, 3, gWindow->portRect.bottom - 22,
                kSidebarWidth - 4, gWindow->portRect.bottom - 6);
        gSaveButton = NewControl(gWindow, &btnRect, (ConstStr255Param)"\pSave...",
                                 false, 0, 0, 1, 0, 0L);
    }
}

/* ------------------------------------------------------------------ */
/* MakeNewWindow — create or reuse the main content window            */
/* ------------------------------------------------------------------ */
void MakeNewWindow(short procID) {
    if (gWindow == nil) {
        SetRect(&gInitialWindowRect, 6, 42, 505, 375);
        gWindow = NewWindow(NULL, &gInitialWindowRect, "\pNotion classic",
                            true, procID, (WindowPtr)-1, true, 0);
    }
    /* Ensure the window is visible — it may have been hidden while the
       settings panel was shown (first-launch or HTTP error path). */
    ShowWindow(gWindow);

    /* Update title to page name. */
    setWindowTitleChrome(pageElm[0].text);

    /* Create scroll bar + save button (shared with EnsureControls) */
    EnsureControls();

    updateBlocks();
    /* Mark the window contents as valid so the OS doesn't queue a redundant
       updateEvt that would cause a second full redraw/blink on page load. */
    ValidRect(&gWindow->portRect);
    syncTextActive();
}

/* Full Save-button box. Shared by drawProgressBar + updateSaveButton so the
   button geometry lives in one place. btnTop = bottom-23, btnBottom = -7. */
static inline Rect SaveButtonBox() {
    const short btnWidth = kSidebarWidth - 8;
    const short btnLeft  = (gColLeft - btnWidth) / 2 + 1;
    Rect r;
    SetRect(&r, btnLeft, gWindow->portRect.bottom - 23,
                btnLeft + btnWidth, gWindow->portRect.bottom - 7);
    return r;
}

/* ------------------------------------------------------------------ */
/* drawProgressBar — 2 px bar at the bottom edge of the Save button   */
/* filled = pixels to paint black (0..barInnerWidth); rest is ltGray  */
/* ------------------------------------------------------------------ */
static void drawProgressBar(short filled) {
    if (gSaveButton == nil || gWindow == nil) return;
    if (filled == gAppState.lastBarFilled) return;
    gAppState.lastBarFilled = filled;

    Rect box = SaveButtonBox();
    Rect barRect;
    SetRect(&barRect, box.left + 1, box.bottom - 2, box.right - 1, box.bottom);
    DrawProgressBar(gWindow, barRect, filled);
}

/* §3: extracted from 3 identical label/hide/bar patterns in updateSaveButton */
static void HideSaveButtonAs(int label) {
    gAppState.lastSaveLabelIdx = label;
    HideControl(gSaveButton);
    gAppState.lastBarFilled = -1;
    drawProgressBar(0);
}

/* §3: resets throttle for immediate redraw; called from drawSidebarControls
   and wherever the save area might be stale after a sidebar paint.    */
void InvalidateSaveButton() {
    gAppState.lastSaveLabelIdx   = -1;
    gAppState.lastBarFilled      = -1;
    gAppState.lastSaveButtonTick = 0;
    updateSaveButton();
}

/* ------------------------------------------------------------------ */
/* scrollActionProc — fires repeatedly while mouse held on arrows      */
/* ------------------------------------------------------------------ */
pascal void scrollActionProc(ControlHandle control, short part)
{
    short visibleHeight = gWindow->portRect.bottom - gWindow->portRect.top;
    short pageAmount    = visibleHeight - kLineScrollAmount;
    short delta = ScrollPartDelta(part, kLineScrollAmount, pageAmount);
    if (delta == 0) return;

    short newVal = GetControlValue(control) + delta;
    short minVal = GetControlMinimum(control);
    short maxVal = GetControlMaximum(control);
    if (newVal < minVal) newVal = minVal;
    if (newVal > maxVal) newVal = maxVal;

    if (newVal == GetControlValue(control)) return;

    short scrollDelta = newVal - GetControlValue(control);

    Rect contentRect = gWindow->portRect;
    contentRect.left   = gColLeft;
    contentRect.right -= kScrollBarWidth;
    static RgnHandle updateRgn = NewRgn();
    ScrollRect(&contentRect, 0, -scrollDelta, updateRgn);
    EraseRgn(updateRgn);

    SetControlValue(control, newVal);
    gScrollOffset = newVal;
    gScrolling      = true;
    gScrollClipRgn  = updateRgn;
    updateBlocks();
    gScrollClipRgn  = nil;
    gScrolling      = false;
    syncTextActive();
    /* Repaint the gray progress bar — ScrollRect can wipe it */
    gAppState.lastBarFilled = -1;
    updateSaveButton();
}

/* ------------------------------------------------------------------ */
/* resizeControls — reposition scroll bar and save button after grow   */
/* ------------------------------------------------------------------ */
void resizeControls() {
    if (gScrollBar != nil) {
        HideControl(gScrollBar);
        MoveControl(gScrollBar,
                    gWindow->portRect.right - kScrollBarWidth,
                    gWindow->portRect.top - 1);
        SizeControl(gScrollBar,
                    kScrollBarWidth + 1,
                    (gWindow->portRect.bottom - 14) - (gWindow->portRect.top - 1));
        ShowControl(gScrollBar);
    }
    if (gSaveButton != nil)
        MoveControl(gSaveButton, 3, gWindow->portRect.bottom - 22);
}

/* ------------------------------------------------------------------ */
/* ApplyScrollDelta — extracted from 2 duplicate scroll blocks         */
/* preserveTE=false: deactivate TE (non-editing keys)                 */
/* preserveTE=true:  keep TE active, reactivate after (Page Up/Down   */
/*                   while editing)                                   */
/* ------------------------------------------------------------------ */
void ApplyScrollDelta(short delta, bool preserveTE) {
    if (!gScrollBar) return;
    short cur = GetControlValue(gScrollBar);
    short nv  = cur + delta;
    short mn  = GetControlMinimum(gScrollBar);
    short mx  = GetControlMaximum(gScrollBar);
    if (nv < mn) nv = mn;
    if (nv > mx) nv = mx;
    if (nv == cur) return;

    if (!preserveTE && textActive) TEDeactivate(textActive);

    Rect cr = gWindow->portRect;
    cr.left = gColLeft; cr.right -= kScrollBarWidth;
    static RgnHandle ur = NewRgn();
    ScrollRect(&cr, 0, -(nv - cur), ur);
    EraseRgn(ur);
    SetControlValue(gScrollBar, nv);
    gScrollOffset = nv;
    gScrolling = true; gScrollClipRgn = ur;
    updateBlocks();
    gScrollClipRgn = nil; gScrolling = false;

    if (preserveTE && HasSel()) {
        textActive = pageElm[blockSelect].textEdit;
        if (textActive) TEActivate(textActive);
    }
}

/* ------------------------------------------------------------------ */
/* updateSaveButton — show/hide button + progress bar, fire auto-save  */
/* ------------------------------------------------------------------ */
/* Save-area states (AppState.lastSaveLabelIdx) */
enum { kLblHidden = 0, kLblSave = 1, kLblSaved = 5, kLblReadOnly = 6,
       kLblFlash = 7, kLblNoNetwork = 8 };
static bool IsTwoLineLabel(short idx) {
    return idx == kLblReadOnly || idx == kLblFlash || idx == kLblNoNetwork;
}

/* Lines of each two-line label: the flashes (AppState.flashKind, kFlash*),
   then "No network" */
enum { kLinesNoNetwork = 4 };
static const unsigned char* const kTwoLines[][2] = {
    { "\pToo",  "\plong"    },   /* kFlashTooLong  */
    { "\pRead", "\ponly"    },   /* kFlashReadOnly, and a read-only page */
    { "\pToo",  "\plarge"   },   /* kFlashTooLarge */
    { "\pNot",  "\psaved"   },   /* kFlashNotSaved */
    { "\pNo",   "\pnetwork" },   /* kLinesNoNetwork */
};

/* Two centred lines in the save area, the first one a line above the
   button; labelR covers both (and "network", a little wider than it). */
static void DrawTwoLineLabel(const Rect& box, const Rect& labelR,
                             ConstStr255Param l1, ConstStr255Param l2) {
    EraseRect(&labelR);
    TextFont(0); TextFace(0); TextSize(12);
    FontInfo fi; GetFontInfo(&fi);
    short lineH = fi.ascent + fi.descent + fi.leading;
    short midY  = box.top + (box.bottom - box.top) / 2 - lineH + fi.ascent - 8;
    short w     = box.right - box.left;
    MoveTo(box.left + (w - StringWidth(l1)) / 2, midY);         DrawString(l1);
    MoveTo(box.left + (w - StringWidth(l2)) / 2, midY + lineH); DrawString(l2);
}

void updateSaveButton() {
    if (gSaveButton == nil || gWindow == nil) return;

    long now = TickCount();
    if (now - gAppState.lastSaveButtonTick < 2) return;
    gAppState.lastSaveButtonTick = now;

    SetPort(gWindow);
    Rect box = SaveButtonBox();
    const short btnLeft   = box.left;
    const short btnRight  = box.right;
    const short btnTop    = box.top;
    const short btnBottom = box.bottom;
    /* Inner bar width (1 px inset each side) */
    const short barWidth  = btnRight - btnLeft - 2;
    Rect eraseR; SetRect(&eraseR, btnLeft, btnTop - 3, btnRight, btnBottom);
    /* Two-line labels start a line above the button, and "network" is a
       little wider than the 48 px button: their area is erased as a whole,
       4 px to either side (still inside the 56 px sidebar). */
    Rect labelR; SetRect(&labelR, btnLeft - 4, btnTop - 18, btnRight + 4, btnBottom);

    /* --- Two-line labels, highest priority first (redrawn every pass:
       updateBlocks may erase) --- */
    short lbl = -1, lines = 0;
    if (gAppState.readOnly) {
        /* Integration token lacks write permission */
        lbl = kLblReadOnly; lines = kFlashReadOnly;
    } else if (gAppState.flashUntil > 0 && TickCount() < gAppState.flashUntil) {
        /* A refused edit or save: "Too long", "Read only", "Too large", "Not saved" */
        lbl = kLblFlash;
        lines = gAppState.flashKind < kLinesNoNetwork ? gAppState.flashKind : kFlashTooLong;
    } else {
        gAppState.flashUntil = 0;
        /* A save that has seen no network traffic for 12 s is waiting on a
           lost connection (a working one moves bytes all the time; the
           longest quiet stretch is the key math in a cold Classic II
           handshake, ~10 s).  Say so instead of a full bar; the save still
           completes by itself once the network returns. */
        unsigned long lastSign = (unsigned long)gAppState.saveStartTick;
        if (g_net_activity_tick > lastSign) lastSign = g_net_activity_tick;
        if (gAppState.isSaving && (unsigned long)now - lastSign > 12UL * 60UL) {
            lbl = kLblNoNetwork; lines = kLinesNoNetwork;
        }
    }
    if (lbl >= 0) {
        if (gAppState.lastSaveLabelIdx != lbl) HideSaveButtonAs(lbl);
        DrawTwoLineLabel(box, labelR, kTwoLines[lines][0], kTwoLines[lines][1]);
        return;
    }
    /* Leaving a two-line label: clear its top line too */
    if (IsTwoLineLabel(gAppState.lastSaveLabelIdx)) {
        EraseRect(&labelR);
        gAppState.lastSaveLabelIdx = -1;
    }

    /* --- "Saved": hide border, draw plain text for 6 s --- */
    if (!gAppState.hasDirtyBlocks && !gAppState.isSaving && gAppState.savedUntil > 0) {
        if (TickCount() < gAppState.savedUntil) {
            /* First entry: hide button control and erase bar */
            if (gAppState.lastSaveLabelIdx != kLblSaved) HideSaveButtonAs(kLblSaved);
            /* Always redraw — updateBlocks() may have erased the text */
            EraseRect(&eraseR);
            TextFont(0); TextFace(0); TextSize(12);
            short tw = StringWidth((ConstStr255Param)"\pSaved");
            MoveTo(btnLeft + (btnRight - btnLeft - tw) / 2, btnTop + 11);
            DrawString((ConstStr255Param)"\pSaved");
            TextFace(0);
            return;
        }
        /* Timer expired — erase area, fall through to hide */
        gAppState.savedUntil = 0;
        if (gAppState.lastSaveLabelIdx == kLblSaved) {
            EraseRect(&eraseR);
            gAppState.lastSaveLabelIdx = -1;
        }
    }

    /* --- Hide when nothing pending --- */
    if (!gAppState.hasDirtyBlocks && !gAppState.isSaving) {
        if (gAppState.lastSaveLabelIdx != kLblHidden) {
            gAppState.lastSaveLabelIdx = kLblHidden;
            HideControl(gSaveButton);
            gAppState.lastBarFilled = -1;
            EraseRect(&eraseR);
        }
        return;
    }

    /* --- Show button with static "Save" label --- */
    if (gAppState.lastSaveLabelIdx != kLblSave) {
        if (gAppState.lastSaveLabelIdx == kLblSaved) gAppState.savedUntil = 0;
        gAppState.lastSaveLabelIdx = kLblSave;
        gAppState.lastBarFilled = -1;
        DrawRoundButton(gWindow, eraseR, (ConstStr255Param)"\pSave");
    }

    /* --- Progress bar fill --- */
    if (gAppState.isSaving) {
        drawProgressBar(barWidth);   /* full bar while sending */
        return;
    }

    /* Use unsigned subtraction so TickCount() wrap-around (~413 days) is handled correctly */
    unsigned long elapsed = (unsigned long)TickCount() - (unsigned long)gAppState.lastEditTick;
    if (elapsed >= (unsigned long)kAutoSaveTicks) {
        doSave();
        return;
    }
    short filled = (short)((long)elapsed * (long)barWidth / kAutoSaveTicks);
    drawProgressBar(filled);
}
