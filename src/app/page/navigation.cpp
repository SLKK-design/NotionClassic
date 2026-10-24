/* navigation.cpp — Page navigation history, recent pages, and nav helpers.
   Extracted from main.cpp. */
#include <string.h>
#include <TextEdit.h>
#include <Windows.h>

#include "navigation.h"
#include "page_lifecycle.h"
#include "blocks.h"
#include "app_state.h"
#include "helpers.h"
#include "debug_con.h"
#include "HttpClient.h"
#include "gason.hpp"

/* ------------------------------------------------------------------ */
/* Externs — globals defined in main.cpp                               */
/* ------------------------------------------------------------------ */
extern Block*        pageElm;
extern int           pageElmCount;
extern int           gMaxBlocks;
extern AppState      gAppState;
extern char          gPageIconName[32];
extern WindowPtr     gWindow;
extern HttpClient    gHttpClient;
extern AppSettings   gSettings;
extern TEHandle      textActive;
extern int           blockSelect;
extern bool          g_titleIsPlaceholder;
extern const char*   EffPageID();
extern const char*   EffAPIKey();
extern void          DrawStatusText(const char* msg);
extern void          scheduleReload();
extern void          stripHyphens(const char* src, char* out, int outLen);
extern void          updateBlocks();

/* ------------------------------------------------------------------ */
/* Navigation state globals                                            */
/* ------------------------------------------------------------------ */
PageHeader gHistBack[kNavHistMax];
int        gHistBackCount = 0;
PageHeader gHistFwd[kNavHistMax];
int        gHistFwdCount  = 0;

PageHeader gRecentPages[kMaxRecentPages];
int        gRecentCount = 0;

int        gNavDelta       = 0;
char       gNavTarget[64]  = {0};
long       gNavPendingTick = 0;
PageHeader g_pendingNavHeader;
bool       g_hasPendingNavHeader = false;

char       gNewPageID[64]         = {0};
char       gNewPageParentIdNH[64] = {0};  /* no-hyphen parent set by doNewPage */

/* The page the last Back/Forward/link navigation left: where a page that
   turns out to be gone (404) sends you back to. */
static PageHeader sNavOrigin;
static bool       sHasNavOrigin = false;

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

/* Copy iconName+title fields from src to dst. */
static inline void CopyPageMeta(PageHeader& dst, const PageHeader& src) {
    SafeStrCopy(dst.iconName, sizeof(dst.iconName), src.iconName);
    SafeStrCopy(dst.title,    sizeof(dst.title),    src.title);
}

/* Hyphen-insensitive UUID compare — nav records may hold either the
   hyphenated (API/link) or no-hyphen (library/cache) form. */
static bool sameId(const char* a, const char* b) {
    char na[64], nb[64];
    stripHyphens(a, na, sizeof(na));
    stripHyphens(b, nb, sizeof(nb));
    return strcmp(na, nb) == 0;
}

/* Push a header onto the back stack, discarding the oldest if full. */
void pushToBack(const PageHeader& h) {
    if (gHistBackCount < kNavHistMax) {
        gHistBack[gHistBackCount++] = h;
    } else {
        memmove(&gHistBack[0], &gHistBack[1], sizeof(PageHeader) * (kNavHistMax - 1));
        gHistBack[kNavHistMax - 1] = h;
    }
}

/* Peek at the history header we would land on given current gNavDelta.
   Returns false if the delta is out of range (no preview available). */
static bool peekNavHeader(PageHeader& out) {
    if (gNavDelta < 0) {
        int steps = -gNavDelta;
        if (steps > gHistBackCount) steps = gHistBackCount;
        int idx = gHistBackCount - steps;
        if (idx < 0 || idx >= gHistBackCount) return false;
        out = gHistBack[idx];
        return true;
    }
    if (gNavDelta > 0) {
        int idx = gNavDelta - 1;
        if (idx < 0 || idx >= gHistFwdCount) return false;
        out = gHistFwd[idx];
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Public functions                                                    */
/* ------------------------------------------------------------------ */

void buildCurrentHeader(PageHeader& h) {
    memset(&h, 0, sizeof(h));
    const char* pid = EffPageID();
    setId(h.id, pid);
    SafeStrCopy(h.iconName, sizeof(h.iconName), gPageIconName);
    if (pageElmCount > 0 && pageElm[0].type == BT_TITLE && pageElm[0].textLen > 0) {
        SafeStrCopy(h.title, sizeof(h.title), pageElm[0].text);
    }
}

void addToRecent(const PageHeader& h) {
    if (!h.id[0]) return;
    /* Remove any existing entry with the same ID */
    for (int i = 0; i < gRecentCount; i++) {
        if (sameId(gRecentPages[i].id, h.id)) {
            memmove(&gRecentPages[i], &gRecentPages[i+1],
                    sizeof(PageHeader) * (gRecentCount - i - 1));
            gRecentCount--;
            break;
        }
    }
    /* Prepend at front, cap at kMaxRecentPages */
    if (gRecentCount < kMaxRecentPages) gRecentCount++;
    memmove(&gRecentPages[1], &gRecentPages[0],
            sizeof(PageHeader) * (gRecentCount - 1));
    gRecentPages[0] = h;
    SaveRecentPages(gRecentPages, gRecentCount);
}

void SyncCurrentPageInHistory() {
    PageHeader cur; buildCurrentHeader(cur);
    for (int i = 0; i < gHistBackCount; i++) {
        if (sameId(gHistBack[i].id, cur.id)) {
            CopyPageMeta(gHistBack[i], cur);
        }
    }
    for (int i = 0; i < gHistFwdCount; i++) {
        if (sameId(gHistFwd[i].id, cur.id)) {
            CopyPageMeta(gHistFwd[i], cur);
        }
    }
    for (int i = 0; i < gRecentCount; i++) {
        if (sameId(gRecentPages[i].id, cur.id)) {
            CopyPageMeta(gRecentPages[i], cur);
        }
    }
    SaveNavHistory(gHistBack, gHistBackCount, gHistFwd, gHistFwdCount);
    SaveRecentPages(gRecentPages, gRecentCount);
}

void navigateBackN(int n) {
    if (n <= 0 || gHistBackCount == 0) return;
    if (n > gHistBackCount) n = gHistBackCount;
    int targetIdx = gHistBackCount - n; /* index of page we land on */

    /* Build current header before we touch any stacks. */
    PageHeader cur; buildCurrentHeader(cur);
    sNavOrigin = cur; sHasNavOrigin = true;

    /* Prepend [gHistBack[targetIdx+1 .. end], cur] to gHistFwd so that
       pressing Forward afterwards traces the same path back. */
    int addCount = (gHistBackCount - 1 - targetIdx) + 1; /* pages we're skipping + current */
    /* Shift existing fwd right; drop entries that no longer fit. */
    int keepFwd = gHistFwdCount;
    if (keepFwd + addCount > kNavHistMax) keepFwd = kNavHistMax - addCount;
    if (keepFwd < 0) keepFwd = 0;
    if (addCount > 0)
        memmove(&gHistFwd[addCount], &gHistFwd[0], sizeof(PageHeader) * keepFwd);
    gHistFwdCount = keepFwd + addCount;
    int fi = 0;
    for (int i = targetIdx + 1; i < gHistBackCount && fi < addCount - 1; i++, fi++)
        gHistFwd[fi] = gHistBack[i];
    if (fi < addCount) gHistFwd[fi] = cur;

    /* Save target header before truncating back stack. */
    g_pendingNavHeader    = gHistBack[targetIdx];
    g_hasPendingNavHeader = true;
    char targetId[64]; SafeStrCopy(targetId, sizeof(targetId), gHistBack[targetIdx].id);
    gHistBackCount = targetIdx;

    stripHyphens(targetId, gSettings.pageID, sizeof(gSettings.pageID));
    SaveSettings(gSettings);
    SaveNavHistory(gHistBack, gHistBackCount, gHistFwd, gHistFwdCount);
    scheduleReload();
}

void navigateForwardN(int n) {
    if (n <= 0 || gHistFwdCount == 0) return;
    if (n > gHistFwdCount) n = gHistFwdCount;

    PageHeader cur; buildCurrentHeader(cur);
    sNavOrigin = cur; sHasNavOrigin = true;

    /* Push current + n-1 fwd entries (that we skip over) onto back. */
    pushToBack(cur);
    for (int i = 0; i < n - 1; i++) pushToBack(gHistFwd[i]);

    /* Save the target (fwd[n-1]) before removing it from fwd. */
    g_pendingNavHeader    = gHistFwd[n-1];
    g_hasPendingNavHeader = true;
    char targetId[64]; SafeStrCopy(targetId, sizeof(targetId), gHistFwd[n-1].id);
    /* Remove the first n entries from fwd. */
    gHistFwdCount -= n;
    if (gHistFwdCount > 0)
        memmove(&gHistFwd[0], &gHistFwd[n], sizeof(PageHeader) * gHistFwdCount);

    stripHyphens(targetId, gSettings.pageID, sizeof(gSettings.pageID));
    SaveSettings(gSettings);
    SaveNavHistory(gHistBack, gHistBackCount, gHistFwd, gHistFwdCount);
    scheduleReload();
}

bool findCachedHeader(const char* pageId, PageHeader& out) {
    if (!pageId || !pageId[0]) return false;
    for (int i = 0; i < gHistBackCount; i++)
        if (sameId(gHistBack[i].id, pageId)) { out = gHistBack[i]; return true; }
    for (int i = 0; i < gHistFwdCount; i++)
        if (sameId(gHistFwd[i].id, pageId)) { out = gHistFwd[i]; return true; }
    for (int i = 0; i < gRecentCount; i++)
        if (sameId(gRecentPages[i].id, pageId)) { out = gRecentPages[i]; return true; }
    /* Fallback: search the disk page list cache (covers Library picks and
       child-page navigations to pages not yet in history/recent). */
    NotionPageList* pl = (NotionPageList*)NewPtrClear((long)sizeof(NotionPageList));
    if (pl) {
        if (LoadPageListCache(gSettings.apiKey, *pl)) {
            for (int i = 0; i < pl->count; i++) {
                if (sameId(pl->pages[i].id, pageId)) {
                    memset(&out, 0, sizeof(out));
                    setId(out.id, pl->pages[i].id);
                    SafeStrCopy(out.title,    sizeof(out.title),    pl->pages[i].title);
                    SafeStrCopy(out.iconName, sizeof(out.iconName), pl->pages[i].iconName);
                    DisposePtr((Ptr)pl);
                    return true;
                }
            }
        }
        DisposePtr((Ptr)pl);
    }
    return false;
}

/* InstallPlaceholderTitle — install h as the sole placeholder title block.
   Single construction point shared by showCachedPreview and reloadPage so
   the two can't diverge.  Always sets the block id (h.id, falling back to
   EffPageID()): an id-less placeholder is live for the whole fetch window,
   and doSave() silently drops blocks with empty ids — typing into the
   title during the load would be lost.                                   */
void InstallPlaceholderTitle(const PageHeader& h) {
    pageElm[0] = Block();
    pageElm[0].type = BT_TITLE;
    short tlen = (short)strlen(h.title);
    if (tlen >= kMaxTextLen) tlen = kMaxTextLen - 1;
    memcpy(pageElm[0].text, h.title, tlen);
    pageElm[0].text[tlen] = '\0';
    pageElm[0].textLen = tlen;
    setId(pageElm[0].id, h.id[0] ? h.id : EffPageID());
    pageElmCount = 1;

    SafeStrCopy(gPageIconName, sizeof(gPageIconName), h.iconName);
    g_titleIsPlaceholder = true;
}

/* Show the cached header for page h immediately.
   Disposes body block TEs, installs h as the sole placeholder block,
   and calls updateBlocks() so stale body content is cleared right away.
   Called from showNavPreview (Cmd+[/]) and scheduleNavigateTo (page links). */
static void showCachedPreview(const PageHeader& h) {
    if (!gWindow || !pageElm || gMaxBlocks < 1) return;

    /* Nil active TE ref to prevent dangling access after body TEs are freed */
    textActive  = nil;
    blockSelect = 0;

    DisposeAllTEHandles();

    InstallPlaceholderTitle(h);

    /* Full redraw: shows cached title+icon, erases stale body content */
    updateBlocks();
    ValidRect(&gWindow->portRect);
}

/* Window-title chrome: leading space matches MakeNewWindow's layout hack.
   Builds a proper Pascal string (length byte first) so titles of any length
   render correctly. */
void setWindowTitleChrome(const char* title) {
    if (!gWindow) return;
    char cstr[256];
    cstr[0] = ' ';
    strncpy(cstr + 1, title, 253);
    cstr[254] = '\0';
    int len = (int)strlen(cstr);
    if (len > 255) len = 255;
    Str255 pstr;
    pstr[0] = (unsigned char)len;
    memcpy(pstr + 1, cstr, len);
    SetWTitle(gWindow, pstr);
}

void showNavPreview() {
    PageHeader h;
    if (!peekNavHeader(h)) return;
    setWindowTitleChrome(h.title[0] ? h.title : "New Page");
    /* Show the cached header immediately — no DrawStatusText full-screen flash */
    if (h.title[0]) showCachedPreview(h);
}

/* Startup header preview: render the current page's cached title+icon as
   a placeholder (status text + fresh fetch follow in main).  Returns false
   when no header is cached — caller falls back to full-screen status text. */
bool ShowStartupHeaderPreview() {
    PageHeader h;
    if (!findCachedHeader(EffPageID(), h) || !h.title[0]) return false;
    setWindowTitleChrome(h.title);
    showCachedPreview(h);
    return true;
}

void scheduleNavigateTo(const char* pageID) {
    SafeStrCopy(gNavTarget, sizeof(gNavTarget), pageID);
    gNavDelta              = 0;
    gNavPendingTick        = TickCount();
    gAppState.navPending   = true;
    /* Show cached header immediately so old (possibly scrolled) body
       is cleared before the nav delay fires — mirrors showNavPreview behaviour */
    if (gWindow) {
        PageHeader h;
        if (findCachedHeader(pageID, h) && h.title[0])
            showCachedPreview(h);
    }
}

void commitNavigateTo(const char* pageID) {
    PageHeader cur; buildCurrentHeader(cur);
    sNavOrigin = cur; sHasNavOrigin = true;
    pushToBack(cur);
    gHistFwdCount = 0;
    stripHyphens(pageID, gSettings.pageID, sizeof(gSettings.pageID));
    SaveSettings(gSettings);
    SaveNavHistory(gHistBack, gHistBackCount, gHistFwd, gHistFwdCount);
    scheduleReload();
}

/* ReturnFromMissingPage — the current page is gone (404).  Load the page
   the last navigation left again.  That navigation put it on top of Back
   (link, Forward) or in Forward (Back); take it off there, since it's the
   current page once more.  Returns false when there's no page to go back
   to (e.g. the start-up page is the missing one). */
bool ReturnFromMissingPage() {
    if (!sHasNavOrigin || !sNavOrigin.id[0] || sameId(sNavOrigin.id, EffPageID()))
        return false;
    sHasNavOrigin = false;
    if (gHistBackCount > 0 && sameId(gHistBack[gHistBackCount - 1].id, sNavOrigin.id)) {
        gHistBackCount--;
    } else {
        for (int i = 0; i < gHistFwdCount; i++) {
            if (sameId(gHistFwd[i].id, sNavOrigin.id)) {
                gHistFwdCount--;
                memmove(&gHistFwd[i], &gHistFwd[i + 1], sizeof(PageHeader) * (gHistFwdCount - i));
                break;
            }
        }
    }
    g_pendingNavHeader    = sNavOrigin;
    g_hasPendingNavHeader = true;
    stripHyphens(sNavOrigin.id, gSettings.pageID, sizeof(gSettings.pageID));
    SaveSettings(gSettings);
    SaveNavHistory(gHistBack, gHistBackCount, gHistFwd, gHistFwdCount);
    scheduleReload();
    return true;
}

/* ------------------------------------------------------------------ */
/* PurgePageFromHistory — remove a deleted page from all nav stores   */
/* ------------------------------------------------------------------ */
static void PurgeFromArray(PageHeader arr[], int& count, const char* wantId) {
    int dst = 0;
    for (int i = 0; i < count; i++)
        if (!sameId(arr[i].id, wantId))
            arr[dst++] = arr[i];
    count = dst;
}

void PurgePageFromHistory(const char* pageIdNH) {
    if (!pageIdNH || !pageIdNH[0]) return;
    /* sameId is hyphen-insensitive — pass the no-hyphen id directly. */
    PurgeFromArray(gHistBack,    gHistBackCount, pageIdNH);
    PurgeFromArray(gHistFwd,     gHistFwdCount,  pageIdNH);
    PurgeFromArray(gRecentPages, gRecentCount,   pageIdNH);
    SaveNavHistory(gHistBack, gHistBackCount, gHistFwd, gHistFwdCount);
    SaveRecentPages(gRecentPages, gRecentCount);
}

/* ------------------------------------------------------------------ */
/* OnNewPageResponse — POST /v1/pages callback                        */
/* ------------------------------------------------------------------ */
void OnNewPageResponse(HttpResponse& resp) {
    gAppState.apiPending = false;
    if (resp.Success && resp.StatusCode == 200) {
        gason::JsonAllocator alloc;
        gason::JsonValue     root;
        if (gason::jsonParse((char*)resp.Content.c_str(), root, alloc) == gason::JSON_PARSE_OK) {
            bool _ok;
            const char* newId = root("id").toString(&_ok);
            if (newId && newId[0]) {
                /* Strip hyphens — Notion returns hyphenated UUIDs; cache uses no-hyphen */
                char cleanId[64];
                stripHyphens(newId, cleanId, sizeof(cleanId));

                /* Defer navigation — cannot call reloadPage() from inside the
                   HTTP callback (we are mid-ProcessRequests, resetting the
                   client here would crash). Event loop picks this up next tick. */
                SafeStrCopy(gNewPageID, sizeof(gNewPageID), cleanId);
                gAppState.newPageReady = true;

                /* Prepend the new page to the page list cache so it appears
                   first in the Open Page picker without a fresh fetch. */
                const char* apiKey = EffAPIKey();
                NotionPageList updated; updated.count = 0;
                LoadPageListCache(apiKey, updated); /* ignore failure — start from empty */
                int newCount = updated.count + 1;
                if (newCount > kMaxNotionPages) newCount = kMaxNotionPages;
                for (int i = newCount - 1; i > 0; i--)
                    updated.pages[i] = updated.pages[i - 1];
                memset(&updated.pages[0], 0, sizeof(NotionPageEntry));
                SafeStrCopy(updated.pages[0].id,    sizeof(updated.pages[0].id),    cleanId);
                SafeStrCopy(updated.pages[0].title, sizeof(updated.pages[0].title), "New Page");
                SafeStrCopy(updated.pages[0].pageParentId, 37, gNewPageParentIdNH);
                updated.pages[0].spriteX = -1;
                updated.count = newCount;
                SavePageListCache(apiKey, updated);
                return;
            }
        }
    }
    char msg[64];
    if (!resp.Success)
        SafeStrCopy(msg, sizeof(msg), "Create page failed: network error.");
    else
        snprintf(msg, sizeof(msg), "Create page failed (HTTP %u).", resp.StatusCode);
    DEBUG_LOG("NewPage", msg);
    if (resp.Success && resp.StatusCode == 401) {
        gAppState.reloadIsNetworkError = false;
        gAppState.needsReload = true;
    } else {
        DrawStatusText(msg);
    }
}
