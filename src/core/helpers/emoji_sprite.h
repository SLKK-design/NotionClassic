#ifndef EMOJI_SPRITE_HDR
#define EMOJI_SPRITE_HDR

#include <stddef.h>  /* size_t */

/* Auto-generated emoji sprite atlas header
 * Atlas:  emoji_sprite.png  (480 x 784 px)
 * Tile:   16 x 16 px per emoji
 * Count:  1153 unique emojis
 * Layout: 30 columns x 39 rows
 *
 * Emoji strings have variation selectors (U+FE0F) stripped.
 * Table is sorted by UTF-8 byte value — use emoji_find() for O(log n) lookup.
 * Strip U+FE0F from your input string before calling emoji_find().
 */

#define EMOJI_TILE_W    16
#define EMOJI_TILE_H    16
#define EMOJI_COUNT     1153

typedef struct {
    const char *utf8;  /* null-terminated UTF-8, variation selectors stripped */
    short       x;     /* x pixel offset in atlas                              */
    short       y;     /* y pixel offset in atlas                              */
    const char *name;  /* lowercase Unicode name                               */
} EmojiSprite;

#ifdef __cplusplus
extern "C" {
#endif

extern const EmojiSprite EMOJI_TABLE[EMOJI_COUNT];

/* Display order — Mac/Unicode category order (Smileys first, then People,
 * Animals, Food, Travel, Activities, Objects, Symbols, Flags).
 * Each entry is an index into EMOJI_TABLE.
 * Use this array to iterate the picker grid; EMOJI_TABLE stays sorted for
 * emoji_find() binary search. */
extern const short EMOJI_DISPLAY_ORDER[EMOJI_COUNT];

/* Binary search — strip U+FE0F (\xef\xb8\x8f) from utf8 before calling.
 * Returns NULL if not found. */
const EmojiSprite *emoji_find(const char *utf8);

/* Like emoji_find, but drops skin-tone modifiers (U+1F3FB-U+1F3FF), falls
 * back from a ZWJ compound to its base, and shows a look-alike for emoji
 * newer than the sheet (see emoji_alias).  Strip U+FE0F before calling.
 * Returns NULL only for codepoints that aren't emoji at all. */
const EmojiSprite *emoji_find_fallback(const char *utf8);

/* Look-alike on the sheet for a codepoint the sheet doesn't have, e.g.
 * U+1F976 cold face → grimacing face.  NULL if none.  Generated into
 * emoji_alias.c by scripts/make_emoji_alias.py. */
const EmojiSprite *emoji_alias(unsigned long cp);

/* Returns the byte length of the first UTF-8 codepoint at utf8.
 * Returns 0 if utf8 is NULL or points to a NUL byte. */
size_t utf8_first_codepoint_len(const char *utf8);

/* Like emoji_find but accepts a non-null-terminated string of exactly len bytes.
 * Strip U+FE0F before calling.  Returns NULL if not found. */
const EmojiSprite *emoji_find_exact(const char *utf8, size_t len);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* EMOJI_SPRITE_HDR */
