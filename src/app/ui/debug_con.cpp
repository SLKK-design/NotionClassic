/* debug_con.cpp — ring-buffer debug console window.
   Self-contained: no app globals needed from main.cpp or Settings.cpp. */

#include "debug_con.h"
#include "helpers.h"
#include <Windows.h>
#include <Controls.h>
#include <Fonts.h>
#include <Quickdraw.h>
#include <TextUtils.h>
#include <Scrap.h>
#include <Memory.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Globals                                                              */
/* ------------------------------------------------------------------ */
WindowPtr               gDebugWin       = nil;   /* extern in debug_con.h */
static ControlHandle    gConScrollBar = nil;
static ControlActionUPP gConScrollUPP = nil;

static const int kConLines = 200;
static char gConBuf[kConLines][80];
static int  gConNext      = 0;  /* next slot to write into */
static int  gConFill      = 0;  /* lines written so far    */
static int  gConScrollTop = 0;  /* first visible line      */
static long gConStartTick = 0;  /* TickCount at first log  */
static bool gConDirty     = false;  /* deferred redraw flag */

/* ------------------------------------------------------------------ */
/* Private helpers                                                      */
/* ------------------------------------------------------------------ */
static short ConLineH()
{
    static short cached = 0;
    if (cached) return cached;
    TextFont(kFontIDMonaco); TextFace(0); TextSize(9);
    FontInfo fi; GetFontInfo(&fi);
    cached = fi.ascent + fi.descent + fi.leading + 1;
    return cached;
}

static int ConVisLines()
{
    if (!gDebugWin) return 1;
    return (gDebugWin->portRect.bottom - gDebugWin->portRect.top) / ConLineH();
}

static void ConUpdateScrollBar()
{
    if (!gConScrollBar) return;
    int total  = (gConFill < kConLines) ? gConFill : kConLines;
    int maxVal = total - ConVisLines();
    if (maxVal < 0) maxVal = 0;
    if (gConScrollTop > maxVal) gConScrollTop = maxVal;
    SetControlMaximum(gConScrollBar, (short)maxVal);
    SetControlValue(gConScrollBar,   (short)gConScrollTop);
}

static pascal void ConScrollAction(ControlHandle, short part)
{
    int vis    = ConVisLines();
    int total  = (gConFill < kConLines) ? gConFill : kConLines;
    int maxVal = total - vis; if (maxVal < 0) maxVal = 0;
    int delta  = ScrollPartDelta(part, 1, (short)vis);
    gConScrollTop += delta;
    if (gConScrollTop < 0)      gConScrollTop = 0;
    if (gConScrollTop > maxVal) gConScrollTop = maxVal;
    DrawDebugWin();
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */
void DrawDebugWin()
{
    if (!gDebugWin) return;
    GrafPtr savedPort; GetPort(&savedPort);
    SetPort(gDebugWin);
    Rect cr = gDebugWin->portRect; cr.right -= 15;
    EraseRect(&cr);
    short    lineH    = ConLineH();
    FontInfo fi;      GetFontInfo(&fi);
    int      total    = (gConFill < kConLines) ? gConFill : kConLines;
    int      startBuf = (gConFill < kConLines) ? 0 : gConNext;
    int      vis      = ConVisLines();
    short    y        = fi.ascent + 2;
    for (int i = gConScrollTop; i < total && i < gConScrollTop + vis; i++) {
        int   idx = (startBuf + i) % kConLines;
        short len = (short)strlen(gConBuf[idx]);
        MoveTo(4, y);
        DrawText(gConBuf[idx], 0, len);
        y += lineH;
    }
    ConUpdateScrollBar();
    DrawControls(gDebugWin);
    SetPort(savedPort);
}

/* Log one line: "NNNNKfree T+NNN.Ns func         msg" */
void DebugLog(const char* func, const char* msg)
{
    long now = TickCount();
    if (gConStartTick == 0) gConStartTick = now;
    long elapsed10 = (now - gConStartTick) * 10L / 60L;   /* tenths of a second */
    long freeK     = FreeMem() / 1024L;
    char line[80];
    snprintf(line, sizeof(line), "%4ldK %4ld.%lds %-11.11s %s",
             freeK, elapsed10 / 10L, elapsed10 % 10L, func, msg);
    line[79] = '\0';
    strncpy(gConBuf[gConNext], line, 79);
    gConBuf[gConNext][79] = '\0';
    gConNext = (gConNext + 1) % kConLines;
    if (gConFill < kConLines) gConFill++;
    if (gDebugWin) gConDirty = true;
}

void FlushDebugLog()
{
    if (!gConDirty || !gDebugWin) return;
    gConDirty = false;
    GrafPtr savedPort; GetPort(&savedPort);
    SetPort(gDebugWin);
    int total  = (gConFill < kConLines) ? gConFill : kConLines;
    int newTop = total - ConVisLines();
    if (newTop < 0) newTop = 0;
    gConScrollTop = newTop;
    DrawDebugWin();
    SetPort(savedPort);
}

void CopyDebugToClipboard()
{
    int total = (gConFill < kConLines) ? gConFill : kConLines;
    if (total == 0) return;
    int    startBuf = (gConFill < kConLines) ? 0 : gConNext;
    Handle h        = NewHandle((Size)(total * 80));
    if (!h) return;
    HLock(h);
    char *dst  = *h;
    long  used = 0;
    for (int i = 0; i < total; i++) {
        int   idx = (startBuf + i) % kConLines;
        short len = (short)strlen(gConBuf[idx]);
        memcpy(dst + used, gConBuf[idx], len);
        used += len;
        dst[used++] = '\r';
    }
    HUnlock(h);
    SetHandleSize(h, used);
    ZeroScrap();
    HLock(h); PutScrap(used, 'TEXT', *h); HUnlock(h);
    DisposeHandle(h);
}

void ToggleDebugWin()
{
    if (gDebugWin) {
        if (gConScrollUPP) { DisposeControlActionUPP(gConScrollUPP); gConScrollUPP = nil; }
        DisposeWindow(gDebugWin); gDebugWin = nil; gConScrollBar = nil;
    } else {
        short scrRight = qd.screenBits.bounds.right;
        short scrBot   = qd.screenBits.bounds.bottom;
        short conW     = 300;
        Rect r; SetRect(&r, scrRight - conW, 44, scrRight - 2, scrBot - 2);
        gDebugWin = NewWindow(nil, &r, "\pDebug Console", true,
                            noGrowDocProc, (WindowPtr)-1, true, 0L);
        SetPort(gDebugWin);
        Rect sb; SetRect(&sb,
            gDebugWin->portRect.right - 15, gDebugWin->portRect.top  - 1,
            gDebugWin->portRect.right +  1, gDebugWin->portRect.bottom + 1);
        gConScrollBar = MakeScrollBar(gDebugWin, sb);
        gConScrollUPP = NewControlActionUPP(ConScrollAction);
        gConFill = 0; gConNext = 0; gConScrollTop = 0;
        DrawDebugWin();
        SelectWindow(gDebugWin);
    }
}

void HandleDebugClick(WindowPtr win, Point lp)
{
    if (!gDebugWin || win != gDebugWin) return;
    SetPort(gDebugWin);
    ControlHandle ctl; short part = FindControl(lp, gDebugWin, &ctl);
    if (ctl == gConScrollBar && part) {
        gConScrollTop = TrackScrollBar(gConScrollBar, lp, part, gConScrollUPP);
        if (part == kControlIndicatorPart) DrawDebugWin();
    }
}

/* ------------------------------------------------------------------ */
/* C-callable bridge so fast_alloc.c can write to the debug console.  */
/* ------------------------------------------------------------------ */
extern "C" void fast_alloc_log(const char* func, const char* msg) {
    DebugLog(func, msg);
}

/* C-callable bridge for uecc_alt.c timing diagnostics. */
extern "C" void uecc_con_log(const char* func, const char* msg) {
    DebugLog(func, msg);
}
