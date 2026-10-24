#pragma once
#include "Settings.h"
#include "library_window.h"   /* LibPageEntry */

void pageRequest();
bool DoFetchPages(const char* apiKey, NotionPageList& list);
int  GetLastFetchStatus(); /* HTTP status of last DoFetchPages call (0 = network error) */
void stripHyphens(const char* src, char* out, int outLen);
bool DoFetchPageChildren(const char* apiKey, const char* parentIdNH,
                         LibPageEntry out[], int maxOut, int& outCount);
bool DoLibMovePage(const char* apiKey, const char* srcIdNH, const char* newParentIdNH);
bool DoLibDeletePage(const char* apiKey, const char* pageIdNH);
void ForgetDeletedPages(const char* apiKey, const char (*idsNH)[33], int n);
void InvalidatePageListCache();
void UpdatePageListCacheEntry(const char* noHyphenId, const char* newTitle, const char* newIconName,
                              const short* titleEmoji = nullptr);
