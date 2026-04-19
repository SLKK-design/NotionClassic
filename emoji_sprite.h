#ifndef EMOJI_SPRITE_HDR
#define EMOJI_SPRITE_HDR

/* Auto-generated emoji sprite atlas header
 * Atlas:  emoji_sprite.png  (270 x 423 px)
 * Tile:   9 x 9 px per emoji
 * Count:  1456 unique emojis
 * Layout: 30 columns x 47 rows  (perfect fit, 0 unused pixels)
 *
 * Emoji strings have variation selectors (U+FE0F) stripped.
 * Table is sorted by UTF-8 byte value — use emoji_find() for O(log n) lookup.
 * Strip U+FE0F from your input string before calling emoji_find().
 */

#define EMOJI_SPRITE_W  270
#define EMOJI_SPRITE_H  423
#define EMOJI_TILE_W    9
#define EMOJI_TILE_H    9
#define EMOJI_COUNT     1456

typedef struct {
    const char *utf8;  /* null-terminated UTF-8, variation selectors stripped */
    short       x;     /* x pixel offset in atlas                              */
    short       y;     /* y pixel offset in atlas                              */
} EmojiSprite;

extern const EmojiSprite EMOJI_TABLE[EMOJI_COUNT];

/* Parallel name table — lowercase Unicode names, max 48 chars each */
extern const char * const EMOJI_NAMES[EMOJI_COUNT];

/* Display order — Mac/Unicode category order (Smileys first, then People,
 * Animals, Food, Travel, Activities, Objects, Symbols, Flags).
 * Each entry is an index into EMOJI_TABLE.
 * Use this array to iterate the picker grid; EMOJI_TABLE stays sorted for
 * emoji_find() binary search. */
extern const short EMOJI_DISPLAY_ORDER[EMOJI_COUNT];

/* Binary search — strip U+FE0F (\xef\xb8\x8f) from utf8 before calling.
 * Returns NULL if not found. */
const EmojiSprite *emoji_find(const char *utf8);

#endif /* EMOJI_SPRITE_HDR */
