/* save_pipeline.h — Block save/patch pipeline and API thread helpers. */
#pragma once

#include <string>
#include <functional>
#include "HttpResponse.h"

/* Save all dirty blocks by queuing them through patchNext callbacks. */
void doSave();

/* Forget the old copies of re-created blocks still waiting for the
   trash (page unload): they stay in Notion, a duplicate at worst. */
void ClearDeferredTrash();

/* Queue-based save continuation — start next queued save request. */
void ContinueQueue();

/* Fire a single PATCH/PatchWithBody in its own cooperative thread.
   keepBody=true: use PatchWithBody (preserves response body for ID parsing). */
void runApiThread(const std::string& url, const std::string& body,
                  std::function<void(HttpResponse&)> cb,
                  bool keepBody = false);

/* HTTP callbacks for loads and API threads started outside the save chain. */
void afterToggleLoad(HttpResponse& resp);
void afterAddInsert(HttpResponse& resp);
void afterDeleteArchive(HttpResponse& resp);
