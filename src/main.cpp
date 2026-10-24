#include <string>

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
#include <Threads.h>
#include <Gestalt.h>
#include <Processes.h>
#include <Scrap.h>
#include <Files.h>
#include <Folders.h>

#include <stdio.h>
#include <stddef.h>

#include "gason.hpp"
#include "HttpClient.h"
#include "Settings.h"
#include "library_window.h"
#include "helpers.h"
#include "debug_con.h"
#include "growzone.h"
#include "blocks.h"
#include "app_state.h"
#include "run_styles.h"
#include "menu.h"
#include "content_events.h"

using std::string;
using std::vector;

AppSettings gSettings;

/* Demo page shown on first launch (no prefs file).  Public read-only credentials —
   intentionally visible in source; not a secret.  Never stored in gSettings so the
   settings dialog fields stay empty until the user enters their own credentials. */
static const char kDemoPageID[] = "3fa05fe4e8c780d481fcfbd7cdb6335a";
static const char kDemoAPIKey[] = "ntn_c1608837319ba4BKLkkgJ9ninUwfzTt4UWReZSecgB754w";
const char* EffPageID() { return gSettings.pageID[0] ? gSettings.pageID : kDemoPageID; }
const char* EffAPIKey() { return gSettings.apiKey[0] ? gSettings.apiKey : kDemoAPIKey; }

static const short kPageNotFoundAlertID = 135;  /* ALRT: the page is gone (404) */

void buildCurrentHeader(PageHeader& h);
void SyncCurrentPageInHistory();

WindowPtr gWindow;
Rect gInitialWindowRect; /* extern'd by block_render.cpp (MakeNewWindow) */
int blockSelect;

/* Scroll state */
ControlHandle   gScrollBar          = nil;
int             gScrollOffset       = 0;
int             gTotalContentHeight = 0;
ControlActionUPP gScrollActionUPP   = nil;

/* Save button state */
ControlHandle   gSaveButton         = nil;
/* BlockType, TextRun, Block, UndoState, kMax* constants, isToggleType etc.
   are all defined in blocks.h (included above).                            */
UndoState       gUndo;                    /* Cmd-Z: the other version of its block */
SaveQueue       gSaveQueue;               /* indices of blocks waiting to be PATCHed */
uint32_t        gRecreateSerial   = 0;  /* block being re-created (type change, move) */
uint32_t        gAddSerial          = 0;  /* newly added block awaiting its API-assigned ID */
uint32_t        gToggleSerial       = 0;  /* toggle whose children are loading */
long            gRequestStartTick   = 0;     /* TickCount() when current request was issued */
static const long kRequestTimeoutTicks = 30L * 60L; /* 30 s × 60 ticks/s */
bool            gHasMore            = false; /* Notion returned has_more:true for blocks */
char            gNextCursor[256] = "";       /* next_cursor from last paginated response */
bool            gScrolling          = false; /* set during scrollActionProc to skip full EraseRect */
RgnHandle gScrollClipRgn     = nil;   /* exposed-strip clip region; non-nil during scroll draw */
ThreadID        gSaveThreadID       = kNoThreadID;

AppState gAppState = {};
void MarkDirty() { gAppState.hasDirtyBlocks = true; gAppState.lastEditTick = TickCount(); }

Rect            gMoveUpArrowRect    = {0,0,0,0};
Rect            gMoveDownArrowRect  = {0,0,0,0};
Rect            gAddBlockRect       = {0,0,0,0};
bool            gArrowsVisible      = false;
/* gHoverBlockIdx, gSidebarPending, gSidebarPendingTick, gSidebarPendingBlock,
   kSidebarPendingTimeout — defined in block_render.cpp; declared in block_render.h */

extern const short kScrollBarWidth   = 16;
extern const short kLineScrollAmount = 28;
extern const short kSidebarWidth     = 56;   /* left sidebar panel width */

static const short kBottomMargin       = 28;
#define kMBarHeight (*(short*)0x0BAA)
extern const long  kSavedDisplayTicks  = 360L;
static const long  kAutoSaveTicks      = 2400L;
extern const long  kFetchThreadStack   = 256L * 1024L;
/* kMaxRuns/kMaxEmoji/kMaxTextLen/kMaxUrlLen/kMaxBlocks defined in blocks.h */
static const int   kMaxIcons   = 512;

/* Menu resource IDs and item numbers — defined in menu.h */

#include "navigation.h"
#include "save_pipeline.h"
#include "page_fetch.h"
#include "block_render.h"
#include "block_ops.h"
#include "page_lifecycle.h"
#include "page_cache.h"

/* True while pageElm[0] holds a placeholder title from cached nav history.
   The kPhaseIdle metadata response overwrites it instead of appending. */
bool              g_titleIsPlaceholder = false;

/* §1: heap-allocated at startup (see main), never freed — keeps this large
   array out of BSS, which would permanently shrink the Classic Mac heap. */
Block* pageElm      = NULL;
int    pageElmCount = 0;
int    gMaxBlocks   = 0;

/* Page load phase — PageRequestPhase lives in app_state.h (shared). */
int gCurRequest = kPhaseIdle;
HttpClient gHttpClient;
TEHandle textActive;
char   gPageIconName[32]  = ""; /* native Notion icon slug, e.g. "house"; empty if none */
char   gPageIconColor[32] = "gray"; /* icon color from API */
short  gAppVRefNum = 0;    /* volume ref of folder containing the app */
long   gAppDirID   = 0;    /* directory ID of folder containing the app */
long   gIconsDirID = 0;    /* cached dirID of the icon/ subfolder */
Rect   gIconRect   = {0,0,0,0}; /* drawn rect of page icon (for click detection) */
short  gColLeft    = kSidebarWidth; /* left edge of centred text column; updated each updateBlocks() */
bool   gFullWidth      = false;     /* full-screen window mode */
Rect   gSavedWindowRect = {42, 6, 375, 505}; /* window rect before entering full-screen */

/* gIconNames, gIconCount, gSpriteBM, gEmojiGWorld — defined in picker.cpp */
/* DebugLog, CopyDebugToClipboard, ToggleDebugWin, DrawDebugWin — defined in debug_con.cpp */
/* LogBuildTime — defined in build_stamp.cpp, always recompiled for fresh __DATE__/__TIME__ */
extern void LogBuildTime(void);
SInt16  gFontHelvetica = 0;  /* cached font number — looked up once at startup */
SInt16  gFontGeneva    = 0;
SInt16  gFontMonaco    = 0;

/* isToggleType, isLineHeightType, promoteToggleHeading — blocks.h
   resolveIconsDirID, load_pict_handle, acquire_port_bitmap, FreeGraphicsAssets,
   DrawNotionIcon — picker.cpp
   TextToTE, StripEmojiPadInto, DrawBlockEmojis, DrawStrikethrough — emoji_text.h
   SyncTEInto, SyncBlockText — emoji_text.cpp */

void QuitApp() {
    if (gAppState.hasDirtyBlocks && !gAppState.saving()) {
        DialogPtr dlg = GetNewDialog(132, NULL, (WindowPtr)-1);
        if (dlg) {
            InitCursor(); ShowWindow(dlg); SetPort(dlg);
            { DialogItemType t; Handle h; Rect r;
              GetDialogItem(dlg, 1, &t, &h, &r);
              InsetRect(&r, -4, -4);
              PenSize(3, 3); FrameRoundRect(&r, 16, 16); PenNormal(); }
            short item = 0;
            EventRecord evt;
            while (item == 0) {
                WaitNextEvent(everyEvent, &evt, 6, nil);
                if (evt.what == updateEvt) {
                    WindowPtr wp = (WindowPtr)evt.message;
                    BeginUpdate(wp); if (((WindowPeek)wp)->windowKind == dialogKind) {
                        DrawDialog((DialogPtr)wp);
                        if ((DialogPtr)wp == dlg) DrawDefaultButtonRing(dlg, 1);
                    } EndUpdate(wp); continue;
                }
                if (evt.what == osEvt || evt.what == activateEvt) continue;
                if (evt.what == mouseDown) {
                    WindowPtr cw; if (FindWindow(evt.where, &cw) == inGoAway &&
                        (DialogPtr)cw == dlg && TrackGoAway(cw, evt.where)) { item = -1; break; }
                }
                if (evt.what == keyDown || evt.what == autoKey) {
                    char k = (char)(evt.message & charCodeMask);
                    if (k == 0x0D || k == 0x03) { item = 1; break; }
                    if (k == 0x1B) { item = -1; break; }
                }
                short di = 0;
                if (DialogSelect(&evt, &dlg, &di) && di > 0) item = di;
            }
            DisposeDialog(dlg);
            if (item <= 0) return;        /* Cancel — don't quit */
            if (item == 1) {              /* Save then quit */
                /* doSave only spawns the save thread — drive it to completion
                   before ExitToShell, as prepareForPageList does. */
                SetCursor(*GetCursor(watchCursor));
                while (gAppState.apiPending || gAppState.togglePending || gAppState.saving())
                    YieldToAnyThread();
                doSave();
                while (gAppState.saving())
                    YieldToAnyThread();
                InitCursor();
                /* Network error or no thread: Save is re-shown, stay open */
                if (gAppState.hasDirtyBlocks) return;
            }
            /* item == 2: Don't Save — fall through to quit */
        }
    }
    gHttpClient.SslFreeAll(); ExitToShell();
}

/* Notion API URL helpers — reserve to avoid realloc when appending the UUID */
string BlocksURL(const char* id) {
    string u; u.reserve(80);
    u = "https://api.notion.com/v1/blocks/"; u += id; return u;
}
string PagesURL(const char* id) {
    string u; u.reserve(80);
    u = "https://api.notion.com/v1/pages/"; u += id; return u;
}
static inline void SetBearer(const char* key) {
    char buf[270]; snprintf(buf, sizeof(buf), "Bearer %s", key);
    gHttpClient.SetAuthorization(buf);
}

/* Warm-up with single retry on stale-ticket rejection. */
static void WarmUpWithRetry(const char* statusMsg = nil) {
    if (!gHttpClient.WarmUp("api.notion.com") && gHttpClient.ResumeRejected()) {
        if (statusMsg) DrawStatusText(statusMsg);
        gHttpClient.WarmUp("api.notion.com");
    }
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main() {
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();

    /* Cache font numbers once — GetFNum is a slow system lookup */
    GetFNum((ConstStr255Param)"\pHelvetica", &gFontHelvetica);
    GetFNum((ConstStr255Param)"\pGeneva",    &gFontGeneva);
    GetFNum((ConstStr255Param)"\pMonaco",    &gFontMonaco);

    /* Cache the folder that contains this app so DrawNotionIcon can find icon/ */
    {
        ProcessSerialNumber psn = { 0, kCurrentProcess };
        ProcessInfoRec info;
        FSSpec appSpec;
        memset(&info, 0, sizeof(info));
        info.processInfoLength = sizeof(ProcessInfoRec);
        info.processName       = nil;
        info.processAppSpec    = &appSpec;
        if (GetProcessInformation(&psn, &info) == noErr) {
            gAppVRefNum = appSpec.vRefNum;
            gAppDirID   = appSpec.parID;
        }
    }
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    GrowZoneInstall();  /* 32K emergency reserve, released under heap pressure */

    SetMenuBar(GetNewMBar(128));
    AppendResMenu(GetMenu(128), 'DRVR');
    DrawMenuBar();

    /* Arrow-key glyphs for Move Up/Down in the Edit menu. SetMenuItemKeyGlyph
     * needs Appearance Manager (Mac OS 8+), so guard with a Gestalt check. */
    {
        MenuHandle editMenu = GetMenuHandle(130);
        if (editMenu) {
            long gestaltResult = 0;
            if (Gestalt(gestaltAppearanceAttr, &gestaltResult) == noErr) {
                SetItemCmd(editMenu, 11, 0x1E);
                SetMenuItemKeyGlyph(editMenu, 11, kMenuUpArrowGlyph);
                SetItemCmd(editMenu, 12, 0x1F);
                SetMenuItemKeyGlyph(editMenu, 12, kMenuDownArrowGlyph);
            }
        }
    }

    InitCursor();

    gScrollActionUPP = NewControlActionUPP(scrollActionProc);

    /* Create window early so status can be shown in it */
    short initBottom = qd.screenBits.bounds.bottom - 4;
    if (initBottom > 570) initBottom = 570;
    short initRight  = qd.screenBits.bounds.right - 4;
    if (initRight  > 505) initRight  = 505;
    SetRect(&gInitialWindowRect, 6, 42, initRight, initBottom);
    gWindow = NewWindow(NULL, &gInitialWindowRect, "\pNotion classic",
                        true, documentProc, (WindowPtr)-1, true, 0);

    /* Allocate the large block array on the heap, not BSS: BSS is carved out
       before the heap exists, so a big BSS array permanently shrinks the heap.
       Size from FreeMem() so low-RAM Macs get fewer blocks; MaxMem first to
       compact+purge for an accurate, low-frag budget. Reserve 1400K for
       TLS/MacTCP/JSON/TE/sprite/ECDSA + margin (raised from 1200K after a 6MB
       Classic II hit -23009 at 1283K free). Clamp to [80, kMaxBlocks]. */
    { Size   _grow;
      MaxMem(&_grow);                   /* compact + purge before reading */
      long avail  = (long)FreeMem();
      long reserve = 1400L * 1024L;
      long budget  = avail > reserve ? avail - reserve : 0L;
      gMaxBlocks   = (int)(budget / (long)sizeof(Block));
      if (gMaxBlocks > kMaxBlocks) gMaxBlocks = kMaxBlocks;
      if (gMaxBlocks < 80)         gMaxBlocks = 80;
    }
    pageElm      = new Block[gMaxBlocks];
    gToggleCache = (ToggleCacheEntry*)NewPtrClear((Size)(4 * sizeof(ToggleCacheEntry)));

    if (LoadDebugMode())
        ToggleDebugWin();
    LogBuildTime();
    { char _mb[32]; snprintf(_mb, sizeof(_mb), "blocks=%d", gMaxBlocks); DebugLog("start", _mb); }

    LoadSettings(gSettings);
    LoadNavHistory(gHistBack, gHistBackCount, gHistFwd, gHistFwdCount);
    LoadRecentPages(gRecentPages, gRecentCount);
    gFullWidth = LoadFullWidth();
    { char _pid[54]; snprintf(_pid, 54, "/v1/pages/%s", EffPageID()); DebugLog("pageID", _pid); }
    if (gFullWidth && gWindow != nil) {
        /* gInitialWindowRect is the content-area rect passed to NewWindow */
        gSavedWindowRect = gInitialWindowRect;
        short mbarH = kMBarHeight;
        if (mbarH < 16 || mbarH > 40) mbarH = 20;
        short screenW = qd.screenBits.bounds.right;
        short screenH = qd.screenBits.bounds.bottom - mbarH;
        MoveWindow(gWindow, 0, mbarH, true);
        SizeWindow(gWindow, screenW, screenH, true);
        /* resizeControls() is called later when MakeNewWindow runs */
    }
    /* Fresh install (no prefs): gSettings fields are empty; EffPageID()/EffAPIKey()
       fall back to the built-in demo page automatically.  No dialog on startup. */

    RefreshAuth();

    /* Render the cached title+icon as a placeholder (~1s, no network) with
       status text below, then ALWAYS fetch fresh. Rendering the full cached
       page here looked finished but was frozen during warm-up and could be
       stale; the disk cache still serves navigation/reload. */
    ShowStartupHeaderPreview();

    /* DrawStatusBelowHeader falls back to full-window DrawStatusText when
       no header placeholder is on screen, so it's safe to call either way. */
    DrawStatusBelowHeader("Notion classic is Dialing in\xC9");
    if (!gHttpClient.WarmUp("api.notion.com") && gHttpClient.ResumeRejected()) {
        /* Stale session ticket rejected (normal after key rotation). Retry
           once with a full handshake. Genuine network failures skip this;
           pageRequest() reconnects from the Dead state anyway. */
        DrawStatusBelowHeader("Crunching elliptic curves at 16 MHz\xC9");
        gHttpClient.WarmUp("api.notion.com");
    }
    DrawStatusBelowHeader("Loading page\xC9");
    pageRequest();

    /* Initialize to "now": TickCount() counts from boot, so 0 would make the
       30-min idle predicate true on the first pass through the loop. */
    static long gLastEventTick = TickCount();  /* last user event — drives idle ticket refresh */

    for (;;) {
        GrowZoneIdle();  /* log + re-arm memory reserve if it was consumed */
        long now = TickCount();  /* cached once per loop; refreshed after WNE */

        /* Toggles opened only to carry their children along: close them
           again once that is saved (no-op otherwise). */
        RecloseOpenedToggles();

        /* Flush the page disk cache after a successful save — keeps the
           cached copy in sync with what was just PATCHed to Notion.
           !hasDirtyBlocks guards the failed-save case: a network error
           re-sets that flag, and caching then would persist unsaved text. */
        if (gPageCacheNeedsSave && gCurRequest == kPhaseFullyLoaded &&
            !gAppState.saving() && !gAppState.apiPending && !gAppState.hasDirtyBlocks) {
            gPageCacheNeedsSave = false;
            SyncCurrentPageInHistory();
            SavePageCacheNow();
        }

        /* A non-200 / network error was flagged from inside a callback.
           Fresh install + TCP failure: don't block with a dialog — retry after
           a short wait so the demo page appears once MacTCP comes up. Otherwise
           show the appropriate settings dialog. */
        if (gAppState.needsReload) {
            gAppState.needsReload = false;
            /* This path calls pageRequest() directly (never TryLoadPageFromCache),
               so a Reload-armed bypass flag would otherwise survive and silently
               skip the cache on a later, unrelated navigation. */
            gPageCacheBypassOnce = false;
            bool freshInstall = gSettings.apiKey[0] == '\0';
            if (freshInstall && gAppState.reloadIsNetworkError) {
                /* Network not up yet — wait 3 s then retry the demo fetch. Give
                   up after 3 attempts so slow/TLS-timeout hardware isn't stuck
                   in an infinite loop; fall through to the settings dialog. */
                static int sFreshRetryCount = 0;
                if (sFreshRetryCount < 3) {
                    sFreshRetryCount++;
                    DrawStatusText("No connection. Retrying\xC9");
                    long retryTick = TickCount() + 180; /* ~3 s */
                    /* Keep the UI alive during the wait: service updates and
                       allow Cmd-Q / Cmd-. to abort. */
                    while (TickCount() < retryTick) {
                        EventRecord e;
                        if (WaitNextEvent(everyEvent, &e, 4, nil)) {
                            if (e.what == updateEvt) {
                                BeginUpdate((WindowPtr)e.message);
                                EndUpdate((WindowPtr)e.message);
                            } else if (e.what == keyDown && (e.modifiers & cmdKey)) {
                                char c = e.message & charCodeMask;
                                if (c == 'q' || c == 'Q' || c == '.') QuitApp();
                            }
                        }
                    }
                    gHttpClient.ForceReconnect();
                    pageRequest();
                    continue;  /* back to top of for(;;) */
                }
                sFreshRetryCount = 0; /* reset for next time */
                /* Fall through to the settings dialog below */
            }
            const char* dlgTitle = freshInstall ? "Welcome" : "Connection Error";
            if (gAppState.pageNotFound) {
                /* A link (or Back/Forward) led to a page that's gone: say so,
                   forget the page and go back to where you were. */
                gAppState.pageNotFound = false;
                char missing[1][33];
                stripHyphens(EffPageID(), missing[0], (int)sizeof(missing[0]));
                InitCursor();
                NoteAlert(kPageNotFoundAlertID, nil);
                ForgetDeletedPages(EffAPIKey(), missing, 1);
                if (ReturnFromMissingPage()) continue;
                dlgTitle = "Page not found";
            }
            HideWindow(gWindow);
            prepareForPageList();
            if (ShowSettingsDialog(gSettings, true, dlgTitle,
                                  DoFetchPages, DoFetchPageChildren, DoLibMovePage)) {
                SetListEmojiGWorld(nil);
                ShowWindow(gWindow);
                RefreshAuth();
                unloadPage();
                gHttpClient.ForceReconnect();
                pageRequest();
            } else if (LibNewPageWasRequested()) {
                SetListEmojiGWorld(nil);
                ShowWindow(gWindow);
                RefreshAuth();
                doNewPage(LibNewPageParentId());
            } else {
                QuitApp();
            }
        }

        if (gAppState.timedOut) {
            gAppState.timedOut = false;
            gPageCacheBypassOnce = false;  /* same reason as needsReload above */
            unloadPage();
            /* Re-warm; retry only on stale-ticket rejection (same gate as
             * startup). Network failures skip it — pageRequest() reconnects. */
            DrawStatusText("Request timed out. Reconnecting\xC9");
            WarmUpWithRetry("Crunching elliptic curves at 16 MHz\xC9");
            DrawStatusText("Retrying\xC9");
            pageRequest();
        }

        /* Deferred reload — wait for any in-progress save to complete first.
           Reload supersedes any pending navigation; clear it to avoid a
           stale navPending firing immediately after the reload completes. */
        if (gAppState.pendingReload && !gAppState.saving()) {
            gAppState.pendingReload = false;
            gAppState.navPending    = false;
            gNavDelta               = 0;
            gNavTarget[0]           = '\0';
            reloadPage();
        }

        /* Deferred new-page navigation (cannot navigate from inside the callback) */
        if (gAppState.newPageReady && !gAppState.saving()) {
            if (gAppState.hasDirtyBlocks) {
                doSave();   /* flush before navigating away */
            } else {
                gAppState.newPageReady = false;
                PageHeader cur; buildCurrentHeader(cur);
                pushToBack(cur);
                gHistFwdCount = 0;
                SafeStrCopy(gSettings.pageID, sizeof(gSettings.pageID), gNewPageID);
                gNewPageID[0] = '\0';
                SaveSettings(gSettings);
                SaveNavHistory(gHistBack, gHistBackCount, gHistFwd, gHistFwdCount);
                reloadPage();
            }
        }

        /* Navigation delay: coalesce rapid Back/Forward presses into one
           HTTP request.  gNavDelta tracks net direction; gNavTarget holds
           an explicit page ID (from page-link clicks) which takes priority. */
        if (gAppState.navPending && !gAppState.needsReload && !gAppState.timedOut &&
            now - gNavPendingTick > kNavDelayTicks) {
            /* Flush any pending save before navigating away */
            if (gAppState.hasDirtyBlocks && !gAppState.saving()) {
                doSave();
            }
            if (gAppState.isSaving || gAppState.saveThreadActive) {
                /* Wait for save to finish; keep gAppState.navPending set */
            } else {
                gAppState.navPending = false;
                if (gNavTarget[0]) {
                    char t[64]; SafeStrCopy(t, sizeof(t), gNavTarget);
                    gNavTarget[0] = '\0'; gNavDelta = 0;
                    commitNavigateTo(t);
                } else if (gNavDelta < 0) {
                    int n = -gNavDelta; gNavDelta = 0;
                    navigateBackN(n);
                } else if (gNavDelta > 0) {
                    int n = gNavDelta; gNavDelta = 0;
                    navigateForwardN(n);
                }
            }
        }

        /* Timeout watchdog: fires only during initial page load */
        if (gCurRequest < kPhaseFullyLoaded && gRequestStartTick > 0
            && now - gRequestStartTick > kRequestTimeoutTicks) {
            gRequestStartTick = 0;
            DrawStatusText("Request timed out.");
            gAppState.timedOut = true;
        }

        if (gCurRequest < kPhaseFullyLoaded || gAppState.togglePending) gHttpClient.ProcessRequests();

        /* Give the save thread a slice each loop iteration */
        if (gAppState.saveThreadActive) YieldToAnyThread();

        FlushDebugLog();
        updateArrowVisibility();
        updateSaveButton();

        /* Watch cursor while loading/saving; arrow otherwise */
        { bool busy = (gCurRequest < kPhaseFullyLoaded) || gAppState.saveThreadActive || gAppState.isSaving
                   || gAppState.togglePending    || gAppState.apiPending;
          static bool sCursorIsWatch = false;
          if (busy && !sCursorIsWatch) {
              CursHandle wc = GetCursor(watchCursor);
              if (wc) SetCursor(*wc);
              sCursorIsWatch = true;
          } else if (!busy && sCursorIsWatch) {
              InitCursor();
              sCursorIsWatch = false;
          }
        }

        EventRecord e;
        WindowRef win;
        /* Spin during page load or active save; blink when editing; sleep when idle */
        long sleepTicks = (gCurRequest < kPhaseFullyLoaded || gAppState.saveThreadActive || gAppState.togglePending) ? 0
                        : (textActive != nil) ? GetCaretTime()
                        : 6;

        if (WaitNextEvent(everyEvent, &e, sleepTicks, nil)) {
            now = TickCount();
            gLastEventTick = now;  /* for idle ticket refresh */
            switch (e.what) {
                case nullEvent:
                    if (textActive) { SetPort(gWindow); TEIdle(textActive); }
                    break;
                case keyDown:
                    HandleKeyDown(e);
                    break;

                case mouseDown:
                    switch (FindWindow(e.where, &win)) {
                        case inGoAway:
                            if (gDebugWin && win == gDebugWin) {
                                if (TrackGoAway(win, e.where)) ToggleDebugWin();
                            } else {
                                if (TrackGoAway(win, e.where)) QuitApp();
                            }
                            break;
                        case inDrag:
                            DragWindow(win, e.where, &qd.screenBits.bounds);
                            break;
                        case inMenuBar:
                            UpdateMenus();
                            DoMenuCommand(MenuSelect(e.where));
                            break;
                        case inContent:
                            doInContent(&e, win);
                            break;
                        case inGrow: {
                            Rect sizeRect;
                            SetRect(&sizeRect, 200, 100,
                                    qd.screenBits.bounds.right  - 2,
                                    qd.screenBits.bounds.bottom - 2);
                            long newSize = GrowWindow(win, e.where, &sizeRect);
                            if (newSize != 0) {
                                SizeWindow(win, (short)(newSize & 0xFFFF), (short)(newSize >> 16), true);
                                resizeControls();
                                updateBlocks();
                                if (HasSel())
                                    textActive = pageElm[blockSelect].textEdit;
                                InvalidateSaveButton();
                            }
                            break;
                        }
                        case inSysWindow:
                            SystemClick(&e, win);
                            break;
                    }
                    break;

                case updateEvt: {
                    WindowPtr uw = (WindowPtr)e.message;
                    if (gDebugWin && uw == gDebugWin) {
                        BeginUpdate(gDebugWin);
                        DrawDebugWin();
                        EndUpdate(gDebugWin);
                    } else {
                        gWindow = (WindowRef)e.message;
                        BeginUpdate(gWindow);
                        /* BeginUpdate clips to the exposed region, so a full erase
                           only repaints what an overlapping window uncovered —
                           including the scrollbar column / margins updateBlocks skips. */
                        gFullEraseOnce = true;
                        updateBlocks();
                        if (HasSel())
                            textActive = pageElm[blockSelect].textEdit;
                        InvalidateSaveButton();
                        EndUpdate(gWindow);
                    }
                    break;
                }
            }
        } else {
            /* WaitNextEvent returned false (no event) — tick caret */
            if (textActive) { SetPort(gWindow); TEIdle(textActive); }

            /* Idle TLS ticket refresh: server tickets live ~24h. If ours is
               >20h old, the user has been idle ≥30s, and nothing is loading
               or saving, re-handshake now so the next action doesn't eat a
               stale-reject + full handshake on slow hardware.              */
            {
                static long sLastRefreshTick = 0;
                bool busy = (gCurRequest < kPhaseFullyLoaded) || gAppState.saveThreadActive
                         || gAppState.isSaving || gAppState.togglePending
                         || gAppState.apiPending;
                now = TickCount();
                if (!busy
                    && now - gLastEventTick > 30L * 60L
                    && (!sLastRefreshTick || now - sLastRefreshTick > 5L * 60L * 60L)
                    && gHttpClient.TicketAgeSeconds() > 20L * 3600L) {
                    sLastRefreshTick = now;
                    DebugLog("ticket", "idle refresh (>20h old)");
                    gHttpClient.ForceReconnect(); /* real handshake, not an Idle no-op */
                    WarmUpWithRetry();
                }
            }
        }
    }
    return 0;
}
