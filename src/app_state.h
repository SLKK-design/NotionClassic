/* app_state.h — AppFlagBits, AppState, and SaveQueue shared across translation units. */
#pragma once
#include "blocks.h"

/* Page load phase for gCurRequest: idle → title received → fully loaded. */
enum PageRequestPhase { kPhaseIdle = 0, kPhaseTitleDone = 1, kPhaseFullyLoaded = 2 };

/* Refused-edit label shown where Save sits (AppState.flashKind) */
enum { kFlashTooLong = 0, kFlashReadOnly = 1, kFlashTooLarge = 2, kFlashNotSaved = 3 };

enum AppFlagBits : unsigned {
    kFlagSaving        = 1 << 0,
    kFlagSaveThread    = 1 << 1,
    kFlagApiPending    = 1 << 2,
    kFlagTogglePending = 1 << 3,
    kFlagBusy = kFlagSaving | kFlagSaveThread | kFlagApiPending | kFlagTogglePending
};

struct AppState {
    bool  isSaving;
    bool  saveThreadActive;
    bool  hasDirtyBlocks;
    bool  readOnly;
    bool  apiPending;
    bool  togglePending;
    long  lastEditTick;
    long  savedUntil;
    long  saveStartTick;       /* when the running save began (for "No network") */
    long  flashUntil;          /* show a refused-edit/save label until this tick */
    uint8_t flashKind;         /* kFlash*: which label that is */
    long  lastSaveButtonTick;  /* throttle for updateSaveButton */
    short lastSaveLabelIdx;
    short lastBarFilled;
    bool  needsReload;
    bool  reloadIsNetworkError; /* true = TCP failure, false = HTTP/auth error */
    bool  pageNotFound;         /* the reload is for a 404: the page is gone */
    bool  timedOut;
    bool  navPending;
    bool  newPageReady;
    bool  pendingReload;
    bool  iconDirty;
    unsigned flags() const {
        unsigned f = 0;
        if (isSaving)         f |= kFlagSaving;
        if (saveThreadActive) f |= kFlagSaveThread;
        if (apiPending)       f |= kFlagApiPending;
        if (togglePending)    f |= kFlagTogglePending;
        return f;
    }
    bool busy()   const { return isSaving || saveThreadActive || apiPending || togglePending; }
    bool saving() const { return isSaving || saveThreadActive; }
};

/* Fixed-size ring buffer for block save queue. One entry per block, worst-case all dirty. */
struct SaveQueue {
    int  buf[kMaxBlocks];
    int  head, tail, count;
    SaveQueue() : head(0), tail(0), count(0) {}
    bool empty()     const { return count == 0; }
    int  front()     const { return buf[head]; }
    void pop_front()       { head = (head + 1) % kMaxBlocks; --count; }
    void push_back(int v)  {
        if (count < kMaxBlocks) { buf[tail] = v; tail = (tail + 1) % kMaxBlocks; ++count; }
    }
    void clear()           { head = tail = count = 0; }
};
