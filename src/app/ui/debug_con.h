/* debug_con.h — ring-buffer debug console window.
   Included by main.cpp and Settings.cpp.  No app-specific state here. */
#pragma once
#include <Windows.h>
#include <Events.h>
#include "debug_log.h"   /* DebugLog — the core logging sink this window implements */

/* The console window pointer — non-nil when the console is open.
   Exposed so callers can test (win == gDebugWin) and call BeginUpdate/EndUpdate. */
extern WindowPtr gDebugWin;

/* Copy all buffered lines to the system scrap as plain text.
   Bound to Cmd+Shift+C in both the main event loop and settings dialog. */
void CopyDebugToClipboard();

/* Open / close the floating console window.  Toggled from the menu and
   at startup (to capture early log entries).                            */
void ToggleDebugWin();

/* Redraw the console window contents (called from update events).       */
void DrawDebugWin();

/* Flush deferred DebugLog redraws. Call from the event loop idle path. */
void FlushDebugLog();

/* Handle a mouseDown in the console window content area.
   Wraps FindControl / TrackControl and the scroll-top update. */
void HandleDebugClick(WindowPtr win, Point localPt);

/* Convenience macro — matches the existing DEBUG_LOG(fn, msg) call sites. */
#define DEBUG_LOG(fn, msg) DebugLog(fn, msg)

/* printf-style variant: formats into a stack buffer, then calls DebugLog. */
#define DEBUG_LOGF(fn, fmt, ...) \
    do { char _pbuf[80]; snprintf(_pbuf, sizeof(_pbuf), fmt, ##__VA_ARGS__); \
         DebugLog(fn, _pbuf); } while(0)
