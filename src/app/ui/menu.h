/* menu.h — Menu update and dispatch interface.
   Public entry points called from main.cpp's event loop.              */
#pragma once

#include <Dialogs.h>   /* DialogPtr */
#include <Events.h>    /* EventRecord */

/* ------------------------------------------------------------------ */
/* Menu resource IDs and item numbers                                  */
/* ------------------------------------------------------------------ */
enum {
    kMenuApple  = 128,
    kMenuFile   = 129,   /* the application's File ("Notion") menu */
    kMenuEdit   = 130,
    kMenuAdd    = 131,
    kMenuPages  = 132,
    kMenuFormat = 133,
    /* kMenuApple items */
    kItemAppleDebug  = 1,
    /* kMenuFile (Notion) items */
    kItemAbout      = 1,
    /* 2 = separator */
    kItemSettings   = 3,
    kItemLibrary    = 4,
    /* 5 = separator */
    kItemFullWidth  = 6,
    kItemQuit       = 7,
    /* kMenuPages items */
    kItemReload    = 1,
    kItemSave      = 2,
    kItemPageIcon  = 3,
    /* 4 = separator */
    kItemOpenPage  = 5,
    kItemNewPage   = 6,
    /* 7 = separator */
    kItemBack      = 8,
    kItemFwd       = 9,
    /* 10 = separator */
    kItemRecent1   = 11   /* items 11..10+kNavHistMax = recent pages, most-recent first */
};

/* ------------------------------------------------------------------ */
/* Public entry points                                                 */
/* ------------------------------------------------------------------ */

/* Rebuild the enabled/disabled/checked state of all menus.
   Called before MenuSelect() and MenuKey() and from keyDown handler. */
void UpdateMenus();

/* Dispatch the result of MenuSelect() or MenuKey() to the correct handler.
   Hilites the menu bar after handling. */
void DoMenuCommand(long menuCommand);

/* ------------------------------------------------------------------ */
/* Dialog menu-bar helpers (used by Settings and the library window)  */
/* ------------------------------------------------------------------ */

/* Gray out block-editing menus (Add 131, Pages 132, Format 133) and the
   text-formatting items 4-7 in the Edit menu (130) so they appear
   unavailable while a modal dialog is open.  Copy (item 1) and Paste
   (item 2) in the Edit menu remain enabled.
   Call before the dialog loop; RestoreDialogMenus after.              */
void DisableDialogMenus();
void RestoreDialogMenus();

/* Handle an inMenuBar mouseDown inside a dialog event loop.
   Quit (Notion menu item 7)    -> QuitApp() (returns if cancelled).
   Edit menu Copy(1)/Paste(2)   -> dispatched to the focused dialog TE.
   All other selections silently discarded. Returns false always.     */
bool HandleDialogMenuBar(DialogPtr dlg, EventRecord& e);
