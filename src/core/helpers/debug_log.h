/* debug_log.h — core logging sink.
   Declares the DebugLog entry point that core code (net, helpers) emits to.
   The implementation and the console-window UI live in app/ui/debug_con.cpp;
   core depends only on this declaration, not on the app layer. */
#pragma once

/* Log one line: "func msg" (func should be ≤11 chars; msg is truncated to the
   console line width).  Implemented by app/ui/debug_con.cpp. */
void DebugLog(const char* func, const char* msg);
