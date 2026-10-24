/* library_window.cpp — Notion-style tree page picker.
   Extracted from Settings.cpp.                                        */
#include "library_window.h"
#include "Settings.h"
#include "helpers.h"
#include "menu.h"   /* Disable/RestoreDialogMenus, HandleDialogMenuBar */
#include "picker.h"
#include "page_fetch.h"
#include "debug_con.h"
#include "utf8_mac_lite.h"   /* kEmojiSentinel */

#include <Dialogs.h>
#include <Windows.h>
#include <TextEdit.h>
#include <Scrap.h>
#include <Events.h>
#include <Fonts.h>
#include <Lists.h>
#include <QDOffscreen.h>
#include <Sound.h>       /* SysBeep: refused move */
#include <string>
#include <string.h>
#include <ctype.h>
using std::string;


/* ------------------------------------------------------------------ */
/* SetDefaultTextStyle — apply the default text attributes after custom draw */
/* ------------------------------------------------------------------ */
static void SetDefaultTextStyle() { TextFont(0); TextSize(12); TextFace(0); }


/* ================================================================== */
/* Library window — Notion-style tree page picker                     */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/* Global display list — accessible from main.cpp for shadow updates  */
/* ------------------------------------------------------------------ */
LibPageEntry*        gLibPageElm        = nil;   /* heap: alloc on Library open, nil when closed */
int                  gLibPageCount      = 0;
LibToggleCacheEntry* gLibToggleCache    = nil;   /* heap: alloc on Library open, nil when closed */
int                  gLibToggleCacheCount = 0;

static bool sLibNewPageRequested  = false;
static char sLibNewPageParentId[64] = {0};  /* id of row where + was clicked; ""=root */
static bool sLibDidDelete         = false;  /* any page deleted this session */
static int  sLibSelIdx            = -1;  /* defined early so DrawNewPageBtn can read it */

bool LibNewPageWasRequested()
{
    bool v = sLibNewPageRequested;
    sLibNewPageRequested = false;
    return v;
}

bool LibNewPageIsPending()
{
    return sLibNewPageRequested;
}

const char* LibNewPageParentId()
{
    return sLibNewPageParentId;
}

bool LibDidDeletePages()
{
    return sLibDidDelete;
}

/* ------------------------------------------------------------------ */
/* Layout constants                                                    */
/* ------------------------------------------------------------------ */
static const short kLibRowHeight  = 26;
static const short kLibIndentStep = 14;   /* px per depth level  */
static const short kLibIconSize   = 16;   /* Notion icon px      */
static const short kLibTopLevelAlertID = 133;  /* ALRT: can't move to top level */
static const short kLibDeleteAlertID   = 134;  /* ALRT: delete page + subpages   */
static const short kLibScrollBarW = 16;
static const short kLibSearchRowH = 33;   /* search bar height incl. dotted divider */

/* ------------------------------------------------------------------ */
/* LibToggleCache helpers                                             */
/* ------------------------------------------------------------------ */
static long sLibCacheGen = 0;   /* monotonic counter for LRU eviction */

static LibToggleCacheEntry* LibToggleCacheFindOrCreate(const char* parentId)
{
    for (int i = 0; i < gLibToggleCacheCount; i++) {
        if (gLibToggleCache[i].valid &&
            strcmp(gLibToggleCache[i].parentId, parentId) == 0) {
            gLibToggleCache[i].accessGen = ++sLibCacheGen;
            return &gLibToggleCache[i];
        }
    }
    int slot = -1;
    /* First try a free slot */
    for (int i = 0; i < kLibToggleCacheSize; i++) {
        if (!gLibToggleCache[i].valid) { slot = i; break; }
    }
    /* All slots full — evict LRU (lowest accessGen) */
    if (slot < 0) {
        slot = 0;
        for (int i = 1; i < kLibToggleCacheSize; i++)
            if (gLibToggleCache[i].accessGen < gLibToggleCache[slot].accessGen)
                slot = i;
    }
    LibToggleCacheEntry* e = &gLibToggleCache[slot];
    memset(e, 0, sizeof(*e));
    SafeStrCopy(e->parentId, (int)sizeof(e->parentId), parentId);
    e->accessGen = ++sLibCacheGen;
    e->valid = true;
    if (slot == gLibToggleCacheCount && gLibToggleCacheCount < kLibToggleCacheSize)
        gLibToggleCacheCount++;
    return e;
}

static LibToggleCacheEntry* LibToggleCacheFind(const char* parentId)
{
    for (int i = 0; i < gLibToggleCacheCount; i++) {
        if (gLibToggleCache[i].valid &&
            strcmp(gLibToggleCache[i].parentId, parentId) == 0)
            return &gLibToggleCache[i];
    }
    return nil;
}

/* ------------------------------------------------------------------ */
/* findLibSubtreeEnd — first index past the last descendant of idx    */
/* ------------------------------------------------------------------ */
static int findLibSubtreeEnd(int idx)
{
    if (idx < 0 || idx >= gLibPageCount) return idx + 1;
    short parentDepth = gLibPageElm[idx].depth;
    int i = idx + 1;
    while (i < gLibPageCount && gLibPageElm[i].depth > parentDepth) i++;
    return i;
}

/* ------------------------------------------------------------------ */
/* closeLibToggle — collapse disclosure, cache children, remove rows  */
/* ------------------------------------------------------------------ */
static void closeLibToggle(int idx)
{
    if (idx < 0 || idx >= gLibPageCount) return;
    LibPageEntry& node = gLibPageElm[idx];
    node.open = false;
    int end = findLibSubtreeEnd(idx);
    int childCount = end - (idx + 1);
    /* Rows under it: not empty. None: keep what the fetch found (a
       failed fetch stays unknown), and forget any cached children (all
       moved out) so they can't come back on reopen. */
    if (childCount > 0) node.confirmedLeaf = false;
    if (childCount == 0) {
        LibToggleCacheEntry* ce = LibToggleCacheFind(node.id);
        if (ce) ce->childCount = 0;
    }
    if (childCount > 0) {
        LibToggleCacheEntry* ce = LibToggleCacheFindOrCreate(node.id);
        int n = childCount < kLibToggleMaxChildren ? childCount : kLibToggleMaxChildren;
        memcpy(ce->children, &gLibPageElm[idx + 1], sizeof(LibPageEntry) * n);
        ce->childCount = n;
        int remaining = gLibPageCount - end;
        if (remaining > 0)
            memmove(&gLibPageElm[idx + 1], &gLibPageElm[end],
                    sizeof(LibPageEntry) * remaining);
        gLibPageCount -= childCount;
    }
}

/* ------------------------------------------------------------------ */
/* insertLibChildrenDirect — splice children after parentIdx          */
/* ------------------------------------------------------------------ */
static void insertLibChildrenDirect(int parentIdx,
                                    const LibPageEntry* children, int count)
{
    if (count <= 0) return;
    short childDepth = gLibPageElm[parentIdx].depth + 1;
    int insertAt = parentIdx + 1;
    int canInsert = kMaxLibPages - gLibPageCount;
    if (count > canInsert) count = canInsert;
    int tail = gLibPageCount - insertAt;
    if (tail > 0)
        memmove(&gLibPageElm[insertAt + count], &gLibPageElm[insertAt],
                sizeof(LibPageEntry) * tail);
    char pid[64]; SafeStrCopy(pid, (int)sizeof(pid), gLibPageElm[parentIdx].id);
    for (int i = 0; i < count; i++) {
        gLibPageElm[insertAt + i] = children[i];
        gLibPageElm[insertAt + i].depth = childDepth;
        /* Fetched children come without it; moves and the saved order
           need it (an empty one reads as a top-level page) */
        SafeStrCopy(gLibPageElm[insertAt + i].parentId,
                    (int)sizeof(gLibPageElm[insertAt + i].parentId), pid);
    }
    gLibPageCount += count;
}

/* ------------------------------------------------------------------ */
/* DrawDash3HLine — 1-black-2-white horizontal line (period-3 dash)  */
/* Cannot use PenPat: period 3 doesn't tile evenly in 8-bit pattern. */
/* ------------------------------------------------------------------ */
static void DrawDash3HLine(short y, short x0, short x1)
{
    PenNormal();
    for (short x = x0; x <= x1; x += 3) {
        MoveTo(x, y); LineTo(x, y);
    }
}


/* ------------------------------------------------------------------ */
/* DrawLibDividerItem — dotted separator line (items 6 and 7)        */
/* Item 6 (search/list): 1-black-2-white (manual period-3 draw)      */
/* Item 7 (list/footer): 50% gray (qd.gray)                         */
/* ------------------------------------------------------------------ */
static pascal void DrawLibDividerItem(DialogPtr dlg, short item)
{
    Rect r; GetDialogItemRect(dlg, item, r);
    SetPort(dlg);
    PenSize(1, 1);
    if (item == 6) {
        DrawDash3HLine(r.top, r.left, r.right);
    } else {
        PenPat(&qd.gray);
        MoveTo(r.left, r.top);
        LineTo(r.right, r.top);
        PenNormal();
    }
}

/* ------------------------------------------------------------------ */
/* buildLibFromPageList — populate gLibPageElm[] from a NotionPageList */
/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* Search state — declared here so buildLibFromPageList can see them  */
/* ------------------------------------------------------------------ */
static string           sLibQuery;
static bool             sLibSearchFocused   = false;
static NotionPageEntry* sLibAllFlat         = nil;  /* heap: all pages from pageList */
static char           (*sLibAllFlatLower)[64] = nil; /* pre-lowercased titles         */
static int              sLibAllFlatCount    = 0;
static int              sLibSearchHits[512];        /* indices into sLibAllFlat */
static int              sLibSearchHitCount  = 0;
static Rect             sLibSearchBarRect;          /* framed input box in search row */
static bool             sLibLastBlinkState  = false;

static bool isLibSearchActive() { return !sLibQuery.empty(); }
static int  libDisplayCount()   { return isLibSearchActive() ? sLibSearchHitCount : gLibPageCount; }

/* Copy a NotionPageEntry into a LibPageEntry. parentId="" means root. */
static void CopyToLibEntry(LibPageEntry& dest, const NotionPageEntry& src,
                           short depth, const char* parentId = nullptr)
{
    memset(&dest, 0, sizeof(dest));
    SafeStrCopy(dest.id,       (int)sizeof(dest.id),       src.id);
    SafeStrCopy(dest.title,    (int)sizeof(dest.title),    src.title);
    SafeStrCopy(dest.iconName, (int)sizeof(dest.iconName), src.iconName);
    if (parentId) SafeStrCopy(dest.parentId, (int)sizeof(dest.parentId), parentId);
    dest.spriteX    = src.spriteX;
    dest.spriteY    = src.spriteY;
    memcpy(dest.titleEmoji, src.titleEmoji, sizeof(dest.titleEmoji));
    dest.iconSprIdx = FindIconSpriteIdx(dest.iconName);
    dest.depth      = depth;
}

/* Copy a LibPageEntry into a NotionPageEntry for cache serialisation.  */
static void CopyToNotionEntry(NotionPageEntry& dest, const LibPageEntry& src)
{
    memset(&dest, 0, sizeof(dest));
    SafeStrCopy(dest.id,           (int)sizeof(dest.id),           src.id);
    SafeStrCopy(dest.title,        (int)sizeof(dest.title),        src.title);
    SafeStrCopy(dest.iconName,     (int)sizeof(dest.iconName),     src.iconName);
    SafeStrCopy(dest.pageParentId, (int)sizeof(dest.pageParentId), src.parentId);
    dest.spriteX = src.spriteX;
    dest.spriteY = src.spriteY;
    memcpy(dest.titleEmoji, src.titleEmoji, sizeof(dest.titleEmoji));
}

/* A list cached before titles kept their emoji has 0 for them: refetch. */
static bool hasTitleEmojiData(const NotionPageList& pageList)
{
    for (int i = 0; i < pageList.count; i++)
        if (strchr(pageList.pages[i].title, kEmojiSentinel) &&
            pageList.pages[i].titleEmoji[0] == 0)
            return false;
    return true;
}

static void buildLibFromPageList(const NotionPageList& pageList)
{
    int n = pageList.count < kMaxNotionPages ? pageList.count : kMaxNotionPages;
    gLibPageCount = 0;

    /* Populate search corpus with ALL entries (not just roots) */
    if (sLibAllFlat) {
        sLibAllFlatCount = 0;
        for (int i = 0; i < n; i++) {
            sLibAllFlat[sLibAllFlatCount] = pageList.pages[i];
            if (sLibAllFlatLower) {
                SafeStrCopy(sLibAllFlatLower[sLibAllFlatCount], 64,
                            pageList.pages[i].title);
                LowerCaseInPlace(sLibAllFlatLower[sLibAllFlatCount],
                                 (int)strlen(sLibAllFlatLower[sLibAllFlatCount]));
            }
            sLibAllFlatCount++;
        }
    }

    /* Pass 1: add workspace root pages (empty parentId) at depth 0 */
    for (int i = 0; i < n && gLibPageCount < kMaxLibPages; i++) {
        if (pageList.pages[i].pageParentId[0] != '\0') continue;
        CopyToLibEntry(gLibPageElm[gLibPageCount++], pageList.pages[i], 0);
    }

    /* Pass 2: for each child page whose parent is in the root list,
       pre-load the toggle cache so triangle clicks need no API call. */
    for (int i = 0; i < n; i++) {
        const char* pid = pageList.pages[i].pageParentId;
        if (!pid[0]) continue;
        for (int j = 0; j < gLibPageCount; j++) {
            if (strcmp(gLibPageElm[j].id, pid) != 0) continue;
            LibToggleCacheEntry* ce = LibToggleCacheFindOrCreate(gLibPageElm[j].id);
            if (ce && ce->childCount < kLibToggleMaxChildren) {
                CopyToLibEntry(ce->children[ce->childCount++], pageList.pages[i], 1, pid);
            }
            break;
        }
    }
}

/* findLibParentId — return the id of the nearest ancestor above idx   */
static const char* findLibParentId(int idx)
{
    if (idx <= 0) return "";
    short targetDepth = gLibPageElm[idx].depth - 1;
    if (targetDepth < 0) return "";
    for (int k = idx - 1; k >= 0; k--)
        if (gLibPageElm[k].depth == targetDepth) return gLibPageElm[k].id;
    return "";
}

/* saveLibOrderToCache — write current gLibPageElm order back to disk.
   Visible entries are written first; for collapsed toggles their cached
   children are appended immediately after the parent so the order is
   preserved when buildLibFromPageList re-reads the cache.              */
static void saveLibOrderToCache(const char* apiKey)
{
    NotionPageList out;
    memset(&out, 0, sizeof(out));

    for (int i = 0; i < gLibPageCount && out.count < kMaxNotionPages; i++) {
        const LibPageEntry& le = gLibPageElm[i];
        CopyToNotionEntry(out.pages[out.count++], le);

        /* For a closed toggle, flush its cached children after the parent */
        if (!le.open) {
            LibToggleCacheEntry* ce = LibToggleCacheFind(le.id);
            if (ce && ce->valid) {
                for (int j = 0; j < ce->childCount && out.count < kMaxNotionPages; j++) {
                    const LibPageEntry& ch = ce->children[j];
                    CopyToNotionEntry(out.pages[out.count++], ch);
                }
            }
        }
    }

    SavePageListCache(apiKey, out);
}

static Rect sLibMoveUpRect   = {0,0,0,0};
static Rect sLibMoveDownRect = {0,0,0,0};
static Rect sLibAddChildRect  = {0,0,0,0};
static Rect        sLibRefreshRect   = {0,0,0,0};
static UserItemUPP sNewPageUPP       = nil;
static int  sLibHoverRow      = -1;
static bool sLibFromConnection = false; /* hides per-row + when true */
static char sLibCurrentPageId[64] = {0}; /* current page ID — delete hidden for this page */
static bool sLibOrderDirty     = false; /* deferred cache write on close */
static char sLibStatusMsg[64]  = {0};   /* non-empty while list is loading */

/* ------------------------------------------------------------------ */
/* DrawNewPageBtn — plain-text "+ New page" footer item (item 3)      */
/* ------------------------------------------------------------------ */
static Rect sLibDeleteRect = {0,0,0,0};

/* Id of the selected row — a search hit while searching — or "" */
static const char* LibSelectedId()
{
    if (sLibSelIdx < 0) return "";
    if (isLibSearchActive())
        return (sLibSelIdx < sLibSearchHitCount && sLibAllFlat)
               ? sLibAllFlat[sLibSearchHits[sLibSelIdx]].id : "";
    return (sLibSelIdx < gLibPageCount) ? gLibPageElm[sLibSelIdx].id : "";
}

static pascal void DrawNewPageBtn(DialogPtr dlg, short item)
{
    if (sLibFromConnection) return;
    Rect r; GetDialogItemRect(dlg, item, r);
    SetPort(dlg);
    /* Erase wider to cover "Delete" text drawn past the item rect */
    Rect eraseR = r; eraseR.right = 180;
    EraseRect(&eraseR);
    /* Draw "+" cross matching hover button style, then "New page" text */
    short cy = r.top + (r.bottom - r.top) / 2;
    short px = r.left + 10;
    DrawPlusCross(px, cy, &qd.gray);
    SetGenevaFont(12, 0);
    FontInfo fi; GetFontInfo(&fi);
    short baseline = r.top + (r.bottom - r.top - fi.ascent - fi.descent) / 2 + fi.ascent;
    MoveTo(px + 11, baseline);
    DrawText("New page", 0, 8);
    /* "Delete" — shown when a non-current page is selected */
    const char* selId = LibSelectedId();
    if (selId[0] && strcmp(selId, sLibCurrentPageId) != 0) {
        short dx = px + 11 + TextWidth("New page", 0, 8) + 14;
        MoveTo(dx, baseline);
        TextFace(0);
        ForeColor(redColor);
        DrawText("Delete", 0, 6);
        ForeColor(blackColor);
        SetRect(&sLibDeleteRect, dx - 2, r.top, dx + TextWidth("Delete", 0, 6) + 2, r.bottom);
    } else {
        SetRect(&sLibDeleteRect, 0, 0, 0, 0);
    }
    SetDefaultTextStyle();
}

/* ------------------------------------------------------------------ */
/* buildLibSearch — filter sLibAllFlat → sLibSearchHits              */
/* ------------------------------------------------------------------ */
static void buildLibSearch()
{
    sLibSearchHitCount = 0;
    if (sLibQuery.empty() || !sLibAllFlat || sLibAllFlatCount == 0) return;
    char lq[128]; int qlen = (int)sLibQuery.size();
    if (qlen > 127) qlen = 127;
    memcpy(lq, sLibQuery.c_str(), qlen);
    LowerCaseInPlace(lq, qlen);
    lq[qlen] = '\0';
    for (int i = 0; i < sLibAllFlatCount && sLibSearchHitCount < 512; i++) {
        if (sLibAllFlatLower && strstr(sLibAllFlatLower[i], lq))
            sLibSearchHits[sLibSearchHitCount++] = i;
    }
}

/* Moved up from below so drawLibSearchBar / drawLibSearchBarScrolled can use them */
static Rect sLibListRect;
static int  sLibScrollOffset = 0;

/* ------------------------------------------------------------------ */
/* drawLibSearchBar — draw search bar at screen y = yBase.           */
/* yBase scrolls with content: 0 when at top, negative when scrolled.*/
/* Clips to sLibListRect so off-screen parts are invisible.          */
/* Also updates sLibSearchBarRect and sLibRefreshRect for hit-testing.*/
/* ------------------------------------------------------------------ */
static void drawLibSearchBar(DialogPtr dlg, short yBase)
{
    SetPort(dlg);
    /* Skip entirely if search bar is outside the visible list area */
    if (yBase + kLibSearchRowH <= sLibListRect.top || yBase >= sLibListRect.bottom)
        return;

    ClipRect(&sLibListRect);

    /* Erase only the visible portion within the list column */
    short eraseTop = yBase > sLibListRect.top ? yBase : sLibListRect.top;
    short eraseBot = (yBase + kLibSearchRowH) < sLibListRect.bottom
                        ? (yBase + kLibSearchRowH) : sLibListRect.bottom;
    Rect eraseR;
    SetRect(&eraseR, sLibListRect.left, eraseTop, sLibListRect.right, eraseBot);
    EraseRect(&eraseR);
    PenNormal();

    /* Update hit-test rects for current scroll position */
    SetRect(&sLibSearchBarRect, 35, yBase + 4, 373, yBase + 24);
    short rcx = sLibListRect.right - 22;  /* refresh icon x: 20px from right edge */
    short rcy = yBase + 18;
    SetRect(&sLibRefreshRect, rcx - 7, yBase + 4, rcx + 7, yBase + 26);

    DrawMagnifyingGlass(yBase + 11, 15);

    SetGenevaFont(12, 0);
    short cw = 0;
    if (!sLibQuery.empty()) {
        MoveTo(sLibSearchBarRect.left + 2, yBase + 20);
        DrawText(sLibQuery.c_str(), 0, (short)sLibQuery.size());
        cw = TextWidth(sLibQuery.c_str(), 0, (short)sLibQuery.size());
    } else if (!sLibSearchFocused) {
        MoveTo(sLibSearchBarRect.left + 2, yBase + 20);
        DrawText("Search", 0, 6);
    }

    if (sLibSearchFocused && (TickCount() / 20) % 2 == 0) {
        short cx = sLibSearchBarRect.left + 2 + cw;
        PenNormal();
        MoveTo(cx, yBase + 7);
        LineTo(cx, yBase + 21);
    }

    /* Refresh icon: 270° arc + arrowhead */
    Rect arcR; SetRect(&arcR, rcx - 5, rcy - 5, rcx + 5, rcy + 5);
    PenNormal();
    FrameArc(&arcR, 90, 270);
    MoveTo(rcx, rcy - 5); LineTo(rcx - 2, rcy - 7);
    MoveTo(rcx, rcy - 5); LineTo(rcx - 2, rcy - 3);

    /* 1-black-2-white divider */
    DrawDash3HLine(yBase + 32, sLibListRect.left, sLibListRect.right - 1);
    PenNormal();
    SetDefaultTextStyle();
}

/* Convenience wrapper: draw at the current scroll position */
static void drawLibSearchBarScrolled(DialogPtr dlg)
{
    drawLibSearchBar(dlg, sLibListRect.top - sLibScrollOffset * kLibRowHeight);
}

/* ------------------------------------------------------------------ */
/* drawSearchRowContent — flat row for search results                 */
/* ------------------------------------------------------------------ */
static void drawSearchRowContent(const Rect& listRect, int scrollOffset,
                                 int selIdx, int i, const FontInfo& fi)
{
    const NotionPageEntry& e = sLibAllFlat[sLibSearchHits[i]];
    short rowTop    = listRect.top + kLibSearchRowH + (i - scrollOffset) * kLibRowHeight;
    short rowBottom = rowTop + kLibRowHeight;
    Rect  rowRect;
    SetRect(&rowRect, listRect.left, rowTop, listRect.right, rowBottom);
    short iconLeft = listRect.left + 20;
    ClipRect(&listRect);
    short iconTop = rowTop + (kLibRowHeight - kLibIconSize) / 2;
    if (e.iconName[0])      DrawListPageIcon(e.iconName, iconLeft, iconTop);
    else if (e.spriteX >= 0) DrawEmojiSpriteAt(e.spriteX, e.spriteY, iconLeft, iconTop);
    else                    DrawPageIcon(iconLeft + 2, rowTop + (kLibRowHeight - 16) / 2 + 1);
    short textLeft = iconLeft + kLibIconSize + 5;
    if (e.title[0]) {
        short baseline = rowTop +
            (kLibRowHeight - fi.ascent - fi.descent) / 2 + fi.ascent;
        short clipBot = rowBottom < listRect.bottom ? rowBottom : listRect.bottom;
        Rect textClip;
        SetRect(&textClip, textLeft, rowTop, listRect.right - 2, clipBot);
        ClipRect(&textClip);
        DrawTitleText(e.title, e.titleEmoji, textLeft, baseline, iconTop);
    }
    if (i == selIdx) {
        ClipRect(&listRect);
        short selRight = textLeft;
        if (e.title[0])
            selRight += TitleTextWidth(e.title) + 3;
        if (selRight > listRect.right - 2) selRight = listRect.right - 2;
        short selBot = rowBottom < listRect.bottom ? rowBottom - 1 : listRect.bottom - 1;
        Rect selRect;
        SetRect(&selRect, iconLeft - 1, rowTop + 1, selRight, selBot);
        InvertRect(&selRect);
    }
}

/* ------------------------------------------------------------------ */
/* Deleting a page — Notion trashes its subpages along with it         */
/* ------------------------------------------------------------------ */
enum { kLibMaxDelIds = 128 };
typedef char LibDelId[33];

static bool libIdListed(const LibDelId* ids, int n, const char* id)
{
    for (int k = 0; k < n; k++)
        if (strcmp(ids[k], id) == 0) return true;
    return false;
}

static void libAddId(LibDelId* ids, int& n, const char* id)
{
    if (n < kLibMaxDelIds && id[0] && !libIdListed(ids, n, id))
        SafeStrCopy(ids[n++], (int)sizeof(LibDelId), id);
}

/* ids[0] = rootId, then every known subpage at any level, from the
   page list, the rows on screen and the cached children of closed
   pages. Returns the id count (1 = no subpages known).                */
static int collectLibSubpages(const char* rootId, LibDelId* ids)
{
    int n = 0;
    libAddId(ids, n, rootId);
    for (int q = 0; q < n; q++) {   /* breadth-first: n grows as we go */
        const char* pid = ids[q];
        for (int i = 0; i < sLibAllFlatCount; i++)
            if (strcmp(sLibAllFlat[i].pageParentId, pid) == 0)
                libAddId(ids, n, sLibAllFlat[i].id);
        for (int i = 0; i < gLibPageCount; i++) {
            if (strcmp(gLibPageElm[i].id, pid) == 0) {   /* open: rows below */
                int end = findLibSubtreeEnd(i);
                for (int j = i + 1; j < end; j++) libAddId(ids, n, gLibPageElm[j].id);
            } else if (strcmp(gLibPageElm[i].parentId, pid) == 0) {
                libAddId(ids, n, gLibPageElm[i].id);
            }
        }
        LibToggleCacheEntry* ce = LibToggleCacheFind(pid);
        if (ce)
            for (int j = 0; j < ce->childCount; j++) libAddId(ids, n, ce->children[j].id);
    }
    return n;
}

/* Drop deleted pages from the tree (with their rows below), the search
   corpus and the cached children of closed pages.                     */
static void libForgetPages(const LibDelId* ids, int n)
{
    for (int k = 0; k < n; k++)
        for (int i = 0; i < gLibPageCount; i++)
            if (strcmp(gLibPageElm[i].id, ids[k]) == 0) {
                int end = findLibSubtreeEnd(i);
                memmove(&gLibPageElm[i], &gLibPageElm[end],
                        sizeof(LibPageEntry) * (gLibPageCount - end));
                gLibPageCount -= end - i;
                break;
            }
    int dst = 0;
    for (int i = 0; i < sLibAllFlatCount; i++) {
        if (libIdListed(ids, n, sLibAllFlat[i].id)) continue;
        if (dst != i) {
            memcpy(&sLibAllFlat[dst], &sLibAllFlat[i], sizeof(NotionPageEntry));
            if (sLibAllFlatLower) memcpy(sLibAllFlatLower[dst], sLibAllFlatLower[i], 64);
        }
        dst++;
    }
    sLibAllFlatCount = dst;
    for (int c = 0; c < gLibToggleCacheCount; c++) {
        LibToggleCacheEntry& ce = gLibToggleCache[c];
        if (libIdListed(ids, n, ce.parentId)) { ce.childCount = 0; continue; }
        int d = 0;
        for (int j = 0; j < ce.childCount; j++) {
            if (libIdListed(ids, n, ce.children[j].id)) continue;
            if (d != j) memcpy(&ce.children[d], &ce.children[j], sizeof(LibPageEntry));
            d++;
        }
        ce.childCount = d;
    }
}

/* Warn that subpages go too; true = the user chose Delete. */
static bool libConfirmDeleteSubpages(DialogPtr dlg, const char* title, int subs)
{
    char t[64]; int tl = 0;   /* title without emoji sentinels */
    for (int i = 0; title[i] && tl < (int)sizeof(t) - 1; i++) {
        if (title[i] == kEmojiSentinel) { if (title[i + 1] == ' ') i++; continue; }
        t[tl++] = title[i];
    }
    t[tl] = '\0';
    char msg[200];
    snprintf(msg, sizeof(msg),
             "\xD2%s\xD3 has %d subpage%s. Deleting it will also delete %s.",
             tl ? t : "Untitled", subs, subs == 1 ? "" : "s", subs == 1 ? "it" : "them");
    Str255 pmsg;
    pmsg[0] = (unsigned char)strlen(msg);
    memcpy(pmsg + 1, msg, pmsg[0]);
    ParamText(pmsg, "\p", "\p", "\p");
    InitCursor();
    short hit = CautionAlert(kLibDeleteAlertID, nil);
    SetPort(dlg);
    return hit == 2;
}

/* drawLibStatusText — same style as DrawStatusText on the main page  */
static void drawLibStatusText(DialogPtr dlg, const Rect& listRect, const char* msg)
{
    SetPort(dlg);
    EraseRect(&listRect);
    DrawCenteredText(dlg, listRect, msg);
}

/* ------------------------------------------------------------------ */
/* drawLibRowContent — draw one visible row (no erase; caller must    */
/* erase rowRect first).  i must be in [scrollOffset, lastVisible).   */
/* ------------------------------------------------------------------ */
static void drawLibRowContent(const Rect& listRect, int scrollOffset,
                              int selIdx, int i, const FontInfo& fi)
{
    LibPageEntry& e   = gLibPageElm[i];
    short rowTop      = listRect.top + kLibSearchRowH + (i - scrollOffset) * kLibRowHeight;
    short rowBottom   = rowTop + kLibRowHeight;
    Rect  rowRect;
    SetRect(&rowRect, listRect.left, rowTop, listRect.right, rowBottom);

    /* indent */
    short indent   = 20 + e.depth * kLibIndentStep;
    short triCX    = listRect.left + indent + 1;
    short triCY    = rowTop + kLibRowHeight / 2 + 3;

    /* disclosure triangle — black while it may have children (not
       checked yet, or it has some), gray once known to have none */
    {
        ClipRect(&listRect);
        PenSize(1, 1);
        bool hasChildRow = e.open && i + 1 < gLibPageCount &&
                           gLibPageElm[i + 1].depth > e.depth;
        bool knownEmpty  = e.confirmedLeaf && !hasChildRow;
        DrawDisclosureTriangle(triCX, triCY, e.open,
                               knownEmpty ? &qd.gray : &qd.black);
    }

    /* icon — SICN outline fallback for pages with no icon */
    short iconLeft = listRect.left + indent + 15;  /* 1px left */
    ClipRect(&listRect);
    short iconTop = rowTop + (kLibRowHeight - kLibIconSize) / 2 + 1;
    if (e.iconName[0])      DrawListPageIconIdx(e.iconSprIdx, iconLeft, iconTop);
    else if (e.spriteX >= 0) DrawEmojiSpriteAt(e.spriteX, e.spriteY, iconLeft, iconTop);
    else                    DrawPageIcon(iconLeft + 2, rowTop + (kLibRowHeight - 16) / 2 + 2);

    /* title */
    short textLeft = iconLeft + kLibIconSize + 8;
    if (e.title[0]) {
        short baseline = rowTop +
            (kLibRowHeight - fi.ascent - fi.descent) / 2 + fi.ascent + 3;  /* 3px down */
        short clipBot = rowBottom < listRect.bottom ? rowBottom : listRect.bottom;
        Rect textClip;
        SetRect(&textClip, textLeft, rowTop, listRect.right - 2, clipBot);
        ClipRect(&textClip);
        DrawTitleText(e.title, e.titleEmoji, textLeft, baseline, iconTop);
    }

    /* selection: invert icon + title label area only (Finder-style) */
    if (i == selIdx) {
        ClipRect(&listRect);
        short selRight = textLeft;
        if (e.title[0])
            selRight += TitleTextWidth(e.title) + 3;
        if (selRight > listRect.right - 2) selRight = listRect.right - 2;
        short selBot = rowBottom < listRect.bottom ? rowBottom - 1 : listRect.bottom - 1;
        Rect selRect;
        SetRect(&selRect, iconLeft - 1, rowTop + 6, selRight, selBot);
        InvertRect(&selRect);
    }

    /* + ▲ ▼ controls — shown on hover; erase a right-side patch so they're visible */
    if (i == sLibHoverRow) {
        SetRect(&sLibAddChildRect, 0,0,0,0);
        SetRect(&sLibMoveUpRect,   0,0,0,0);
        SetRect(&sLibMoveDownRect, 0,0,0,0);
        short cy  = rowTop + kLibRowHeight / 2;
        short px  = listRect.right - 20;  /* + rightmost */
        short dx  = px - 17;              /* ▼ */
        short ux  = dx - 17;              /* ▲ */
        Rect arrowBg;
        SetRect(&arrowBg, ux - 6, rowTop + 1, listRect.right - 2, rowBottom - 1);
        ClipRect(&listRect);
        /* background is never row-inverted now, so always erase */
        EraseRect(&arrowBg);
        PenNormal();
        /* ▲ left, ▼ right — gray idle, matches main page sidebar style */
        if (i > 0) {
            DrawArrowTriangle(ux, cy - 3, true,  6, &qd.gray);
            SetRect(&sLibMoveUpRect,   ux - 6, rowTop, ux + 6, rowBottom);
        }
        if (i < gLibPageCount - 1) {
            DrawArrowTriangle(dx, cy - 3, false, 6, &qd.gray);
            SetRect(&sLibMoveDownRect, dx - 6, rowTop, dx + 6, rowBottom);
        }
        /* + */
        DrawPlusCross(px, cy, &qd.gray);
        SetRect(&sLibAddChildRect, px - 6, rowTop, px + 6, rowBottom);
    }
}

/* ------------------------------------------------------------------ */
/* drawLibList — draw the visible rows of the Library list            */
/* ------------------------------------------------------------------ */
static void drawLibList(DialogPtr dlg, const Rect& listRect,
                        int scrollOffset, int selIdx)
{
    SetPort(dlg);
    Rect portRect = ((GrafPtr)dlg)->portRect;
    ClipRect(&listRect);
    EraseRect(&listRect);

    /* Draw search bar — it scrolls: top = listRect.top - scrollOffset*kLibRowHeight */
    drawLibSearchBar(dlg, listRect.top - scrollOffset * kLibRowHeight);

    SetGenevaFont();
    FontInfo fi; GetFontInfo(&fi);

    /* visRows for page content below the search bar */
    int visRows   = (listRect.bottom - listRect.top) / kLibRowHeight;
    /* firstRow: when scrollOffset=1, row 0 is at y=7 (still partly visible) */
    int firstRow  = (scrollOffset > 0) ? scrollOffset - 1 : 0;
    int lastRow   = scrollOffset + visRows + 1;
    int dispCount = libDisplayCount();
    if (lastRow > dispCount) lastRow = dispCount;

    for (int i = firstRow; i < lastRow; i++) {
        if (isLibSearchActive())
            drawSearchRowContent(listRect, scrollOffset, selIdx, i, fi);
        else
            drawLibRowContent(listRect, scrollOffset, selIdx, i, fi);
    }

    ClipRect(&portRect);
    SetDefaultTextStyle();
}

/* ------------------------------------------------------------------ */
/* redrawLibHoverRows — targeted redraw of just old and new hover row */
/* Avoids full EraseRect+repaint on every mouse-move event.           */
/* ------------------------------------------------------------------ */
static void redrawLibHoverRows(DialogPtr dlg, const Rect& listRect,
                               int scrollOffset, int selIdx,
                               int oldRow, int newRow)
{
    SetPort(dlg);
    Rect portRect = ((GrafPtr)dlg)->portRect;
    SetGenevaFont();
    FontInfo fi; GetFontInfo(&fi);

    int visRows = (listRect.bottom - listRect.top) / kLibRowHeight;

    for (int pass = 0; pass < 2; pass++) {
        int row = (pass == 0) ? oldRow : newRow;
        if (row < 0 || row >= libDisplayCount()) continue;
        short rowTop    = listRect.top + kLibSearchRowH + (row - scrollOffset) * kLibRowHeight;
        if (rowTop >= listRect.bottom || rowTop + kLibRowHeight <= listRect.top) continue;
        short rowBottom = rowTop + kLibRowHeight;
        Rect rowRect;
        SetRect(&rowRect, listRect.left, rowTop, listRect.right, rowBottom);
        ClipRect(&listRect);
        EraseRect(&rowRect);
        if (isLibSearchActive())
            drawSearchRowContent(listRect, scrollOffset, selIdx, row, fi);
        else
            drawLibRowContent(listRect, scrollOffset, selIdx, row, fi);
    }

    ClipRect(&portRect);
    SetDefaultTextStyle();
}

/* ------------------------------------------------------------------ */
/* invalidateLibList — schedule redraw of list area                   */
/* ------------------------------------------------------------------ */
static void invalidateLibList(DialogPtr dlg, const Rect& listRect)
{
    SetPort(dlg);
    InvalRect(&listRect);
}

/* ------------------------------------------------------------------ */
/* UpdateLibScrollRange — sync scroll bar max to list length          */
/* ------------------------------------------------------------------ */
static void UpdateLibScrollRange(ControlHandle scrollBar, const Rect& listRect,
                                  int& scrollOffset)
{
    if (!scrollBar) return;
    int visRows   = (listRect.bottom - listRect.top - kLibSearchRowH) / kLibRowHeight;
    if (visRows < 1) visRows = 1;
    int maxScroll = libDisplayCount() - visRows;
    if (maxScroll < 0) maxScroll = 0;
    /* Ensure full-window clip so the CDEF can paint the scroll bar (which
       lives at x=384-400, outside sLibListRect x=0-384).                */
    {
        GrafPtr ownerPort = (GrafPtr)(**scrollBar).contrlOwner;
        SetPort(ownerPort);
        ClipRect(&ownerPort->portRect);
    }
    SetControlMaximum(scrollBar, (short)maxScroll);
    if (scrollOffset > maxScroll) scrollOffset = maxScroll;
    if (scrollOffset < 0)        scrollOffset = 0;
    SetControlValue(scrollBar, (short)scrollOffset);
}

/* ------------------------------------------------------------------ */
/* LibSetOpenBtn — enable/disable button 1 and redraw its ring        */
/* ------------------------------------------------------------------ */
static void LibSetOpenBtn(DialogPtr dlg, bool enable)
{
    if (enable) EnableDialogButton(dlg, 1);
    else        DisableDialogButton(dlg, 1);
    DrawDefaultButtonRing(dlg, 1);
    DrawNewPageBtn(dlg, 3);   /* "Delete" shows only with a selection */
}

/* ------------------------------------------------------------------ */
/* scrollToSelection — adjust scroll so selIdx is visible             */
/* ------------------------------------------------------------------ */
static void scrollToSelection(ControlHandle scrollBar, const Rect& listRect,
                               int selIdx, int& scrollOffset)
{
    if (selIdx < 0) return;
    int visRows = (listRect.bottom - listRect.top - kLibSearchRowH) / kLibRowHeight;
    if (visRows < 1) visRows = 1;
    if (selIdx < scrollOffset)
        scrollOffset = selIdx;
    if (selIdx >= scrollOffset + visRows)
        scrollOffset = selIdx - visRows + 1;
    if (scrollOffset < 0) scrollOffset = 0;
    int maxScroll = libDisplayCount() - visRows;
    if (maxScroll < 0) maxScroll = 0;
    if (scrollOffset > maxScroll) scrollOffset = maxScroll;
    if (scrollBar) SetControlValue(scrollBar, (short)scrollOffset);
}

/* ------------------------------------------------------------------ */
/* LibScrollAction — ControlActionUPP for live scroll bar tracking    */
/* ------------------------------------------------------------------ */
static ControlHandle    sLibScrollBar     = nil;
static ControlActionUPP sLibScrollUPP     = nil;
static int*             sLibScrollOff     = nil;
static DialogPtr        sLibDlg           = nil;
static long             sLibLastClickTick = 0;   /* double-click detection */
static int              sLibLastClickRow  = -1;

static pascal void LibScrollAction(ControlHandle ctl, short part)
{
    if (!sLibScrollOff) return;
    int visRows   = (sLibListRect.bottom - sLibListRect.top - kLibSearchRowH) / kLibRowHeight;
    if (visRows < 1) visRows = 1;
    int maxScroll = libDisplayCount() - visRows;
    if (maxScroll < 0) maxScroll = 0;
    int delta = ScrollPartDelta(part, 1, (short)visRows);
    if (delta == 0) return;
    int newOff = *sLibScrollOff + delta;
    if (newOff < 0)         newOff = 0;
    if (newOff > maxScroll) newOff = maxScroll;
    if (newOff != *sLibScrollOff) {
        *sLibScrollOff = newOff;
        /* Save/restore port around all drawing so TrackControl's
           internal state is not disturbed when the UPP returns.    */
        GrafPtr savedPort; GetPort(&savedPort);
        SetControlValue(ctl, (short)newOff);
        if (sLibDlg)
            drawLibList(sLibDlg, sLibListRect, *sLibScrollOff, sLibSelIdx);
        SetPort(savedPort);
    }
}

/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* Move animation — fade source, scroll only the two-row region      */
/* ------------------------------------------------------------------ */
static void LibAnimateMove(DialogPtr dlg, short srcTop, short srcH,
                           short destTop, short destH, bool up)
{
    if (!dlg || srcH <= 4) return;
    SetPort(dlg);

    /* Clamp to list bounds (below search bar) */
    short listTop = sLibListRect.top + kLibSearchRowH;
    if (srcTop < listTop) { srcH -= (listTop - srcTop); srcTop = listTop; }
    if (srcH <= 0) return;

    /* Fade source row(s): 50% gray, pause, then very light */
    short fadeBot = (srcTop + srcH < sLibListRect.bottom) ? srcTop + srcH : sLibListRect.bottom;
    Rect grayR;
    SetRect(&grayR, sLibListRect.left, srcTop, sLibListRect.right, fadeBot);
    PenPat(&qd.gray); PenMode(patBic);
    PaintRect(&grayR);
    PenNormal();
    { unsigned long d; Delay(4, &d); }
    PenPat(&qd.dkGray); PenMode(patBic);
    PaintRect(&grayR);
    PenNormal();

    /* Scroll only the two-row region — other rows stay still */
    RgnHandle rgn = NewRgn();
    if (!rgn) return;

    short regionTop = up ? destTop : srcTop;
    if (regionTop < listTop) regionTop = listTop;
    short regionBot = regionTop + srcH + destH;
    if (regionBot > sLibListRect.bottom) regionBot = sLibListRect.bottom;
    short scrollDir = up ? 1 : -1;

    int steps = 4;
    short remaining = srcH;
    for (int i = 0; i < steps; i++) {
        short dv = (i < steps - 1) ? srcH / steps : remaining;
        remaining -= dv;
        Rect r;
        SetRect(&r, sLibListRect.left, regionTop, sLibListRect.right, regionBot);
        ScrollRect(&r, 0, scrollDir * dv, rgn);
        EraseRgn(rgn);
        unsigned long dummy; Delay(2, &dummy);
    }
    DisposeRgn(rgn);

    /* Brief pause to see the gap */
    { unsigned long d; Delay(5, &d); }
}

/* LibMoveRowUp / LibMoveRowDown — move selected row one position     */
/* Both functions update sLibSelIdx, sLibHoverRow, sLibOrderDirty     */
/* and refresh the scroll bar + list.                                  */
/* ------------------------------------------------------------------ */
static void LibMoveRow(DialogPtr dlg, int row, bool up,
                       AppSettings& settings, LibMovePageFunc movePage)
{
    int subtreeEnd = findLibSubtreeEnd(row);
    if (!up && subtreeEnd >= gLibPageCount) return;

    /* ---- Move animation ---- */
    int subtreeSize = subtreeEnd - row;
    short srcH      = (short)(subtreeSize * kLibRowHeight);
    short srcTop    = (short)(sLibListRect.top + kLibSearchRowH +
                      (row - sLibScrollOffset) * kLibRowHeight);
    /* Displaced element is a single row (the immediate neighbor) */
    int   destRow   = up ? row - 1 : subtreeEnd;
    short destTop   = (short)(sLibListRect.top + kLibSearchRowH +
                      (destRow - sLibScrollOffset) * kLibRowHeight);
    short destH     = kLibRowHeight;
    bool onScreen   = (srcTop < sLibListRect.bottom &&
                       srcTop + srcH > sLibListRect.top + kLibSearchRowH);
    if (onScreen) LibAnimateMove(dlg, srcTop, srcH, destTop, destH, up);

    /* The rows the move touches, to put back if Notion refuses it */
    int   bakFirst = up ? row - 1 : row;
    int   bakCount = subtreeEnd - row + 1;
    Ptr   bak      = NewPtr((Size)bakCount * (Size)sizeof(LibPageEntry));
    if (!bak) { SysBeep(1); return; }
    memcpy(bak, &gLibPageElm[bakFirst], (size_t)bakCount * sizeof(LibPageEntry));

    short oldDepth = gLibPageElm[row].depth;
    if (up) {
        LibPageEntry displaced = gLibPageElm[row - 1];
        memmove(&gLibPageElm[row - 1], &gLibPageElm[row],
                sizeof(LibPageEntry) * (subtreeEnd - row));
        gLibPageElm[subtreeEnd - 1] = displaced;
    } else {
        LibPageEntry displaced = gLibPageElm[subtreeEnd];
        memmove(&gLibPageElm[row + 1], &gLibPageElm[row],
                sizeof(LibPageEntry) * (subtreeEnd - row));
        gLibPageElm[row] = displaced;
    }
    int newHeaderPos = up ? row - 1 : row + 1;
    short newDepth = 0;
    if (newHeaderPos > 0) {
        const LibPageEntry& above = gLibPageElm[newHeaderPos - 1];
        newDepth = above.open ? (short)(above.depth + 1) : above.depth;
    }
    short delta = newDepth - oldDepth;
    if (delta != 0) {
        /* move-up: adjust newHeaderPos..subtreeEnd-2 (displaced row already moved)
           move-down: adjust newHeaderPos..subtreeEnd (guard against gLibPageCount) */
        int loopEnd = up ? subtreeEnd - 1 : subtreeEnd + 1;
        for (int k = newHeaderPos; k < loopEnd && k < gLibPageCount; k++)
            gLibPageElm[k].depth += delta;
    }
    /* A new parent (also at the same depth: past an open toggle into
       its children) is a real move in Notion.  Only the order among
       siblings is local: the API can't set it. */
    char newPid[64]; SafeStrCopy(newPid, (int)sizeof(newPid), findLibParentId(newHeaderPos));
    if (strcmp(newPid, gLibPageElm[newHeaderPos].parentId) != 0) {
        if (!movePage || !movePage(settings.apiKey, gLibPageElm[newHeaderPos].id, newPid)) {
            /* Refused, offline, or to the top level (not in the API):
               put the rows back as they were */
            memcpy(&gLibPageElm[bakFirst], bak, (size_t)bakCount * sizeof(LibPageEntry));
            DisposePtr(bak);
            if (newPid[0] == '\0') {   /* top level: say why, it's not a glitch */
                InitCursor();
                NoteAlert(kLibTopLevelAlertID, nil);
                SetPort(dlg);
            } else {
                SysBeep(1);
            }
            invalidateLibList(dlg, sLibListRect);
            return;
        }
        SafeStrCopy(gLibPageElm[newHeaderPos].parentId,
                    (int)sizeof(gLibPageElm[newHeaderPos].parentId), newPid);
    }
    DisposePtr(bak);
    /* Track selection through the memmove shuffle:
       - within moved subtree: follow shift by ±1
       - displaced single row: follow to its new slot
       - outside affected range: unchanged */
    if (sLibSelIdx >= row && sLibSelIdx < subtreeEnd) {
        sLibSelIdx += (up ? -1 : 1);   /* subtree shifted by ±1 */
    } else if (up && sLibSelIdx == row - 1) {
        sLibSelIdx = subtreeEnd - 1;   /* displaced row moved to end */
    } else if (!up && sLibSelIdx == subtreeEnd) {
        sLibSelIdx = row;              /* displaced row moved to start */
    }
    sLibHoverRow = newHeaderPos;
    sLibOrderDirty = true;
    UpdateLibScrollRange(sLibScrollBar, sLibListRect, sLibScrollOffset);
    invalidateLibList(dlg, sLibListRect);
}

static void LibMoveRowUp(DialogPtr dlg, int row,
                         AppSettings& settings, LibMovePageFunc movePage)
{ LibMoveRow(dlg, row, true,  settings, movePage); }

static void LibMoveRowDown(DialogPtr dlg, int row,
                           AppSettings& settings, LibMovePageFunc movePage)
{ LibMoveRow(dlg, row, false, settings, movePage); }

/* ------------------------------------------------------------------ */
/* LibSetupDialog — allocate buffers, create dialog, populate list     */
/* ------------------------------------------------------------------ */
static bool LibSetupDialog(AppSettings& settings,
                           FetchPagesFunc fetchPages,
                           LibraryPrimaryAction action,
                           bool forceReload,
                           bool fromConnection,
                           DialogPtr& dlg,
                           UserItemUPP& divUPP)
{
    sLibNewPageRequested = false;
    sLibDidDelete        = false;
    sLibFromConnection   = fromConnection;
    stripHyphens(settings.pageID, sLibCurrentPageId, (int)sizeof(sLibCurrentPageId));

    gLibPageElm     = (LibPageEntry*)NewPtrClear((long)kMaxLibPages * sizeof(LibPageEntry));
    gLibToggleCache = (LibToggleCacheEntry*)NewPtrClear((long)kLibToggleCacheSize * sizeof(LibToggleCacheEntry));
    sLibAllFlat      = (NotionPageEntry*)NewPtrClear((long)kMaxNotionPages * sizeof(NotionPageEntry));
    sLibAllFlatLower = (char(*)[64])NewPtrClear((long)kMaxNotionPages * 64);
    if (!gLibPageElm || !gLibToggleCache || !sLibAllFlat || !sLibAllFlatLower) {
        if (gLibPageElm)      { DisposePtr((Ptr)gLibPageElm);      gLibPageElm      = nil; }
        if (gLibToggleCache)  { DisposePtr((Ptr)gLibToggleCache);  gLibToggleCache  = nil; }
        if (sLibAllFlat)      { DisposePtr((Ptr)sLibAllFlat);      sLibAllFlat      = nil; }
        if (sLibAllFlatLower) { DisposePtr((Ptr)sLibAllFlatLower); sLibAllFlatLower = nil; }
        return false;
    }
    gLibPageCount = 0; gLibToggleCacheCount = 0;

    dlg = GetNewDialog(kLibraryDialogID, NULL, (WindowPtr)-1);
    if (!dlg) {
        DisposePtr((Ptr)gLibPageElm);      gLibPageElm      = nil;
        DisposePtr((Ptr)gLibToggleCache);  gLibToggleCache  = nil;
        DisposePtr((Ptr)sLibAllFlat);      sLibAllFlat      = nil;
        DisposePtr((Ptr)sLibAllFlatLower); sLibAllFlatLower = nil;
        return false;
    }

    divUPP = NewUserItemUPP(DrawLibDividerItem);
    { short it = 7;   /* item 6 (search divider) scrolls with content — drawn in drawLibList */
      SetUserItems(dlg, &it, 1, divUPP); }
    if (sNewPageUPP) { DisposeUserItemUPP(sNewPageUPP); sNewPageUPP = nil; }
    sNewPageUPP = NewUserItemUPP(DrawNewPageBtn);
    { short it = 3; SetUserItems(dlg, &it, 1, sNewPageUPP); }

    SetControlTitle(GetDialogControl(dlg, 1),
                    action == kLibActionInsertLink ? "\pInsert link" : "\pOpen");
    SetControlTitle(GetDialogControl(dlg, 2), fromConnection ? "\pBack" : "\pCancel");
    HideControl(GetDialogControl(dlg, 5));  /* placeholder — always hidden */

    GetDialogItemRect(dlg, 4, sLibListRect);
    Rect sbRect;
    SetRect(&sbRect, sLibListRect.right, 0,   /* top=0: scroll bar spans full height incl. search */
            sLibListRect.right + kLibScrollBarW, sLibListRect.bottom);
    sLibScrollBar = MakeScrollBar((WindowPtr)dlg, sbRect);
    DisableDialogButton(dlg, 1);
    sLibQuery.clear();
    sLibSearchHitCount  = 0;
    sLibAllFlatCount    = 0;
    sLibSearchFocused   = false;
    sLibLastBlinkState  = false;
    SetRect(&sLibSearchBarRect, 35, 4, 373, 24);   /* scrollOffset=0 default */
    SafeStrCopy(sLibStatusMsg, sizeof(sLibStatusMsg), "Loading Library\xC9");

    ShowWindow(dlg);
    drawLibStatusText(dlg, sLibListRect, sLibStatusMsg);
    drawLibSearchBarScrolled(dlg);

    gLibPageCount = 0;
    {   NotionPageList pageList; bool gotPages = false;
        bool stale = false;
        if (!forceReload) {
            gotPages = LoadPageListCache(settings.apiKey, pageList);
            stale    = gotPages && !hasTitleEmojiData(pageList);
        }
        if ((!gotPages || stale) && fetchPages) {
            if (stale) InvalidatePageListCache();   /* skip the stale disk copy */
            if (fetchPages(settings.apiKey, pageList)) {
                gotPages = true;
                SavePageListCache(settings.apiKey, pageList);
            } else if (stale) {
                gotPages = LoadPageListCache(settings.apiKey, pageList);  /* offline: old list */
            }
        }
        ReloadListAssets();
        if (gotPages) buildLibFromPageList(pageList);
    }
    if (gLibPageCount > 0) {
        sLibStatusMsg[0] = '\0';
    } else {
        SafeStrCopy(sLibStatusMsg, sizeof(sLibStatusMsg),
                    "No pages found. Check your integration token.");
    }

    sLibSelIdx       = -1;
    sLibHoverRow     = -1;
    sLibScrollOffset = 0;
    sLibScrollOff    = &sLibScrollOffset;
    sLibDlg          = dlg;
    sLibLastClickTick = 0;
    sLibLastClickRow  = -1;
    UpdateLibScrollRange(sLibScrollBar, sLibListRect, sLibScrollOffset);
    if (sLibStatusMsg[0]) {
        drawLibStatusText(dlg, sLibListRect, sLibStatusMsg);
        drawLibSearchBarScrolled(dlg);
    } else {
        drawLibList(dlg, sLibListRect, sLibScrollOffset, sLibSelIdx);
        /* search bar drawn inside drawLibList */
    }
    ValidRect(&sLibListRect);
    sLibScrollUPP = NewControlActionUPP(LibScrollAction);
    return true;
}

/* ------------------------------------------------------------------ */
/* LibHandleNull — null-event hover tracking                           */
/* ------------------------------------------------------------------ */
static void LibHandleNull(DialogPtr dlg)
{
    SetPort(dlg);
    /* Cursor blink while search field is focused (empty or not) */
    if (sLibSearchFocused) {
        bool blinkNow = (TickCount() / 20) % 2 == 0;
        if (blinkNow != sLibLastBlinkState) {
            sLibLastBlinkState = blinkNow;
            drawLibSearchBarScrolled(dlg);
        }
    }
    Point mp; GetMouse(&mp);
    /* IBeam over search bar (uses sLibSearchBarRect updated by drawLibSearchBar) */
    if (PtInRect(mp, &sLibSearchBarRect)) {
        CursHandle ibeam = GetCursor(iBeamCursor);
        if (ibeam) SetCursor(*ibeam);
    } else {
        InitCursor();
    }
    /* Hover row — account for search bar height offset */
    int hrow = -1;
    if (!isLibSearchActive() && PtInRect(mp, &sLibListRect)) {
        short contentPx = mp.v - sLibListRect.top
                          + sLibScrollOffset * kLibRowHeight
                          - kLibSearchRowH;
        if (contentPx >= 0)
            hrow = contentPx / kLibRowHeight;
    }
    if (hrow < 0 || hrow >= libDisplayCount()) hrow = -1;
    if (hrow != sLibHoverRow) {
        int oldHover = sLibHoverRow;
        sLibHoverRow = hrow;
        redrawLibHoverRows(dlg, sLibListRect, sLibScrollOffset,
                           sLibSelIdx, oldHover, hrow);
    }
}

/* ------------------------------------------------------------------ */
/* LibHandleKey — keydown / autoKey handler                            */
/* ------------------------------------------------------------------ */
static void LibHandleKey(DialogPtr dlg, EventRecord& e,
                         bool& done, bool& result)
{
    char c = (char)(e.message & charCodeMask);
    if (c == 0x0D || c == 0x03) {
        if (sLibSelIdx >= 0) { result = true; done = true; } return;
    }
    if (c == 0x1B) {  /* Escape: clear search first, then dismiss */
        if (!sLibQuery.empty()) {
            sLibQuery.clear(); sLibSearchHitCount = 0;
            sLibSelIdx = -1; LibSetOpenBtn(dlg, false);
            UpdateLibScrollRange(sLibScrollBar, sLibListRect, sLibScrollOffset);
            drawLibList(dlg, sLibListRect, sLibScrollOffset, sLibSelIdx);
            /* search bar drawn inside drawLibList */
        } else { done = true; }
        return;
    }
    /* Backspace / Delete — remove last search character */
    if (c == 0x08 || c == (char)0x7F) {
        if (!sLibQuery.empty()) {
            sLibQuery.erase(sLibQuery.size() - 1, 1);
            buildLibSearch();
            sLibSelIdx = -1; LibSetOpenBtn(dlg, false);
            UpdateLibScrollRange(sLibScrollBar, sLibListRect, sLibScrollOffset);
            drawLibList(dlg, sLibListRect, sLibScrollOffset, sLibSelIdx);
        }
        return;
    }
    /* Printable ASCII — append to search query (skip Cmd combos) */
    if (!(e.modifiers & cmdKey) && (unsigned char)c >= 0x20 && (unsigned char)c < 0x7F) {
        if (sLibQuery.size() < 64) {
            sLibQuery += c;
            buildLibSearch();
            sLibSelIdx = -1; LibSetOpenBtn(dlg, false);
            UpdateLibScrollRange(sLibScrollBar, sLibListRect, sLibScrollOffset);
            drawLibList(dlg, sLibListRect, sLibScrollOffset, sLibSelIdx);
        }
        return;
    }
    if (c == (char)0x1E) {   /* up arrow */
        if (sLibSelIdx > 0) {
            int oldSel = sLibSelIdx, oldOff = sLibScrollOffset;
            sLibSelIdx--;
            scrollToSelection(sLibScrollBar, sLibListRect, sLibSelIdx, sLibScrollOffset);
            if (sLibScrollOffset != oldOff) invalidateLibList(dlg, sLibListRect);
            else redrawLibHoverRows(dlg, sLibListRect, sLibScrollOffset,
                                    sLibSelIdx, oldSel, sLibSelIdx);
            LibSetOpenBtn(dlg, true);
        } else if (sLibSelIdx < 0 && libDisplayCount() > 0) {
            sLibSelIdx = 0;
            redrawLibHoverRows(dlg, sLibListRect, sLibScrollOffset, sLibSelIdx, -1, sLibSelIdx);
            LibSetOpenBtn(dlg, true);
        }
        if (sLibSearchFocused) { sLibSearchFocused = false; drawLibSearchBarScrolled(dlg); }
        return;
    }
    if (c == (char)0x1F) {   /* down arrow */
        if (sLibSelIdx < libDisplayCount() - 1) {
            int oldSel = sLibSelIdx, oldOff = sLibScrollOffset;
            sLibSelIdx++;
            scrollToSelection(sLibScrollBar, sLibListRect, sLibSelIdx, sLibScrollOffset);
            if (sLibScrollOffset != oldOff) invalidateLibList(dlg, sLibListRect);
            else redrawLibHoverRows(dlg, sLibListRect, sLibScrollOffset,
                                    sLibSelIdx, oldSel, sLibSelIdx);
            LibSetOpenBtn(dlg, true);
        } else if (sLibSelIdx < 0 && libDisplayCount() > 0) {
            sLibSelIdx = 0;
            redrawLibHoverRows(dlg, sLibListRect, sLibScrollOffset, sLibSelIdx, -1, sLibSelIdx);
            LibSetOpenBtn(dlg, true);
        }
        if (sLibSearchFocused) { sLibSearchFocused = false; drawLibSearchBarScrolled(dlg); }
        return;
    }
    if ((e.modifiers & cmdKey) && (c == 'c' || c == 'C') && (e.modifiers & shiftKey))
        CopyDebugToClipboard();
}

/* ------------------------------------------------------------------ */
/* LibHandleMouse — mouseDown handler                                  */
/* ------------------------------------------------------------------ */
static void LibHandleMouse(DialogPtr dlg, EventRecord& e,
                           AppSettings& settings,
                           FetchPagesFunc fetchPages,
                           FetchPageChildrenFunc fetchChildren,
                           LibMovePageFunc movePage,
                           bool& done, bool& result)
{
    WindowPtr hitWin; short part = FindWindow(e.where, &hitWin);
    if (part == inMenuBar) { if (HandleDialogMenuBar(dlg, e)) done = true; return; }
    if (part == inDrag)    { HandleDialogDrag(dlg, e, hitWin); return; }
    if (part == inGoAway && hitWin == (WindowPtr)dlg) {
        if (TrackGoAway(hitWin, e.where)) done = true; return;
    }
    if (hitWin != (WindowPtr)dlg) return;

    SetPort(dlg); Point localPt = e.where; GlobalToLocal(&localPt);

    /* Scroll bar */
    if (sLibScrollBar) {
        ControlHandle hitCtl = nil;
        short ctlPart = FindControl(localPt, (WindowPtr)dlg, &hitCtl);
        if (hitCtl == sLibScrollBar && ctlPart != 0) {
            sLibScrollOffset = TrackScrollBar(sLibScrollBar, localPt,
                                              ctlPart, sLibScrollUPP);
            /* For thumb drag (kControlIndicatorPart) the action UPP is not
               called during tracking — redraw immediately after release.
               For arrow/page parts the action UPP already redraws live, but
               a final redraw ensures consistency.                           */
            drawLibList(dlg, sLibListRect, sLibScrollOffset, sLibSelIdx);
            return;
        }
    }

    /* Click in scrollable content area (search bar + list rows) */
    if (PtInRect(localPt, &sLibListRect)) {
        /* content pixel offset from the virtual top of all content */
        short contentPx = localPt.v - sLibListRect.top
                          + sLibScrollOffset * kLibRowHeight;

        /* Click in search bar area */
        if (contentPx < kLibSearchRowH) {
            if (PtInRect(localPt, &sLibRefreshRect)) {
                gLibPageCount = 0;
                sLibQuery.clear(); sLibSearchHitCount = 0; sLibAllFlatCount = 0;
                SafeStrCopy(sLibStatusMsg, sizeof(sLibStatusMsg), "Loading Library\xC9");
                drawLibStatusText(dlg, sLibListRect, sLibStatusMsg);
                drawLibSearchBarScrolled(dlg);
                if (fetchPages) {
                    /* Bypass disk cache so DoFetchPages hits the API */
                    InvalidatePageListCache();
                    /* Clear toggle cache to prevent child duplication */
                    gLibToggleCacheCount = 0;
                    if (gLibToggleCache)
                        memset(gLibToggleCache, 0, (long)kLibToggleCacheSize * sizeof(LibToggleCacheEntry));
                    NotionPageList pageList;
                    if (fetchPages(settings.apiKey, pageList)) {
                        SavePageListCache(settings.apiKey, pageList);
                        ReloadListAssets();
                        buildLibFromPageList(pageList);
                    }
                }
                if (gLibPageCount > 0) sLibStatusMsg[0] = '\0';
                else SafeStrCopy(sLibStatusMsg, sizeof(sLibStatusMsg),
                                 "No pages found. Check your integration token.");
                sLibSelIdx = -1; LibSetOpenBtn(dlg, false);
                UpdateLibScrollRange(sLibScrollBar, sLibListRect, sLibScrollOffset);
                invalidateLibList(dlg, sLibListRect);
            } else {
                sLibSearchFocused = true;
                drawLibSearchBarScrolled(dlg);
            }
            return;
        }

        int row = (contentPx - kLibSearchRowH) / kLibRowHeight;
        if (row >= 0 && row < libDisplayCount()) {
            if (!isLibSearchActive() && row == sLibHoverRow) {
                if (PtInRect(localPt, &sLibMoveUpRect) && row > 0) {
                    LibMoveRowUp(dlg, row, settings, movePage); return;
                }
                if (PtInRect(localPt, &sLibMoveDownRect) && row < gLibPageCount - 1) {
                    LibMoveRowDown(dlg, row, settings, movePage); return;
                }
                if (PtInRect(localPt, &sLibAddChildRect)) {
                    sLibNewPageRequested = true;
                    SafeStrCopy(sLibNewPageParentId, (int)sizeof(sLibNewPageParentId),
                                gLibPageElm[row].id);
                    done = true; return;
                }
            }

            if (!isLibSearchActive()) {
                LibPageEntry& entry = gLibPageElm[row];
                /* click zone aligned with DrawDisclosureTriangle draw position:
                   triCX = listRect.left + 20 + depth*kLibIndentStep + 5 */
                short triCX    = sLibListRect.left + 20 + entry.depth * kLibIndentStep + 1;
                short triLeft  = triCX - 6;
                short triRight = triCX + 8;
                /* a gray (known empty) triangle still opens: it re-checks */
                bool  triClick = (localPt.h >= triLeft && localPt.h <= triRight);
                if (triClick) {
                    if (entry.open) {
                        int subtreeEnd = findLibSubtreeEnd(row);
                        closeLibToggle(row);
                        if (sLibSelIdx > row && sLibSelIdx < subtreeEnd) { sLibSelIdx = -1; LibSetOpenBtn(dlg, false); }
                        UpdateLibScrollRange(sLibScrollBar, sLibListRect, sLibScrollOffset);
                    } else {
                        entry.open = true;
                        LibToggleCacheEntry* ce = LibToggleCacheFind(entry.id);
                        if (ce && ce->childCount > 0) {
                            insertLibChildrenDirect(row, ce->children, ce->childCount);
                        } else if (fetchChildren) {
                            drawLibList(dlg, sLibListRect, sLibScrollOffset, sLibSelIdx);
                            LibPageEntry childBuf[kLibToggleMaxChildren]; int childCount = 0;
                            bool ok = fetchChildren(settings.apiKey, entry.id,
                                                    childBuf, kLibToggleMaxChildren, childCount);
                            if (ok) {
                                if (childCount == 0) {
                                    entry.confirmedLeaf = true;
                                } else {
                                    entry.confirmedLeaf = false;
                                    LibToggleCacheEntry* nc = LibToggleCacheFindOrCreate(entry.id);
                                    int n = childCount < kLibToggleMaxChildren ? childCount : kLibToggleMaxChildren;
                                    memcpy(nc->children, childBuf, sizeof(LibPageEntry) * n);
                                    nc->childCount = n;
                                    insertLibChildrenDirect(row, childBuf, childCount);
                                }
                            }
                        } else {
                            entry.confirmedLeaf = true;
                        }
                        UpdateLibScrollRange(sLibScrollBar, sLibListRect, sLibScrollOffset);
                    }
                    invalidateLibList(dlg, sLibListRect); return;
                }
            }

            /* Row selection */
            bool wasSelected = (row == sLibSelIdx);
            int oldSel = sLibSelIdx;
            sLibSelIdx = row;
            LibSetOpenBtn(dlg, true);
            redrawLibHoverRows(dlg, sLibListRect, sLibScrollOffset,
                              sLibSelIdx, oldSel, sLibSelIdx);
            long now = TickCount();
            if (wasSelected && sLibLastClickRow == row &&
                (now - sLibLastClickTick) < GetDblTime()) {
                result = true; done = true;
            }
            /* Reset timer when switching rows so prior click on a
               different row can't accidentally trigger a double-click */
            if (sLibLastClickRow != row) sLibLastClickTick = 0;
            sLibLastClickTick = now;
            sLibLastClickRow  = row;
        }
        return;
    }

    /* "Delete" click — archive the selected page inline, stay in Library */
    if (!sLibFromConnection && sLibSelIdx >= 0 &&
        sLibDeleteRect.right > sLibDeleteRect.left &&
        PtInRect(localPt, &sLibDeleteRect)) {
        int delRow = sLibSelIdx;
        const char* delId = isLibSearchActive()
            ? sLibAllFlat[sLibSearchHits[delRow]].id
            : gLibPageElm[delRow].id;
        const char* delTitle = isLibSearchActive()
            ? sLibAllFlat[sLibSearchHits[delRow]].title
            : gLibPageElm[delRow].title;
        /* Don't delete the currently open page */
        if (strcmp(delId, sLibCurrentPageId) == 0) return;
        LibDelId* ids = (LibDelId*)NewPtr((Size)kLibMaxDelIds * (Size)sizeof(LibDelId));
        if (!ids) { SysBeep(1); return; }
        int n = collectLibSubpages(delId, ids);
        /* ...nor the page that is open, inside it; and say what goes too */
        bool ok = !libIdListed(ids, n, sLibCurrentPageId);
        if (!ok) SysBeep(1);
        else if (n > 1) ok = libConfirmDeleteSubpages(dlg, delTitle, n - 1);
        if (ok && DoLibDeletePage(settings.apiKey, delId)) {
            sLibDidDelete = true;
            if (n > 1) ForgetDeletedPages(settings.apiKey, ids + 1, n - 1);
            libForgetPages(ids, n);
            if (isLibSearchActive()) buildLibSearch();
            /* Nothing selected afterwards: a second click on Delete must
               not take the page that slid up into this row */
            sLibSelIdx = -1;
            UpdateLibScrollRange(sLibScrollBar, sLibListRect, sLibScrollOffset);
            drawLibList(dlg, sLibListRect, sLibScrollOffset, sLibSelIdx);
            LibSetOpenBtn(dlg, false);
        }
        DisposePtr((Ptr)ids);
        return;
    }
    /* "+ New page" UserItem click (not handled by DialogSelect) */
    if (!sLibFromConnection) {
        Rect newPageR; GetDialogItemRect(dlg, 3, newPageR);
        if (PtInRect(localPt, &newPageR)) {
            sLibNewPageRequested = true;
            SafeStrCopy(sLibNewPageParentId, (int)sizeof(sLibNewPageParentId), sLibCurrentPageId);
            done = true; return;
        }
    }

    /* Dialog button clicks */
    short dlgItem = 0;
    DialogSelect(&e, &dlg, &dlgItem);
    if (dlgItem == 1 && sLibSelIdx >= 0) {
        result = true; done = true;
    } else if (dlgItem == 2) {
        done = true;
    }
}

/* ------------------------------------------------------------------ */
/* LibTeardown — UPP disposal, dialog cleanup, buffer free            */
/* ------------------------------------------------------------------ */
static bool LibTeardown(DialogPtr dlg,
                        UserItemUPP divUPP,
                        bool result,
                        AppSettings& settings,
                        LibPageEntry* selectedOut)
{
    if (result && sLibSelIdx >= 0) {
        if (isLibSearchActive() && sLibSelIdx < sLibSearchHitCount) {
            const NotionPageEntry& e = sLibAllFlat[sLibSearchHits[sLibSelIdx]];
            SafeStrCopy(settings.pageID, (int)sizeof(settings.pageID), e.id);
            if (selectedOut) {
                /* Recover real tree depth from display list if visible */
                short realDepth = 0;
                for (int k = 0; k < gLibPageCount; k++) {
                    if (strcmp(gLibPageElm[k].id, e.id) == 0) {
                        realDepth = gLibPageElm[k].depth; break;
                    }
                }
                CopyToLibEntry(*selectedOut, e, realDepth);
            }
        } else if (sLibSelIdx < gLibPageCount) {
            SafeStrCopy(settings.pageID, (int)sizeof(settings.pageID),
                        gLibPageElm[sLibSelIdx].id);
            if (selectedOut) *selectedOut = gLibPageElm[sLibSelIdx];
        }
    }
    if (sLibOrderDirty) {
        saveLibOrderToCache(settings.apiKey);
        sLibOrderDirty = false;
    }
    if (sLibScrollUPP) { DisposeControlActionUPP(sLibScrollUPP); sLibScrollUPP = nil; }
    if (sLibScrollBar) { DisposeControl(sLibScrollBar); sLibScrollBar = nil; }
    sLibScrollOff = nil; sLibDlg = nil;
    DisposeDialog(dlg);
    DisposeUserItemUPP(divUPP);
    if (sNewPageUPP) { DisposeUserItemUPP(sNewPageUPP); sNewPageUPP = nil; }
    if (gLibPageElm)      { DisposePtr((Ptr)gLibPageElm);      gLibPageElm      = nil; }
    if (gLibToggleCache)  { DisposePtr((Ptr)gLibToggleCache);  gLibToggleCache  = nil; }
    if (sLibAllFlat)      { DisposePtr((Ptr)sLibAllFlat);      sLibAllFlat      = nil; }
    if (sLibAllFlatLower) { DisposePtr((Ptr)sLibAllFlatLower); sLibAllFlatLower = nil; }
    sLibQuery.clear(); sLibSearchHitCount = 0; sLibAllFlatCount = 0;
    gLibPageCount = 0; gLibToggleCacheCount = 0;
    return result;
}

/* ------------------------------------------------------------------ */
/* ShowLibraryDialog — core Library window used by all three entries  */
/* ------------------------------------------------------------------ */
bool ShowLibraryDialog(AppSettings &settings,
                       FetchPagesFunc fetchPages,
                       FetchPageChildrenFunc fetchChildren,
                       LibMovePageFunc movePage,
                       LibraryPrimaryAction action,
                       bool forceReload,
                       bool fromConnection,
                       LibPageEntry* selectedOut)
{
    DialogPtr dlg = nil;
    UserItemUPP divUPP = nil;

    if (!LibSetupDialog(settings, fetchPages, action, forceReload,
                        fromConnection, dlg, divUPP))
        return false;

    /* Wrong token: forced reload (fromConnection) found no pages.
       Close the library immediately and return to the token panel. */
    if (fromConnection && gLibPageCount == 0) {
        LibTeardown(dlg, divUPP, false, settings, selectedOut);
        return false;
    }

    DrawDefaultButtonRing(dlg, 1);
    DisableDialogMenus();
    bool result = false, done = false;

    while (!done) {
        EventRecord e;
        WaitNextEvent(everyEvent, &e, 4, nil);

        if (e.what == updateEvt) {
            WindowPtr wp = (WindowPtr)e.message;
            BeginUpdate(wp);
            if (((WindowPeek)wp)->windowKind == dialogKind && (DialogPtr)wp == dlg) {
                DrawDialog((DialogPtr)wp);
                if (sLibStatusMsg[0]) {
                    drawLibStatusText(dlg, sLibListRect, sLibStatusMsg);
                    drawLibSearchBarScrolled(dlg);
                } else {
                    drawLibList(dlg, sLibListRect, sLibScrollOffset, sLibSelIdx);
                    /* search bar drawn inside drawLibList */
                }
                DrawDefaultButtonRing(dlg, 1);
            }
            EndUpdate(wp); continue;
        }
        if (e.what == osEvt || e.what == activateEvt) continue;
        if (e.what == nullEvent)              { LibHandleNull(dlg); continue; }
        if (e.what == keyDown || e.what == autoKey) {
            LibHandleKey(dlg, e, done, result); continue;
        }
        if (e.what == mouseDown) {
            LibHandleMouse(dlg, e, settings, fetchPages, fetchChildren,
                           movePage, done, result);
            continue;
        }
        if (e.what != nullEvent) {
            short dlgItem = 0; DialogSelect(&e, &dlg, &dlgItem);
        }
    }

    RestoreDialogMenus();
    return LibTeardown(dlg, divUPP, result, settings, selectedOut);
}
