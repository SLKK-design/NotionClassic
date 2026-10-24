#pragma once

#include "crypto_persist.h"  /* DNS/ECDSA/EC-midpoint/TLS persistence (core/net) */

#define kSettingsDialogID  129
#define kPageDialogID      130
#define kLibraryDialogID   131

struct AppSettings {
    char pageID[64];
    char apiKey[256];
};

/* ------------------------------------------------------------------ */
/* Notion page list — populated before opening the settings dialog so  */
/* the user can pick from a dropdown instead of typing a raw page ID.  */
/* ------------------------------------------------------------------ */
#define kMaxNotionPages 100
#define kTitleEmojiMax  8   /* emoji drawn per page title in the Library */

struct NotionPageEntry {
    char  id[48];           /* 32-char hex UUID, no hyphens              */
    short titleEmoji[kTitleEmojiMax]; /* per title sentinel: EMOJI_TABLE
                               index + 1, -1 = no sprite, 0 = not known
                               (was id's spare bytes, so the prefs
                               layout is unchanged)                     */
    char  title[64];        /* MacRoman display title                    */
    short spriteX;          /* emoji sprite atlas x offset; -1=none      */
    short spriteY;          /* emoji sprite atlas y offset               */
    char  iconName[32];     /* Notion icon slug, e.g. "house"; ""=none   */
    char  pageParentId[37]; /* 36-char UUID with hyphens + NUL; ""=root  */
    char  _pad[3];          /* alignment pad                             */
};

struct NotionPageList {
    NotionPageEntry pages[kMaxNotionPages];
    int count;
};

/* LibPageEntry, LibToggleCacheEntry, kMaxLibPages, kLibToggleCacheSize,
   kLibToggleMaxChildren, LibraryPrimaryAction, FetchPageChildrenFunc,
   LibMovePageFunc, ShowSettingsDialog, ShowHomePageDialog,
   ShowPagePickerForLink, and the global externs
   gLibPageElm/gLibPageCount/gLibToggleCache/gLibToggleCacheCount are
   defined in library_window.h — include that header directly.         */

void SetListEmojiGWorld(void* gw);       /* pass GWorldPtr; nil to clear         */
void DrawListPageIcon(const char* name, short x, short y); /* draws at native sprite size */
void ReloadListAssets();                 /* reload sprite+emoji after fetch frees them */

bool LoadSettings(AppSettings &settings); /* returns true if prefs file existed */
void SaveSettings(const AppSettings &settings);

/* ------------------------------------------------------------------ */
/* Navigation history — lightweight header records (id + title + icon) */
/* Persisted across restarts; no page content cached.                  */
/* ------------------------------------------------------------------ */
#define kNavHistMax      10
#define kMaxRecentPages   5
struct PageHeader {
    char id[37];       /* 36-char UUID with hyphens + NUL */
    char title[80];    /* truncated display title          */
    char iconName[32]; /* Notion icon slug, e.g. "house"  */
};
void LoadNavHistory(PageHeader back[], int& backCount, PageHeader fwd[], int& fwdCount);
void SaveNavHistory(const PageHeader back[], int backCount,
                    const PageHeader fwd[], int fwdCount);

void LoadRecentPages(PageHeader recent[], int& count);
void SaveRecentPages(const PageHeader recent[], int count);

/* ------------------------------------------------------------------ */
/* Page list disk cache — avoids a TLS connection on repeated opens   */
/* of the page picker within the same session.                        */
/* ------------------------------------------------------------------ */
void SavePageListCache(const char* apiKey, const NotionPageList& list);
bool LoadPageListCache(const char* apiKey, NotionPageList& out);

/* Callback type used by ShowSettingsDialog to fetch the page list.
   Implementations should set HTTP auth to apiKey and call the search API. */
typedef bool (*FetchPagesFunc)(const char* apiKey, NotionPageList& list);

/* ShowSettingsDialog, ShowHomePageDialog, ShowPagePickerForLink,
   ShowLibraryDialog, LibNewPageWasRequested, LibNewPageParentId
   are declared in library_window.h — include that header directly.  */

/* ------------------------------------------------------------------ */
/* Full-width layout preference                                        */
/* ------------------------------------------------------------------ */
bool LoadFullWidth();
void SaveFullWidth(bool value);

bool LoadDebugMode();
void SaveDebugMode(bool value);

/* DNS / ECDSA / EC-midpoint / TLS-session persistence types and their
   C-callable wrappers now live in core/net/crypto_persist.h (included
   at the top of this header). */
