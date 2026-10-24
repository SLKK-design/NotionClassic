/* navigation.h — Page navigation history, recent pages, and nav helpers. */
#pragma once

#include "Settings.h"
#include "HttpResponse.h"

/* ------------------------------------------------------------------ */
/* Navigation state globals (defined in navigation.cpp)               */
/* ------------------------------------------------------------------ */
extern PageHeader gHistBack[kNavHistMax];
extern int        gHistBackCount;
extern PageHeader gHistFwd[kNavHistMax];
extern int        gHistFwdCount;

extern PageHeader gRecentPages[kMaxRecentPages];
extern int        gRecentCount;

extern int        gNavDelta;
extern char       gNavTarget[64];
extern long       gNavPendingTick;

extern PageHeader g_pendingNavHeader;
extern bool       g_hasPendingNavHeader;

extern char       gNewPageID[64];
extern char       gNewPageParentIdNH[64];

static const long kNavDelayTicks = 60;  /* ~1 s coalesce window */

/* ------------------------------------------------------------------ */
/* Navigation functions                                                */
/* ------------------------------------------------------------------ */

/* Push a header onto the back stack, discarding the oldest if full. */
void pushToBack(const PageHeader& h);

/* Build a PageHeader snapshot of the currently loaded page. */
void buildCurrentHeader(PageHeader& h);

/* Add h to the front of gRecentPages, persisting to disk. */
void addToRecent(const PageHeader& h);

/* Update stored icon/title for the current page in all history stores. */
void SyncCurrentPageInHistory();

/* Navigate back/forward N steps in history. */
void navigateBackN(int n);
void navigateForwardN(int n);

/* Search history stacks, recent pages, and disk cache for a matching header. */
bool findCachedHeader(const char* pageId, PageHeader& out);

/* Install h as the sole placeholder title block (sets id, icon, flag).
   Shared by showCachedPreview and reloadPage. */
void InstallPlaceholderTitle(const PageHeader& h);

/* Set the main window's title bar to `title`, with the leading-space layout
   hack. Builds a proper Pascal string so any-length titles render correctly. */
void setWindowTitleChrome(const char* title);

/* Show title preview + "Loading…" status while nav coalesce delay runs. */
void showNavPreview();

/* Startup: render cached title+icon placeholder for the current page.
   Returns false if no header is cached (caller shows full-screen status). */
bool ShowStartupHeaderPreview();

/* Schedule a navigate-to-specific-page (coalesces rapid link clicks). */
void scheduleNavigateTo(const char* pageID);

/* Commit the deferred page-link navigation. */
void commitNavigateTo(const char* pageID);

/* Current page is gone (404): go back to the page the last navigation
   left.  False when there's none. */
bool ReturnFromMissingPage();

/* Remove a deleted page from all history stacks and recent pages. */
void PurgePageFromHistory(const char* pageIdNH);

/* HTTP callback for POST /v1/pages (create new page). */
void OnNewPageResponse(HttpResponse& resp);
