/* page_lifecycle.h — Page load, unload, snapshot, and auth helpers.
   Extracted from main.cpp.                                           */
#pragma once

void DisposeAllTEHandles();
void unloadPage();
void scheduleReload();
void reloadPage();
void prepareForPageList();
void doPagePickerCancel();
void RefreshAuth();
void AfterPickerConfirm();
void AfterPickerCancel();
