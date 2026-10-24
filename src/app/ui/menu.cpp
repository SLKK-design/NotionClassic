/* menu.cpp — Menu update and command dispatch extracted from main.cpp.
   Contains: UpdateMenus, HandleMenuApple, HandleMenuFile, HandleMenuEdit,
             HandleMenuAdd, HandleMenuPages, DoMenuCommand.              */

#include <Memory.h>
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
#include <Gestalt.h>

#include <string.h>

#include "menu.h"
#include "Settings.h"
#include "blocks.h"
#include "app_state.h"
#include "helpers.h"
#include "block_render.h"
#include "block_ops.h"
#include "navigation.h"
#include "page_lifecycle.h"
#include "page_fetch.h"
#include "page_cache.h"   /* gPageCacheBypassOnce */
#include "picker_window.h"
#include "about_dialog.h"
#include "run_styles.h"
#include "library_window.h"
#include "debug_con.h"
#include "content_events.h"   /* HandleKeyDown: menu Copy/Paste */

/* ------------------------------------------------------------------ */
/* Menu ID / item constants (mirror main.cpp enum)                    */
/* ------------------------------------------------------------------ */
/* Menu resource IDs and item numbers — defined in menu.h */

/* MBarHeight low-mem global (0x0BAA): read directly rather than via
   GetMBarHeight() so this stays usable when the trap isn't available. */
#define kMBarHeight (*(short*)0x0BAA)

/* ------------------------------------------------------------------ */
/* Extern globals — defined in main.cpp                               */
/* ------------------------------------------------------------------ */
extern AppSettings      gSettings;
extern AppState         gAppState;
extern Block*           pageElm;
extern int              pageElmCount;
extern int              blockSelect;
extern TEHandle         textActive;
extern bool             gFullWidth;
extern Rect             gSavedWindowRect;
extern WindowPtr        gWindow;
extern int              gCurRequest;
extern bool             gArrowsVisible;
extern short            gColLeft;

/* ------------------------------------------------------------------ */
/* Extern globals — defined in navigation.cpp (via navigation.h)      */
/* ------------------------------------------------------------------ */
/* gHistBackCount, gHistFwdCount, gRecentPages, gRecentCount,
   gNavDelta, gNavPendingTick, gNavTarget — declared in navigation.h  */

/* ------------------------------------------------------------------ */
/* Extern functions — defined in other TUs                            */
/* ------------------------------------------------------------------ */
const char* EffPageID();
const char* EffAPIKey();
void QuitApp();

/* Return the slot-th recent page that isn't the current page, or nil. */
static const PageHeader* NthRecentPage(int slot)
{
    const char* curId = EffPageID();
    int shown = 0;
    for (int j = 0; j < gRecentCount; j++) {
        if (curId && strcmp(gRecentPages[j].id, curId) == 0) continue;
        if (shown == slot) return &gRecentPages[j];
        shown++;
    }
    return nil;
}
bool HasSel();
void doSave();
void MarkDirty();

/* ------------------------------------------------------------------ */
/* HandleMenuApple                                                     */
/* ------------------------------------------------------------------ */
static void HandleMenuApple(short menuItem)
{
    Str255 str;
    if (menuItem == kItemAppleDebug) { ToggleDebugWin(); SaveDebugMode(gDebugWin != nil); }
    else { GetMenuItemText(GetMenu(128), menuItem, str); OpenDeskAcc(str); }
}

/* ------------------------------------------------------------------ */
/* HandleMenuFile                                                      */
/* ------------------------------------------------------------------ */
static void HandleMenuFile(short menuItem)
{
    switch (menuItem) {
        case kItemSettings:
            prepareForPageList();
            if (ShowSettingsDialog(gSettings, false, "Settings",
                                  DoFetchPages, DoFetchPageChildren, DoLibMovePage)) {
                AfterPickerConfirm();
            } else {
                bool deleted = LibDidDeletePages();
                AfterPickerCancel();
                if (LibNewPageWasRequested()) {
                    RefreshAuth();
                    doNewPage(LibNewPageParentId());
                } else if (deleted) {
                    gPageCacheBypassOnce = true;
                    scheduleReload();
                }
            }
            break;
        case kItemLibrary: {
            PageHeader curBeforeOpen; buildCurrentHeader(curBeforeOpen);
            prepareForPageList();
            if (ShowHomePageDialog(gSettings, DoFetchPages, DoFetchPageChildren, DoLibMovePage)) {
                SetListEmojiGWorld(nil); RefreshAuth();
                pushToBack(curBeforeOpen);
                gHistFwdCount = 0;
                gAppState.navPending = false; gNavDelta = 0;
                SaveNavHistory(gHistBack, gHistBackCount, gHistFwd, gHistFwdCount);
                scheduleReload();
            } else {
                bool deleted = LibDidDeletePages();
                AfterPickerCancel();
                if (LibNewPageWasRequested()) doNewPage(LibNewPageParentId());
                else if (deleted) {
                    gPageCacheBypassOnce = true;
                    scheduleReload();
                }
            }
            break;
        }
        case kItemFullWidth:
            gFullWidth = !gFullWidth;
            SaveFullWidth(gFullWidth);
            if (gWindow != nil) {
                SetPort(gWindow);
                if (gFullWidth) {
                    short contTop  = -gWindow->portBits.bounds.top;
                    short contLeft = -gWindow->portBits.bounds.left;
                    short winW     = gWindow->portRect.right  - gWindow->portRect.left;
                    short winH     = gWindow->portRect.bottom - gWindow->portRect.top;
                    SetRect(&gSavedWindowRect, contLeft, contTop,
                            contLeft + winW, contTop + winH);
                    short mbarH = kMBarHeight;
                    if (mbarH < 16 || mbarH > 40) mbarH = 20;
                    short screenW = qd.screenBits.bounds.right;
                    short screenH = qd.screenBits.bounds.bottom - mbarH;
                    MoveWindow(gWindow, 0, mbarH, true);
                    SizeWindow(gWindow, screenW, screenH, true);
                } else {
                    MoveWindow(gWindow, gSavedWindowRect.left, gSavedWindowRect.top, true);
                    SizeWindow(gWindow,
                               gSavedWindowRect.right  - gSavedWindowRect.left,
                               gSavedWindowRect.bottom - gSavedWindowRect.top, true);
                }
                resizeControls();
                updateBlocks();
                InvalidateSaveButton();
            }
            break;
        case kItemAbout:    ShowAboutBox(); break;
        case kItemQuit:     QuitApp(); break;
    }
}

/* ------------------------------------------------------------------ */
/* HandleMenuEdit                                                      */
/* ------------------------------------------------------------------ */
static void HandleMenuEdit(short menuItem)
{
    if (menuItem >= 4 && menuItem <= 7 && textActive && HasSel()) {
        if (!AllowEdit(blockSelect, kEditText)) return;
        /* Bold(4) Italic(5) Underline(6) Strikethrough(7) */
        static const int kStyleMap[] = {kStyleBold,kStyleItalic,kStyleUnderline,kStyleStrikethrough};
        UndoBegin(blockSelect, false);   /* a style change is its own undo step */
        ToggleRunStyleBit(pageElm[blockSelect], kStyleMap[menuItem - 4]);
        pageElm[blockSelect].runsDirty = true;
    } else if (menuItem == 1 || menuItem == 2) {
        /* A desk accessory takes it; else the same path as Cmd-C / Cmd-V,
           which keeps emoji, runs and undo right */
        if (SystemEdit(menuItem == 1 ? 3 /* copyCmd */ : 4 /* pasteCmd */)) return;
        if (!textActive || !HasSel()) return;   /* HandleKeyDown would route back here */
        EventRecord e = {};
        e.what      = keyDown;
        e.message   = (menuItem == 1) ? 'c' : 'v';
        e.modifiers = cmdKey;
        HandleKeyDown(e);
    }
}

/* ------------------------------------------------------------------ */
/* HandleMenuFormat                                                    */
/* ------------------------------------------------------------------ */
static void HandleMenuFormat(short menuItem)
{
    if (menuItem == 1) {
        doDeleteBlock();
    } else if (menuItem == 3) {
        moveBlock(-1);
    } else if (menuItem == 4) {
        moveBlock(1);
    } else if (menuItem >= 6 && HasSel() && pageElm[blockSelect].type != BT_TITLE) {
        int ti = menuItem - 6;
        if (ti >= 0 && ti < 14)
            RetypeBlock(blockSelect, (BlockType)ti);
    }
}

/* ------------------------------------------------------------------ */
/* HandleMenuAdd                                                       */
/* ------------------------------------------------------------------ */
static void HandleMenuAdd(short menuItem)
{
    if      (menuItem == 18)                     doInsertPageLink();
    else if (menuItem == 17)                     ShowEmojiPicker();
    else if (menuItem >= 1 && menuItem <= 15)    doAddBlock(menuItem - 1);
}

/* ------------------------------------------------------------------ */
/* HandleMenuPages                                                     */
/* ------------------------------------------------------------------ */
static void HandleMenuPages(short menuItem)
{
    switch (menuItem) {
        case kItemReload:   gPageCacheBypassOnce = true; /* Reload = always fetch fresh */
                            scheduleReload(); break;
        case kItemSave:     doSave();         break;
        case kItemPageIcon: ShowIconPicker(); break;
        case kItemOpenPage: {
            PageHeader curBeforeOpen; buildCurrentHeader(curBeforeOpen);
            prepareForPageList();
            if (ShowHomePageDialog(gSettings, DoFetchPages, DoFetchPageChildren, DoLibMovePage)) {
                SetListEmojiGWorld(nil); RefreshAuth();
                pushToBack(curBeforeOpen);
                gHistFwdCount = 0;
                gAppState.navPending = false; gNavDelta = 0;
                SaveNavHistory(gHistBack, gHistBackCount, gHistFwd, gHistFwdCount);
                scheduleReload();
            } else {
                bool deleted = LibDidDeletePages();
                AfterPickerCancel();
                if (LibNewPageWasRequested()) doNewPage(LibNewPageParentId());
                else if (deleted) {
                    gPageCacheBypassOnce = true;
                    scheduleReload();
                }
            }
            break;
        }
        case kItemNewPage:  doNewPage(); break;
        case kItemBack:
            if (gHistBackCount > 0 && -gNavDelta < gHistBackCount) {
                gNavDelta--;
                gNavPendingTick = TickCount();
                gAppState.navPending = true;
                showNavPreview();
            }
            break;
        case kItemFwd:
            if (gHistFwdCount > 0 && gNavDelta < gHistFwdCount) {
                gNavDelta++;
                gNavPendingTick = TickCount();
                gAppState.navPending = true;
                showNavPreview();
            }
            break;
        default:
            if (menuItem >= kItemRecent1 && menuItem < kItemRecent1 + kNavHistMax) {
                int slot = menuItem - kItemRecent1;
                const PageHeader* pg = NthRecentPage(slot);
                if (pg) commitNavigateTo(pg->id);
            }
            break;
    }
}

/* ------------------------------------------------------------------ */
/* UpdateMenus                                                         */
/* ------------------------------------------------------------------ */
void UpdateMenus() {
    MenuHandle addMenu    = GetMenuHandle(kMenuAdd);
    MenuHandle editMenu   = GetMenuHandle(kMenuEdit);
    MenuHandle fmtMenu    = GetMenuHandle(kMenuFormat);
    MenuHandle notionMenu = GetMenuHandle(kMenuFile);
    MenuHandle pagesMenu  = GetMenuHandle(kMenuPages);
    bool busy = (gCurRequest < kPhaseFullyLoaded || gAppState.busy());

    bool canDelete = !busy && blockSelect > 0 && blockSelect < pageElmCount
                     && pageElm[blockSelect].type != BT_TITLE;
    if (addMenu)    { if (busy)      DisableItem(addMenu,  0); else EnableItem(addMenu,  0); }
    if (fmtMenu)    { if (canDelete) EnableItem(fmtMenu,   1); else DisableItem(fmtMenu,  1); }
    { MenuHandle appleMenu = GetMenuHandle(kMenuApple);
      if (appleMenu) CheckItem(appleMenu, kItemAppleDebug, gDebugWin != nil); }
    if (notionMenu) {
        CheckItem(notionMenu, kItemFullWidth, gFullWidth ? true : false);
        bool hasToken = gSettings.apiKey[0] != '\0';
        if (!busy && hasToken) EnableItem(notionMenu, kItemLibrary);
        else                   DisableItem(notionMenu, kItemLibrary);
    }
    if (pagesMenu) {
        if (busy) DisableItem(pagesMenu, kItemReload);
        else      EnableItem(pagesMenu,  kItemReload);
        if (busy || !gAppState.hasDirtyBlocks) DisableItem(pagesMenu, kItemSave);
        else                          EnableItem(pagesMenu,  kItemSave);
        if (!busy && gSettings.apiKey[0]) EnableItem(pagesMenu, kItemOpenPage);
        else                              DisableItem(pagesMenu, kItemOpenPage);
        bool canNew = !busy && gSettings.apiKey[0];
        if (canNew) EnableItem(pagesMenu, kItemNewPage);
        else        DisableItem(pagesMenu, kItemNewPage);
        if (!busy) EnableItem(pagesMenu, kItemPageIcon);
        else       DisableItem(pagesMenu, kItemPageIcon);
        if (gHistBackCount > 0) EnableItem(pagesMenu, kItemBack);
        else                    DisableItem(pagesMenu, kItemBack);
        if (gHistFwdCount > 0) EnableItem(pagesMenu, kItemFwd);
        else                   DisableItem(pagesMenu, kItemFwd);
        /* Recent pages — delete old dynamic items, then append only what we have */
        {
            short total = CountMenuItems(pagesMenu);
            for (short di = total; di >= kItemRecent1; di--)
                DeleteMenuItem(pagesMenu, di);

            for (int slot = 0; slot < kNavHistMax; slot++) {
                const PageHeader* pg = NthRecentPage(slot);
                if (!pg) break;
                const char* src = pg->title[0] ? pg->title : pg->id;
                char buf[80]; SafeStrCopy(buf, sizeof(buf), src);
                Str255 pstr; int len = (int)strlen(buf);
                if (len > 255) len = 255;
                pstr[0] = (unsigned char)len; memcpy(pstr + 1, buf, len);
                AppendMenu(pagesMenu, "\p ");
                short mitem = CountMenuItems(pagesMenu);
                SetMenuItemText(pagesMenu, mitem, pstr);
                EnableItem(pagesMenu, mitem);
            }
        }
    }

    if (editMenu) {
        bool isTitle = (blockSelect == 0 && pageElmCount > 0
                        && pageElm[0].type == BT_TITLE);
        for (short fi = 4; fi <= 7; fi++) {
            if (isTitle) DisableItem(editMenu, fi);
            else         EnableItem(editMenu,  fi);
        }
    }

    if (fmtMenu) {
        bool isTitle = (blockSelect == 0 && pageElmCount > 0
                        && pageElm[0].type == BT_TITLE);

        if (isTitle) { DisableItem(fmtMenu, 3); DisableItem(fmtMenu, 4); }
        else         { EnableItem(fmtMenu,  3); EnableItem(fmtMenu,  4); }

        static const int kTypeCount = 14;
        static const int kFirstTypeItem = 6;
        BlockType curType = (HasSel())
                         ? pageElm[blockSelect].type : BT_UNKNOWN;
        for (int i = 0; i < kTypeCount; i++) {
            short item = (short)(kFirstTypeItem + i);
            if (isTitle) {
                DisableItem(fmtMenu, item);
                SetItemMark(fmtMenu, item, noMark);
            } else {
                EnableItem(fmtMenu, item);
                short mark = (curType == (BlockType)i) ? (short)checkMark : (short)noMark;
                SetItemMark(fmtMenu, item, mark);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* DoMenuCommand                                                       */
/* ------------------------------------------------------------------ */
void DoMenuCommand(long menuCommand) {
    short menuID   = menuCommand >> 16;
    short menuItem = menuCommand & 0xFFFF;
    switch (menuID) {
        case kMenuApple: HandleMenuApple(menuItem); break;
        case kMenuFile:  HandleMenuFile(menuItem);  break;
        case kMenuEdit:   HandleMenuEdit(menuItem);   break;
        case kMenuFormat: HandleMenuFormat(menuItem); break;
        case kMenuAdd:    HandleMenuAdd(menuItem);    break;
        case kMenuPages: HandleMenuPages(menuItem); break;
    }
    HiliteMenu(0);
}

/* ------------------------------------------------------------------ */
/* Dialog menu-bar helpers (moved here from core/helpers — they encode */
/* this app's specific menu layout).                                   */
/* ------------------------------------------------------------------ */

/* Disable all app menu items while a modal dialog is open (Quit stays enabled).
   Restore them when the dialog closes. */
static void ApplyDialogMenus(bool enable)
{
    static const short kGrayIDs[] = { kMenuAdd, kMenuPages, kMenuFormat };
    for (int i = 0; i < 3; i++) {
        MenuHandle mh = GetMenuHandle(kGrayIDs[i]);
        if (mh) { if (enable) EnableItem(mh, 0); else DisableItem(mh, 0); }
    }
    /* Notion menu (129): disable all except Quit (item 7); re-enable all.
       EnableItem(mh,0) only enables the title — items need individual calls. */
    MenuHandle notionMH = GetMenuHandle(kMenuFile);
    if (notionMH) {
        short total = CountMenuItems(notionMH);
        for (short it = 1; it <= total; it++) {
            if (enable) EnableItem(notionMH, it);
            else if (it != kItemQuit) DisableItem(notionMH, it);
        }
    }
    /* Edit menu (130): Bold/Italic/Underline/Strike (items 4-7);
       Copy (1) and Paste (2) remain enabled.                          */
    MenuHandle editMH = GetMenuHandle(kMenuEdit);
    if (editMH) {
        for (short it = 4; it <= 7; it++) {
            if (enable) EnableItem(editMH, it); else DisableItem(editMH, it);
        }
    }
    DrawMenuBar();
}

void DisableDialogMenus() { ApplyDialogMenus(false); }
void RestoreDialogMenus() { ApplyDialogMenus(true); }

bool HandleDialogMenuBar(DialogPtr dlg, EventRecord& e)
{
    long  mr    = MenuSelect(e.where);
    short mID   = (short)(mr >> 16);
    short mItem = (short)(mr & 0xFFFF);
    HiliteMenu(0);

    /* Quit — always available, even when dialogs disable other items.
       QuitApp, not ExitToShell: it asks about unsaved changes and frees
       the MacTCP streams (left behind, they crash the next launch). */
    if (mID == kMenuFile && mItem == kItemQuit) { QuitApp(); SetPort(dlg); }

    /* Edit menu (130): Copy(1) / Paste(2) dispatched to focused TE   */
    if (mID == kMenuEdit) {
        TEHandle te = ((DialogPeek)dlg)->textH;
        if (te) {
            if (mItem == 1) {                           /* Copy */
                TECopy(te); ZeroScrap(); TEToScrap();
            } else if (mItem == 2) {                    /* Paste — strip newlines/tabs */
                PasteCleanIntoTE(te);
            }
        }
    }
    return false;
}
