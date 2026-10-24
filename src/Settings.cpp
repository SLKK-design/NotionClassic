/* Settings.cpp — Settings/picker dialogs.
   Library window lives in ui/library_window.cpp.
   Extracted prefs file I/O lives in prefs_io.cpp.                    */
#include "Settings.h"
#include "library_window.h"
#include "helpers.h"
#include "menu.h"   /* Disable/RestoreDialogMenus, HandleDialogMenuBar */
#include "picker.h"
#include "page_fetch.h"
#include "debug_con.h"

#include <Dialogs.h>
#include <Windows.h>
#include <TextEdit.h>
#include <Scrap.h>
#include <Events.h>
#include <Fonts.h>
#include <Lists.h>
#include <QDOffscreen.h>
#include <string.h>

extern "C" {
#include "emoji_sprite.h"
}

/* SetDefaultTextStyle — apply the default text attributes after custom drawing */
static void SetDefaultTextStyle() { TextFont(0); TextSize(12); TextFace(0); }

/* ------------------------------------------------------------------ */
/* Helper: C string <-> Pascal string                                 */
/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* ExtractPageID — take everything after the last '-', which is the   */
/* Notion page ID in any URL or slug.  No '-' → leave as-is.         */
/* ------------------------------------------------------------------ */
static void ExtractPageID(char *buf, int bufLen)
{
    char *last = strrchr(buf, '-');
    if (last && *(last + 1) != '\0') {
        SafeStrCopy(buf, bufLen, last + 1);
    }
}

/* ------------------------------------------------------------------ */
/* Dialog-session state shared between the draw proc and event loops  */
/* ------------------------------------------------------------------ */
static int           sCurrentPanel   = 0;    /* 1 = token panel, 2 = page panel */
static char          sTokenErrMsg[80]  = {0}; /* message shown in item 3 of token panel */
static char          sTokenHintMsg[80] = {0}; /* message shown in item 5 when error set */
static bool          sTokenMsgBold     = false; /* true for errors, false for status */
static ListHandle    sPageListHandle = nil;  /* Panel 2 List Manager list        */
static GWorldPtr     sListEmojiGWorld = nil; /* set by main.cpp before dialog    */
static NotionPageList sPageListBuf;          /* populated in ShowPagePanel       */
static FontInfo      sPageListFI = {0};      /* cached Geneva metrics for LDEF   */

void SetListEmojiGWorld(void* gw) { sListEmojiGWorld = (GWorldPtr)gw; }

/* ------------------------------------------------------------------ */
/* DrawSettingsItem — draw proc for Panel 1 items 3 and 5,            */
/* and Panel 2 item 3 (title label).                                  */
/* ------------------------------------------------------------------ */
static pascal void DrawSettingsItem(DialogPtr dlg, short item)
{
    short  type; Handle h; Rect r;
    GetDialogItem(dlg, item, &type, &h, &r);
    SetPort(dlg);
    EraseRect(&r);

    FontInfo fi;

    switch (item) {
        case 3:
            SetGenevaFont(12, sCurrentPanel == 2 ? 0 : bold);
            GetFontInfo(&fi);
            MoveTo(r.left, r.top + fi.ascent);
            if (sCurrentPanel == 2) {
                DrawString("\pSelect a page:");
            } else if (sTokenErrMsg[0] && sTokenMsgBold) {
                /* Error: replace label with error message in bold */
                Str255 es; CStrToPStr(sTokenErrMsg, es);
                DrawString(es);
            } else {
                DrawString("\pIntegration Token");
            }
            break;

        case 5: /* Panel 1 hint — connecting status, error hint, or default */
            {
                const char* line5 = nullptr;
                if (sTokenErrMsg[0] && !sTokenMsgBold)
                    line5 = sTokenErrMsg;          /* "Connecting to Notion…" */
                else if (sTokenHintMsg[0] && sTokenMsgBold)
                    line5 = sTokenHintMsg;         /* context hint for error */
                SetGenevaFont(12, 0);
                GetFontInfo(&fi);
                MoveTo(r.left, r.top + fi.ascent);
                if (line5) {
                    Str255 es; CStrToPStr(line5, es);
                    DrawString(es);
                } else {
                    DrawString("\pnotion.so/my-integrations");
                }
            }
            break;
    }

    SetDefaultTextStyle();
}

/* InstallDialogDrawProcs — now SetUserItems from helpers.h */

/* ------------------------------------------------------------------ */
/* HandleListKeypress — process up/down arrow keys for a list.        */
/* Returns true if the key was consumed.                              */
/* ------------------------------------------------------------------ */
static bool HandleListKeypress(ListHandle lH, char c)
{
    if (!lH) return false;
    if (c != (char)0x1E && c != (char)0x1F) return false;   /* only up/down */
    Cell  cur; cur.h = 0; cur.v = 0;
    bool  hasSel = LGetSelect(true, &cur, lH);
    short rows   = (*lH)->dataBounds.bottom;
    Cell  next   = cur;
    if (c == (char)0x1F && rows > 0) {
        next.v = hasSel ? (cur.v + 1 < rows ? cur.v + 1 : cur.v) : 0;
    } else if (c == (char)0x1E && hasSel && cur.v > 0) {
        next.v = cur.v - 1;
    }
    if (hasSel) LSetSelect(false, cur, lH);
    LSetSelect(true, next, lH);
    LAutoScroll(lH);
    return true;
}

/* HandleDialogMenuBar, HandleDialogDrag — defined in helpers.h */

/* ------------------------------------------------------------------ */
/* RunDialogLoop — shared event loop for movableDBoxProc dialogs.     */
/* Returns 1 (primary button) or 2 (secondary button).               */
/* Pass a ListHandle for Panel 2 list navigation and redraw.          */
/* ------------------------------------------------------------------ */
static short RunDialogLoop(DialogPtr dlg, ListHandle listH = nil)
{
    DrawDefaultButtonRing(dlg, 1);
    DisableDialogMenus();
    short item = 0;
    while (item != 1 && item != 2) {
        EventRecord e;
        WaitNextEvent(everyEvent, &e, 6, nil);

        /* Update events — DrawDialog + LUpdate for list */
        if (e.what == updateEvt) {
            WindowPtr wp = (WindowPtr)e.message;
            BeginUpdate(wp);
            if (((WindowPeek)wp)->windowKind == dialogKind) {
                DrawDialog((DialogPtr)wp);
                if (listH && (DialogPtr)wp == dlg)
                    LUpdate(((GrafPtr)wp)->visRgn, listH);
                if ((DialogPtr)wp == dlg) DrawDefaultButtonRing(dlg, 1);
            }
            EndUpdate(wp);
            continue;
        }

        if (e.what == osEvt) {
            if (((e.message >> 24) & 0xFF) == suspendResumeMessage) {
                TEHandle te = ((DialogPeek)dlg)->textH;
                if (e.message & resumeFlag) { if (te) TEActivate(te); }
                else                        { if (te) TEDeactivate(te); }
            }
            continue;
        }
        if (e.what == activateEvt) {
            TEHandle te = ((DialogPeek)dlg)->textH;
            if (e.modifiers & activeFlag) { if (te) TEActivate(te); }
            else                          { if (te) TEDeactivate(te); }
            continue;
        }
        if (e.what == keyDown || e.what == autoKey) {
            char c = (char)(e.message & charCodeMask);
            if (c == 0x0D || c == 0x03) { item = 1; break; }

            /* Arrow-key navigation for the list */
            if ((e.modifiers & cmdKey) == 0 && HandleListKeypress(listH, c))
                continue;

            TEHandle te = ((DialogPeek)dlg)->textH;
            if (e.modifiers & cmdKey) {
                if ((c == 'v' || c == 'V') && te) {
                    PasteCleanIntoTE(te);
                    continue;
                }
                if ((c == 'a' || c == 'A') && te) { TESetSelect(0, 32767, te); continue; }
                if ((c == 'x' || c == 'X') && te) { TECut(te);  ZeroScrap(); TEToScrap(); continue; }
                if ((c == 'c' || c == 'C') && te && !(e.modifiers & shiftKey)) { TECopy(te); ZeroScrap(); TEToScrap(); continue; }
                if ((c == 'c' || c == 'C') && (e.modifiers & shiftKey)) { CopyDebugToClipboard(); continue; }
            }
        }
        if (e.what == mouseDown) {
            WindowPtr hitWin;
            short part = FindWindow(e.where, &hitWin);
            if (part == inMenuBar) {
                if (HandleDialogMenuBar(dlg, e)) { item = 2; break; }
                continue;
            }
            if (part == inDrag) { HandleDialogDrag(dlg, e, hitWin); continue; }
            if (hitWin != (WindowPtr)dlg) continue;

            /* Intercept clicks inside the list area */
            if (listH) {
                short tp; Handle hp; Rect rList;
                GetDialogItem(dlg, 4, &tp, &hp, &rList);
                Point localPt = e.where;
                SetPort(dlg);
                GlobalToLocal(&localPt);
                /* Extend hit-rect rightward to include the 15px scrollbar strip */
                Rect rListAndScroll = rList;
                if ((*listH)->vScroll) rListAndScroll.right += 15;
                if (PtInRect(localPt, &rListAndScroll)) {
                    if (LClick(localPt, e.modifiers, listH))
                        item = 1;   /* double-click = Open */
                    continue;
                }
            }
        }

        DialogSelect(&e, &dlg, &item);
    }
    RestoreDialogMenus();
    return item;
}

/* ------------------------------------------------------------------ */
/* SetDlgTitle — set a dialog's window title from a C string          */
/* ------------------------------------------------------------------ */
static void SetDlgTitle(DialogPtr dlg, const char* title)
{
    if (!title || !title[0]) return;
    Str255 pt;
    CStrToPStr(title, pt);
    SetWTitle((WindowPtr)dlg, pt);
}

/* ------------------------------------------------------------------ */
/* ShowTokenPanel — Panel 1: enter / confirm the integration token.
   If fetchPages is non-null, validates the token with a live fetch
   while keeping the dialog visible ("Connecting to Notion…").
   On failure the dialog stays open with an error title and hint.
   Returns true (validated Continue) or false (Cancel / Quit).      */
/* ------------------------------------------------------------------ */
static bool ShowTokenPanel(char* tokenBuf, int bufSize, bool modal,
                           const char* title, FetchPagesFunc fetchPages)
{
    sCurrentPanel   = 1;
    sPageListHandle = nil;

    DialogPtr dlg = GetNewDialog(kSettingsDialogID, NULL, (WindowPtr)-1);
    if (!dlg) return false;

    UserItemUPP drawUPP = NewUserItemUPP(DrawSettingsItem);
    if (!drawUPP) { DisposeDialog(dlg); return false; }
    { static const short kItems[] = {3, 5};
      SetUserItems(dlg, kItems, 2, drawUPP); }

    Handle h; short type; Rect r; Str255 pstr;
    GetDialogItem(dlg, 4, &type, &h, &r);
    CStrToPStr(tokenBuf, pstr);
    SetDialogItemText(h, pstr);
    SelectDialogItemText(dlg, 4, 0, 32767);

    if (modal) {
        short bt; Handle bh; Rect br;
        GetDialogItem(dlg, 2, &bt, &bh, &br);
        SetControlTitle((ControlHandle)bh, "\pQuit");
    }
    SetDlgTitle(dlg, title);
    ShowWindow(dlg);

    bool cont = false;
    for (;;) {
        short item = RunDialogLoop(dlg);
        if (item != 1) break;   /* Cancel / Quit */

        /* Read token from field */
        GetDialogItem(dlg, 4, &type, &h, &r);
        GetDialogItemText(h, pstr);
        PStrToCStr(pstr, tokenBuf, bufSize);

        if (!fetchPages) { cont = true; break; }  /* no validation */

        /* Show "Connecting…" while fetch runs — dialog stays open.
           RunOneFetchPage handles updateEvt so the panel repaints. */
        SafeStrCopy(sTokenErrMsg, sizeof(sTokenErrMsg), "Connecting to Notion\xC9");
        sTokenMsgBold = false;
        DisableDialogButton(dlg, 1);
        DisableDialogButton(dlg, 2);
        DrawDialog(dlg);
        DrawDefaultButtonRing(dlg, 1);

        /* Clear stale cache so fetch always hits the network */
        { NotionPageList empty; memset(&empty, 0, sizeof(empty));
          SavePageListCache(tokenBuf, empty); }

        NotionPageList pageList;
        bool ok = fetchPages(tokenBuf, pageList);

        if (ok) { cont = true; break; }  /* success — close panel */

        /* Failure: update title + hint, re-enable both buttons */
        int sc = GetLastFetchStatus();
        char errTitle[64];
        sTokenMsgBold = true;
        static const struct { int code; const char* title; const char* msg; const char* hint; } kErr[] = {
            { 401, "Unauthorized (401)", "Check your integration token.", "notion.so/my-integrations" },
            { 403, "Forbidden (403)",    "Token lacks access to this page.", "Share the page with your integration." },
            { 400, "Bad Request (400)",  "Token format invalid.",         "Tokens start with ntn_" },
            { 0,   "Connection Failed",  "Could not reach Notion.",        "Check your network connection." },
        };
        int hit = -1;
        for (int i = 0; i < (int)(sizeof(kErr) / sizeof(kErr[0])); i++)
            if (kErr[i].code == sc) { hit = i; break; }
        if (hit >= 0) {
            SafeStrCopy(errTitle,      sizeof(errTitle),      kErr[hit].title);
            SafeStrCopy(sTokenErrMsg,  sizeof(sTokenErrMsg),  kErr[hit].msg);
            SafeStrCopy(sTokenHintMsg, sizeof(sTokenHintMsg), kErr[hit].hint);
        } else {
            snprintf(errTitle,      sizeof(errTitle),      "Error (%d)", sc);
            snprintf(sTokenErrMsg,  sizeof(sTokenErrMsg),  "Notion returned error %d.", sc);
            sTokenHintMsg[0] = '\0';
        }
        SetDlgTitle(dlg, errTitle);
        EnableDialogButton(dlg, 1);
        EnableDialogButton(dlg, 2);
        SelectDialogItemText(dlg, 4, 0, 32767);
        DrawDialog(dlg);
        DrawDefaultButtonRing(dlg, 1);
        /* loop → RunDialogLoop shows error state, waits for retry */
    }

    sTokenErrMsg[0] = '\0';
    sTokenHintMsg[0] = '\0';
    sTokenMsgBold = false;
    DisposeDialog(dlg);
    DisposeUserItemUPP(drawUPP);
    return cont;
}

/* ------------------------------------------------------------------ */
/* PageListDef — custom LDEF that draws each cell with 2 px top pad   */
/* and an optional emoji icon from the sprite atlas on the left.      */
/* ------------------------------------------------------------------ */
static pascal void PageListDef(short msg, Boolean sel, Rect *r, Cell c,
                                short off, short len, ListHandle lH)
{
    switch (msg) {
        case lDrawMsg: {
            GrafPtr port = (*lH)->port;
            SetPort(port);
            EraseRect(r);

            short textLeft = r->left + 4;

            /* Draw page icon if this entry has one */
            if (c.v < sPageListBuf.count) {
                const NotionPageEntry& pg = sPageListBuf.pages[c.v];
                short cellH = r->bottom - r->top;

                if (pg.iconName[0]) {
                    /* Native Notion icon — draw at native sprite size (20×20), centred vertically */
                    static const short kNative = 20;
                    short iconTop = r->top + (cellH - kNative) / 2;
                    DrawListPageIcon(pg.iconName, r->left + 2, iconTop);
                    textLeft = r->left + 2 + kNative + 3;
                } else if (sListEmojiGWorld && pg.spriteX >= 0) {
                    /* Emoji icon — scale 9→13px from atlas */
                    static const short kSrc = EMOJI_TILE_W;  /* 9 */
                    static const short kDst = 13;
                    short iconTop = r->top + (cellH - kDst) / 2;
                    Rect srcR; SetRect(&srcR, pg.spriteX, pg.spriteY,
                                              pg.spriteX + kSrc, pg.spriteY + kSrc);
                    Rect dstR; SetRect(&dstR, r->left + 2, iconTop,
                                              r->left + 2 + kDst, iconTop + kDst);
                    PixMapHandle pm = GetGWorldPixMap(sListEmojiGWorld);
                    LockPixels(pm);
                    BitMap* dstBM = nil;
                    PixMapHandle dstPM = nil;
                    if (((CGrafPtr)port)->portVersion & (short)0xC000) {
                        dstPM = ((CGrafPtr)port)->portPixMap;
                        LockPixels(dstPM);
                        dstBM = (BitMap*)(*dstPM);
                    } else {
                        dstBM = &port->portBits;
                    }
                    if (dstBM) CopyBits((BitMap*)*pm, dstBM, &srcR, &dstR, srcCopy, nil);
                    if (dstPM) UnlockPixels(dstPM);
                    UnlockPixels(pm);
                    textLeft = r->left + 2 + kDst + 3;
                }
            }

            if (len > 0) {
                SetGenevaFont();
                short cellH2 = r->bottom - r->top;
                short baseline = r->top + (cellH2 - sPageListFI.ascent - sPageListFI.descent) / 2 + sPageListFI.ascent;
                MoveTo(textLeft, baseline);
                DrawText(*(*lH)->cells + off, 0, len);
            }
            if (sel) InvertRect(r);
            break;
        }
        case lHiliteMsg:
            InvertRect(r);
            break;
        case lInitMsg:
        case lCloseMsg:
        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/* ShowPagePanel — Panel 2: pick a page from a scrollable list.       */
/* Opens the dialog, fetches pages, builds a List Manager list.       */
/* Double-click or Enter = Open.  Back/Cancel = return false.         */
/* ------------------------------------------------------------------ */
static bool ShowPagePanel(char* pageIDBuf, int bufSize,
                           FetchPagesFunc fetchPages, const char* token,
                           const char* title, bool cancelMode = false)
{
    sPageListBuf.count = 0;

    sCurrentPanel   = 2;
    sPageListHandle = nil;

    DialogPtr dlg = GetNewDialog(kPageDialogID, NULL, (WindowPtr)-1);
    if (!dlg) return false;

    /* Install draw proc on item 3 (title label) only */
    UserItemUPP drawUPP = NewUserItemUPP(DrawSettingsItem);
    if (!drawUPP) { DisposeDialog(dlg); return false; }
    { static const short kItem = 3;
      SetUserItems(dlg, &kItem, 1, drawUPP); }

    SetDlgTitle(dlg, title);
    if (cancelMode) {
        short bt; Handle bh; Rect br;
        GetDialogItem(dlg, 2, &bt, &bh, &br);
        SetControlTitle((ControlHandle)bh, "\pCancel");
    }
    ShowWindow(dlg);

    /* Fetch pages with dialog already visible */
    if (fetchPages && token && token[0])
        fetchPages(token, sPageListBuf);

    /* The fetch may have freed the sprite/emoji GWorld to reclaim memory
       for the TLS handshake — reload them now before building the list. */
    ReloadListAssets();

    /* Build the List Manager list in item 4's rect */
    Rect rList;
    GetDialogItemRect(dlg, 4, rList);
    SetPort(dlg);
    SetGenevaFont();

    FontInfo fi; GetFontInfo(&fi);
    sPageListFI = fi;                   /* cache for PageListDef per-row draws */
    Rect   dataBounds = {0, 0, 0, 1};   /* 0 rows, 1 col to start */
    Point  cSize;
    cSize.v = fi.ascent + fi.descent + fi.leading + 4;  /* 2px top + 2px bottom */
    if (cSize.v < 24) cSize.v = 24;  /* ensure room for 20px native icons */
    cSize.h = 0;

    /* Only show scrollbar when content exceeds visible area */
    short visRows   = (rList.bottom - rList.top) / cSize.v;
    bool  needScroll = sPageListBuf.count > visRows;
    Rect  listRect  = rList;
    if (!needScroll)
        listRect.right = ((GrafPtr)dlg)->portRect.right;  /* full width, no scrollbar gap */

    ListHandle listH = LNew(&listRect, &dataBounds, cSize, 0,
                             (WindowPtr)dlg, true, false, false, (Boolean)needScroll);

    Handle sSavedLdefProc = nil;   /* original system LDEF proc — restored before LDispose */
    Handle sCustomLdefH   = nil;   /* our stub — we dispose it manually after LDispose     */
    if (listH) {
        sPageListHandle = listH;
        /* Install custom LDEF using the same PC-relative stub trick as
           wdefshell.r WDEF — the Mac OS JSRs into the handle data, so
           the data must be executable 68K code, not a raw pointer:
             move.l L1(pc), -(sp)  ; 2F3A 0004
             rts                   ; 4E75
           L1: dc.l <PageListDef>  ; patched below
           We save the original proc handle and restore it before LDispose
           so the Resource Manager can release it properly. Calling DisposeHandle
           on a resource handle leaves a dangling map entry — on the second LNew
           the RM returns the stale handle → type-10 crash when called.         */
        static const unsigned char kLDEFStub[10] = {
            0x2F, 0x3A, 0x00, 0x04,  /* move.l L1(pc), -(sp) */
            0x4E, 0x75,              /* rts                  */
            0x00, 0x00, 0x00, 0x00   /* L1: function address */
        };
        Handle ldefH = NewHandle(10);
        if (ldefH) {
            BlockMoveData(kLDEFStub, *ldefH, 10);
            *(long*)(*ldefH + 6) = (long)PageListDef;
            MoveHHi(ldefH); HLock(ldefH);
            sSavedLdefProc = (*listH)->listDefProc;  /* save original system LDEF */
            sCustomLdefH   = ldefH;
            (*listH)->listDefProc = ldefH;
        }
        for (int i = 0; i < sPageListBuf.count; i++) {
            short row = LAddRow(1, 32767, listH);
            Cell c; c.h = 0; c.v = row;
            const char* pt = sPageListBuf.pages[i].title;
            LSetCell((Ptr)pt, (short)strlen(pt), c, listH);
        }
        /* Pre-select the current page */
        for (int i = 0; i < sPageListBuf.count; i++) {
            if (strcmp(sPageListBuf.pages[i].id, pageIDBuf) == 0) {
                Cell c; c.h = 0; c.v = i;
                LSetSelect(true, c, listH);
                LAutoScroll(listH);
                break;
            }
        }
        SetPort(dlg);
        InvalRect(&rList);
    }

    /* Invalidate title label */
    { short t2; Handle h2; Rect r2;
      GetDialogItem(dlg, 3, &t2, &h2, &r2); InvalRect(&r2); }

    short result = RunDialogLoop(dlg, listH);
    bool saved   = (result == 1);

    if (saved && listH) {
        Cell c; c.h = 0; c.v = 0;
        if (LGetSelect(true, &c, listH)) {
            SafeStrCopy(pageIDBuf, bufSize, sPageListBuf.pages[c.v].id);
        } else {
            saved = false;   /* Open with no selection = no-op */
        }
    }

    sPageListHandle = nil;
    if (listH) {
        /* Restore original system LDEF proc before LDispose so the Resource
           Manager can release it via ReleaseResource (not DisposeHandle). */
        if (sSavedLdefProc) (*listH)->listDefProc = sSavedLdefProc;
        LDispose(listH);
    }
    /* Dispose our stub handle now that the list is gone. */
    if (sCustomLdefH) { HUnlock(sCustomLdefH); DisposeHandle(sCustomLdefH); }
    DisposeDialog(dlg);
    DisposeUserItemUPP(drawUPP);
    return saved;
}

/* ================================================================== */
/* ShowSettingsDialog — two-panel connection flow                     */
/*   Panel 1: enter token  →  Continue / Cancel (Quit in modal mode) */
/*   Panel 2: Library window for page pick                           */
/* Nothing is written to disk until the user confirms in Library.    */
/* ================================================================== */
bool ShowSettingsDialog(AppSettings &settings, bool modal, const char* title,
                        FetchPagesFunc fetchPages, FetchPageChildrenFunc fetchChildren,
                        LibMovePageFunc movePage)
{
    char pendingToken[256];
    SafeStrCopy(pendingToken, (int)sizeof(pendingToken), settings.apiKey);

    sTokenErrMsg[0] = '\0';
    for (;;) {
        /* Panel 1 — token entry + live validation (dialog stays open during fetch) */
        if (!ShowTokenPanel(pendingToken, sizeof(pendingToken), modal, title, fetchPages))
            return false;

        AppSettings pending = settings;
        SafeStrCopy(pending.apiKey, (int)sizeof(pending.apiKey), pendingToken);

        /* Panel 2 — pages already cached by ShowTokenPanel's validation fetch */
        if (ShowLibraryDialog(pending, fetchPages, fetchChildren, movePage,
                              kLibActionOpenPage, false, true)) {
            /* If API key changed, the cache was already populated by the
               pre-fetch above so no need to wipe it here. */
            settings = pending;
            SaveSettings(settings);
            return true;
        }
        /* "+ Add new" clicked — save the entered token so doNewPage can use it,
           then return false so the caller can call doNewPage(LibNewPageParentId()).
           Do NOT call LibNewPageWasRequested() here — that would clear the flag
           before the caller gets to check it. */
        if (LibNewPageIsPending()) {
            SafeStrCopy(settings.apiKey, (int)sizeof(settings.apiKey), pendingToken);
            return false;
        }
        /* Back → loop to Panel 1 */
    }
}

bool ShowHomePageDialog(AppSettings &settings, FetchPagesFunc fetchPages,
                        FetchPageChildrenFunc fetchChildren,
                        LibMovePageFunc movePage)
{
    AppSettings pending = settings;
    if (ShowLibraryDialog(pending, fetchPages, fetchChildren, movePage,
                          kLibActionOpenPage, false)) {
        settings = pending;
        SaveSettings(settings);
        return true;
    }
    return false;
}

bool ShowPagePickerForLink(const char* apiKey, FetchPagesFunc fetchPages,
                           FetchPageChildrenFunc fetchChildren,
                           NotionPageEntry& out,
                           LibMovePageFunc movePage)
{
    AppSettings tmp;
    memset(&tmp, 0, sizeof(tmp));
    SafeStrCopy(tmp.apiKey, (int)sizeof(tmp.apiKey), apiKey);

    LibPageEntry selected;
    memset(&selected, 0, sizeof(selected));
    if (!ShowLibraryDialog(tmp, fetchPages, fetchChildren, movePage,
                           kLibActionInsertLink, false, false, &selected))
        return false;

    /* selected was filled before gLibPageElm was freed */
    memset(&out, 0, sizeof(out));
    SafeStrCopy(out.id,       (int)sizeof(out.id),       selected.id);
    SafeStrCopy(out.title,    (int)sizeof(out.title),    selected.title);
    SafeStrCopy(out.iconName, (int)sizeof(out.iconName), selected.iconName);
    out.spriteX = selected.spriteX;
    out.spriteY = selected.spriteY;
    memcpy(out.titleEmoji, selected.titleEmoji, sizeof(out.titleEmoji));
    return true;
}
