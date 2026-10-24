/* run_styles.h — Text run annotation parsing and style application.   */
#pragma once

#include <TextEdit.h>
#include "blocks.h"
#include "gason.hpp"

/* Parse a Notion "annotations" JSON object into a TextRun's style flags. */
void parseAnnotations(TextRun& run, gason::JsonValue ann);

/* Read a rich_text item's link into run.url: text.link.url for text,
   "http://notioncl/page/<id>" for page mentions, href otherwise.
   text = the run's chars in the block (run.textLen long), to spot links
   whose text is the URL itself.                                       */
void parseRunLink(TextRun& run, gason::JsonValue item, const char* text);

/* rich_text item we show but can't write back as it was (a user/date/
   database mention, an equation, …) — page mentions and text are fine. */
bool RichTextItemLossy(gason::JsonValue item);

/* After loading c: true when part of it was lost (an emoji without its
   sequence, one too long to keep, a link longer than url[]).  Text and
   run overflow are handled by ParseRichTextPieces.                    */
bool BlockLoadedPartially(const Block& c);

/* Notion annotation colour name for TextRun.color. */
const char* RunColorName(uint8_t color);

/* Keep blk.runs in step with a typing edit: blk.text still holds the old
   text, newText the new, teCaret the TE caret after the edit.  Inserted text joins the run before it, except
   a space typed at a link's edge, or anything typed at a page mention:
   that starts a new run outside the link.  Returns that run's char
   range in *outStart/*outLen (len 0 = none) when the caller should drop
   the link underline TE carried over.                                       */
void AdjustRunsForEdit(Block& blk, const char* newText, short newLen, short teCaret,
                       short* outStart, short* outLen);

/* A page mention is one unit (Notion shows the page's title, never our
   text), and so is an emoji (SnapSelectionToEmoji).  Before an edit with `key` (0 = paste/insert/cut): a caret inside
   one moves to its end, a selection edge inside one widens to cover it,
   and Backspace after / Delete before (or inside) one selects it whole —
   so no edit ever splits or shortens a mention. */
void SnapSelectionForEdit(const Block& blk, TEHandle te, char key);

/* Keep b's emoji sequences in step with an edit that turns b.text into
   newText (stored text; teCaret = TE caret after the edit): emoji before
   and after the edit keep theirs, the ones it inserted take ins[] in
   order.  Matching by count instead gave every emoji after a deleted one
   its neighbour's sequence.  Call before b.text changes. */
void SyncEmojiSeqs(Block& b, const char* newText, short newLen, short teCaret,
                   const char (*ins)[32] = nullptr, int insCount = 0);

/* Copy te's text into b (emoji pads stripped, like SyncBlockText) and keep
   b.runs and b's emoji in step (AdjustRunsForEdit, SyncEmojiSeqs), as
   typing does — for cut, paste, the markdown prefix and the emoji picker.
   ins[]: the sequences of the emoji the edit inserted (paste, picker).
   A sync that left the runs alone got them re-mapped by position at
   save, which could split a mention or move styles onto the wrong text. */
void SyncBlockTextKeepRuns(Block& b, TEHandle te,
                           const char (*ins)[32] = nullptr, int insCount = 0);

/* Cut b's runs at text index pos (Return): b keeps the runs of [0, pos),
   tail[] / *tailCount get those of the rest, offsets from 0.  A run cut
   in two keeps its style and link in both halves. */
void SplitRunsAt(Block& b, int pos, TextRun* tail, uint8_t* tailCount);

/* Remove the TE underline a link lends to text typed right after it. */
void ClearLinkUnderline(const Block& blk, short start, short len);

/* True when two runs agree on the attributes TE can't hold (url, code, colour). */
bool SameRunExtras(const TextRun& a, const TextRun& b);

/* Apply Block.runs bold/italic/underline formatting to a TE handle.
   Returns false if no styled runs exist (skip TECalText).               */
bool ApplyRunStyles(TEHandle te, const Block& blk);

/* Rebuild Block.runs from current TE style data.
   Carries strikethrough and url forward from old runs (best-effort).   */
void RebuildRunsFromTE(Block& blk);

/* Toggle one formatting bit (kStyleBold/kStyleItalic/kStyleUnderline/
   kStyleStrikethrough) on the TE selection, keeping Block.runs in sync. */
void ToggleRunStyleBit(Block& blk, int which);
