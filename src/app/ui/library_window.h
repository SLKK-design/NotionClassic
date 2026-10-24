#pragma once

/* library_window.h — Notion-style tree page picker declarations.     */

#include "Settings.h"   /* AppSettings, NotionPageEntry, NotionPageList,
                           FetchPagesFunc, SaveSettings, LoadPageListCache,
                           SavePageListCache — all needed by library_window.cpp */

/* ------------------------------------------------------------------ */
/* Library page display list — flat array expanded from page tree.    */
/* Each LibPageEntry is a view-layer record; disk cache is the        */
/* existing NotionPageList (kMaxNotionPages entries from /v1/search). */
/* ------------------------------------------------------------------ */
#define kMaxLibPages 75

struct LibPageEntry {
    char  id[48];           /* 32-char hex UUID, no hyphens              */
    short titleEmoji[kTitleEmojiMax]; /* title emoji, as NotionPageEntry */
    char  title[64];        /* MacRoman display title                    */
    char  iconName[32];     /* Notion icon slug; ""=none                 */
    char  parentId[64];     /* parent page id, no hyphens; ""=root       */
    short spriteX;          /* emoji sprite X; -1=none                   */
    short spriteY;          /* emoji sprite Y                            */
    short iconSprIdx;       /* icon sprite sheet index; -1=none          */
    short depth;            /* 0=root, 1=child, 2=grandchild …           */
    bool  open;             /* disclosure triangle state                 */
    bool  confirmedLeaf;    /* true after fetch returned 0 children      */
    char  _pad[2];
};

/* Toggle-reveal cache: mirrors Block toggle cache for Library children */
#define kLibToggleCacheSize 16
#define kLibToggleMaxChildren 10

struct LibToggleCacheEntry {
    char         parentId[64];
    LibPageEntry children[kLibToggleMaxChildren];
    int          childCount;
    long         accessGen;  /* LRU generation counter — higher = more recent */
    bool         valid;
    char         _pad[3];
};

/* ------------------------------------------------------------------ */
/* Library action modes                                               */
/* ------------------------------------------------------------------ */
enum LibraryPrimaryAction {
    kLibActionOpenPage   = 0,  /* "Open page"  — after connection or nav  */
    kLibActionInsertLink = 1   /* "Insert link" — from Add menu            */
};

/* Callback: fetch child pages for a parent page ID (no-hyphens form).
   Must set HTTP auth and call GET /v1/blocks/{parentId}/children.
   Returns true on success; fills out[] up to maxOut; sets outCount.   */
typedef bool (*FetchPageChildrenFunc)(const char* apiKey,
                                      const char* parentIdNH,
                                      LibPageEntry out[],
                                      int maxOut,
                                      int& outCount);

/* Callback: move a page under another page (POST /v1/pages/{id}/move).
   srcIdNH and newParentIdNH are no-hyphen UUIDs; "" (workspace top
   level) can't be done through the API and returns false.            */
typedef bool (*LibMovePageFunc)(const char* apiKey,
                                 const char* srcIdNH,
                                 const char* newParentIdNH);

/* ------------------------------------------------------------------ */
/* Library globals and window functions                                */
/* ------------------------------------------------------------------ */

/* Extern globals — accessible from main.cpp for shadow updates */
extern LibPageEntry*        gLibPageElm;           /* nil when Library is closed */
extern int                  gLibPageCount;
extern LibToggleCacheEntry* gLibToggleCache;        /* nil when Library is closed */
extern int                  gLibToggleCacheCount;

/* Returns true (and clears the flag) if "+ Add new" was clicked in Library. */
bool LibNewPageWasRequested();
/* Returns true WITHOUT clearing the flag — used by ShowSettingsDialog to
   distinguish cancel from "+ Add new" before the caller checks the flag.  */
bool LibNewPageIsPending();
/* Returns the no-hyphen page ID of the row whose "+" was clicked; "" = root. */
const char* LibNewPageParentId();

/* Returns true if any page was deleted during the library session. */
bool LibDidDeletePages();

/* Core Library window — used by all three entry points in Settings.cpp.
   action selects the primary button label and return semantics.
   Returns true when user confirms (open / insert link).
   Returns false on cancel; check LibNewPageWasRequested() for "+ Add new". */
bool ShowLibraryDialog(AppSettings &settings,
                       FetchPagesFunc fetchPages,
                       FetchPageChildrenFunc fetchChildren,
                       LibMovePageFunc movePage,
                       LibraryPrimaryAction action = kLibActionOpenPage,
                       bool forceReload = false,
                       bool fromConnection = false,
                       LibPageEntry* selectedOut = 0);

/* ------------------------------------------------------------------ */
/* Settings / connection dialog entry points                           */
/* ------------------------------------------------------------------ */

/* Two-panel connection flow:
     Panel 1 — enter / confirm the integration token ("Continue" / "Cancel").
     Panel 2 — Library window for page pick.
   fetchPages is called between the two panels with the just-entered token.
   modal=true renames Cancel→Quit (used on credential errors).           */
bool ShowSettingsDialog(AppSettings &settings, bool modal = false,
                        const char* title = "Connection",
                        FetchPagesFunc fetchPages = nullptr,
                        FetchPageChildrenFunc fetchChildren = nullptr,
                        LibMovePageFunc movePage = nullptr);
bool ShowHomePageDialog(AppSettings &settings, FetchPagesFunc fetchPages,
                        FetchPageChildrenFunc fetchChildren = nullptr,
                        LibMovePageFunc movePage = nullptr);

/* Show the Library window to select a page for inline linking.
   Fills `out` with the chosen entry; returns false if cancelled.     */
bool ShowPagePickerForLink(const char* apiKey, FetchPagesFunc fetchPages,
                           FetchPageChildrenFunc fetchChildren,
                           NotionPageEntry& out,
                           LibMovePageFunc movePage = nullptr);
