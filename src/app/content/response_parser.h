/* response_parser.h — Notion API GET response parsing. */
#pragma once

#include "HttpResponse.h"
#include "blocks.h"
#include "gason.hpp"

/* Main HTTP response handler — parses page metadata (kPhaseIdle) or
   block list (kPhaseTitleDone) JSON and populates the global pageElm array. */
void OnGetResponse(HttpResponse& response);

/* Receives each full piece of a split block; false = no room (pieces dropped). */
typedef bool (*PieceSink)(Block& piece, void* ctx);

/* Fill c (type, id etc. already set) from a block's rich_text array.  When
   the text, run or emoji budget runs out and splittable is true, the full
   piece goes to emit and c carries on as the next isCont piece — split
   after the last space before 2,047 - 125 (room for small edits; a block
   that fits in 2,047 stays one piece).  Returns false when content
   had to be dropped (not splittable, too many pieces, emit refused); c is
   then marked cutShort and the caller should mark the whole group.     */
bool ParseRichTextPieces(Block& c, gason::JsonValue richText, bool splittable,
                         PieceSink emit, void* ctx);

/* Shared by page loads and toggle-children loads (one copy, so a new
   Block field is read in one place):
   ParseBlockMeta  — type, checked, toggleable heading, id, has_children
                     (before the text: split pieces copy it);
   ParseBlockText  — the rich text as pieces through emit, cut-short marks;
   FinishLoadedBlock — after appending the last piece (added=false if it
                     didn't fit): a block missing any part makes every
                     piece read-only.  lastIdx = last row appended.      */
void ParseBlockMeta(Block& c, gason::JsonValue blockNode, const char* listtype);
void ParseBlockText(Block& c, gason::JsonValue blockNode, const char* listtype,
                    PieceSink emit, void* ctx);
void FinishLoadedBlock(Block& c, bool added, int lastIdx);
