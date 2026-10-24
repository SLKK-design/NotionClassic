 /* picker_window.cpp — icon and emoji picker windows.
   Extracted from main.cpp (lines 856-1618).
   Contains: icon picker state/callbacks, emoji picker state/callbacks,
   PickerConfig struct, runPicker event loop, ShowIconPicker, ShowEmojiPicker,
   InsertPickedEmoji.                                                          */

#include <vector>
#include <string>
#include <string.h>

#include <Memory.h>
#include <Windows.h>
#include <Controls.h>
#include <Events.h>
#include <TextEdit.h>
#include <Quickdraw.h>
#include <QDOffscreen.h>

#include "picker_window.h"
#include "picker.h"
#include "picker_geom.h"
#include "Settings.h"
#include "app_state.h"
#include "helpers.h"
#include "blocks.h"
extern "C" {
#include "emoji_sprite.h"
}
#include "debug_con.h" 
#include "emoji_text.h"
#include "run_styles.h"
#include "block_ops.h"

using std::string;
using std::vector;

/* ------------------------------------------------------------------ */
/* Extern globals from main.cpp                                        */
/* ------------------------------------------------------------------ */
extern WindowPtr      gWindow;
extern char           gPageIconName[32];
extern char           gPageIconColor[32];
extern AppState       gAppState;
extern AppSettings    gSettings;
extern TEHandle       textActive;
extern int            blockSelect;
extern Block*         pageElm;
extern int            pageElmCount;
extern int            gScrollOffset;
extern short          gColLeft;
extern short          gFontHelvetica;
extern SInt16         gFontGeneva;
extern Rect           gIconRect;

/* ------------------------------------------------------------------ */
/* Extern functions from main.cpp                                      */
/* ------------------------------------------------------------------ */
extern void updateBlocks();
extern bool gFullEraseOnce;
extern void DrawStatusText(const char* msg);
extern void MarkDirty();
extern void SyncCurrentPageInHistory();

/* ------------------------------------------------------------------ */
/* Icon picker constants and state                                     */
/* ------------------------------------------------------------------ */
/* kPSCell=16, kSprCols=12 — defined in picker.h */
static const int kPPad    = 4;                       /* padding around each 16px icon */
static const int kPCell   = kPSCell + kPPad * 2;    /* 16 + 8 = 24px display cell */
static const int kPCols   = 12;                      /* display columns */
static const int kPVisR   = 9;                       /* visible rows — fits SE/30 342px screen */
static const int kPInset  = 4;                       /* window inset (left + top) */
static const int kPNameH  = 30;                      /* name label height */
static const int kPSBW    = 16;                      /* scroll bar width */
static const int kPSBGap  = 8;                       /* gap between grid and scroll bar */
static const int kPGridW  = kPCols * kPCell + kPInset; /* grid + inset */
static const int kPGridH  = kPVisR  * kPCell + kPInset; /* grid + inset */
static const int kPWinH   = kPGridH + kPNameH;
static const int kPWinW   = kPGridW + kPSBGap + kPSBW;

static const GridMetrics kIconGrid = { kPCell, kPPad, kPInset, kPSCell, kPCols, kPVisR, kSprCols };

static WindowPtr        sPickerWin          = nil;
static GWorldPtr        sPickerGridGW       = nil;  /* offscreen back-buffer for grid */
static ControlHandle    sPickerSB           = nil;
static int              sPickerScrollRow    = 0;
static int              sPickerSelIdx       = -1;  /* filtIdx; total = "no icon" cell */
static bool             sPickerSearchActive = false;
static string           sPickerQuery;
static vector<int>      sPickerFiltered;   /* indices into gIconNames (= sprite index) */
static void DrawPickerName(int hoverIdx);

/* Shared scroll UPP — one UPP serves both pickers via gPicker */
static ControlActionUPP gPickerScrollUPP = nil;

/* Case-insensitive substring match (no heap allocs) */
static bool iconNameMatch(const char* name, const char* lq, int lqLen) {
    int nLen = (int)strlen(name);
    for (int s = 0; s <= nLen - lqLen; s++) {
        int j = 0;
        while (j < lqLen && (name[s+j] | 0x20) == (unsigned char)lq[j]) j++;
        if (j == lqLen) return true;
    }
    return false;
}

static void BuildPickerFilter() {
    sPickerFiltered.clear();
    if (sPickerQuery.empty()) {
        sPickerFiltered.reserve(gIconCount);
        for (int i = 0; i < gIconCount; i++)
            sPickerFiltered.push_back(i);
        return;
    }
    string lq = sPickerQuery;
    LowerCaseInPlace(&lq[0], (int)lq.size());
    for (int i = 0; i < gIconCount; i++) {
        if (iconNameMatch(gIconNames[i], lq.c_str(), (int)lq.size()))
            sPickerFiltered.push_back(i);
    }
}

/* LoadIconList, LoadSprite, DrawListPageIcon, ReloadListAssets — in picker.cpp */

/* ------------------------------------------------------------------ */
/* blitSpriteCellToGW — memcpy one 16×16 sprite cell into the GWorld   */
/* Both source X and dest X are byte-aligned (multiples of 8).         */
/* ------------------------------------------------------------------ */
static void blitSpriteCellToGW(char* gwBase, short gwRB,
                                const char* srcBase, short srcRB,
                                short sx, short sy, short dx, short dy) {
    for (int r = 0; r < kPSCell; r++)
        memcpy(gwBase + (long)(dy+r)*gwRB + dx/8,
               srcBase + (long)(sy+r)*srcRB + sx/8, 2); /* 16px = 2 bytes */
}

/* ------------------------------------------------------------------ */
/* UpdateIconCell — blit the GWorld cell to screen, draw selection      */
/* ------------------------------------------------------------------ */
static void UpdateIconCell(int fi) {
    if (!sPickerWin || fi < 0) return;
    int total = (int)sPickerFiltered.size();
    if (fi >= total + 1) return;
    int row = fi / kPCols - sPickerScrollRow;
    int col = fi % kPCols;
    if (row < 0 || row >= kPVisR) return;
    SetPort(sPickerWin);
    PixMapHandle dstPM; BitMap* dstBM = acquire_port_bitmap((GrafPtr)sPickerWin, &dstPM);

    /* Blit the plain cell from GWorld to screen (erases any old selection) */
    Rect blitR = CellRect(kIconGrid, row, col);
    InsetRect(&blitR, -2, -2);
    if (sPickerGridGW && dstBM) {
        PixMapHandle gwPM = GetGWorldPixMap(sPickerGridGW);
        CopyBits((BitMap*)(*gwPM), dstBM, &blitR, &blitR, srcCopy, nil);
    } else {
        EraseRect(&blitR);
        if (fi < total && gSpriteBM.baseAddr && dstBM) {
            Rect srcR = SpriteSrcRect(kIconGrid, sPickerFiltered[fi]);
            Rect dstR = TileRect(kIconGrid, row, col);
            CopyBits(&gSpriteBM, dstBM, &srcR, &dstR, srcCopy, nil);
        }
    }

    bool isNone = (fi == total);
    bool isSel  = (fi == sPickerSelIdx);

    /* Draw "No icon" text on screen */
    if (isNone) {
        TextFont(gFontHelvetica); TextFace(0); TextSize(12);
        Rect nr = CellRect(kIconGrid, row, col);
        nr.right = kPGridW;
        if (!isSel) EraseRect(&nr);
        const char* lbl = "No icon";
        short tw = TextWidth(lbl, 0, 7);
        MoveTo(nr.left + (nr.right - nr.left - tw) / 2, nr.top + (kPCell + 9) / 2);
        if (isSel) TextMode(srcBic);
        DrawText(lbl, 0, 7);
        if (isSel) TextMode(srcOr);
    }

    /* Selection overlay on screen */
    if (isSel) {
        Rect hr = SelFrameRect(kIconGrid, row, col);
        if (isNone) hr.right = kPGridW + 1;
        PaintSelBg(hr);
        if (!isNone && fi < total && gSpriteBM.baseAddr && dstBM) {
            Rect srcR = SpriteSrcRect(kIconGrid, sPickerFiltered[fi]);
            Rect dstR = TileRect(kIconGrid, row, col);
            CopyBits(&gSpriteBM, dstBM, &srcR, &dstR, notSrcCopy, nil);
        }
    }

    if (dstPM) UnlockPixels(dstPM);
}

/* DrawPickerGrid — memcpy cells to GWorld, one CopyBits to screen     */
/* ------------------------------------------------------------------ */
static void DrawPickerGrid(int hoverIdx) {
    if (!sPickerWin) return;
    SetPort(sPickerWin);

    int total = (int)sPickerFiltered.size();
    PixMapHandle dstPM; BitMap* dstBM = acquire_port_bitmap((GrafPtr)sPickerWin, &dstPM);

    if (sPickerGridGW && gSpriteBM.baseAddr) {
        PixMapHandle gwPM = GetGWorldPixMap(sPickerGridGW);
        char* gwBase = GetPixBaseAddr(gwPM);
        short gwRB   = (**gwPM).rowBytes & 0x3FFF;
        memset(gwBase, 0, (long)gwRB * kPGridH);

        for (int dr = 0; dr < kPVisR; dr++) {
            for (int dc = 0; dc < kPCols; dc++) {
                int idx = (dr + sPickerScrollRow) * kPCols + dc;
                if (idx >= total) break;
                short sx, sy; SpriteSrcXY(kIconGrid, sPickerFiltered[idx], &sx, &sy);
                short dx, dy; TileDestXY(kIconGrid, dr, dc, &dx, &dy);
                blitSpriteCellToGW(gwBase, gwRB,
                    gSpriteBM.baseAddr, gSpriteBM.rowBytes, sx, sy, dx, dy);
            }
        }

        /* PaintSelBg overhangs the cell by ~2px; the gridR copy below stops at
           the grid edge, so a selected cell in the last column/row would leave a
           ghost sliver in the scrollbar gap / bottom margin as you scroll.
           Clear those strips first. */
        { Rect r; SetRect(&r, kPGridW, 0, kPGridW + 2, kPGridH + 2); EraseRect(&r);
          SetRect(&r, 0, kPGridH, kPGridW + 2, kPGridH + 2); EraseRect(&r); }
        Rect gridR; SetRect(&gridR, 0, 0, kPGridW, kPGridH);
        if (dstBM) CopyBits((BitMap*)(*gwPM), dstBM, &gridR, &gridR, srcCopy, nil);
    } else if (gSpriteBM.baseAddr && dstBM) {
        /* Fallback: direct to window — erase first since we draw individual cells */
        { Rect gridR; SetRect(&gridR, 0, 0, kPGridW + 4, kPGridH + 4); EraseRect(&gridR); }
        for (int dr = 0; dr < kPVisR; dr++) {
            for (int dc = 0; dc < kPCols; dc++) {
                int idx = (dr + sPickerScrollRow) * kPCols + dc;
                if (idx >= total) break;
                Rect srcR = SpriteSrcRect(kIconGrid, sPickerFiltered[idx]);
                Rect dstR = TileRect(kIconGrid, dr, dc);
                CopyBits(&gSpriteBM, dstBM, &srcR, &dstR, srcCopy, nil);
            }
        }
    }

    /* "No icon" cell — drawn on window after GWorld blit */
    {
        int noneAbsRow = total / kPCols, noneCol = total % kPCols;
        int noneRow    = noneAbsRow - sPickerScrollRow;
        if (noneRow >= 0 && noneRow < kPVisR) {
            Rect nr = CellRect(kIconGrid, noneRow, noneCol);
            nr.right = kPGridW;
            EraseRect(&nr);
            TextFont(gFontHelvetica); TextFace(0); TextSize(12);
            const char* lbl = "No icon";
            short tw = TextWidth(lbl, 0, 7);
            MoveTo(nr.left + (nr.right - nr.left - tw) / 2, nr.top + (kPCell + 9) / 2);
            DrawText(lbl, 0, 7);
        }
    }

    /* Selection overlay on screen */
    {
        int selIdx = sPickerSelIdx;
        if (selIdx >= 0) {
            int absRow = selIdx / kPCols, col = selIdx % kPCols;
            int row    = absRow - sPickerScrollRow;
            if (row >= 0 && row < kPVisR) {
                bool isNone = (selIdx == total);
                Rect hr = SelFrameRect(kIconGrid, row, col);
                if (isNone) hr.right = kPGridW + 1;
                PaintSelBg(hr);
                if (!isNone && selIdx < total && gSpriteBM.baseAddr && dstBM) {
                    Rect srcR = SpriteSrcRect(kIconGrid, sPickerFiltered[selIdx]);
                    Rect dstR = TileRect(kIconGrid, row, col);
                    CopyBits(&gSpriteBM, dstBM, &srcR, &dstR, notSrcCopy, nil);
                }
            }
        }
    }

    if (dstPM) UnlockPixels(dstPM);
    DrawPickerName(hoverIdx);
}

/* ------------------------------------------------------------------ */
/* DrawPickerName — redraw just the name label (hover or selected)     */
/* ------------------------------------------------------------------ */
static void DrawPickerName(int hoverIdx) {
    if (!sPickerWin) return;
    SetPort(sPickerWin);
    Rect nameR = {kPGridH, 0, kPWinH, kPGridW};
    EraseRect(&nameR);
    TextFont(gFontHelvetica); TextFace(0); TextSize(12);

    DrawMagnifyingGlass(kPGridH + 10);

    int total = (int)sPickerFiltered.size();

    /* Hover takes priority: show icon name while mouse is over a cell */
    if (hoverIdx >= 0 && hoverIdx <= total) {
        const char* label = (hoverIdx < total)
                           ? gIconNames[sPickerFiltered[hoverIdx]]
                           : "No icon";
        if (label && label[0]) {
            short len = (short)strlen(label);
            MoveTo(17, kPGridH + 20);
            DrawText(label, 0, len);
        }
    } else if (sPickerSearchActive) {
        /* Not hovering: show search input (no focus border) */
        static string sPickerDisplay;
        sPickerDisplay = sPickerQuery; sPickerDisplay += '|';
        MoveTo(19, kPGridH + 20);
        DrawText(sPickerDisplay.c_str(), 0, (short)sPickerDisplay.length());
    } else {
        /* Not hovering, not searching: show current keyboard selection */
        if (sPickerSelIdx >= 0 && sPickerSelIdx < total) {
            const char* selName = gIconNames[sPickerFiltered[sPickerSelIdx]];
            MoveTo(17, kPGridH + 20);
            DrawText(selName, 0, (short)strlen(selName));
        }
    }
}

/* ------------------------------------------------------------------ */
/* PickerHitTest — shared grid hit-test for mouseDown and hover        */
/* ------------------------------------------------------------------ */
struct PickerConfig;  /* forward */
static bool PickerHitTest(const PickerConfig& cfg, Point lp,
                          short cellW, short cellH, int* outIdx);

/* ------------------------------------------------------------------ */
/* PickerConfig + runPicker — unified event loop for icon/emoji pickers */
/* ------------------------------------------------------------------ */
struct PickerConfig {
    short cols, visRows, gridW, gridH, winW, winH, sbGap, inset;
    bool  hasExtraSlot;  /* icon: index==total means "no icon" slot    */
    bool  alwaysSearch;  /* emoji: printable keys go to search always  */
    bool  doBlink;       /* emoji: redraw bar every 15 ticks for cursor */
    ConstStr255Param title;
    /* mutable state — pointers into caller's module globals */
    int*           scrollRow;
    int*           selIdx;
    string*        query;
    bool*          searchActive;
    WindowPtr*     win;
    ControlHandle* sb;
    /* callbacks */
    int  (*getTotal)(void);
    void (*buildFilter)(void);
    void (*drawGrid)(int hoverIdx);
    void (*drawBar)(int hoverIdx);
    void (*onCommit)(int filtIdx);
    void (*onArrow)(int newFiltIdx);    /* NULL ok — icon live preview   */
    void (*onHover)(int oldIdx, int newIdx); /* NULL ok — emoji cheap redraw  */
    void (*onCancel)(void);             /* NULL ok — icon restore icon   */
};
static const PickerConfig* gPicker = NULL;

static bool PickerHitTest(const PickerConfig& cfg, Point lp,
                          short cellW, short cellH, int* outIdx) {
    if (lp.v < cfg.inset || lp.v >= cfg.gridH ||
        lp.h < cfg.inset || lp.h >= cfg.gridW) return false;
    int col = (lp.h - cfg.inset) / cellW, row = (lp.v - cfg.inset) / cellH;
    int idx = (row + *cfg.scrollRow) * cfg.cols + col;
    int total = cfg.getTotal();
    if (cfg.hasExtraSlot && idx > total && row + *cfg.scrollRow == total / cfg.cols)
        idx = total;
    int maxIdx = total + (cfg.hasExtraSlot ? 0 : -1);
    if (idx < 0 || idx > maxIdx) return false;
    *outIdx = idx;
    return true;
}

static pascal void RunPickerScrollAction(ControlHandle ctl, short part) {
    if (!gPicker || part == 0) return;
    short visR = gPicker->visRows;
    short delta = ScrollPartDelta(part, 1, (short)(visR - 1));
    if (delta == 0) return;
    short v = GetControlValue(ctl) + delta;
    if (v < GetControlMinimum(ctl)) v = GetControlMinimum(ctl);
    if (v > GetControlMaximum(ctl)) v = GetControlMaximum(ctl);
    if (v == GetControlValue(ctl)) return;
    SetControlValue(ctl, v);
    *gPicker->scrollRow = v;
    gPicker->drawGrid(-1);
    DrawControls(*gPicker->win);
}

static void runPicker(const PickerConfig& cfg) {
    gPicker = &cfg;

    /* Compute initial maxScroll */
    auto calcMax = [&]() -> int {
        int tot   = cfg.getTotal();
        int slots = tot + (cfg.hasExtraSlot ? 1 : 0);
        int rows  = (slots + cfg.cols - 1) / cfg.cols;
        return (rows > cfg.visRows) ? rows - cfg.visRows : 0;
    };
    int maxScroll = calcMax();

    /* Pre-scroll to reveal current selection */
    {
        int selRow = *cfg.selIdx / cfg.cols;
        *cfg.scrollRow = selRow - cfg.visRows / 2;
        if (*cfg.scrollRow < 0) *cfg.scrollRow = 0;
        if (*cfg.scrollRow > maxScroll) *cfg.scrollRow = maxScroll;
    }

    /* Create window + scrollbar — clamp wy so the title bar (≈19px) clears the menu bar */
    short wx = (qd.screenBits.bounds.right  - cfg.winW) / 2;
    short wy = (qd.screenBits.bounds.bottom - cfg.winH) / 2;
    short minWY = GetMBarHeight() + 19;
    if (wy < minWY) wy = minWY;
    Rect  winRect; SetRect(&winRect, wx, wy, wx + cfg.winW, wy + cfg.winH);
    *cfg.win = NewWindow(nil, &winRect, cfg.title, true,
                         noGrowDocProc, (WindowPtr)-1, true, 0L);
    if (!*cfg.win) { gPicker = NULL; return; }

    Rect sbRect; SetRect(&sbRect, cfg.gridW + cfg.sbGap, -1,
                          cfg.winW + 1, cfg.gridH + 1);
    *cfg.sb = MakeScrollBar(*cfg.win, sbRect, *cfg.scrollRow, 0, maxScroll);

    if (!gPickerScrollUPP)
        gPickerScrollUPP = NewControlActionUPP(RunPickerScrollAction);

    SetPort(*cfg.win);
    cfg.drawGrid(-1);
    DrawControls(*cfg.win);

    /* Rebuild filter + reset scroll/scrollbar — called after search text changes */
    auto rebuildAndRedraw = [&]() {
        cfg.buildFilter();
        maxScroll = calcMax();
        *cfg.scrollRow = 0; *cfg.selIdx = 0;
        SetControlValue(*cfg.sb, 0);
        SetControlMaximum(*cfg.sb, maxScroll);
        SetPort(*cfg.win);
        cfg.drawGrid(-1);
        DrawControls(*cfg.win);
    };

    short cellW = cfg.gridW / cfg.cols;
    short cellH = cfg.gridH / cfg.visRows;
    int   hoverIdx = -1;
    long  lastBlink = 0;
    bool  done = false;

    while (!done) {
        EventRecord ev = {}; WaitNextEvent(everyEvent, &ev, 1, nil);
        switch (ev.what) {

            case mouseDown: {
                WindowPtr hitWin;
                short wPart = FindWindow(ev.where, &hitWin);
                if (wPart == inGoAway && hitWin == *cfg.win) {
                    if (TrackGoAway(*cfg.win, ev.where)) {
                        if (cfg.onCancel) cfg.onCancel();
                        done = true;
                    }
                    break;
                }
                if (hitWin != *cfg.win) { done = true; break; }
                if (wPart == inDrag) {
                    DragWindow(*cfg.win, ev.where, &qd.screenBits.bounds); break;
                }
                SetPort(*cfg.win);
                Point lp = ev.where; GlobalToLocal(&lp);
                ControlHandle hitCtl; short cPart = FindControl(lp, *cfg.win, &hitCtl);
                if (hitCtl == *cfg.sb) {
                    *cfg.scrollRow = TrackScrollBar(*cfg.sb, lp, cPart, gPickerScrollUPP);
                    cfg.drawGrid(hoverIdx); DrawControls(*cfg.win);
                } else {
                    int idx;
                    if (PickerHitTest(cfg, lp, cellW, cellH, &idx)) {
                        cfg.onCommit(idx);
                        done = true;
                    } else if (lp.v >= cfg.gridH) {
                        *cfg.searchActive = true;
                        cfg.drawBar(hoverIdx);
                    }
                }
                break;
            }

            case updateEvt:
                if ((WindowPtr)ev.message == *cfg.win) {
                    BeginUpdate(*cfg.win);
                    cfg.drawGrid(hoverIdx); DrawControls(*cfg.win);
                    EndUpdate(*cfg.win);
                }
                break;

            case keyDown: case autoKey: {
                char c = (char)(ev.message & charCodeMask);
                if (c == 0x1B) {
                    /* Esc: clear search first; if already empty, cancel + close */
                    if (!cfg.query->empty()) {
                        cfg.query->clear(); *cfg.searchActive = false;
                        rebuildAndRedraw();
                    } else {
                        if (cfg.onCancel) cfg.onCancel();
                        done = true;
                    }
                } else if (c == 0x0D || c == 0x03) {
                    /* Enter: commit keyboard selection */
                    int total  = cfg.getTotal();
                    int maxSel = total + (cfg.hasExtraSlot ? 0 : -1);
                    if (*cfg.selIdx >= 0 && *cfg.selIdx <= maxSel)
                        cfg.onCommit(*cfg.selIdx);
                    done = true;
                } else if (c == 0x1C || c == 0x1D || c == 0x1E || c == 0x1F) {
                    /* Arrow keys: navigate grid */
                    int total  = cfg.getTotal();
                    int maxSel = cfg.hasExtraSlot ? total : (total > 0 ? total - 1 : 0);
                    int newSel = *cfg.selIdx;
                    if      (c == 0x1C) newSel--;
                    else if (c == 0x1D) newSel++;
                    else if (c == 0x1E) newSel -= cfg.cols;
                    else if (c == 0x1F) newSel += cfg.cols;
                    if (newSel < 0) newSel = 0;
                    if (newSel > maxSel) newSel = maxSel;
                    if (newSel != *cfg.selIdx) {
                        *cfg.selIdx = newSel;
                        int selRow = *cfg.selIdx / cfg.cols;
                        if (selRow < *cfg.scrollRow) {
                            *cfg.scrollRow = selRow;
                            SetControlValue(*cfg.sb, *cfg.scrollRow);
                        } else if (selRow >= *cfg.scrollRow + cfg.visRows) {
                            *cfg.scrollRow = selRow - cfg.visRows + 1;
                            SetControlValue(*cfg.sb, *cfg.scrollRow);
                        }
                        if (cfg.onArrow) cfg.onArrow(*cfg.selIdx);
                        cfg.drawGrid(hoverIdx); DrawControls(*cfg.win);
                    }
                } else if ((c == 0x08 || c == 0x7F) && !cfg.query->empty()) {
                    /* Backspace */
                    cfg.query->erase(cfg.query->size() - 1, 1);
                    rebuildAndRedraw();
                } else if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7F
                           && (*cfg.searchActive || cfg.alwaysSearch)) {
                    /* Printable: add to search query */
                    *cfg.searchActive = true;
                    *cfg.query += c;
                    rebuildAndRedraw();
                }
                break;
            }

            case nullEvent: {
                if (cfg.doBlink) {
                    long now = TickCount();
                    if (now - lastBlink > 15) { lastBlink = now; cfg.drawBar(hoverIdx); }
                }
                SetPort(*cfg.win);
                Point mp = ev.where; GlobalToLocal(&mp);
                int newHover = -1;
                PickerHitTest(cfg, mp, cellW, cellH, &newHover);
                if (newHover != hoverIdx) {
                    if (cfg.onHover) cfg.onHover(hoverIdx, newHover);
                    hoverIdx = newHover;
                    cfg.drawBar(hoverIdx);
                }
                break;
            }
        }
    }

    DisposeControl(*cfg.sb);
    *cfg.sb = nil;
    /* Dispose normally so PaintBehind repaints the desktop and any window
       behind the screen-centered picker — skipping it (visible=false) left
       ghost pixels there. The caller redraws gWindow immediately and ValidRects
       it, so the main window's own exposed area doesn't double-flash. */
    DisposeWindow(*cfg.win);
    *cfg.win = nil;
    gPicker = NULL;
}

/* Redraw only the page icon area in the main window (no block relayout) */
static void RedrawPageIcon() {
    SetPort(gWindow);
    short ix = gColLeft + 2;
    short iy = 48;  /* scroll is 0 while picker is open */
    Rect area; SetRect(&area, ix - 2, iy - 2, ix + 44, iy + 44);
    EraseRect(&area);
    SetRect(&gIconRect, 0, 0, 0, 0);
    if (gPageIconName[0] != '\0') {
        DrawNotionIcon(gPageIconName, ix, iy);
        SetRect(&gIconRect, ix, iy, ix + 40, iy + 40);
    }
}

/* ------------------------------------------------------------------ */
/* ShowIconPicker — thin wrapper around runPicker                       */
/* ------------------------------------------------------------------ */
static string sIconOriginal;

static void IconPickerOnCommit(int filtIdx) {
    int total = (int)sPickerFiltered.size();
    SafeStrCopy(gPageIconName, sizeof(gPageIconName),
                (filtIdx < total) ? gIconNames[sPickerFiltered[filtIdx]] : "");
    gAppState.iconDirty      = true;
    MarkDirty();
    SyncCurrentPageInHistory();
}
static void IconPickerOnArrow(int newFiltIdx) {
    int total = (int)sPickerFiltered.size();
    SafeStrCopy(gPageIconName, sizeof(gPageIconName),
                (newFiltIdx < total) ? gIconNames[sPickerFiltered[newFiltIdx]] : "");
    RedrawPageIcon();
}
static void IconPickerOnCancel() { SafeStrCopy(gPageIconName, sizeof(gPageIconName), sIconOriginal.c_str()); }

void ShowIconPicker() {
    SetCursor(*GetCursor(watchCursor));
    if (gIconCount == 0) LoadIconList();
    if (gIconCount == 0) { InitCursor(); return; }
    LoadSprite();
    InitCursor();

    sPickerSearchActive = false;
    sPickerQuery.clear();
    BuildPickerFilter();

    /* Find current icon in filtered list; default to "no icon" slot */
    int total = (int)sPickerFiltered.size();
    sPickerSelIdx = total;
    for (int i = 0; i < total; i++) {
        if (strcmp(gIconNames[sPickerFiltered[i]], gPageIconName) == 0) { sPickerSelIdx = i; break; }
    }
    sPickerScrollRow = 0;
    sIconOriginal    = gPageIconName;

    /* Scroll main window to top so page icon stays visible for live preview.
       Don't updateBlocks() here — runPicker() redraws nothing behind it, and
       the single updateBlocks() on close will re-render at offset 0. */
    gScrollOffset = 0;

    /* Offscreen back-buffer for the icon grid — single blit per frame */
    { Rect gwr; SetRect(&gwr, 0, 0, kPGridW, kPGridH);
      NewGWorld(&sPickerGridGW, 1, &gwr, nil, nil, 0);
      if (sPickerGridGW) LockPixels(GetGWorldPixMap(sPickerGridGW)); }

    static const PickerConfig kCfg = {
        kPCols, kPVisR, kPGridW, kPGridH, kPWinW, kPWinH, kPSBGap, kPInset,
        true, false, false,
        "\pSelect icon",
        &sPickerScrollRow, &sPickerSelIdx, &sPickerQuery, &sPickerSearchActive,
        &sPickerWin, &sPickerSB,
        [](){ return (int)sPickerFiltered.size(); },
        BuildPickerFilter,
        DrawPickerGrid,
        DrawPickerName,
        IconPickerOnCommit,
        IconPickerOnArrow,
        [](int oldIdx, int newIdx) {
            int prevSel = sPickerSelIdx;
            if (newIdx >= 0) sPickerSelIdx = newIdx;
            UpdateIconCell(prevSel);
            UpdateIconCell(oldIdx);
            UpdateIconCell(newIdx);
        },
        IconPickerOnCancel,
    };
    runPicker(kCfg);

    if (sPickerGridGW) {
        UnlockPixels(GetGWorldPixMap(sPickerGridGW));
        DisposeGWorld(sPickerGridGW); sPickerGridGW = nil;
    }

    CalcVis(gWindow);
    SetPort(gWindow);
    gFullEraseOnce = true;  /* clear chrome the screen-centered picker ghosted */
    updateBlocks();
    ValidRect(&gWindow->portRect);
}

/* ------------------------------------------------------------------ */
/* Emoji picker — select an emoji to insert into the active text field */
/* ------------------------------------------------------------------ */
static const int kEPPad   = 4;                /* padding around each 16×16 tile */
static const int kEPCell  = EMOJI_TILE_W + kEPPad * 2;  /* 16 + 8 = 24px per cell */
static const int kEPCols  = 11;
static const int kEPVisR  = 9;                /* visible rows — fits SE/30 342px screen */
static const int kEPInset = 4;                /* window inset (left + top) */
static const int kEPBotH  = 30;               /* hint bar height */
static const int kEPSBW   = 16;
static const int kEPSBGap = 8;
static const int kEPGridW = kEPCols * kEPCell + kEPInset;
static const int kEPGridH = kEPVisR  * kEPCell + kEPInset;
static const int kEPWinW  = kEPGridW + kEPSBGap + kEPSBW;
static const int kEPWinH  = kEPGridH + kEPBotH;

static const GridMetrics kEmojiGrid = { kEPCell, kEPPad, kEPInset, EMOJI_TILE_W, kEPCols, kEPVisR, 0 };

static WindowPtr        sEPWin          = nil;
static GWorldPtr        sEPGridGW       = nil;    /* offscreen back-buffer for emoji grid */
static ControlHandle    sEPSB           = nil;
static int              sEPScrollRow    = 0;
static int              sEPSelIdx       = 0;   /* index into sEPFiltered */
static string           sEPQuery;
static vector<int>      sEPFiltered;            /* EMOJI_TABLE indices matching current query */
static bool             sEPSearchActive = false;

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
    LowerCaseInPlace(&lq[0], (int)lq.size());
    for (int i = 0; i < EMOJI_COUNT; i++)
        if (strstr(EMOJI_TABLE[i].name, lq.c_str())) sEPFiltered.push_back(i);
}

static void DrawEmojiPickerGrid(int hoverIdx);  /* forward */

static void DrawEmojiSearchBar(const string& query, int hoverIdx = -1) {
    if (!sEPWin) return;
    SetPort(sEPWin);
    Rect bar; SetRect(&bar, 0, kEPGridH, kEPGridW, kEPWinH);
    EraseRect(&bar);
    TextFont(gFontHelvetica); TextFace(0); TextSize(12);

    DrawMagnifyingGlass(kEPGridH + 10);

    /* Hovering an emoji: show its name */
    if (hoverIdx >= 0 && hoverIdx < (int)sEPFiltered.size()) {
        int ti = sEPFiltered[hoverIdx];
        const char* ename = EMOJI_TABLE[ti].name;
        if (ename && *ename) {
            MoveTo(17, kEPGridH + 20);
            DrawText(ename, 0, (short)strlen(ename));
        }
    } else {
        /* Not hovering: show search input (no focus border) */
        MoveTo(17, kEPGridH + 20);
        if (!query.empty()) DrawText(query.c_str(), 0, (short)query.size());
        /* Blinking cursor */
        if ((TickCount() / 30) % 2 == 0) {
            short cx = 19 + (query.empty() ? 0 : TextWidth(query.c_str(), 0, (short)query.size()));
            MoveTo(cx, kEPGridH + 11); LineTo(cx, kEPGridH + 21);
        }
    }
}

/* UpdateEmojiCell — blit GWorld cell to screen, draw selection         */
static void UpdateEmojiCell(int fi) {
    if (!sEPWin || !gEmojiGWorld) return;
    if (fi < 0 || fi >= (int)sEPFiltered.size()) return;
    int row = fi / kEPCols - sEPScrollRow;
    int col = fi % kEPCols;
    if (row < 0 || row >= kEPVisR) return;
    SetPort(sEPWin);
    PixMapHandle dstPM; BitMap* dstBM = acquire_port_bitmap((GrafPtr)sEPWin, &dstPM);
    BitMap* srcBM = (BitMap*)(*GetGWorldPixMap(gEmojiGWorld));

    Rect blitR = CellRect(kEmojiGrid, row, col);
    InsetRect(&blitR, -1, -1);
    if (sEPGridGW && dstBM) {
        PixMapHandle gwPM = GetGWorldPixMap(sEPGridGW);
        CopyBits((BitMap*)(*gwPM), dstBM, &blitR, &blitR, srcCopy, nil);
    } else {
        EraseRect(&blitR);
        if (srcBM && dstBM) {
            int ti = sEPFiltered[fi];
            short sx = EMOJI_TABLE[ti].x, sy = EMOJI_TABLE[ti].y;
            Rect srcR = EmojiSrcRect(sx, sy);
            Rect dstR = TileRect(kEmojiGrid, row, col);
            CopyBits(srcBM, dstBM, &srcR, &dstR, srcCopy, nil);
        }
    }

    bool isSel = (fi == sEPSelIdx);
    if (isSel) {
        Rect hr = SelFrameRect(kEmojiGrid, row, col);
        PaintSelBg(hr);
        if (srcBM && dstBM) {
            int ti = sEPFiltered[fi];
            short sx = EMOJI_TABLE[ti].x, sy = EMOJI_TABLE[ti].y;
            Rect srcR = EmojiSrcRect(sx, sy);
            Rect dstR = TileRect(kEmojiGrid, row, col);
            CopyBits(srcBM, dstBM, &srcR, &dstR, notSrcCopy, nil);
        }
    }

    if (dstPM) UnlockPixels(dstPM);
}

static void DrawEmojiPickerGrid(int hoverIdx) {
    if (!sEPWin || !gEmojiGWorld) return;
    SetPort(sEPWin);

    int total = (int)sEPFiltered.size();
    PixMapHandle dstPM; BitMap* dstBM = acquire_port_bitmap((GrafPtr)sEPWin, &dstPM);
    BitMap* srcBM = (BitMap*)(*GetGWorldPixMap(gEmojiGWorld));

    if (sEPGridGW) {
        PixMapHandle gwPM  = GetGWorldPixMap(sEPGridGW);
        char*  gwBase = GetPixBaseAddr(gwPM);
        short  gwRB   = (**gwPM).rowBytes & 0x3FFF;
        PixMapHandle epPM  = GetGWorldPixMap(gEmojiGWorld);
        char*  epBase = GetPixBaseAddr(epPM);
        short  epRB   = (**epPM).rowBytes & 0x3FFF;

        memset(gwBase, 0, (long)gwRB * kEPGridH);

        for (int dr = 0; dr < kEPVisR; dr++) {
            for (int dc = 0; dc < kEPCols; dc++) {
                int displayIdx = (dr + sEPScrollRow) * kEPCols + dc;
                if (displayIdx >= total) break;
                int ti = sEPFiltered[displayIdx];
                short dx, dy; TileDestXY(kEmojiGrid, dr, dc, &dx, &dy);
                blitSpriteCellToGW(gwBase, gwRB, epBase, epRB,
                    EMOJI_TABLE[ti].x, EMOJI_TABLE[ti].y, dx, dy);
            }
        }

        /* PaintSelBg overhangs the cell by ~2px; the gridR copy below stops at
           the grid edge, so a selected cell in the last column/row would leave a
           ghost sliver in the scrollbar gap / bottom margin as you scroll.
           Clear those strips first. */
        { Rect r; SetRect(&r, kEPGridW, 0, kEPGridW + 2, kEPGridH + 2); EraseRect(&r);
          SetRect(&r, 0, kEPGridH, kEPGridW + 2, kEPGridH + 2); EraseRect(&r); }
        Rect gridR; SetRect(&gridR, 0, 0, kEPGridW, kEPGridH);
        if (dstBM) CopyBits((BitMap*)(*gwPM), dstBM, &gridR, &gridR, srcCopy, nil);
    } else if (srcBM && dstBM) {
        /* Fallback: direct to window — erase first since we draw individual cells */
        { Rect gr; SetRect(&gr, 0, 0, kEPGridW + 4, kEPGridH + 4); EraseRect(&gr); }
        for (int dr = 0; dr < kEPVisR; dr++) {
            for (int dc = 0; dc < kEPCols; dc++) {
                int displayIdx = (dr + sEPScrollRow) * kEPCols + dc;
                if (displayIdx >= total) break;
                int ti = sEPFiltered[displayIdx];
                short sx = EMOJI_TABLE[ti].x, sy = EMOJI_TABLE[ti].y;
                Rect srcR = EmojiSrcRect(sx, sy);
                Rect dstR = TileRect(kEmojiGrid, dr, dc);
                CopyBits(srcBM, dstBM, &srcR, &dstR, srcCopy, nil);
            }
        }
    }

    /* "No results" text on screen */
    if (total == 0) {
        TextFont(gFontHelvetica); TextFace(0); TextSize(10);
        const char* msg = "No results";
        MoveTo((kEPGridW - TextWidth(msg, 0, 10)) / 2, kEPGridH / 2);
        DrawText(msg, 0, 10); TextSize(12);
    }

    /* Selection + hover on screen */
    auto drawFrame = [&](int fi, bool isHover) {
        if (fi < 0 || fi >= total) return;
        int row = fi / kEPCols - sEPScrollRow, col = fi % kEPCols;
        if (row < 0 || row >= kEPVisR) return;
        Rect hr = SelFrameRect(kEmojiGrid, row, col);
        if (!isHover) {
            PaintSelBg(hr);
            int ti = sEPFiltered[fi];
            short sx = EMOJI_TABLE[ti].x, sy = EMOJI_TABLE[ti].y;
            Rect srcR = EmojiSrcRect(sx, sy);
            Rect dstR = TileRect(kEmojiGrid, row, col);
            if (srcBM && dstBM) CopyBits(srcBM, dstBM, &srcR, &dstR, notSrcCopy, nil);
        } else {
            Rect br = hr; InsetRect(&br, -2, -2);
            PenSize(2, 2); FrameRoundRect(&br, 8, 8); PenSize(1, 1);
        }
    };
    drawFrame(sEPSelIdx, false);
    if (hoverIdx >= 0 && hoverIdx != sEPSelIdx) drawFrame(hoverIdx, true);

    if (dstPM) UnlockPixels(dstPM);
    DrawEmojiSearchBar(sEPQuery, hoverIdx);
}

/* Insert the emoji at EMOJI_TABLE[tableIdx] at the active TE cursor */
static void InsertPickedEmoji(int tableIdx) {
    if (!textActive || tableIdx < 0 || tableIdx >= EMOJI_COUNT) return;
    if (!AllowEdit(blockSelect, kEditText)) return;
    Block& b = pageElm[blockSelect];
    SnapSelectionForEdit(b, textActive, 0);   /* never into the middle of a mention */

    /* No room for another emoji (text or emoji slots): refuse rather than
       insert a sentinel with no sequence — unless it replaces one. */
    short selStart = (*textActive)->selStart, selEnd = (*textActive)->selEnd;
    int replaced = CountSentinels(*(*textActive)->hText, selStart, selEnd);
    if (replaced == 0 && (b.emojiCount >= kMaxEmoji || b.textLen >= kMaxTextLen - 1)) {
        FlashTooLong();
        return;
    }

    if (selStart != selEnd) TEDelete(textActive);
    const char kIns[3] = { kEmojiSentinel, ' ', ' ' };
    TEInsert(kIns, 3, textActive);

    /* The text, its runs (the emoji joins the run before it) and the emoji
       sequences all follow the edit; without the runs they no longer summed
       to the text and the save stripped every style, link and mention. */
    char seq[1][32];
    strncpy(seq[0], EMOJI_TABLE[tableIdx].utf8, 31);
    seq[0][31] = '\0';
    SyncBlockTextKeepRuns(b, textActive, seq, 1);
    b.runsDirty = true;
    b.teCachedWidth = 0; b.teCachedType = BT_UNKNOWN;
    b.dirty = true; MarkDirty();

    SetPort(gWindow);
    {
      static char teBuf2[kTEBufLen]; short teLen2 = 0;
      TextToTE(b.text, b.textLen, teBuf2, sizeof(teBuf2), teLen2);
      DrawBlockEmojis(textActive, teBuf2, teLen2, b);
    }
}

/* ------------------------------------------------------------------ */
/* ShowEmojiPicker — thin wrapper around runPicker                     */
/* ------------------------------------------------------------------ */
static int sEPResultIdx = -1;

static void EmojiPickerOnCommit(int filtIdx) {
    if (filtIdx >= 0 && filtIdx < (int)sEPFiltered.size())
        sEPResultIdx = sEPFiltered[filtIdx];
}
static void EmojiPickerOnHover(int oldIdx, int newIdx) {
    int prevSel = sEPSelIdx;
    if (newIdx >= 0) sEPSelIdx = newIdx;
    UpdateEmojiCell(prevSel);
    UpdateEmojiCell(oldIdx);
    UpdateEmojiCell(newIdx);
}
static void EmojiPickerDrawBar(int hoverIdx) {
    DrawEmojiSearchBar(sEPQuery, hoverIdx);
}

void ShowEmojiPicker() {
    if (!textActive) {
        doAddBlock(0); /* add paragraph, focuses it */
        if (!textActive) return;
    }
    LoadEmojiSprite();
    if (!gEmojiGWorld) return;

    sEPQuery.clear();
    sEPSearchActive = false;
    sEPResultIdx    = -1;
    BuildEmojiFilter();
    sEPScrollRow = 0; sEPSelIdx = 0;

    /* Offscreen back-buffer for the emoji grid */
    { Rect gwr; SetRect(&gwr, 0, 0, kEPGridW, kEPGridH);
      NewGWorld(&sEPGridGW, 1, &gwr, nil, nil, 0);
      if (sEPGridGW) LockPixels(GetGWorldPixMap(sEPGridGW)); }

    static const PickerConfig kCfg = {
        kEPCols, kEPVisR, kEPGridW, kEPGridH, kEPWinW, kEPWinH, kEPSBGap, kEPInset,
        false, true, true,
        "\pSelect Emoji",
        &sEPScrollRow, &sEPSelIdx, &sEPQuery, &sEPSearchActive,
        &sEPWin, &sEPSB,
        [](){ return (int)sEPFiltered.size(); },
        BuildEmojiFilter,
        DrawEmojiPickerGrid,
        EmojiPickerDrawBar,
        EmojiPickerOnCommit,
        NULL,
        EmojiPickerOnHover,
        NULL,
    };
    runPicker(kCfg);

    if (sEPGridGW) {
        UnlockPixels(GetGWorldPixMap(sEPGridGW));
        DisposeGWorld(sEPGridGW); sEPGridGW = nil;
    }

    CalcVis(gWindow);
    sEPQuery.clear(); sEPFiltered.clear();
    SetPort(gWindow);
    gFullEraseOnce = true;  /* clear chrome the screen-centered picker ghosted */

    if (sEPResultIdx >= 0) {
        InsertPickedEmoji(sEPResultIdx);
        /* Single updateBlocks() restores the main window and renders the new emoji */
        short sel = (*textActive)->selStart;
        updateBlocks();
        textActive = pageElm[blockSelect].textEdit;
        if (textActive) {
            TEActivate(textActive);
            TESetSelect(sel, sel, textActive);
        }
    } else {
        updateBlocks();
    }
    ValidRect(&gWindow->portRect);
}
