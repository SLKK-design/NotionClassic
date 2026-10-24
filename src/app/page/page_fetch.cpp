/* page_fetch.cpp — Notion page list fetching and child-fetch utilities
   Extracted from main.cpp: pageRequest, ParseNotionPageListAppend,
   FetchNotionPageList thread globals, DoFetchPages, stripHyphens,
   addHyphensToId, ChildFetchState/ChildFetchThreadFunc,
   DoFetchPageChildren, DoLibMovePage, UpdatePageListCacheEntry.        */

#include <string.h>
#include <string>
#include <Threads.h>
#include "page_fetch.h"
#include "app_state.h"
#include "helpers.h"
#include "HttpClient.h"
#include "Settings.h"
#include "library_window.h"   // LibPageEntry, FetchPageChildrenFunc, LibMovePageFunc
#include "navigation.h"       // PurgePageFromHistory
#include "debug_con.h"
#include "gason.hpp"
#include "picker.h"
#include "emoji_sprite.h"
#include "response_parser.h"

using std::string;

/* ------------------------------------------------------------------ */
/* Externs — defined in main.cpp                                       */
/* ------------------------------------------------------------------ */
extern HttpClient   gHttpClient;
extern AppSettings  gSettings;
extern AppState     gAppState;
extern ThreadID     gSaveThreadID;
extern LibPageEntry*        gLibPageElm;
extern int                  gLibPageCount;
extern LibToggleCacheEntry* gLibToggleCache;
extern int                  gLibToggleCacheCount;

extern const char* EffAPIKey();
extern const char* EffPageID();
extern string BlocksURL(const char* id);
extern string PagesURL(const char* id);
extern int   gCurRequest;
extern long  gRequestStartTick;
extern char  gNextCursor[256];
extern bool  gHasMore;

extern const long kFetchThreadStack;

/* ------------------------------------------------------------------ */
/* ParseIconNode — extract icon info from Notion JSON icon object.    */
/* Handles both "emoji" (sprite coords) and "icon" (slug name).       */
/* ------------------------------------------------------------------ */
static void ParseIconNode(gason::JsonValue iconNode,
                          short& spriteX, short& spriteY,
                          char* iconName, int iconNameSize,
                          short* iconSprIdx,
                          bool stripVariations)
{
    spriteX = -1; spriteY = -1;
    if (iconSprIdx) *iconSprIdx = -1;
    if (iconNode.getTag() == gason::JSON_NULL) return;
    bool okT = false;
    const char* itype = iconNode("type").toString(&okT);
    if (!okT || !itype) return;
    if (strcmp(itype, "emoji") == 0) {
        bool okE = false;
        const char* emojiStr = iconNode("emoji").toString(&okE);
        if (okE && emojiStr && emojiStr[0]) {
            const char* lookup = emojiStr;
            char stripped[16];
            if (stripVariations)
                lookup = StripVariationSelectors(emojiStr, stripped, sizeof(stripped));
            const EmojiSprite* sp = emoji_find_fallback(lookup);
            if (sp) { spriteX = sp->x; spriteY = sp->y; }
        }
    } else if (strcmp(itype, "icon") == 0) {
        bool okN = false;
        const char* slug = iconNode("icon")("name").toString(&okN);
        if (okN && slug && slug[0]) {
            SafeStrCopy(iconName, iconNameSize, slug);
            if (iconSprIdx) *iconSprIdx = FindIconSpriteIdx(slug);
        }
    }
}

/* ------------------------------------------------------------------ */
/* CopyTitleToMacRoman — convert a UTF-8 page title to MacRoman,      */
/* NUL-terminated, and record its emoji in titleEmoji[kTitleEmojiMax]. */
/* ------------------------------------------------------------------ */
static void CopyTitleToMacRoman(const char* utf8, char* dst, int dstSize,
                                short* titleEmoji) {
    static char seqs[kTitleEmojiMax][32];   /* static: off the thread stack */
    uint8_t n = 0;
    short len = utf8_to_macroman_into(utf8, dst, (short)(dstSize - 1),
                                      seqs, &n, kTitleEmojiMax);
    dst[len] = '\0';
    TitleEmojiFromSeqs(seqs, n, titleEmoji);
}

/* ------------------------------------------------------------------ */
/* Local helper — mirrors SetBearer in main.cpp                        */
/* ------------------------------------------------------------------ */
static inline void SetBearer(const char* key)
{
    char buf[270]; snprintf(buf, sizeof(buf), "Bearer %s", key);
    gHttpClient.SetAuthorization(buf);
}

/* ------------------------------------------------------------------ */
/* pageRequest                                                         */
/* ------------------------------------------------------------------ */
void pageRequest()
{
    { char pbuf[24]; snprintf(pbuf, sizeof(pbuf), "req=%d", gCurRequest);
      DEBUG_LOG("pageRequest", pbuf); }
    gRequestStartTick = TickCount();
    const char* pid = EffPageID();
    string url;
    if (gCurRequest == kPhaseIdle) {
        url = PagesURL(pid);
    } else {
        url = BlocksURL(pid) + "/children?page_size=100";
        if (gNextCursor[0] != '\0') {
            url += "&start_cursor="; url += gNextCursor;
        }
    }
    gHttpClient.Get(url, OnGetResponse);
}

/* ------------------------------------------------------------------ */
/* stripHyphens — copy src into out removing '-' chars                */
/* out must be at least 33 bytes for a 32-char UUID result.           */
/* ------------------------------------------------------------------ */
void stripHyphens(const char* src, char* out, int outLen)
{
    int di = 0;
    for (int i = 0; src[i] && di < outLen - 1; i++)
        if (src[i] != '-') out[di++] = src[i];
    out[di] = '\0';
}


/* ------------------------------------------------------------------ */
/* Deleted-page blocklist — IDs trashed in the current session that   */
/* the search API may not have caught up with yet.                    */
/* ------------------------------------------------------------------ */
#define kMaxDeletedIds 64   /* a deleted page takes its subpages along */
static char  g_deletedIds[kMaxDeletedIds][33];
static int   g_deletedCount = 0;

static void AddDeletedId(const char* idNH) {
    if (g_deletedCount >= kMaxDeletedIds) {
        memmove(g_deletedIds[0], g_deletedIds[1], sizeof(g_deletedIds[0]) * (kMaxDeletedIds - 1));
        g_deletedCount = kMaxDeletedIds - 1;
    }
    SafeStrCopy(g_deletedIds[g_deletedCount], (int)sizeof(g_deletedIds[0]), idNH);
    g_deletedCount++;
}
static bool IsDeleted(const char* idNH) {
    for (int i = 0; i < g_deletedCount; i++)
        if (strcmp(g_deletedIds[i], idNH) == 0) return true;
    return false;
}

/* ParseNotionPageListAppend — parse one page of /v1/search results   */
/* into a growable Handle.  Also extracts has_more + next_cursor.     */
/* ------------------------------------------------------------------ */
static void ParseNotionPageListAppend(const string& body,
                                      Handle pagesH, int& count,
                                      bool& hasMore, string& nextCursor)
{
    hasMore = false; nextCursor = "";
    gason::JsonAllocator alloc;
    gason::JsonValue     root;
    if (gason::jsonParse((char*)body.c_str(), root, alloc) != gason::JSON_PARSE_OK)
        return;

    /* Pagination fields */
    hasMore = (root("has_more").getTag() == gason::JSON_TRUE);
    if (hasMore) {
        bool okC = false;
        const char* cur = root("next_cursor").toString(&okC);
        if (okC && cur && cur[0]) nextCursor = cur;
        else hasMore = false;
    }

    gason::JsonIterator it = gason::begin(root("results"));
    while (it.isValid() && count < kMaxNotionPages) {
        gason::JsonValue pg    = it->value;

        /* Skip trashed pages */
        if (pg("in_trash").getTag() == gason::JSON_TRUE) { it++; continue; }

        bool okId = false;
        const char*      rawId = pg("id").toString(&okId);
        if (!okId || !rawId || !rawId[0]) { it++; continue; }

        /* Strip hyphens once — used for both blocklist check and storage */
        char cleanId[64];
        stripHyphens(rawId, cleanId, sizeof(cleanId));

        /* Skip pages deleted this session (search API eventual consistency) */
        if (IsDeleted(cleanId)) { it++; continue; }

        /* Title */
        const char* titleStr = nullptr;
        gason::JsonValue props = pg("properties");
        static const char* kPropNames[] = { "title", "Name", "Title", nullptr };
        for (int pi = 0; kPropNames[pi] && !titleStr; pi++) {
            gason::JsonValue tp = props(kPropNames[pi]);
            if (tp.getTag() != gason::JSON_NULL) {
                bool ok = false;
                const char* pt = tp("title").at(0)("plain_text").toString(&ok);
                if (ok && pt && pt[0]) titleStr = pt;
            }
        }
        if (!titleStr || !titleStr[0]) { it++; continue; }

        /* Grow Handle by one entry */
        SetHandleSize(pagesH, (long)(count + 1) * sizeof(NotionPageEntry));
        if (MemError() != noErr) { hasMore = false; break; }

        NotionPageEntry& entry = ((NotionPageEntry*)*pagesH)[count];
        memset(&entry, 0, sizeof(entry));
        strncpy(entry.id, cleanId, sizeof(entry.id) - 1);

        CopyTitleToMacRoman(titleStr, entry.title, (int)sizeof(entry.title), entry.titleEmoji);

        /* Icon */
        ParseIconNode(pg("icon"), entry.spriteX, entry.spriteY,
                       entry.iconName, (int)sizeof(entry.iconName), nil, true);

        /* Parent — strip hyphens so it matches the no-hyphen id field */
        gason::JsonValue parentNode = pg("parent");
        if (parentNode.getTag() != gason::JSON_NULL) {
            bool okPT = false;
            const char* parentType = parentNode("type").toString(&okPT);
            if (okPT && parentType && strcmp(parentType, "page_id") == 0) {
                bool okPI = false;
                const char* parentRaw = parentNode("page_id").toString(&okPI);
                if (okPI && parentRaw && parentRaw[0]) {
                    stripHyphens(parentRaw, entry.pageParentId, sizeof(entry.pageParentId));
                }
            }
        }

        count++;
        it++;
    }
}

/* ------------------------------------------------------------------ */
/* FetchNotionPageList — POST /v1/search, paginated, cooperative thread*/
/* ------------------------------------------------------------------ */
static Handle g_fetchPagesH       = nil;
static int    g_fetchPageCount    = 0;
static bool   g_fetchHasMore      = false;
static string g_fetchNextCursor;
static bool   g_fetchDone         = false;
static bool   g_fetchInProgress   = false;
static bool   g_fetchThreadActive = false;
static string g_fetchReqBody;
static int    g_lastFetchStatus   = 0;   /* HTTP status of most recent /v1/search */

int GetLastFetchStatus() { return g_lastFetchStatus; }

/* Thread function — one cooperative thread per pagination request.
   TLS/ECDH handshakes use significant stack (P-256 scalar mult +
   certificate chain walk).  Running on a separate cooperative thread
   avoids overflowing the main thread's stack — SetApplLimit is
   unreliable once the ROM's MaxApplZone has run.                    */
static pascal void* FetchPageThreadFunc(void*)
{
    gHttpClient.Post("https://api.notion.com/v1/search", g_fetchReqBody,
        [](HttpResponse& r) {
            g_lastFetchStatus = r.Success ? r.StatusCode : 0;
            if (r.Success && r.StatusCode == 200)
                ParseNotionPageListAppend(r.Content, g_fetchPagesH,
                                          g_fetchPageCount,
                                          g_fetchHasMore, g_fetchNextCursor);
            else
                g_fetchHasMore = false;
            g_fetchDone       = true;
            g_fetchInProgress = false;
        });
    g_fetchThreadActive = false;
    return nil;
}

/* Run one fetch page synchronously or via cooperative thread.        */
static void RunOneFetchPage(DialogPtr dlg)
{
    g_fetchDone         = false;
    g_fetchInProgress   = true;
    g_fetchThreadActive = true;

    ThreadID fetchTID = 0;
    OSErr terr = NewThread(kCooperativeThread,
                           (ThreadEntryUPP)FetchPageThreadFunc, nil,
                           kFetchThreadStack, kCreateIfNeeded, nil, &fetchTID);
    if (terr != noErr) {
        g_fetchThreadActive = false;
        FetchPageThreadFunc(nil);
    } else {
        while (g_fetchThreadActive) {
            EventRecord e;
            WaitNextEvent(everyEvent, &e, 1, nil);
            if (e.what == updateEvt) {
                WindowPtr wp = (WindowPtr)e.message;
                BeginUpdate(wp);
                if (((WindowPeek)wp)->windowKind == dialogKind)
                    DrawDialog((DialogPtr)wp);
                if (dlg && (DialogPtr)wp == dlg)
                    DrawDialog(dlg);
                EndUpdate(wp);
            }
            YieldToAnyThread();
        }
        /* Dispose the terminated thread — leaving it alive causes a
           trace-trap (type 8) when the Thread Manager reschedules
           a terminated cooperative thread on the next event tick.  */
        DisposeThread(fetchTID, nil, false);
    }
}

static bool FetchNotionPageList(const char* apiKey, NotionPageList& out)
{
    DEBUG_LOG("FetchPageList", "TLS start");
    out.count = 0;
    if (!apiKey || !apiKey[0]) return false;
    if (g_fetchInProgress)     return false;

    g_fetchPagesH     = NewHandle(0);
    if (!g_fetchPagesH) return false;
    g_fetchPageCount  = 0;
    g_fetchHasMore    = true;
    g_fetchNextCursor = "";

    SetBearer(apiKey);
    Size grow; MaxMem(&grow);

    while (g_fetchHasMore && g_fetchPageCount < kMaxNotionPages) {
        /* Stop if heap too tight for the next thread + cushion */
        if (FreeMem() < 300L * 1024L) {
            DEBUG_LOG("FetchPageList", "heap low, stop paginate");
            break;
        }

        g_fetchReqBody =
            "{\"filter\":{\"value\":\"page\",\"property\":\"object\"},"
            "\"sort\":{\"direction\":\"descending\",\"timestamp\":\"last_edited_time\"},"
            "\"page_size\":100";
        if (!g_fetchNextCursor.empty()) {
            g_fetchReqBody += ",\"start_cursor\":\"";
            g_fetchReqBody += g_fetchNextCursor;
            g_fetchReqBody += "\"";
        }
        g_fetchReqBody += "}";

        RunOneFetchPage(nil);

        { char pbuf[40];
          snprintf(pbuf, sizeof(pbuf), "page done count=%d more=%d",
                   g_fetchPageCount, (int)g_fetchHasMore);
          DEBUG_LOG("FetchPageList", pbuf); }
    }

    /* Copy Handle contents into fixed-size output struct */
    if (g_fetchPageCount > 0) {
        int n = g_fetchPageCount < kMaxNotionPages ? g_fetchPageCount : kMaxNotionPages;
        HLock(g_fetchPagesH);
        memcpy(out.pages, *g_fetchPagesH, (size_t)n * sizeof(NotionPageEntry));
        HUnlock(g_fetchPagesH);
        out.count = n;
    }
    DisposeHandle(g_fetchPagesH); g_fetchPagesH = nil;
    return out.count > 0;
}

/* ------------------------------------------------------------------ */
/* UpdatePageListCacheEntry — shadow-update mem + disk caches and     */
/* Library display list when the current page's title or icon changes */
/* without waiting for a full re-fetch from the API.                  */
/* noHyphenId: 32-char hex UUID (gSettings.pageID format).           */
/* Pass nullptr for fields that have not changed. titleEmoji goes     */
/* with newTitle (kTitleEmojiMax entries).                             */
/* ------------------------------------------------------------------ */
void UpdatePageListCacheEntry(const char* noHyphenId,
                              const char* newTitle,
                              const char* newIconName,
                              const short* titleEmoji)
{
    static const short kNoEmoji[kTitleEmojiMax] = { 0 };
    if (!titleEmoji) titleEmoji = kNoEmoji;
    if (!noHyphenId || !noHyphenId[0]) return;

    /* Update disk cache — load, patch matching entry, save. */
    {
        NotionPageList disk; disk.count = 0;
        if (LoadPageListCache(gSettings.apiKey, disk) && disk.count > 0) {
            bool changed = false;
            for (int i = 0; i < disk.count; i++) {
                if (strcmp(disk.pages[i].id, noHyphenId) == 0) {
                    if (newTitle) {
                        SafeStrCopy(disk.pages[i].title, sizeof(disk.pages[i].title), newTitle);
                        memcpy(disk.pages[i].titleEmoji, titleEmoji, sizeof(disk.pages[i].titleEmoji));
                    }
                    if (newIconName) {
                        SafeStrCopy(disk.pages[i].iconName, sizeof(disk.pages[i].iconName), newIconName);
                        disk.pages[i].spriteX = -1;   /* set or removed: no emoji either way */
                        disk.pages[i].spriteY = -1;
                    }
                    changed = true;
                    break;
                }
            }
            if (changed)
                SavePageListCache(gSettings.apiKey, disk);
        }
    }

    /* Update Library display list only while it is open. */
    if (!gLibPageElm) return;
    for (int i = 0; i < gLibPageCount; i++) {
        if (strcmp(gLibPageElm[i].id, noHyphenId) == 0) {
            if (newTitle) {
                SafeStrCopy(gLibPageElm[i].title, sizeof(gLibPageElm[i].title), newTitle);
                memcpy(gLibPageElm[i].titleEmoji, titleEmoji, sizeof(gLibPageElm[i].titleEmoji));
            }
            if (newIconName) {
                SafeStrCopy(gLibPageElm[i].iconName, sizeof(gLibPageElm[i].iconName), newIconName);
                gLibPageElm[i].iconSprIdx = FindIconSpriteIdx(newIconName);
                gLibPageElm[i].spriteX = -1;
                gLibPageElm[i].spriteY = -1;
            }
            /* Also scan toggle cache children */
            for (int tc = 0; tc < gLibToggleCacheCount; tc++) {
                for (int ci = 0; ci < gLibToggleCache[tc].childCount; ci++) {
                    if (strcmp(gLibToggleCache[tc].children[ci].id, noHyphenId) == 0) {
                        LibPageEntry& ch = gLibToggleCache[tc].children[ci];
                        if (newTitle) {
                            SafeStrCopy(ch.title, sizeof(ch.title), newTitle);
                            memcpy(ch.titleEmoji, titleEmoji, sizeof(ch.titleEmoji));
                        }
                        if (newIconName) {
                            SafeStrCopy(ch.iconName, sizeof(ch.iconName), newIconName);
                            ch.iconSprIdx = FindIconSpriteIdx(newIconName);
                            ch.spriteX = -1;
                            ch.spriteY = -1;
                        }
                    }
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* DoFetchPages — FetchPagesFunc callback passed to ShowSettingsDialog */
/* ------------------------------------------------------------------ */
static bool g_pageListCacheBypass = false;

void InvalidatePageListCache() { g_pageListCacheBypass = true; }

bool DoFetchPages(const char* apiKey, NotionPageList& list)
{
    /* Drain any active save/API or fetch cooperative thread before
       reusing gHttpClient — including in-flight per-block (apiPending)
       and toggle-load (togglePending) requests, which also share it.  */
    while (gAppState.saveThreadActive || g_fetchThreadActive ||
           gAppState.apiPending || gAppState.togglePending)
        YieldToAnyThread();

    /* Try disk cache first (persists across sessions). */
    if (!g_pageListCacheBypass && LoadPageListCache(apiKey, list) && list.count > 0) {
        DEBUG_LOG("DoFetchPages", "disk cache hit");
        return true;
    }
    g_pageListCacheBypass = false;

    /* Cache miss: fetch from Notion, reusing the connection if still alive. */
    DEBUG_LOG("DoFetchPages", "cache miss -> TLS fetch");
    bool ok = FetchNotionPageList(apiKey, list);
    if (ok) {
        char pbuf[32]; snprintf(pbuf, sizeof(pbuf), "got %d pages", list.count);
        DEBUG_LOG("DoFetchPages", pbuf);
        SavePageListCache(apiKey, list);
    } else {
        DEBUG_LOG("DoFetchPages", "TLS fetch failed");
    }
    return ok;
}

/* ------------------------------------------------------------------ */
/* DoFetchPageChildren — FetchPageChildrenFunc implementation          */
/* GET /v1/blocks/{parentId}/children, filter for child_page type.    */
/* parentIdNH: 32-char no-hyphen UUID.                                */
/* ------------------------------------------------------------------ */
struct ChildFetchState {
    LibPageEntry* out;
    int           maxOut;
    int           outCount;
    bool          done;
    bool          ok;
};
static ChildFetchState g_childFetch;

static pascal void* ChildFetchThreadFunc(void* param)
{
    const char* url = (const char*)param;
    gHttpClient.Get(url,
        [](HttpResponse& r) {
            g_childFetch.ok = false;
            if (!r.Success || r.StatusCode != 200) {
                g_childFetch.done = true; return;
            }
            /* Parse child_page blocks from results[] */
            gason::JsonAllocator alloc;
            gason::JsonValue     root;
            if (gason::jsonParse((char*)r.Content.c_str(), root, alloc) !=
                gason::JSON_PARSE_OK) {
                g_childFetch.done = true; return;
            }
            g_childFetch.ok = true;
            g_childFetch.outCount = 0;
            gason::JsonIterator it = gason::begin(root("results"));
            while (it.isValid() &&
                   g_childFetch.outCount < g_childFetch.maxOut) {
                gason::JsonValue blk = it->value;
                bool okT = false;
                const char* btype = blk("type").toString(&okT);
                if (!okT || !btype || strcmp(btype, "child_page") != 0) {
                    it++; continue;
                }
                bool okId2 = false;
                const char* rawId = blk("id").toString(&okId2);
                if (!okId2 || !rawId || !rawId[0]) { it++; continue; }
                bool okTi = false;
                const char* titleStr =
                    blk("child_page")("title").toString(&okTi);

                LibPageEntry& le =
                    g_childFetch.out[g_childFetch.outCount];
                memset(&le, 0, sizeof(le));

                /* Strip hyphens for id */
                stripHyphens(rawId, le.id, sizeof(le.id));

                if (okTi && titleStr && titleStr[0])
                    CopyTitleToMacRoman(titleStr, le.title, (int)sizeof(le.title), le.titleEmoji);

                /* Icon from block — child_page blocks may have icon field */
                ParseIconNode(blk("icon"), le.spriteX, le.spriteY,
                              le.iconName, (int)sizeof(le.iconName),
                              &le.iconSprIdx, false);

                g_childFetch.outCount++;
                it++;
            }
            g_childFetch.done = true;
        });
    return nil;
}

bool DoFetchPageChildren(const char* apiKey,
                         const char* parentIdNH,
                         LibPageEntry out[],
                         int maxOut,
                         int& outCount)
{
    outCount = 0;
    if (!apiKey || !apiKey[0] || !parentIdNH || !parentIdNH[0]) return false;

    char parentHyph[37];
    addHyphensToId(parentIdNH, parentHyph, (int)sizeof(parentHyph));

    string url = BlocksURL(parentHyph) + "/children?page_size=100";

    SetBearer(apiKey);

    g_childFetch.out      = out;
    g_childFetch.maxOut   = maxOut;
    g_childFetch.outCount = 0;
    g_childFetch.done     = false;
    g_childFetch.ok       = false;

    /* Run synchronously on main thread — called from Library event loop
       which already yields via WaitNextEvent; no separate cooperative
       thread needed for the short child-page fetch.                     */
    ChildFetchThreadFunc((void*)url.c_str());

    outCount = g_childFetch.outCount;
    return g_childFetch.ok;
}

/* ------------------------------------------------------------------ */
/* DoLibMovePage — LibMovePageFunc: POST /v1/pages/{id}/move          */
/* srcIdNH and newParentIdNH are 32-char no-hyphen UUIDs.  The API can */
/* only move a page under another page: "" (the workspace top level)   */
/* is refused here.  Called synchronously from the Library event loop. */
/* ------------------------------------------------------------------ */
static bool   g_libMoveOk   = false;
static string g_libMoveBody;

static pascal void* LibMoveThreadFunc(void* param)
{
    const char* url = (const char*)param;
    gHttpClient.Post(url, g_libMoveBody,
        [](HttpResponse& r) {
            g_libMoveOk = r.Success && r.StatusCode == 200;
            if (!g_libMoveOk)
                DEBUG_LOGF("libMove", "refused status=%d", r.StatusCode);
        });
    return nullptr;
}

bool DoLibMovePage(const char* apiKey,
                   const char* srcIdNH,
                   const char* newParentIdNH)
{
    if (!apiKey || !apiKey[0] || !srcIdNH || !srcIdNH[0]) return false;
    if (!newParentIdNH || !newParentIdNH[0]) return false;   /* no workspace parent in the API */

    char srcH[37], parentH[37];
    addHyphensToId(srcIdNH, srcH, (int)sizeof(srcH));
    addHyphensToId(newParentIdNH, parentH, (int)sizeof(parentH));
    string url = PagesURL(srcH) + "/move";
    g_libMoveBody  = "{\"parent\":{\"type\":\"page_id\",\"page_id\":\"";
    g_libMoveBody += parentH;
    g_libMoveBody += "\"}}";

    SetBearer(apiKey);
    g_libMoveOk = false;
    LibMoveThreadFunc((void*)url.c_str());
    return g_libMoveOk;
}

/* ------------------------------------------------------------------ */
/* DoLibDeletePage — archive a page from the Library (synchronous)     */
/* ------------------------------------------------------------------ */
static bool   g_libDelOk = false;

static pascal void* LibDeleteThreadFunc(void* param)
{
    const char* url = (const char*)param;
    gHttpClient.PatchWithBody(url, "{\"in_trash\":true}",
        [](HttpResponse& r) {
            g_libDelOk = r.Success && r.StatusCode == 200;
            /* 400 "already archived" = page is already trashed — treat as success */
            if (!g_libDelOk && r.StatusCode == 400 &&
                r.Content.find("archived") != string::npos)
                g_libDelOk = true;
        });
    return nullptr;
}

bool DoLibDeletePage(const char* apiKey, const char* pageIdNH)
{
    if (!apiKey || !apiKey[0] || !pageIdNH || !pageIdNH[0]) return false;
    char pidH[37];
    addHyphensToId(pageIdNH, pidH, (int)sizeof(pidH));
    SetBearer(apiKey);
    g_libDelOk = false;
    string url = PagesURL(pidH);
    LibDeleteThreadFunc((void*)url.c_str());
    if (g_libDelOk) {
        char one[1][33];
        SafeStrCopy(one[0], (int)sizeof(one[0]), pageIdNH);
        ForgetDeletedPages(apiKey, one, 1);
    } else {
        DEBUG_LOG("LibDel", "failed");
    }
    return g_libDelOk;
}

/* ForgetDeletedPages — after a delete (Notion trashes subpages along   */
/* with their parent): block the ids from search results, which can lag */
/* behind, and purge them from history, recent pages and the disk cache. */
void ForgetDeletedPages(const char* apiKey, const char (*idsNH)[33], int n)
{
    for (int k = 0; k < n; k++) {
        AddDeletedId(idsNH[k]);
        PurgePageFromHistory(idsNH[k]);
    }
    /* Disk cache: one load + save for the whole batch */
    NotionPageList* pl = (NotionPageList*)NewPtrClear((long)sizeof(NotionPageList));
    if (!pl) return;
    if (LoadPageListCache(apiKey, *pl)) {
        int dst = 0;
        for (int i = 0; i < pl->count; i++) {
            bool gone = false;
            for (int k = 0; k < n && !gone; k++)
                gone = strcmp(pl->pages[i].id, idsNH[k]) == 0;
            if (!gone) {
                if (dst != i) memcpy(&pl->pages[dst], &pl->pages[i], sizeof(NotionPageEntry));
                dst++;
            }
        }
        if (dst != pl->count) {
            pl->count = dst;
            SavePageListCache(apiKey, *pl);
        }
    }
    DisposePtr((Ptr)pl);
}
