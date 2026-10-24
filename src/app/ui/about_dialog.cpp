/* about_dialog.cpp — About box (window 128). */
#include <Windows.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <Events.h>

#include "about_dialog.h"
#include "picker.h"   /* load_pict_handle */

static WindowRef sAboutBox = nil;

void ShowAboutBox() {
    sAboutBox = GetNewWindow(128, NULL, (WindowPtr)-1);
    if (sAboutBox == nil) return;
    MoveWindow(sAboutBox,
               qd.screenBits.bounds.right  / 2 - sAboutBox->portRect.right  / 2,
               qd.screenBits.bounds.bottom / 2 - sAboutBox->portRect.bottom / 2,
               false);
    ShowWindow(sAboutBox);
    SetPort(sAboutBox);

    /* Draw NotionClassic.pict centered horizontally, 32px from top */
    {
        Handle ph = load_pict_handle((ConstStr255Param)"\pNotionClassic.pict");
        if (ph) {
            HUnlock(ph);
            PicHandle pic = (PicHandle)ph;
            Rect pf = (*pic)->picFrame;
            short pw = pf.right - pf.left, ph2 = pf.bottom - pf.top;
            short cx = (sAboutBox->portRect.right - pw) / 2;
            Rect dst = { 28, cx, (short)(28 + ph2), (short)(cx + pw) };
            DrawPicture(pic, &dst);
            DisposeHandle(ph);
        }
    }

    /* Draw hey.pict at bottom-right */
    {
        Handle ph = load_pict_handle((ConstStr255Param)"\phey.pict");
        if (ph) {
            HUnlock(ph);
            PicHandle pic = (PicHandle)ph;
            Rect pf = (*pic)->picFrame;
            short pw = pf.right - pf.left, ph2 = pf.bottom - pf.top;
            short r2 = sAboutBox->portRect.right - 12;
            short b2 = sAboutBox->portRect.bottom;
            Rect dst = { (short)(b2 - ph2), (short)(r2 - pw), b2, r2 };
            DrawPicture(pic, &dst);
            DisposeHandle(ph);
        }
    }

    {
        TextFont(3); TextSize(10);
        FontInfo fi;
        GetFontInfo(&fi);
        short lineH = fi.ascent + fi.descent + fi.leading;

        short x = 30;
        short yBase = sAboutBox->portRect.bottom - 20 - 88 + fi.ascent;

        MoveTo(x, yBase);
        DrawString("\pVersion 2.0");
        yBase += lineH;
        yBase += lineH;

        MoveTo(x, yBase);
        DrawString("\pFor Macintosh System 7 - 9");
        yBase += lineH;

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
        yBase += lineH;

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
    DisposeWindow(sAboutBox);
    sAboutBox = nil;
}
