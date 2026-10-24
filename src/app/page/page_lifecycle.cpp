/* page_lifecycle.cpp — Page load, unload, snapshot, and auth helpers.
   Extracted from main.cpp.                                             */
#include "page_lifecycle.h"
#include <Memory.h>
#include <Windows.h>
#include <Controls.h>
#include <TextEdit.h>
#include <Quickdraw.h>
#include <Threads.h>
#include "HttpClient.h"        // HttpClient type for extern gHttpClient
#include "block_ops.h"         // HasSel, syncTextActive, ToggleCacheClear, gToggleCacheCount
#include "block_render.h"      // updateBlocks, SyncActiveBlock, MakeNewWindow, DrawStatusText
#include "save_pipeline.h"     // doSave
#include "navigation.h"        // gHistBackCount, gHistFwdCount, gRecentCount, findCachedHeader, g_pendingNavHeader, g_hasPendingNavHeader
#include "page_fetch.h"        // pageRequest
#include "page_cache.h"        // TryLoadPageFromCache, gPageCacheBypassOnce
#include "picker.h"            // FreeGraphicsAssets, gIconList
#include "Settings.h"          // SetListEmojiGWorld, SaveNavHistory
#include "helpers.h"           // SafeStrCopy
#include "app_state.h"
#include "debug_con.h"
#include <Dialogs.h>
#include <string.h>
#include <string>
using std::string;

extern AppState     gAppState;
extern AppSettings  gSettings;
extern Block*       pageElm;
extern int          pageElmCount;
extern int          gMaxBlocks;
extern int          blockSelect;
extern TEHandle     textActive;
extern HttpClient   gHttpClient;
extern int          gCurRequest;
extern bool         gHasMore;
extern char         gNextCursor[];
extern long         gRequestStartTick;
extern int          gScrollOffset;
extern int          gTotalContentHeight;
extern WindowPtr    gWindow;
extern ControlHandle gScrollBar;
extern bool         g_titleIsPlaceholder;
extern SaveQueue    gSaveQueue;
extern uint32_t     gRecreateSerial;
extern uint32_t     gAddSerial;
extern char         gPageIconName[32];
extern char         gPageIconColor[32];
extern short        gAppVRefNum;
extern long         gAppDirID;
extern long         gIconsDirID;
extern void MarkDirty();
extern const char* EffPageID();
extern const char* EffAPIKey();

/* ------------------------------------------------------------------ */
/* Page-list context — snapshot for Cancel restore                    */
/* ------------------------------------------------------------------ */
struct PageListCtx {
    int savedScroll;
    int savedSelect;
};
static PageListCtx gPLCtx = { 0, 0 };

/* ------------------------------------------------------------------ */
/* DisposeAllTEHandles — free TE handles for all blocks               */
/* ------------------------------------------------------------------ */
void DisposeAllTEHandles() {
    for (int i = 0; i < pageElmCount; i++) {
        if (pageElm[i].textEdit != nil) {
            TEDispose(pageElm[i].textEdit);
            pageElm[i].textEdit = nil;
        }
    }
}

/* ------------------------------------------------------------------ */
/* unloadPage — drain threads and discard all current page state       */
/* ------------------------------------------------------------------ */
void unloadPage() {
    DEBUG_LOG("unloadPage", "start");
    /* Drain any in-flight save/API cooperative thread BEFORE touching shared
       state.  These threads call patchNext/afterRecreate etc. which index
       into pageElm — clearing the vector while a thread is suspended mid-
       callback causes the type-3 (Illegal Instruction / bad pointer) crash. */
    while (gAppState.saveThreadActive || gAppState.apiPending)
        YieldToAnyThread();

    DisposeAllTEHandles();
    pageElmCount = 0;
    g_titleIsPlaceholder  = false;
    g_hasPendingNavHeader = false;
    ToggleCacheClear();   /* disposes the packed rows too */

    gAppState.isSaving         = false;
    gAppState.saveThreadActive = false;
    gAppState.readOnly         = false;
    gAppState.apiPending       = false;
    gRecreateSerial = 0;
    gAddSerial        = 0;
    ClearDeferredTrash();
    gSaveQueue.clear();

    gCurRequest       = kPhaseIdle;
    gHasMore          = false;
    gNextCursor[0] = '\0';
    gRequestStartTick = 0;
    textActive        = nil;
    blockSelect       = 0;
    ClearUndo();
    gScrollOffset     = 0;
    gAppState.hasDirtyBlocks   = false;
    gAppState.lastSaveLabelIdx = -1;
    gPageIconName[0] = '\0';
    SafeStrCopy(gPageIconColor, sizeof(gPageIconColor), "gray");
    gAppState.iconDirty        = false;
    /* Reset icon names so LoadIconList re-reads from disk */
    if (gIconNames) { DisposePtr((Ptr)gIconNames); gIconNames = nil; }
    gIconCount = 0;
    FreeGraphicsAssets();
    gIconsDirID = 0;

    if (gScrollBar != nil) {
        SetControlMaximum(gScrollBar, 0);
        SetControlValue(gScrollBar, 0);
    }
}

/* ------------------------------------------------------------------ */
/* reloadPage — discard current content and re-fetch from Notion       */
/* ------------------------------------------------------------------ */
void reloadPage() {
    DEBUG_LOG("reloadPage", "start");

    /* If showCachedPreview already rendered the header, remember it so we can
       skip the MakeNewWindow redraw (which would flash the window white). */
    bool previewShown = (g_titleIsPlaceholder && pageElm && pageElmCount == 1);

    /* Capture pending nav header BEFORE unloadPage() clears g_hasPendingNavHeader. */
    PageHeader cached;
    bool haveCached = false;
    if (g_hasPendingNavHeader) {
        cached = g_pendingNavHeader;
        haveCached = cached.title[0] != '\0';
    }

    unloadPage();  /* clears g_hasPendingNavHeader, pageElmCount, TE handles, etc. */

    /* Disk cache: a previously fetched copy of this page renders instantly
       with no network round-trip. Reload menu sets gPageCacheBypassOnce to
       force a fresh fetch; misses fall through to the normal HTTP path. */
    if (TryLoadPageFromCache())
        return;

    /* Fall back to history/recent cache if no pending header. */
    if (!haveCached)
        haveCached = findCachedHeader(EffPageID(), cached) && cached.title[0];

    if (pageElm && gMaxBlocks > 0 && haveCached) {
        InstallPlaceholderTitle(cached);
        if (!previewShown) {
            /* Preview not on screen yet — draw it now */
            MakeNewWindow(documentProc);
        }
        /* If previewShown, the cached header is already rendered; skip the
           redraw so there is no white-flash while awaiting the HTTP response. */
    } else {
        MakeNewWindow(documentProc);
    }

    /* Connection status under the header during the fetch — same feedback
       as startup; falls back to full-window status when no header shown. */
    DrawStatusBelowHeader("Loading page\xC9");

    pageRequest();
}

/* ------------------------------------------------------------------ */
/* prepareForPageList — save, free cold data, compact heap            */
/* Must be called before any dialog that triggers FetchNotionPageList */
/* ------------------------------------------------------------------ */
void prepareForPageList()
{
    DEBUG_LOG("prepareForPL", "start");
    /* 1. Drain all threads — never touch shared state while suspended */
    while (gAppState.saveThreadActive || gAppState.apiPending || gAppState.togglePending)
        YieldToAnyThread();

    /* 2. Sync active TE text into b.text */
    SyncActiveBlock();

    /* 3. Auto-save before leaving the page */
    if (gAppState.hasDirtyBlocks || gAppState.iconDirty) {
        doSave();
        while (gAppState.isSaving || gAppState.saveThreadActive)
            YieldToAnyThread();
    }

    /* 4. Snapshot scroll + selection for Cancel restore */
    gPLCtx.savedScroll = gScrollOffset;
    gPLCtx.savedSelect = blockSelect;

    /* 5. Free cold-cache data (Tier 1) — all reconstructable on demand */
    FreeGraphicsAssets();
    /* 5d. Toggle child cache */
    ToggleCacheClear();   /* disposes the packed rows too */
    /* 5f. Undo snapshot — a fixed struct, just forget it. */
    ClearUndo();

    /* 6. Compact: purge + compact all unlocked relocatable handles.
          TEHandles are unlocked at rest so they compact freely here. */
    Size grow;
    MaxMem(&grow);

    /* 7. Tier 2: if still not enough contiguous space for the 256 KB
          fetch thread + 40 KB cushion, also free all TE handles.
          Cancel will be more expensive (updateBlocks() recreates them)
          but this guarantees the thread can always be allocated. */
    long totalFree, contigFree;
    PurgeSpace(&totalFree, &contigFree);
    { char pbuf[32]; snprintf(pbuf, sizeof(pbuf), "contig=%ldK", contigFree/1024);
      DEBUG_LOG("prepareForPL", pbuf); }
    if (contigFree < (256L * 1024L + 40L * 1024L)) {
        DEBUG_LOG("prepareForPL", "Tier2: free TEs");
        for (int _bi = 0; _bi < pageElmCount; _bi++)
            pageElm[_bi].invalidateTE();
        MaxMem(&grow);
    }
} /* end prepareForPageList */

/* ------------------------------------------------------------------ */
/* doPagePickerCancel — restore editing state after picker dismissed  */
/* ------------------------------------------------------------------ */
void doPagePickerCancel()
{
    /* If FetchNotionPageList's emergency path wiped pageElm, we
       can't restore — do a silent reload of the current page. */
    if ((pageElmCount == 0)) {
        gHttpClient.ForceReconnect();
        gCurRequest   = kPhaseIdle;
        gScrollOffset = 0;
        pageRequest();
        return;
    }

    /* Restore scroll and selection */
    gScrollOffset = gPLCtx.savedScroll;
    blockSelect   = gPLCtx.savedSelect;
    if (blockSelect < 0 || blockSelect >= pageElmCount)
        blockSelect = 0;

    syncTextActive();
    if (textActive) TEActivate(textActive);

    /* updateBlocks() calls addTE() for every visible block.
       If Tier 2 freed TEHandles, it recreates them; otherwise it only
       re-sets text that no longer matches the TE. */
    gFullEraseOnce = true;   /* clear the dialog's PaintBehind exposure */
    updateBlocks();
    ValidRect(&gWindow->portRect);  /* cancel the queued updateEvt — render once, not twice */

    /* Restore scroll bar to saved position, clamped to new max */
    if (gScrollBar) {
        short newMax = gTotalContentHeight - gWindow->portRect.bottom;
        if (newMax < 0) newMax = 0;
        if (gScrollOffset > newMax) gScrollOffset = newMax;
        SetControlMaximum(gScrollBar, newMax);
        SetControlValue(gScrollBar, gScrollOffset);
    }
}

/* Rebuild the HTTP Authorization header from the effective API key. */
void RefreshAuth() {
    char ba[256] = "Bearer ";
    strncat(ba, EffAPIKey(), sizeof(ba) - strlen(ba) - 1);
    gHttpClient.SetAuthorization(ba);
}
/* Shared picker-confirm tail: reauth, drop nav history, reload the new page. */
void AfterPickerConfirm() {
    SetListEmojiGWorld(nil); RefreshAuth();
    gHistBackCount = 0; gHistFwdCount = 0; gRecentCount = 0;
    scheduleReload();
}
/* Shared picker-cancel tail: reauth and dismiss without reloading. */
void AfterPickerCancel() {
    SetListEmojiGWorld(nil); RefreshAuth(); doPagePickerCancel();
}

/* ------------------------------------------------------------------ */
/* scheduleReload — trigger a page reload, flushing dirty blocks first */
/* Sets gAppState.pendingReload so the event loop waits for any save to finish  */
/* before calling reloadPage().                                        */
/* ------------------------------------------------------------------ */
void scheduleReload() {
    if (gAppState.hasDirtyBlocks && !gAppState.saving()) {
        DialogPtr dlg = GetNewDialog(132, NULL, (WindowPtr)-1);
        short hit = 1; /* default: Save */
        if (dlg != nil) {
            InitCursor();
            ShowWindow(dlg);
            SetPort(dlg);
            /* Outline the default Save button */
            { DialogItemType t; Handle h; Rect r;
              GetDialogItem(dlg, 1, &t, &h, &r);
              InsetRect(&r, -4, -4);
              PenSize(3, 3); FrameRoundRect(&r, 16, 16); PenNormal(); }
            EventRecord evt;
            short item = 0;
            while (item == 0) {
                WaitNextEvent(everyEvent, &evt, 6, nil);
                if (evt.what == updateEvt) {
                    WindowPtr wp = (WindowPtr)evt.message;
                    BeginUpdate(wp);
                    if (((WindowPeek)wp)->windowKind == dialogKind) {
                        DrawDialog((DialogPtr)wp);
                        if ((DialogPtr)wp == dlg) DrawDefaultButtonRing(dlg, 1);
                    }
                    EndUpdate(wp);
                    continue;
                }
                if (evt.what == osEvt || evt.what == activateEvt) continue;
                if (evt.what == mouseDown) {
                    WindowPtr clickWin;
                    if (FindWindow(evt.where, &clickWin) == inGoAway &&
                        (DialogPtr)clickWin == dlg &&
                        TrackGoAway(clickWin, evt.where)) { item = -1; break; } /* close box → Cancel */
                }
                if (evt.what == keyDown || evt.what == autoKey) {
                    char k = (char)(evt.message & charCodeMask);
                    if (k == 0x0D || k == 0x03) { item = 1;  break; } /* Return/Enter → Save */
                    if (k == 0x1B)               { item = -1; break; } /* Escape → Cancel */
                }
                short dlgItem = 0;
                if (DialogSelect(&evt, &dlg, &dlgItem) && dlgItem > 0) item = dlgItem;
            }
            DisposeDialog(dlg);
            hit = item;
        }
        if (hit == 1)  { doSave(); gAppState.pendingReload = true; } /* Save */
        else if (hit == 2) { gAppState.pendingReload = true; }      /* Don't Save */
        else { gPageCacheBypassOnce = false; } /* Cancel — drop any pending Reload cache bypass */
        return;
    }
    gAppState.pendingReload = true;
}
