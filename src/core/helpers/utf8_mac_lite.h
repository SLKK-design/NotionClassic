/* utf8_mac_lite.h — Fixed-array UTF-8 helpers with no std::string / std::vector dependency.
   Include this instead of utf8_mac.h when only the C-style fixed-array functions are needed. */
#pragma once

#include <stdint.h>
#include <stddef.h>

/* Sentinel byte standing in for one emoji/unmapped sequence in Mac Roman text.
   Defined here (the encoding owns it) rather than in app/content/blocks.h so
   the core conversion code needn't reach up into the app layer. */
#define kEmojiSentinel '\x01'

/* Sentinels (emoji) in text[from, to) */
static inline int CountSentinels(const char* text, int from, int to) {
    int n = 0;
    for (int i = from; i < to; i++)
        n += ((unsigned char)text[i] == (unsigned char)kEmojiSentinel);
    return n;
}

/* Strip U+FE0F variation-selector bytes from a UTF-8 string into buf.
   Returns s unchanged if no VS bytes; returns buf if stripping happened. */
const char* StripVariationSelectors(const char* s, char* buf, size_t bufMax);

/* Convert UTF-8 to Mac Roman into caller-provided buffers (zero heap).
   Returns bytes written to textDst (no NUL appended).
   Pass emojiDst=nullptr / emojiCap=0 to skip emoji extraction.          */
short utf8_to_macroman_into(const char* __restrict__ utf8,
                             char* __restrict__ textDst, short textCap,
                             char (* __restrict__ emojiDst)[32],
                             uint8_t* __restrict__ emojiCount,
                             uint8_t emojiCap);

/* Same, for utf8len input bytes, reporting in *consumed how many were
   used.  Stops early (consumed < utf8len) when textCap is full or, with
   emojiDst, when a character needs an emoji slot and none is left — so
   the caller can carry on in a fresh block.                             */
short utf8_to_macroman_step(const char* __restrict__ utf8, size_t utf8len,
                            char* __restrict__ textDst, short textCap,
                            char (* __restrict__ emojiDst)[32],
                            uint8_t* __restrict__ emojiCount,
                            uint8_t emojiCap, size_t* consumed);
