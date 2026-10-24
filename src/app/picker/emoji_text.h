/* emoji_text.h — Emoji sentinel/TE padding helpers and drawing utilities.
   Included by main.cpp and any file that works with Block TE buffers.  */
#pragma once

#include <TextEdit.h>
#include <stdint.h>
#include <stddef.h>
#include "blocks.h"

/* A stored sentinel occupies this many TE cells once padded:
   sentinel + 2 trailing spaces (nothing before it).                     */
static const short kEmojiTECells = 3;
/* Longest TE text a full block can have: every char plus the pad cells of
   every emoji.  Size raw-TE scratch buffers with this, never kMaxTextLen,
   or the padded tail of a near-full block is cut off before stripping. */
static const int kTEBufLen = kMaxTextLen + kMaxEmoji * kEmojiTECells;

/* Number of TE cells occupied by `len` chars of stored text starting at
   `off`. Each sentinel expands to kEmojiTECells; everything else is 1.
   Header-inline so callers in hot loops keep inlining on 68k.           */
static inline short TELenForText(const char* text, short off, short len) {
    short n = 0;
    for (short i = 0; i < len; i++)
        n += ((unsigned char)text[off + i] == (unsigned char)kEmojiSentinel)
                 ? kEmojiTECells : 1;
    return n;
}

/* Stored-text index for TE position tePos: the first char whose TE cells
   start at or after it (a position inside an emoji's padding rounds up). */
static inline short TextIndexForTE(const char* text, short len, short tePos) {
    int tep = 0, ci = 0;
    while (ci < len && tep < tePos) {
        tep += ((unsigned char)text[ci] == (unsigned char)kEmojiSentinel) ? kEmojiTECells : 1;
        ci++;
    }
    return (short)ci;
}

/* Expand sentinel (\x01) chars to space-sentinel-space for TE display.
   Writes into out[outMax]; sets outLen to bytes written (no NUL).       */
void TextToTE(const char* text, short textLen,
              char* out, short outMax, short& outLen);

/* Strip the spaces TextToTE added around sentinels; write into dst[dstMax]. */
void StripEmojiPadInto(const char* buf, size_t len, char* dst, size_t dstMax);

/* Big-font blocks (headings, title): each emoji's sentinel and first pad
   space at 10pt, so the gap after the 16px tile matches a paragraph's.
   The last pad space keeps the block's size (typing after the emoji takes
   its style).  teText is the TE's text, just set by TESetText.  True if
   it restyled (call TECalText). */
bool ShrinkEmojiPads(TEHandle te, const char* teText, short teLen, BlockType type);
/* An emoji is one unit in the TE (sentinel + its pad spaces).  Before an
   edit with `key` (0 = paste/insert/cut): Backspace after or inside one
   and Delete before or inside one select it whole, a caret inside its pad
   moves after it, and a selection edge inside one widens to cover it — so
   no edit leaves an emoji with too few pad cells. */
void SnapSelectionToEmoji(TEHandle te, char key);
/* Before typing/pasting at an insertion point whose style TE would take
   from a shrunk cell: give the typing style the block's font and size. */
void KeepTypingSize(TEHandle te, BlockType type);

/* Sync the globally active TE handle into a Block's text buffer.        */
void SyncBlockText(Block& b);

/* Draw the correct emoji tiles for every sentinel in the TE buffer.
   Uses cached x-positions from blk.emojiXPos for stable rendering.      */
void DrawBlockEmojis(TEHandle teh, const char* teText, short teLen,
                     Block& blk);

/* Draw strikethrough lines for all strikethrough runs in blk.           */
void DrawStrikethrough(TEHandle te, const Block& blk);
