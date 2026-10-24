/* response_parser.cpp — Notion API GET response parsing.
   Extracted from main.cpp. */
#include <string.h>
#include <stdint.h>

#include "response_parser.h"
#include "blocks.h"
#include "app_state.h"
#include "utf8_mac_lite.h"
#include "run_styles.h"   /* parseAnnotations, parseRunLink */
#include "helpers.h"
#include "emoji_sprite.h" /* emoji_find_fallback, EmojiSprite — for child_page icons */
#include "Settings.h"
#include "HttpClient.h"
#include "debug_con.h"
#include "page_cache.h"   /* SavePageCacheNow */
#include "block_ops.h"    /* MarkGroupCutShort */

/* ------------------------------------------------------------------ */
/* Externs — globals defined in main.cpp                               */
/* ------------------------------------------------------------------ */
extern Block*     pageElm;
extern int        pageElmCount;
extern TEHandle   textActive;
extern int        gMaxBlocks;
extern AppState   gAppState;
extern bool       g_titleIsPlaceholder;
extern char       gPageIconName[32];
extern char       gPageIconColor[32];
extern bool       gHasMore;
extern char       gNextCursor[256];
extern int        gCurRequest;
extern long       gRequestStartTick;
extern WindowPtr  gWindow;
extern HttpClient gHttpClient;

extern const char* EffPageID();
extern void DrawStatusText(const char* msg);
extern void DrawStatusBelowHeader(const char* msg);
extern void MakeNewWindow(short procID);
extern void EnsureControls();
extern void pageRequest();
extern void updateBlocks();
extern void addToRecent(const PageHeader& h);
extern void buildCurrentHeader(PageHeader& h);
extern void SyncCurrentPageInHistory();
extern bool gScrolling;
extern bool gSkipHeaderErase;

/* ------------------------------------------------------------------ */
/* appendToBlock — decode utf8 and append into b.text/b.emojiSeqs.   */
/* Returns false when not all of it fit (text full, or past kMaxEmoji  */
/* emoji / non-Mac-Roman chars): the caller must mark b cut short.     */
/* ------------------------------------------------------------------ */
static bool appendToBlock(Block& b, const char* utf8)
{
    if (!utf8) utf8 = "";
    size_t len = strlen(utf8), used = 0;
    uint8_t newEmoji = 0;
    short rlen = utf8_to_macroman_step(utf8, len,
        b.text + b.textLen, (short)(kMaxTextLen - 1 - b.textLen),
        b.emojiSeqs + b.emojiCount, &newEmoji, (uint8_t)(kMaxEmoji - b.emojiCount), &used);
    b.textLen    += rlen;
    b.emojiCount += newEmoji;
    return used >= len;
}

/* ------------------------------------------------------------------ */
/* ParseRichTextPieces / SplitPiece                                    */
/* ------------------------------------------------------------------ */
static const int kMaxPieces     = 24;   /* ~46K chars per block; beyond: cutShort */
static const int kSplitLookback = 400;  /* how far back to look for a space */
/* A block that has to be split leaves this much room in every piece, so
   small edits fit without hitting "Too long" */
static const int kPieceHeadroom = 125;

/* Tail that moves to the next piece — static: keeps it off the 68k stack */
static char    sTailText[kMaxTextLen];
static TextRun sTailRuns[kMaxRuns];
static char    sTailEmoji[kMaxEmoji][32];

/* Characters the rich_text items will take (a UTF-8 code point each; an
   emoji sequence counts its parts, so this can only overestimate). */
static long RichTextChars(gason::JsonValue richText)
{
    long n = 0; bool ok;
    for (gason::JsonIterator it = gason::begin(richText); it.isValid(); ++it) {
        const char* pt = it->value("plain_text").toString(&ok);
        if (!ok || !pt) continue;
        for (; *pt; pt++)
            if (((unsigned char)*pt & 0xC0) != 0x80) n++;
    }
    return n;
}

/* Emit piece c and turn c into the next (isCont) piece.  atSpace: cut after
   the last space near the end and move the rest on; else cut at the end.
   *itemRun (the current item's run in c, or -1) follows it into the tail. */
static bool SplitPiece(Block& c, bool atSpace, int* itemRun, PieceSink emit, void* ctx)
{
    int cut = c.textLen;
    if (atSpace) {
        int lo = c.textLen - kSplitLookback; if (lo < 1) lo = 1;
        for (int i = c.textLen - 1; i >= lo; i--)
            if (c.text[i] == ' ') { cut = i + 1; break; }
    }
    /* Never through a page mention (it would become two): cut before it,
       or after it if it starts the piece */
    for (int ri = 0; ri < c.runCount; ri++) {
        const TextRun& r = c.runs[ri];
        int rs = r.textOffset, re = rs + r.textLen;
        if (rs < cut && cut < re && IsMentionRun(r))
            cut = (rs > 0) ? rs : re;
    }
    int tailLen = c.textLen - cut;
    memcpy(sTailText, c.text + cut, tailLen);
    int keepEmoji = CountSentinels(c.text, 0, cut);
    if (keepEmoji > c.emojiCount) keepEmoji = c.emojiCount;
    int tailEmoji = c.emojiCount - keepEmoji;
    memcpy(sTailEmoji, c.emojiSeqs + keepEmoji, 32 * tailEmoji);

    /* Start and length are read straight from the array, before any copy:
       with -Os, m68k GCC miscompiled "TextRun r = c.runs[ri]; re = rs +
       r.textLen" — it took r.textLen from d0 after the struct-copy memcpy
       (its return value), so a run straddling the cut was kept whole and
       its tail lost its run: every later style landed tailLen chars early. */
    int keepRuns = 0, tailRuns = 0, newItemRun = -1;
    for (int ri = 0; ri < c.runCount; ri++) {
        int rs = c.runs[ri].textOffset;
        int re = rs + c.runs[ri].textLen;
        if (re <= cut) {                /* wholly before the cut: stays */
            if (keepRuns != ri) c.runs[keepRuns] = c.runs[ri];
            keepRuns++;
            continue;
        }
        TextRun& t = sTailRuns[tailRuns];
        t = c.runs[ri];
        t.textOffset = (short)(rs > cut ? rs - cut : 0);
        if (rs < cut) {                 /* straddles the cut: split it */
            t.textLen = (short)(re - cut);
            t.textUrl = 0;
            if (keepRuns != ri) c.runs[keepRuns] = c.runs[ri];
            c.runs[keepRuns].textLen = (short)(cut - rs);
            c.runs[keepRuns].textUrl = 0;
            keepRuns++;
        }
        if (ri == *itemRun) newItemRun = tailRuns;
        tailRuns++;
    }
    c.runCount = (uint8_t)keepRuns;
    c.textLen  = (short)cut; c.text[cut] = '\0';
    c.emojiCount = (uint8_t)keepEmoji;
    if (!emit(c, ctx)) return false;

    /* Same Notion block, fresh content (cutShort stays sticky for the caller) */
    c.isCont = true;
    c.serial = NextBlockSerial();   /* a row of its own */
    memcpy(c.text, sTailText, tailLen); c.textLen = (short)tailLen; c.text[tailLen] = '\0';
    memcpy(c.runs, sTailRuns, sizeof(TextRun) * tailRuns); c.runCount = (uint8_t)tailRuns;
    memcpy(c.emojiSeqs, sTailEmoji, 32 * tailEmoji); c.emojiCount = (uint8_t)tailEmoji;
    *itemRun = newItemRun;
    return true;
}

bool ParseRichTextPieces(Block& c, gason::JsonValue richText, bool splittable,
                         PieceSink emit, void* ctx)
{
    int  pieces = 1;
    bool ok;
    /* Fill each piece up to here (a block that fits stays one piece) */
    int  fill = (splittable && RichTextChars(richText) > kMaxTextLen - 1)
              ? kMaxTextLen - 1 - kPieceHeadroom : kMaxTextLen - 1;
    for (gason::JsonIterator it = gason::begin(richText); it.isValid(); ++it) {
        gason::JsonValue item = it->value;
        if (RichTextItemLossy(item)) MarkCut(c, kCutMention);
        const char* pt = item("plain_text").toString(&ok);
        if (!ok || !pt) pt = "";
        size_t left = strlen(pt);

        /* Style + link shared by every piece of this item.  textUrl spotting
           compares the UTF-8 text, which equals Mac Roman for URL chars. */
        TextRun tmpl;
        parseAnnotations(tmpl, item("annotations"));
        tmpl.textLen = (short)(left < 32767 ? left : 32767);
        parseRunLink(tmpl, item, pt);
        bool mention = IsMentionRun(tmpl);

        int itemRun = -1;
        do {
            /* A new run needs a slot; a page mention must not be split */
            bool noRun   = itemRun < 0 && c.runCount >= kMaxRuns;
            bool noRoom  = mention && c.textLen > 0 && (long)left > (long)(fill - c.textLen);
            if (noRun || noRoom) {
                if (!splittable || pieces >= kMaxPieces) { MarkCut(c, kCutTooLong); return false; }
                if (!SplitPiece(c, false, &itemRun, emit, ctx)) { MarkCut(c, kCutTooLong); return false; }
                pieces++;
            }
            uint8_t newEmoji = 0; size_t used = 0;
            short w = utf8_to_macroman_step(pt, left,
                          c.text + c.textLen, (short)(fill - c.textLen),
                          c.emojiSeqs + c.emojiCount, &newEmoji,
                          (uint8_t)(kMaxEmoji - c.emojiCount), &used);
            if (itemRun >= 0) {
                c.runs[itemRun].textLen = (short)(c.runs[itemRun].textLen + w);
            } else {
                TextRun r = tmpl;
                r.textOffset = c.textLen; r.textLen = w;
                c.runs[c.runCount] = r;
                itemRun = c.runCount++;
            }
            c.textLen = (short)(c.textLen + w); c.emojiCount = (uint8_t)(c.emojiCount + newEmoji);
            pt += used; left -= used;
            if (left > 0) {                     /* piece full (text or emoji) */
                if (!splittable || pieces >= kMaxPieces || (used == 0 && c.textLen == 0)) {
                    MarkCut(c, kCutTooLong); return false;
                }
                if (!SplitPiece(c, true, &itemRun, emit, ctx)) { MarkCut(c, kCutTooLong); return false; }
                pieces++;
            }
        } while (left > 0);
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* ParseBlockMeta / ParseBlockText / FinishLoadedBlock                 */
/* ------------------------------------------------------------------ */
void ParseBlockMeta(Block& c, gason::JsonValue blockNode, const char* listtype)
{
    gason::JsonValue typeNode = blockNode.child(listtype);
    c.type = blockTypeFromStr(listtype);
    if (blockDesc(c.type).hasChecked)
        c.check = (typeNode.child("checked").getTag() == gason::JSON_TRUE);
    promoteToggleHeading(c, typeNode.child("is_toggleable").getTag() == gason::JSON_TRUE);
    bool ok = false;
    const char* id = blockNode.child("id").toString(&ok);
    setId(c.id, (ok && id) ? id : "");
    c.hasChildren = (blockNode.child("has_children").getTag() == gason::JSON_TRUE);
}

void ParseBlockText(Block& c, gason::JsonValue blockNode, const char* listtype,
                    PieceSink emit, void* ctx)
{
    /* Toggles aren't split: their children load right after them.
       ParseRichTextPieces marks c cut short itself when it drops text. */
    ParseRichTextPieces(c, blockNode.child(listtype).child("rich_text"),
                        !isToggleType(c.type), emit, ctx);
    c.text[c.textLen] = '\0';
    if (BlockLoadedPartially(c)) MarkCut(c, kCutTooLong);
}

void FinishLoadedBlock(Block& c, bool added, int lastIdx)
{
    if (!added) MarkCut(c, kCutTooLong);   /* no room: its last piece is missing */
    if (c.cutShort && lastIdx >= 0 && lastIdx < pageElmCount &&
        strcmp(pageElm[lastIdx].id, c.id) == 0)
        MarkGroupCutShort(lastIdx, c.cutShort);
}

/* PieceSink for page loads: append to pageElm, numbering as it goes */
static bool AppendPiece(Block& piece, void* ctx)
{
    int* pos = (int*)ctx;
    if (pageElmCount >= gMaxBlocks) return false;
    piece.text[piece.textLen] = '\0';
    piece.pos = ++(*pos);
    pageElm[pageElmCount++] = piece;
    return true;
}

/* ------------------------------------------------------------------ */
/* OnGetResponse                                                       */
/* ------------------------------------------------------------------ */
static bool sCacheMatchedHTTP = false;  /* set when cached preview matches HTTP title+icon */

void OnGetResponse(HttpResponse& response) {
    { char pbuf[64]; snprintf(pbuf, sizeof(pbuf), "HTTP %d ok=%d %s",
          response.StatusCode, (int)response.Success,
          response.Success ? "" : response.ErrorMsg.substr(0,30).c_str());
      DEBUG_LOG("OnGetResponse", pbuf); }

    /* Snapshot the displayed placeholder state before request-0 parsing
       overwrites pageElm[0], so we can decide whether a MakeNewWindow
       redraw is actually needed once the HTTP data arrives. */
    bool hadPlaceholder = g_titleIsPlaceholder;
    static char prevTitle[kMaxTextLen]; prevTitle[0] = '\0';
    char prevIcon[32];           prevIcon[0]  = '\0';
    if (hadPlaceholder && pageElmCount > 0)
        SafeStrCopy(prevTitle, sizeof(prevTitle), pageElm[0].text);
    SafeStrCopy(prevIcon, sizeof(prevIcon), gPageIconName);

    /* A non-200 status means the request reached Notion but was rejected.
       Check this before touching the JSON body — the error body has a
       completely different shape and would crash the parsers below. */
    if (response.Success && response.StatusCode != 200) {
        if (response.StatusCode == 400) {
            DrawStatusText("Page ID invalid (400). Check your settings.");
        } else if (response.StatusCode == 401 || response.StatusCode == 403) {
            DrawStatusText("Not authorized (401). Check your integration token.");
        } else if (response.StatusCode == 404) {
            /* Deleted, or not shared with the integration — not a token problem */
            DrawStatusText("Page not found (404).");
            gAppState.pageNotFound = true;
        } else if (response.StatusCode == 429) {
            DrawStatusText("Rate limited (429). Wait a moment and try again.");
        } else {
            { char errbuf[160];
              snprintf(errbuf, sizeof(errbuf), "Notion error %d: %.100s",
                       response.StatusCode, response.Content.c_str());
              DrawStatusText(errbuf); }
        }
        gAppState.reloadIsNetworkError = false;
        gAppState.needsReload = true;
        return;
    }

    if (response.Success) {
        if (response.Content.empty()) {
            /* Empty body would make gason scan/write past the std::string
               terminator — treat as a (likely transient) network error. */
            DrawStatusText("Empty response body");
            gAppState.reloadIsNetworkError = true;
            gAppState.needsReload = true;
            return;
        }
        int pos = 0;
        gason::JsonAllocator allocator;
        gason::JsonValue root;
        gason::JsonParseStatus status = gason::jsonParse(&response.Content[0], root, allocator);
        { char pbuf[32]; snprintf(pbuf, sizeof(pbuf), "json %lu bytes", (unsigned long)response.Content.size());
          DEBUG_LOG("jsonParse", pbuf); }
        if (status == gason::JSON_PARSE_OK) {
            bool _ok; /* scratch bool — used with every toString(&_ok) call so the
                         assert in gason.hpp checkType() is bypassed safely       */
            if (gCurRequest == kPhaseIdle) {
                Block c; c.pos = pos; c.type = BT_TITLE;
                {
                    /* Every title item, not just the first — the title PATCH
                       writes the whole text back. */
                    for (gason::JsonIterator tit = gason::begin(root("properties").child("title").child("title"));
                         tit.isValid(); ++tit) {
                        gason::JsonValue item = tit->value;
                        if (RichTextItemLossy(item)) MarkCut(c, kCutMention);
                        /* A title that doesn't fit (e.g. over 12 emoji) is
                           read-only: the title PATCH writes the whole text */
                        if (!appendToBlock(c, item("plain_text").toString(&_ok)))
                            MarkCut(c, kCutTooLong);
                    }
                    c.text[c.textLen] = '\0';
                    if (c.textLen >= kMaxTextLen - 1 || BlockLoadedPartially(c)) MarkCut(c, kCutTooLong);
                }
                setId(c.id, EffPageID());
                /* Invariant: g_titleIsPlaceholder is true IFF pageElm[0] came from nav
                   history and has not yet been overwritten by an HTTP response.
                   Set true in showCachedPreview(); cleared here on the first gCurRequest==0
                   parse. Navigation clears pageElm before setting it again, so this flag
                   is never stale across page changes. */
                if (g_titleIsPlaceholder) {
                    /* Overwrite the cached-title placeholder with fresh data.
                       The startup/reload preview path runs updateBlocks(),
                       which gives the placeholder a live TEHandle — dispose
                       it first or the struct copy below orphans it (c.textEdit
                       is nil), leaking a relocatable handle every launch. */
                    if (pageElm[0].textEdit) {
                        if (textActive == pageElm[0].textEdit) textActive = nil;
                        TEDispose(pageElm[0].textEdit);
                        pageElm[0].textEdit = nil;
                    }
                    pageElm[0] = c;
                    g_titleIsPlaceholder = false;
                } else if (pageElmCount < gMaxBlocks) {
                    pageElm[pageElmCount++] = c;
                }

                /* Parse native Notion icon: type=="icon", icon.name gives the slug */
                gPageIconName[0] = '\0';
                {
                    gason::JsonValue iconNode = root("icon");
                    const char* iconType = iconNode("type").toString(&_ok);
                    if (iconType && strcmp(iconType, "icon") == 0) {
                        const char* iconName  = iconNode("icon")("name").toString(&_ok);
                        const char* iconColor = iconNode("icon")("color").toString(&_ok);
                        if (iconName)  SafeStrCopy(gPageIconName,  sizeof(gPageIconName),  iconName);
                        if (iconColor) SafeStrCopy(gPageIconColor, sizeof(gPageIconColor), iconColor);
                    }
                }
            } else {
                /* Offset pos so appended pages continue numbering after existing blocks */
                pos = pageElmCount - 1;  /* title is at 0, blocks start at 1 */
                gason::JsonIterator it = gason::begin(root("results"));
                while (it.isValid()) {
                    gason::JsonValue blockNode = it->value;
                    const char *listtype = blockNode.child("type").toString(&_ok);
                    if (!listtype) { it++; continue; }
                    Block c;
                    /* Metadata first: pieces of a split block copy it */
                    ParseBlockMeta(c, blockNode, listtype);
                    if (!blockDesc(c.type).useTitle)
                        ParseBlockText(c, blockNode, listtype, AppendPiece, &pos);
                    /* Include all blocks with a recognised type — empty paragraphs
                       (rich_text:[]) are valid Notion spacing and must be shown. */
                    {
                        if (blockDesc(c.type).useTitle) {
                            /* child_page has no rich_text; title lives in child_page.title */
                            const char* pt = blockNode.child("child_page").child("title").toString(&_ok);
                            /* child_page titles don't have emoji in practice; skip extraction */
                            short rlen = utf8_to_macroman_into(pt ? pt : "New Page",
                                c.text, (short)(kMaxTextLen - 1), nullptr, nullptr, 0);
                            c.text[rlen] = '\0'; c.textLen = rlen;
                            c.runCount = 0; /* child_page: no runs, text is the title only */
                            /* child_page icon: parse top-level "icon" object */
                            gason::JsonValue iconNode = blockNode.child("icon");
                            bool _iok = false;
                            const char* itype = iconNode("type").toString(&_iok);
                            if (_iok && itype) {
                                if (strcmp(itype, "icon") == 0) {
                                    const char* slug = iconNode("icon")("name").toString(&_iok);
                                    if (_iok && slug && slug[0])
                                        SafeStrCopy(c.iconName, (int)sizeof(c.iconName), slug);
                                } else if (strcmp(itype, "emoji") == 0) {
                                    const char* emojiStr = iconNode("emoji").toString(&_iok);
                                    if (_iok && emojiStr && emojiStr[0]) {
                                        const EmojiSprite* sp = emoji_find_fallback(emojiStr);
                                        if (sp) { c.iconSprX = sp->x; c.iconSprY = sp->y; }
                                    }
                                }
                            }
                        }
                        c.pos = ++pos;
                        bool added = pageElmCount < gMaxBlocks;
                        if (added) pageElm[pageElmCount++] = c;
                        FinishLoadedBlock(c, added, pageElmCount - 1);
                    }
                    it++;
                }
                /* Check for more pages */
                gHasMore = (root("has_more").getTag() == gason::JSON_TRUE);
                if (gHasMore) {
                    const char* cur = root("next_cursor").toString(&_ok);
                    SafeStrCopy(gNextCursor, sizeof(gNextCursor), cur ? cur : "");
                    if (gNextCursor[0] == '\0') gHasMore = false;
                } else {
                    gNextCursor[0] = '\0';
                }
            }
        } else {
            { char errbuf[200];
              snprintf(errbuf, sizeof(errbuf), "JSON parse error req=%d sc=%d body=%.120s",
                       gCurRequest, response.StatusCode, response.Content.c_str());
              DrawStatusText(errbuf); }
            while (!Button()); while (Button());
        }
    } else {
        { char errbuf[160];
          snprintf(errbuf, sizeof(errbuf), "HTTP error req=%d: %.100s",
                   gCurRequest, response.ErrorMsg.c_str());
          DrawStatusText(errbuf); }
        gAppState.reloadIsNetworkError = true;
        gAppState.needsReload = true;
        return;
    }

    /* JSON parse is done and all string data has been copied into pageElm.
       Free the raw response body now — before updateBlocks() — so the 54 KB
       Content buffer and the gason zones are never live at the same time as
       the TextEdit handles created during rendering. */
    gHttpClient.ReleaseContent();

    gRequestStartTick = 0;  /* request completed — cancel timeout */
    gCurRequest++;
    if (gCurRequest == kPhaseTitleDone) {
        /* If the cached preview was on screen and HTTP title+icon match,
           skip the intermediate MakeNewWindow redraw entirely — the screen
           already shows the right header.  The single updateBlocks() at
           req 1→2 will render the blocks without any intermediate flash. */
        bool titleSame = hadPlaceholder &&
                         (pageElmCount > 0) &&
                         strcmp(prevTitle, pageElm[0].text) == 0;
        bool iconSame  = strcmp(prevIcon, gPageIconName) == 0;
        sCacheMatchedHTTP = (titleSame && iconSame);
        if (!sCacheMatchedHTTP) {
            /* Header changed or no preview shown — redraw now.
               MakeNewWindow must do a full erase (headerStable=false) so any
               stale pixels from an updateEvt that fired with the old icon
               position are cleared.  Set gSkipHeaderErase only AFTER the
               MakeNewWindow draw so the subsequent request-2 updateBlocks can
               skip re-erasing the now-correct header. */
            MakeNewWindow(documentProc);
            if (hadPlaceholder) gSkipHeaderErase = true;
        } else {
            /* Preview matches — skip MakeNewWindow to avoid flash, but ensure
               scroll bar + save button controls exist (they are only created
               inside MakeNewWindow, which we're skipping).
               Erase the "Loading page…" status line — on a new empty page,
               updateBlocks() draws no content blocks so nothing overwrites it. */
            EnsureControls();
        }
        pageRequest();
    } else if (gCurRequest == kPhaseFullyLoaded) {
        if (gHasMore) {
            gCurRequest = kPhaseTitleDone;
            gSkipHeaderErase = true;
            updateBlocks();
            ValidRect(&gWindow->portRect);
            pageRequest();
            return;
        }
        if ((pageElmCount == 0)) {
            DrawStatusText("No page content (empty pageElm)");
            while (!Button()); while (Button());
            return;
        }
        /* Final batch received — render complete page.
           If the cached preview already showed correct header (titleSame && iconSame
           in the kPhaseTitleDone branch), skip the erase to avoid a content flash
           when the HTTP data matches the cache. */
        { char pbuf[32]; snprintf(pbuf, sizeof(pbuf), "done %d blocks", pageElmCount);
          DEBUG_LOG("pageRequest", pbuf); }
        gSkipHeaderErase = true;
        updateBlocks();
        ValidRect(&gWindow->portRect);
        { PageHeader rh; buildCurrentHeader(rh); addToRecent(rh); }
        gPageCacheNeedsSave = true;  /* defer SavePageCacheNow to idle */
        sCacheMatchedHTTP = false;   /* reset for next load */
    } // close else if (gCurRequest == kPhaseFullyLoaded)
} // close OnGetResponse
