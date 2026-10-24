/* page_cache.h — page block content disk cache.
   Caches the last kPageCacheSlots fully-loaded pages' blocks in a
   dedicated file in the Preferences folder, so revisiting a page (or
   relaunching the app) renders instantly from disk instead of waiting
   ~10 s for the HTTPS fetch.  The Reload menu item always bypasses the
   cache and fetches fresh from Notion.                                 */
#pragma once

/* If a valid cache slot exists for EffPageID(): populate pageElm, set
   gCurRequest = kPhaseFullyLoaded, render the page, and return true.
   Call after unloadPage() where pageRequest() would otherwise start a
   network load. Returns false on miss/bypass — caller fetches normally. */
bool TryLoadPageFromCache();

/* Serialize the current fully-loaded page into its cache slot.
   No-op unless gCurRequest == kPhaseFullyLoaded and the page fits in one slot (32 KB). */
void SavePageCacheNow();

/* Set by the Reload menu item: the next TryLoadPageFromCache() returns
   false (one-shot), forcing a fresh network fetch.                     */
extern bool gPageCacheBypassOnce;

/* Set by the save pipeline after a successful PATCH flush; the main
   event loop calls SavePageCacheNow() at idle and clears it, keeping
   the cache in sync with what was just written to Notion.              */
extern bool gPageCacheNeedsSave;
