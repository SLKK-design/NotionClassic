/* helpers.cpp — implementations of shared drawing, dialog, and string utilities */
#include "helpers.h"
#include <Memory.h>

/* ------------------------------------------------------------------ */
/* DrawArrowTriangle — pixel-exact ▼/▲ arrow.                        */
/* pointUp=false: widest row at cy, tip at cy+height-1.              */
/* pointUp=true:  tip at cy, widest row at cy+height-1.              */
/* half-base = height-1; FillPoly misses outermost edge pixels.      */
/* ------------------------------------------------------------------ */
void DrawArrowTriangle(short cx, short cy, bool pointUp,
                       short height, const Pattern* fill)
{
    PenPat(fill); PenSize(1, 1);
    short halfBase = (short)(height - 1);
    for (short r = 0; r < height; r++) {
        short half = pointUp ? r : (short)(halfBase - r);
        MoveTo((short)(cx - half), (short)(cy + r));
        LineTo((short)(cx + half), (short)(cy + r));
    }
    PenNormal();
}

/* ------------------------------------------------------------------ */
/* DrawDisclosureTriangle — pixel-exact disclosure triangle.          */
/* Closed ▶: left edge flat, 4 wide × 7 tall, tip at (cx, cy).      */
/* Open   ▼: top edge flat, 7 wide × 4 tall, tip at (cx, cy).       */
/* outline parameter kept for API compatibility (ignored).            */
/* ------------------------------------------------------------------ */
void DrawDisclosureTriangle(short cx, short cy, bool open,
                            const Pattern* fill, bool /*outline*/)
{
    PenPat(fill); PenSize(1, 1);
    if (!open) {
        /* ▶ right-pointing: rows dy=-3..+3, width 1,2,3,4,3,2,1 */
        for (short dy = -3; dy <= 3; dy++) {
            short w = (short)(4 - (dy < 0 ? -dy : dy));
            MoveTo((short)(cx - 3),         (short)(cy + dy));
            LineTo((short)(cx - 3 + w - 1), (short)(cy + dy));
        }
    } else {
        /* ▼ down-pointing: rows dy=-3..0, widths 7,5,3,1 */
        for (short dy = -3; dy <= 0; dy++) {
            short half = (short)(-dy);
            MoveTo((short)(cx - half), (short)(cy + dy));
            LineTo((short)(cx + half), (short)(cy + dy));
        }
    }
    PenNormal();
}

/* ------------------------------------------------------------------ */
/* DrawMagnifyingGlass                                                 */
/* ------------------------------------------------------------------ */
void DrawMagnifyingGlass(short gy, short gx)
{
    Rect lens;
    SetRect(&lens, gx, gy, gx + 8, gy + 8);
    FrameOval(&lens);
    MoveTo(gx + 7, gy + 7);
    LineTo(gx + 10, gy + 9);
}

/* ------------------------------------------------------------------ */
/* DrawPlusCross — 2px-wide + cross used by sidebar and library       */
/* cx,cy = visual centre; fill sets the pen pattern.                  */
/* Offsets: horizontal cx±3 at cy-1, vertical cy-4..cy+2 at cx.      */
/* ------------------------------------------------------------------ */
void DrawPlusCross(short cx, short cy, const Pattern* fill)
{
    PenSize(2, 2);
    PenPat(fill);
    MoveTo((short)(cx - 3), (short)(cy - 1)); LineTo((short)(cx + 3), (short)(cy - 1));
    MoveTo(cx,               (short)(cy - 4)); LineTo(cx,               (short)(cy + 2));
    PenNormal();
}

/* ------------------------------------------------------------------ */
/* DrawSICN — blit a cached 16×16 SICN resource with srcOr.          */
/* ------------------------------------------------------------------ */
void DrawSICN(short resID, short x, short y)
{
    enum { kMaxCached = 4 };
    static struct { short id; Handle h; } cache[kMaxCached];
    static int cacheCount = 0;

    Handle sh = nil;
    for (int i = 0; i < cacheCount; i++)
        if (cache[i].id == resID) { sh = cache[i].h; break; }
    if (!sh) {
        sh = GetResource('SICN', resID);
        if (!sh) return;
        HNoPurge(sh);
        if (cacheCount < kMaxCached)
            cache[cacheCount++] = { resID, sh };
    }
    HLock(sh);
    BitMap bm;
    bm.baseAddr = *sh;
    bm.rowBytes = 2;
    SetRect(&bm.bounds, 0, 0, 16, 16);
    Rect dstR; SetRect(&dstR, x, y, (short)(x + 16), (short)(y + 16));
    GrafPtr port; GetPort(&port);
    CopyBits(&bm, &port->portBits, &bm.bounds, &dstR, srcOr, nil);
    HUnlock(sh);
}

void DrawPageIcon(short x, short y)   { DrawSICN(200, x, y); }
void DrawCheckedBox(short x, short y) { DrawSICN(201, x, y); }

/* ------------------------------------------------------------------ */
/* SetGenevaFont                                                       */
/* ------------------------------------------------------------------ */
void SetGenevaFont(short size, short face)
{
    static SInt16 sFontID = 0;
    if (!sFontID) GetFNum((ConstStr255Param)"\pGeneva", &sFontID);
    TextFont(sFontID);
    TextSize(size);
    TextFace(face);
}

/* ------------------------------------------------------------------ */
/* Dialog helpers                                                      */
/* ------------------------------------------------------------------ */
ControlHandle GetDialogControl(DialogPtr dlg, short itemID)
{
    short t; Handle h; Rect r;
    GetDialogItem(dlg, itemID, &t, &h, &r);
    return (ControlHandle)h;
}

void GetDialogItemRect(DialogPtr dlg, short itemID, Rect& r)
{
    short t; Handle h;
    GetDialogItem(dlg, itemID, &t, &h, &r);
}

void DrawDefaultButtonRing(DialogPtr dlg, short itemID)
{
    Rect r;
    GetDialogItemRect(dlg, itemID, r);
    SetPort(dlg);
    InsetRect(&r, -4, -4);
    PenSize(3, 3);
    bool disabled = ((**GetDialogControl(dlg, itemID)).contrlHilite == 255);
    PenPat(disabled ? &qd.ltGray : &qd.black);
    FrameRoundRect(&r, 16, 16);
    PenNormal();
}

void EnableDialogButton(DialogPtr dlg, short itemID)
{
    HiliteControl(GetDialogControl(dlg, itemID), 0);
}

void DisableDialogButton(DialogPtr dlg, short itemID)
{
    HiliteControl(GetDialogControl(dlg, itemID), 255);
}

/* ------------------------------------------------------------------ */
/* DrawProgressBar                                                     */
/* ------------------------------------------------------------------ */
void DrawProgressBar(GrafPtr port, Rect r, short filledPx)
{
    SetPort(port);
    FillRect(&r, &qd.ltGray);
    if (filledPx > 0) {
        Rect filled = r;
        filled.right = (short)(r.left + filledPx);
        FillRect(&filled, &qd.black);
    }
}

/* ------------------------------------------------------------------ */
/* DrawRoundButton                                                     */
/* ------------------------------------------------------------------ */
void DrawRoundButton(GrafPtr port, Rect r, ConstStr255Param label)
{
    SetPort(port);
    /* Erase interior (leave 2 px at bottom for progress bar) */
    Rect inner;
    SetRect(&inner, r.left + 1, r.top + 1, r.right - 1, r.bottom - 2);
    EraseRect(&inner);
    /* 1 px border: top + left/right with rounded top corners           */
    PenNormal();
    MoveTo(r.left + 1,  r.top);      LineTo(r.right - 2, r.top);      /* top   */
    MoveTo(r.left,      r.top + 1);  LineTo(r.left,      r.bottom - 3); /* left  */
    MoveTo(r.right - 1, r.top + 1);  LineTo(r.right - 1, r.bottom - 3); /* right */
    /* Label centred in the button area */
    TextFont(0); TextFace(0); TextSize(0);
    short tw = StringWidth(label);
    short tx = r.left + (r.right - r.left - tw) / 2;
    if (tx < r.left) tx = r.left;
    MoveTo(tx, (short)(r.top + (r.bottom - r.top) / 2 + 4));
    DrawString(label);
}

/* ------------------------------------------------------------------ */
/* DrawCenteredText                                                    */
/* ------------------------------------------------------------------ */
void DrawCenteredText(GrafPtr port, Rect bounds, const char* msg, short lineW)
{
    if (!msg || !msg[0]) return;
    SetPort(port);
    ClipRect(&bounds);
    SetGenevaFont(12);
    const short lineH  = 14;
    short rectW = bounds.right  - bounds.left;
    short y     = bounds.top + (bounds.bottom - bounds.top) / 3;
    short len   = (short)strlen(msg);
    short i = 0;
    while (i < len) {
        short end = (short)(i + lineW);
        if (end >= len) {
            end = len;
        } else {
            /* soft-wrap at last space before lineW */
            short sp = end;
            while (sp > i && msg[sp] != ' ') sp--;
            if (sp > i) end = sp;
        }
        short linePixW = TextWidth(msg, i, end - i);
        short x = bounds.left + (rectW - linePixW) / 2;
        if (x < bounds.left + 4) x = bounds.left + 4;
        MoveTo(x, y);
        DrawText(msg, i, end - i);
        i = end;
        if (i < len && msg[i] == ' ') i++; /* skip wrapped space */
        y += lineH;
    }
    /* Restore clip to full port */
    Rect portRect = port->portRect;
    ClipRect(&portRect);
}

/* ------------------------------------------------------------------ */
/* PasteCleanIntoTE                                                    */
/* ------------------------------------------------------------------ */
void PasteCleanIntoTE(TEHandle te)
{
    Handle scrapH = NewHandle(0);
    long offset;
    long len = GetScrap(scrapH, 'TEXT', &offset);
    if (len > 0) {
        HLock(scrapH);
        /* Compact in place — write index never overtakes read index since we
           only ever drop chars. TEInsert copies the bytes, so the locked
           handle can be reused as the source buffer (no second allocation). */
        char* p = *scrapH;
        long  cleanLen = 0;
        for (long i = 0; i < len; i++) {
            char ch = p[i];
            if (ch != '\r' && ch != '\n' && ch != '\t')
                p[cleanLen++] = ch;
        }
        TESetSelect(0, 32767, te); TEDelete(te);
        if (cleanLen > 0) TEInsert(p, cleanLen, te);
        HUnlock(scrapH);
    }
    DisposeHandle(scrapH);
}

/* ------------------------------------------------------------------ */
/* SafeStrCopy                                                         */
/* ------------------------------------------------------------------ */
void SafeStrCopy(char* dst, int dstLen, const char* src)
{
    if (!dst || dstLen <= 0) return;
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, (size_t)(dstLen - 1));
    dst[dstLen - 1] = '\0';
}

/* ------------------------------------------------------------------ */
/* CStrToPStr / PStrToCStr                                            */
/* ------------------------------------------------------------------ */
void CStrToPStr(const char* cstr, unsigned char* pstr)
{
    int len = cstr ? (int)strlen(cstr) : 0;
    if (len > 254) len = 254;
    pstr[0] = (unsigned char)len;
    if (len) memcpy(pstr + 1, cstr, len);
}

void PStrToCStr(const unsigned char* pstr, char* cstr, int maxLen)
{
    int len = (int)pstr[0];
    if (len >= maxLen) len = maxLen - 1;
    memcpy(cstr, pstr + 1, len);
    cstr[len] = '\0';
}

/* ------------------------------------------------------------------ */
/* ScrollPartDelta / ClampScrollBar                                    */
/* ------------------------------------------------------------------ */
short ScrollPartDelta(short part, short lineAmt, short pageAmt)
{
    switch (part) {
        case 20: return -lineAmt;   /* inUpButton   */
        case 21: return  lineAmt;   /* inDownButton */
        case 22: return -pageAmt;   /* inPageUp     */
        case 23: return  pageAmt;   /* inPageDown   */
    }
    return 0;
}

static void ClampScrollBar(ControlHandle bar, short maxVal, short& offset)
{
    if (maxVal < 0) maxVal = 0;
    if (offset > maxVal) offset = maxVal;
    if (offset < 0) offset = 0;
    SetControlMaximum(bar, maxVal);
    SetControlValue(bar, offset);
}

ControlHandle MakeScrollBar(GrafPtr port, const Rect& r,
                            short val, short minVal, short maxVal)
{
    return NewControl(port, &r, "\p", true, val, minVal, maxVal,
                      16 /* scrollBarProc */, 0L);
}

short TrackScrollBar(ControlHandle bar, Point lp, short part,
                     ControlActionUPP actionUPP)
{
    if (part == kControlIndicatorPart)
        TrackControl(bar, lp, nil);
    else if (part)
        TrackControl(bar, lp, actionUPP);
    return GetControlValue(bar);
}

/* ------------------------------------------------------------------ */
/* Dialog event helpers                                                */
/* ------------------------------------------------------------------ */
void SetUserItems(DialogPtr dlg, const short* items, int count, UserItemUPP drawUPP)
{
    for (int i = 0; i < count; i++) {
        Rect r;
        GetDialogItemRect(dlg, items[i], r);
        SetDialogItem(dlg, items[i], userItem, (Handle)drawUPP, &r);
    }
}

/* DisableDialogMenus / RestoreDialogMenus / HandleDialogMenuBar moved to
   app/ui/menu.cpp — they encode the app's specific menu layout, which has no
   place in this app-agnostic helper TU. */

bool HandleDialogDrag(DialogPtr dlg, EventRecord& e, WindowPtr hitWin)
{
    if (hitWin != (WindowPtr)dlg) return false;
    Rect sb = qd.screenBits.bounds;
    DragWindow((WindowPtr)dlg, e.where, &sb);
    return true;
}

/* ------------------------------------------------------------------ */
/* addHyphensToId                                                       */
/* ------------------------------------------------------------------ */
void addHyphensToId(const char* src, char* out, int outLen)
{
    if (outLen < 37) { if (outLen > 0) out[0] = '\0'; return; }
    int len = (int)strlen(src);
    if (len != 32) {
        /* already hyphenated or unexpected length — copy as-is */
        strncpy(out, src, (size_t)(outLen - 1)); out[outLen - 1] = '\0'; return;
    }
    /* 8-4-4-4-12 */
    memcpy(out,      src,      8); out[8]  = '-';
    memcpy(out + 9,  src + 8,  4); out[13] = '-';
    memcpy(out + 14, src + 12, 4); out[18] = '-';
    memcpy(out + 19, src + 16, 4); out[23] = '-';
    memcpy(out + 24, src + 20, 12); out[36] = '\0';
}
