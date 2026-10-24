/* content_events.h — Content-area and key event handlers extracted from main.cpp. */
#pragma once
#include <Events.h>
#include <Windows.h>

/* Handle a mouseDown in the content area of gWindow or gDebugWin. */
void doInContent(EventRecord* event, WindowPtr win);

/* Handle a keyDown / autoKey event from the main event loop. */
void HandleKeyDown(EventRecord& e);
