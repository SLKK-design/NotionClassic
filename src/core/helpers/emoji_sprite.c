#include "emoji_sprite.h"
#include <string.h>

const EmojiSprite EMOJI_TABLE[EMOJI_COUNT] = {
    { "\x23\xe2\x83\xa3",    0,    0, "hash" },  /* #⃣ */
    { "\x2a\xe2\x83\xa3",   16,    0, "asterisk" },  /* *⃣ */
    { "\x30\xe2\x83\xa3",   32,    0, "digit zero" },  /* 0⃣ */
    { "\x31\xe2\x83\xa3",   48,    0, "digit one" },  /* 1⃣ */
    { "\x32\xe2\x83\xa3",   64,    0, "digit two" },  /* 2⃣ */
    { "\x33\xe2\x83\xa3",   80,    0, "digit three" },  /* 3⃣ */
    { "\x34\xe2\x83\xa3",   96,    0, "digit four" },  /* 4⃣ */
    { "\x35\xe2\x83\xa3",  112,    0, "digit five" },  /* 5⃣ */
    { "\x36\xe2\x83\xa3",  128,    0, "digit six" },  /* 6⃣ */
    { "\x37\xe2\x83\xa3",  144,    0, "digit seven" },  /* 7⃣ */
    { "\x38\xe2\x83\xa3",  160,    0, "digit eight" },  /* 8⃣ */
    { "\x39\xe2\x83\xa3",  176,    0, "digit nine" },  /* 9⃣ */
    { "\xc2\xa9",  192,    0, "copyright" },  /* © */
    { "\xc2\xae",  208,    0, "registered" },  /* ® */
    { "\xe2\x80\xbc",  224,    0, "double exclamation" },  /* ‼ */
    { "\xe2\x81\x89",  240,    0, "exclamation question mark" },  /* ⁉ */
    { "\xe2\x84\xa2",  256,    0, "trademark" },  /* ™ */
    { "\xe2\x84\xb9",  272,    0, "information source" },  /* ℹ */
    { "\xe2\x86\x94",  288,    0, "left right arrow" },  /* ↔ */
    { "\xe2\x86\x95",  304,    0, "up down arrow" },  /* ↕ */
    { "\xe2\x86\x96",  320,    0, "north west arrow" },  /* ↖ */
    { "\xe2\x86\x97",  336,    0, "north east arrow" },  /* ↗ */
    { "\xe2\x86\x98",  352,    0, "south east arrow" },  /* ↘ */
    { "\xe2\x86\x99",  368,    0, "south west arrow" },  /* ↙ */
    { "\xe2\x86\xa9",  384,    0, "return arrow" },  /* ↩ */
    { "\xe2\x86\xaa",  400,    0, "forward arrow" },  /* ↪ */
    { "\xe2\x8c\x9a",  416,    0, "watch" },  /* ⌚ */
    { "\xe2\x8c\x9b",  432,    0, "hourglass" },  /* ⌛ */
    { "\xe2\x8c\xa8",  448,    0, "keyboard" },  /* ⌨ */
    { "\xe2\x8f\x8f",  464,    0, "eject" },  /* ⏏ */
    { "\xe2\x8f\xa9",    0,   16, "forward" },  /* ⏩ */
    { "\xe2\x8f\xaa",   16,   16, "rewind" },  /* ⏪ */
    { "\xe2\x8f\xab",   32,   16, "fast up" },  /* ⏫ */
    { "\xe2\x8f\xac",   48,   16, "fast down" },  /* ⏬ */
    { "\xe2\x8f\xad",   64,   16, "next track" },  /* ⏭ */
    { "\xe2\x8f\xae",   80,   16, "previous track" },  /* ⏮ */
    { "\xe2\x8f\xaf",   96,   16, "play/pause" },  /* ⏯ */
    { "\xe2\x8f\xb0",  112,   16, "alarm clock" },  /* ⏰ */
    { "\xe2\x8f\xb1",  128,   16, "stopwatch" },  /* ⏱ */
    { "\xe2\x8f\xb2",  144,   16, "timer clock" },  /* ⏲ */
    { "\xe2\x8f\xb3",  160,   16, "hourglass with flowing sand" },  /* ⏳ */
    { "\xe2\x8f\xb8",  176,   16, "pause" },  /* ⏸ */
    { "\xe2\x8f\xb9",  192,   16, "stop" },  /* ⏹ */
    { "\xe2\x8f\xba",  208,   16, "record" },  /* ⏺ */
    { "\xe2\x93\x82",  224,   16, "M circle" },  /* Ⓜ */
    { "\xe2\x96\xaa",  240,   16, "small black square" },  /* ▪ */
    { "\xe2\x96\xab",  256,   16, "small white square" },  /* ▫ */
    { "\xe2\x96\xb6",  272,   16, "play" },  /* ▶ */
    { "\xe2\x97\x80",  288,   16, "reverse" },  /* ◀ */
    { "\xe2\x97\xbb",  304,   16, "medium white square" },  /* ◻ */
    { "\xe2\x97\xbc",  320,   16, "medium black square" },  /* ◼ */
    { "\xe2\x97\xbd",  336,   16, "small white square" },  /* ◽ */
    { "\xe2\x97\xbe",  352,   16, "small black square" },  /* ◾ */
    { "\xe2\x98\x80",  368,   16, "sun" },  /* ☀ */
    { "\xe2\x98\x81",  384,   16, "cloud" },  /* ☁ */
    { "\xe2\x98\x82",  400,   16, "umbrella" },  /* ☂ */
    { "\xe2\x98\x83",  416,   16, "snowman" },  /* ☃ */
    { "\xe2\x98\x84",  432,   16, "comet" },  /* ☄ */
    { "\xe2\x98\x8e",  448,   16, "telephone" },  /* ☎ */
    { "\xe2\x98\x91",  464,   16, "ballot box with check" },  /* ☑ */
    { "\xe2\x98\x94",    0,   32, "umbrella with rain drops" },  /* ☔ */
    { "\xe2\x98\x95",   16,   32, "hot beverage" },  /* ☕ */
    { "\xe2\x98\x98",   32,   32, "shamrock" },  /* ☘ */
    { "\xe2\x98\x9d",   48,   32, "point up" },  /* ☝ */
    { "\xe2\x98\xa0",   64,   32, "skull and crossbones" },  /* ☠ */
    { "\xe2\x98\xa2",   80,   32, "radioactive" },  /* ☢ */
    { "\xe2\x98\xa3",   96,   32, "biohazard" },  /* ☣ */
    { "\xe2\x98\xa6",  112,   32, "orthodox cross" },  /* ☦ */
    { "\xe2\x98\xaa",  128,   32, "star and crescent" },  /* ☪ */
    { "\xe2\x98\xae",  144,   32, "peace" },  /* ☮ */
    { "\xe2\x98\xaf",  160,   32, "yin yang" },  /* ☯ */
    { "\xe2\x98\xb8",  176,   32, "wheel of dharma" },  /* ☸ */
    { "\xe2\x98\xb9",  192,   32, "frowning face" },  /* ☹ */
    { "\xe2\x99\x88",  208,   32, "aries" },  /* ♈ */
    { "\xe2\x99\x89",  224,   32, "taurus" },  /* ♉ */
    { "\xe2\x99\x8a",  240,   32, "gemini" },  /* ♊ */
    { "\xe2\x99\x8b",  256,   32, "cancer" },  /* ♋ */
    { "\xe2\x99\x8c",  272,   32, "leo" },  /* ♌ */
    { "\xe2\x99\x8d",  288,   32, "virgo" },  /* ♍ */
    { "\xe2\x99\x8e",  304,   32, "libra" },  /* ♎ */
    { "\xe2\x99\x8f",  320,   32, "scorpius" },  /* ♏ */
    { "\xe2\x99\x90",  336,   32, "sagittarius" },  /* ♐ */
    { "\xe2\x99\x91",  352,   32, "capricorn" },  /* ♑ */
    { "\xe2\x99\x92",  368,   32, "aquarius" },  /* ♒ */
    { "\xe2\x99\x93",  384,   32, "pisces" },  /* ♓ */
    { "\xe2\x99\xa0",  400,   32, "spade" },  /* ♠ */
    { "\xe2\x99\xa3",  416,   32, "club" },  /* ♣ */
    { "\xe2\x99\xa5",  432,   32, "heart suit" },  /* ♥ */
    { "\xe2\x99\xa6",  448,   32, "diamond suit" },  /* ♦ */
    { "\xe2\x99\xa8",  464,   32, "hot springs" },  /* ♨ */
    { "\xe2\x99\xbb",    0,   48, "recycling" },  /* ♻ */
    { "\xe2\x99\xbf",   16,   48, "wheelchair" },  /* ♿ */
    { "\xe2\x9a\x92",   32,   48, "hammer and pick" },  /* ⚒ */
    { "\xe2\x9a\x93",   48,   48, "anchor" },  /* ⚓ */
    { "\xe2\x9a\x94",   64,   48, "crossed swords" },  /* ⚔ */
    { "\xe2\x9a\x96",   80,   48, "scales" },  /* ⚖ */
    { "\xe2\x9a\x97",   96,   48, "alembic" },  /* ⚗ */
    { "\xe2\x9a\x99",  112,   48, "gear" },  /* ⚙ */
    { "\xe2\x9a\x9b",  128,   48, "atom" },  /* ⚛ */
    { "\xe2\x9a\x9c",  144,   48, "fleur-de-lis" },  /* ⚜ */
    { "\xe2\x9a\xa0",  160,   48, "warning" },  /* ⚠ */
    { "\xe2\x9a\xa7",  176,   48, "transgender" },  /* ⚧ */
    { "\xe2\x9a\xaa",  192,   48, "white circle" },  /* ⚪ */
    { "\xe2\x9a\xab",  208,   48, "black circle" },  /* ⚫ */
    { "\xe2\x9a\xb0",  224,   48, "coffin" },  /* ⚰ */
    { "\xe2\x9a\xb1",  240,   48, "funeral urn" },  /* ⚱ */
    { "\xe2\x9a\xbd",  256,   48, "soccer ball" },  /* ⚽ */
    { "\xe2\x9a\xbe",  272,   48, "baseball" },  /* ⚾ */
    { "\xe2\x9b\x84",  288,   48, "snowman without snow" },  /* ⛄ */
    { "\xe2\x9b\x85",  304,   48, "sun behind cloud" },  /* ⛅ */
    { "\xe2\x9b\x88",  320,   48, "thunder cloud and rain" },  /* ⛈ */
    { "\xe2\x9b\x8e",  336,   48, "ophiuchus" },  /* ⛎ */
    { "\xe2\x9b\x8f",  352,   48, "pick" },  /* ⛏ */
    { "\xe2\x9b\x91",  368,   48, "rescue helmet" },  /* ⛑ */
    { "\xe2\x9b\x93",  384,   48, "chains" },  /* ⛓ */
    { "\xe2\x9b\x94",  400,   48, "no entry" },  /* ⛔ */
    { "\xe2\x9b\xa9",  416,   48, "shinto shrine" },  /* ⛩ */
    { "\xe2\x9b\xaa",  432,   48, "church" },  /* ⛪ */
    { "\xe2\x9b\xb1",  448,   48, "umbrella on ground" },  /* ⛱ */
    { "\xe2\x9b\xb2",  464,   48, "fountain" },  /* ⛲ */
    { "\xe2\x9b\xb3",    0,   64, "flag in hole" },  /* ⛳ */
    { "\xe2\x9b\xb4",   16,   64, "ferry" },  /* ⛴ */
    { "\xe2\x9b\xb5",   32,   64, "sailboat" },  /* ⛵ */
    { "\xe2\x9b\xb7",   48,   64, "skier" },  /* ⛷ */
    { "\xe2\x9b\xb8",   64,   64, "ice skate" },  /* ⛸ */
    { "\xe2\x9b\xb9\xe2\x80\x8d\xe2\x99\x80",   80,   64, "person with ball (woman)" },  /* ⛹‍♀ */
    { "\xe2\x9b\xb9\xe2\x80\x8d\xe2\x99\x82",   96,   64, "person with ball (man)" },  /* ⛹‍♂ */
    { "\xe2\x9b\xba",  112,   64, "tent" },  /* ⛺ */
    { "\xe2\x9b\xbd",  128,   64, "fuel pump" },  /* ⛽ */
    { "\xe2\x9c\x82",  144,   64, "scissors" },  /* ✂ */
    { "\xe2\x9c\x85",  160,   64, "check mark" },  /* ✅ */
    { "\xe2\x9c\x88",  176,   64, "airplane" },  /* ✈ */
    { "\xe2\x9c\x89",  192,   64, "envelope" },  /* ✉ */
    { "\xe2\x9c\x8a",  208,   64, "raised fist" },  /* ✊ */
    { "\xe2\x9c\x8b",  224,   64, "raised hand" },  /* ✋ */
    { "\xe2\x9c\x8c",  240,   64, "victory hand" },  /* ✌ */
    { "\xe2\x9c\x8d",  256,   64, "writing hand" },  /* ✍ */
    { "\xe2\x9c\x8f",  272,   64, "pencil" },  /* ✏ */
    { "\xe2\x9c\x92",  288,   64, "pen nib" },  /* ✒ */
    { "\xe2\x9c\x94",  304,   64, "check mark" },  /* ✔ */
    { "\xe2\x9c\x96",  320,   64, "multiply" },  /* ✖ */
    { "\xe2\x9c\x9d",  336,   64, "latin cross" },  /* ✝ */
    { "\xe2\x9c\xa1",  352,   64, "star of david" },  /* ✡ */
    { "\xe2\x9c\xa8",  368,   64, "sparkles" },  /* ✨ */
    { "\xe2\x9c\xb3",  384,   64, "eight spoked asterisk" },  /* ✳ */
    { "\xe2\x9c\xb4",  400,   64, "eight-pointed star" },  /* ✴ */
    { "\xe2\x9d\x84",  416,   64, "snowflake" },  /* ❄ */
    { "\xe2\x9d\x87",  432,   64, "sparkle" },  /* ❇ */
    { "\xe2\x9d\x8c",  448,   64, "cross mark" },  /* ❌ */
    { "\xe2\x9d\x8e",  464,   64, "cross mark" },  /* ❎ */
    { "\xe2\x9d\x93",    0,   80, "question mark" },  /* ❓ */
    { "\xe2\x9d\x94",   16,   80, "white question mark" },  /* ❔ */
    { "\xe2\x9d\x95",   32,   80, "white exclamation" },  /* ❕ */
    { "\xe2\x9d\x97",   48,   80, "exclamation mark" },  /* ❗ */
    { "\xe2\x9d\xa3",   64,   80, "heart exclamation" },  /* ❣ */
    { "\xe2\x9d\xa4",   80,   80, "red heart" },  /* ❤ */
    { "\xe2\x9e\x95",   96,   80, "plus" },  /* ➕ */
    { "\xe2\x9e\x96",  112,   80, "minus" },  /* ➖ */
    { "\xe2\x9e\x97",  128,   80, "divide" },  /* ➗ */
    { "\xe2\x9e\xa1",  144,   80, "right arrow" },  /* ➡ */
    { "\xe2\x9e\xb0",  160,   80, "curly loop" },  /* ➰ */
    { "\xe2\x9e\xbf",  176,   80, "curly loop" },  /* ➿ */
    { "\xe2\xa4\xb4",  192,   80, "curved up arrow" },  /* ⤴ */
    { "\xe2\xa4\xb5",  208,   80, "curved down arrow" },  /* ⤵ */
    { "\xe2\xac\x85",  224,   80, "left arrow" },  /* ⬅ */
    { "\xe2\xac\x86",  240,   80, "up arrow" },  /* ⬆ */
    { "\xe2\xac\x87",  256,   80, "down arrow" },  /* ⬇ */
    { "\xe2\xac\x9b",  272,   80, "large black square" },  /* ⬛ */
    { "\xe2\xac\x9c",  288,   80, "large white square" },  /* ⬜ */
    { "\xe2\xad\x90",  304,   80, "star" },  /* ⭐ */
    { "\xe2\xad\x95",  320,   80, "circle" },  /* ⭕ */
    { "\xe3\x80\xb0",  336,   80, "wavy dash" },  /* 〰 */
    { "\xe3\x80\xbd",  352,   80, "part alternation mark" },  /* 〽 */
    { "\xe3\x8a\x97",  368,   80, "congratulations" },  /* ㊗ */
    { "\xe3\x8a\x99",  384,   80, "secret" },  /* ㊙ */
    { "\xf0\x9f\x80\x84",  400,   80, "mahjong tile red dragon" },  /* 🀄 */
    { "\xf0\x9f\x83\x8f",  416,   80, "joker" },  /* 🃏 */
    { "\xf0\x9f\x85\xb0",  432,   80, "A button" },  /* 🅰 */
    { "\xf0\x9f\x85\xb1",  448,   80, "B button" },  /* 🅱 */
    { "\xf0\x9f\x85\xbe",  464,   80, "O button" },  /* 🅾 */
    { "\xf0\x9f\x85\xbf",    0,   96, "P button" },  /* 🅿 */
    { "\xf0\x9f\x86\x8e",   16,   96, "AB button" },  /* 🆎 */
    { "\xf0\x9f\x86\x91",   32,   96, "CL button" },  /* 🆑 */
    { "\xf0\x9f\x86\x92",   48,   96, "cool" },  /* 🆒 */
    { "\xf0\x9f\x86\x93",   64,   96, "free" },  /* 🆓 */
    { "\xf0\x9f\x86\x94",   80,   96, "ID" },  /* 🆔 */
    { "\xf0\x9f\x86\x95",   96,   96, "new" },  /* 🆕 */
    { "\xf0\x9f\x86\x96",  112,   96, "NG" },  /* 🆖 */
    { "\xf0\x9f\x86\x97",  128,   96, "OK" },  /* 🆗 */
    { "\xf0\x9f\x86\x98",  144,   96, "SOS" },  /* 🆘 */
    { "\xf0\x9f\x86\x99",  160,   96, "UP!" },  /* 🆙 */
    { "\xf0\x9f\x86\x9a",  176,   96, "VS" },  /* 🆚 */
    { "\xf0\x9f\x88\x81",  192,   96, "koko" },  /* 🈁 */
    { "\xf0\x9f\x88\x82",  208,   96, "sa" },  /* 🈂 */
    { "\xf0\x9f\x88\x9a",  224,   96, "free (Japanese)" },  /* 🈚 */
    { "\xf0\x9f\x88\xaf",  240,   96, "reserved" },  /* 🈯 */
    { "\xf0\x9f\x88\xb2",  256,   96, "prohibited" },  /* 🈲 */
    { "\xf0\x9f\x88\xb3",  272,   96, "vacant" },  /* 🈳 */
    { "\xf0\x9f\x88\xb4",  288,   96, "passing" },  /* 🈴 */
    { "\xf0\x9f\x88\xb5",  304,   96, "full/no vacancy" },  /* 🈵 */
    { "\xf0\x9f\x88\xb6",  320,   96, "not free" },  /* 🈶 */
    { "\xf0\x9f\x88\xb7",  336,   96, "monthly" },  /* 🈷 */
    { "\xf0\x9f\x88\xb8",  352,   96, "apply" },  /* 🈸 */
    { "\xf0\x9f\x88\xb9",  368,   96, "discount" },  /* 🈹 */
    { "\xf0\x9f\x88\xba",  384,   96, "open for business" },  /* 🈺 */
    { "\xf0\x9f\x89\x90",  400,   96, "advantage" },  /* 🉐 */
    { "\xf0\x9f\x89\x91",  416,   96, "accept" },  /* 🉑 */
    { "\xf0\x9f\x8c\x80",  432,   96, "cyclone" },  /* 🌀 */
    { "\xf0\x9f\x8c\x81",  448,   96, "foggy" },  /* 🌁 */
    { "\xf0\x9f\x8c\x82",  464,   96, "closed umbrella" },  /* 🌂 */
    { "\xf0\x9f\x8c\x83",    0,  112, "night with stars" },  /* 🌃 */
    { "\xf0\x9f\x8c\x84",   16,  112, "sunrise over mountains" },  /* 🌄 */
    { "\xf0\x9f\x8c\x85",   32,  112, "sunrise" },  /* 🌅 */
    { "\xf0\x9f\x8c\x86",   48,  112, "cityscape at dusk" },  /* 🌆 */
    { "\xf0\x9f\x8c\x87",   64,  112, "sunset over buildings" },  /* 🌇 */
    { "\xf0\x9f\x8c\x88",   80,  112, "rainbow" },  /* 🌈 */
    { "\xf0\x9f\x8c\x89",   96,  112, "bridge at night" },  /* 🌉 */
    { "\xf0\x9f\x8c\x8a",  112,  112, "water wave" },  /* 🌊 */
    { "\xf0\x9f\x8c\x8b",  128,  112, "volcano" },  /* 🌋 */
    { "\xf0\x9f\x8c\x8c",  144,  112, "milky way" },  /* 🌌 */
    { "\xf0\x9f\x8c\x8d",  160,  112, "earth globe europe-africa" },  /* 🌍 */
    { "\xf0\x9f\x8c\x8e",  176,  112, "earth globe americas" },  /* 🌎 */
    { "\xf0\x9f\x8c\x8f",  192,  112, "earth globe asia-australia" },  /* 🌏 */
    { "\xf0\x9f\x8c\x90",  208,  112, "globe with meridians" },  /* 🌐 */
    { "\xf0\x9f\x8c\x91",  224,  112, "new moon" },  /* 🌑 */
    { "\xf0\x9f\x8c\x92",  240,  112, "waxing crescent moon" },  /* 🌒 */
    { "\xf0\x9f\x8c\x93",  256,  112, "first quarter moon" },  /* 🌓 */
    { "\xf0\x9f\x8c\x94",  272,  112, "waxing gibbous moon" },  /* 🌔 */
    { "\xf0\x9f\x8c\x95",  288,  112, "full moon" },  /* 🌕 */
    { "\xf0\x9f\x8c\x96",  304,  112, "waning gibbous moon" },  /* 🌖 */
    { "\xf0\x9f\x8c\x97",  320,  112, "last quarter moon" },  /* 🌗 */
    { "\xf0\x9f\x8c\x98",  336,  112, "waning crescent moon" },  /* 🌘 */
    { "\xf0\x9f\x8c\x99",  352,  112, "crescent moon" },  /* 🌙 */
    { "\xf0\x9f\x8c\x9a",  368,  112, "new moon with face" },  /* 🌚 */
    { "\xf0\x9f\x8c\x9b",  384,  112, "first quarter moon with face" },  /* 🌛 */
    { "\xf0\x9f\x8c\x9c",  400,  112, "last quarter moon with face" },  /* 🌜 */
    { "\xf0\x9f\x8c\x9d",  416,  112, "full moon with face" },  /* 🌝 */
    { "\xf0\x9f\x8c\x9e",  432,  112, "sun with face" },  /* 🌞 */
    { "\xf0\x9f\x8c\x9f",  448,  112, "glowing star" },  /* 🌟 */
    { "\xf0\x9f\x8c\xa0",  464,  112, "shooting star" },  /* 🌠 */
    { "\xf0\x9f\x8c\xa1",    0,  128, "thermometer" },  /* 🌡 */
    { "\xf0\x9f\x8c\xa4",   16,  128, "sun behind cloud" },  /* 🌤 */
    { "\xf0\x9f\x8c\xa5",   32,  128, "sun behind large cloud" },  /* 🌥 */
    { "\xf0\x9f\x8c\xa6",   48,  128, "sun behind rain cloud" },  /* 🌦 */
    { "\xf0\x9f\x8c\xa7",   64,  128, "cloud with rain" },  /* 🌧 */
    { "\xf0\x9f\x8c\xa8",   80,  128, "cloud with snow" },  /* 🌨 */
    { "\xf0\x9f\x8c\xa9",   96,  128, "cloud with lightning" },  /* 🌩 */
    { "\xf0\x9f\x8c\xaa",  112,  128, "cloud with tornado" },  /* 🌪 */
    { "\xf0\x9f\x8c\xab",  128,  128, "fog" },  /* 🌫 */
    { "\xf0\x9f\x8c\xac",  144,  128, "wind blowing face" },  /* 🌬 */
    { "\xf0\x9f\x8c\xad",  160,  128, "hot dog" },  /* 🌭 */
    { "\xf0\x9f\x8c\xae",  176,  128, "taco" },  /* 🌮 */
    { "\xf0\x9f\x8c\xaf",  192,  128, "burrito" },  /* 🌯 */
    { "\xf0\x9f\x8c\xb0",  208,  128, "chestnut" },  /* 🌰 */
    { "\xf0\x9f\x8c\xb1",  224,  128, "seedling" },  /* 🌱 */
    { "\xf0\x9f\x8c\xb2",  240,  128, "evergreen tree" },  /* 🌲 */
    { "\xf0\x9f\x8c\xb3",  256,  128, "deciduous tree" },  /* 🌳 */
    { "\xf0\x9f\x8c\xb4",  272,  128, "palm tree" },  /* 🌴 */
    { "\xf0\x9f\x8c\xb5",  288,  128, "cactus" },  /* 🌵 */
    { "\xf0\x9f\x8c\xb6",  304,  128, "hot pepper" },  /* 🌶 */
    { "\xf0\x9f\x8c\xb7",  320,  128, "tulip" },  /* 🌷 */
    { "\xf0\x9f\x8c\xb9",  336,  128, "rose" },  /* 🌹 */
    { "\xf0\x9f\x8c\xba",  352,  128, "hibiscus" },  /* 🌺 */
    { "\xf0\x9f\x8c\xbb",  368,  128, "sunflower" },  /* 🌻 */
    { "\xf0\x9f\x8c\xbc",  384,  128, "blossom" },  /* 🌼 */
    { "\xf0\x9f\x8c\xbd",  400,  128, "ear of maize" },  /* 🌽 */
    { "\xf0\x9f\x8c\xbe",  416,  128, "ear of rice" },  /* 🌾 */
    { "\xf0\x9f\x8c\xbf",  432,  128, "herb" },  /* 🌿 */
    { "\xf0\x9f\x8d\x80",  448,  128, "four leaf clover" },  /* 🍀 */
    { "\xf0\x9f\x8d\x81",  464,  128, "maple leaf" },  /* 🍁 */
    { "\xf0\x9f\x8d\x82",    0,  144, "fallen leaf" },  /* 🍂 */
    { "\xf0\x9f\x8d\x83",   16,  144, "leaf fluttering in wind" },  /* 🍃 */
    { "\xf0\x9f\x8d\x84",   32,  144, "mushroom" },  /* 🍄 */
    { "\xf0\x9f\x8d\x85",   48,  144, "tomato" },  /* 🍅 */
    { "\xf0\x9f\x8d\x86",   64,  144, "aubergine" },  /* 🍆 */
    { "\xf0\x9f\x8d\x87",   80,  144, "grapes" },  /* 🍇 */
    { "\xf0\x9f\x8d\x88",   96,  144, "melon" },  /* 🍈 */
    { "\xf0\x9f\x8d\x89",  112,  144, "watermelon" },  /* 🍉 */
    { "\xf0\x9f\x8d\x8a",  128,  144, "tangerine" },  /* 🍊 */
    { "\xf0\x9f\x8d\x8b",  144,  144, "lemon" },  /* 🍋 */
    { "\xf0\x9f\x8d\x8c",  160,  144, "banana" },  /* 🍌 */
    { "\xf0\x9f\x8d\x8d",  176,  144, "pineapple" },  /* 🍍 */
    { "\xf0\x9f\x8d\x8e",  192,  144, "red apple" },  /* 🍎 */
    { "\xf0\x9f\x8d\x8f",  208,  144, "green apple" },  /* 🍏 */
    { "\xf0\x9f\x8d\x90",  224,  144, "pear" },  /* 🍐 */
    { "\xf0\x9f\x8d\x91",  240,  144, "peach" },  /* 🍑 */
    { "\xf0\x9f\x8d\x92",  256,  144, "cherries" },  /* 🍒 */
    { "\xf0\x9f\x8d\x93",  272,  144, "strawberry" },  /* 🍓 */
    { "\xf0\x9f\x8d\x94",  288,  144, "hamburger" },  /* 🍔 */
    { "\xf0\x9f\x8d\x95",  304,  144, "slice of pizza" },  /* 🍕 */
    { "\xf0\x9f\x8d\x96",  320,  144, "meat on bone" },  /* 🍖 */
    { "\xf0\x9f\x8d\x97",  336,  144, "poultry leg" },  /* 🍗 */
    { "\xf0\x9f\x8d\x98",  352,  144, "rice cracker" },  /* 🍘 */
    { "\xf0\x9f\x8d\x99",  368,  144, "rice ball" },  /* 🍙 */
    { "\xf0\x9f\x8d\x9a",  384,  144, "cooked rice" },  /* 🍚 */
    { "\xf0\x9f\x8d\x9b",  400,  144, "curry and rice" },  /* 🍛 */
    { "\xf0\x9f\x8d\x9c",  416,  144, "steaming bowl" },  /* 🍜 */
    { "\xf0\x9f\x8d\x9d",  432,  144, "spaghetti" },  /* 🍝 */
    { "\xf0\x9f\x8d\x9e",  448,  144, "bread" },  /* 🍞 */
    { "\xf0\x9f\x8d\x9f",  464,  144, "french fries" },  /* 🍟 */
    { "\xf0\x9f\x8d\xa0",    0,  160, "roasted sweet potato" },  /* 🍠 */
    { "\xf0\x9f\x8d\xa1",   16,  160, "dango" },  /* 🍡 */
    { "\xf0\x9f\x8d\xa2",   32,  160, "oden" },  /* 🍢 */
    { "\xf0\x9f\x8d\xa3",   48,  160, "sushi" },  /* 🍣 */
    { "\xf0\x9f\x8d\xa4",   64,  160, "fried shrimp" },  /* 🍤 */
    { "\xf0\x9f\x8d\xa5",   80,  160, "fish cake with swirl design" },  /* 🍥 */
    { "\xf0\x9f\x8d\xa6",   96,  160, "soft ice cream" },  /* 🍦 */
    { "\xf0\x9f\x8d\xa7",  112,  160, "shaved ice" },  /* 🍧 */
    { "\xf0\x9f\x8d\xa8",  128,  160, "ice cream" },  /* 🍨 */
    { "\xf0\x9f\x8d\xa9",  144,  160, "doughnut" },  /* 🍩 */
    { "\xf0\x9f\x8d\xaa",  160,  160, "cookie" },  /* 🍪 */
    { "\xf0\x9f\x8d\xab",  176,  160, "chocolate bar" },  /* 🍫 */
    { "\xf0\x9f\x8d\xac",  192,  160, "candy" },  /* 🍬 */
    { "\xf0\x9f\x8d\xad",  208,  160, "lollipop" },  /* 🍭 */
    { "\xf0\x9f\x8d\xae",  224,  160, "custard" },  /* 🍮 */
    { "\xf0\x9f\x8d\xaf",  240,  160, "honey pot" },  /* 🍯 */
    { "\xf0\x9f\x8d\xb0",  256,  160, "shortcake" },  /* 🍰 */
    { "\xf0\x9f\x8d\xb1",  272,  160, "bento box" },  /* 🍱 */
    { "\xf0\x9f\x8d\xb2",  288,  160, "pot of food" },  /* 🍲 */
    { "\xf0\x9f\x8d\xb3",  304,  160, "cooking" },  /* 🍳 */
    { "\xf0\x9f\x8d\xb4",  320,  160, "fork and knife" },  /* 🍴 */
    { "\xf0\x9f\x8d\xb5",  336,  160, "teacup without handle" },  /* 🍵 */
    { "\xf0\x9f\x8d\xb6",  352,  160, "sake bottle and cup" },  /* 🍶 */
    { "\xf0\x9f\x8d\xb8",  368,  160, "cocktail glass" },  /* 🍸 */
    { "\xf0\x9f\x8d\xb9\xf0\x9f\xa7\x89",  384,  160, "tropical drink mate drink" },  /* 🍹🧉 */
    { "\xf0\x9f\x8d\xba",  400,  160, "beer mug" },  /* 🍺 */
    { "\xf0\x9f\x8d\xbb",  416,  160, "clinking beer mugs" },  /* 🍻 */
    { "\xf0\x9f\x8d\xbc",  432,  160, "baby bottle" },  /* 🍼 */
    { "\xf0\x9f\x8d\xbd",  448,  160, "fork and knife with plate" },  /* 🍽 */
    { "\xf0\x9f\x8d\xbe",  464,  160, "bottle with popping cork" },  /* 🍾 */
    { "\xf0\x9f\x8d\xbf",    0,  176, "popcorn" },  /* 🍿 */
    { "\xf0\x9f\x8e\x80",   16,  176, "ribbon" },  /* 🎀 */
    { "\xf0\x9f\x8e\x81",   32,  176, "wrapped present" },  /* 🎁 */
    { "\xf0\x9f\x8e\x82",   48,  176, "birthday cake" },  /* 🎂 */
    { "\xf0\x9f\x8e\x83",   64,  176, "jack-o-lantern" },  /* 🎃 */
    { "\xf0\x9f\x8e\x84",   80,  176, "christmas tree" },  /* 🎄 */
    { "\xf0\x9f\x8e\x85",   96,  176, "father christmas" },  /* 🎅 */
    { "\xf0\x9f\x8e\x86",  112,  176, "fireworks" },  /* 🎆 */
    { "\xf0\x9f\x8e\x87",  128,  176, "firework sparkler" },  /* 🎇 */
    { "\xf0\x9f\x8e\x88",  144,  176, "balloon" },  /* 🎈 */
    { "\xf0\x9f\x8e\x89",  160,  176, "party popper" },  /* 🎉 */
    { "\xf0\x9f\x8e\x8a",  176,  176, "confetti ball" },  /* 🎊 */
    { "\xf0\x9f\x8e\x8b",  192,  176, "tanabata tree" },  /* 🎋 */
    { "\xf0\x9f\x8e\x8d",  208,  176, "pine decoration" },  /* 🎍 */
    { "\xf0\x9f\x8e\x8e",  224,  176, "japanese dolls" },  /* 🎎 */
    { "\xf0\x9f\x8e\x8f",  240,  176, "carp streamer" },  /* 🎏 */
    { "\xf0\x9f\x8e\x90",  256,  176, "wind chime" },  /* 🎐 */
    { "\xf0\x9f\x8e\x91",  272,  176, "moon viewing ceremony" },  /* 🎑 */
    { "\xf0\x9f\x8e\x92",  288,  176, "school satchel" },  /* 🎒 */
    { "\xf0\x9f\x8e\x93",  304,  176, "graduation cap" },  /* 🎓 */
    { "\xf0\x9f\x8e\x96",  320,  176, "military medal" },  /* 🎖 */
    { "\xf0\x9f\x8e\x97",  336,  176, "reminder ribbon" },  /* 🎗 */
    { "\xf0\x9f\x8e\x99",  352,  176, "studio microphone" },  /* 🎙 */
    { "\xf0\x9f\x8e\x9a",  368,  176, "level slider" },  /* 🎚 */
    { "\xf0\x9f\x8e\x9b",  384,  176, "control knobs" },  /* 🎛 */
    { "\xf0\x9f\x8e\x9e",  400,  176, "film frames" },  /* 🎞 */
    { "\xf0\x9f\x8e\x9f",  416,  176, "admission tickets" },  /* 🎟 */
    { "\xf0\x9f\x8e\xa0",  432,  176, "carousel horse" },  /* 🎠 */
    { "\xf0\x9f\x8e\xa1",  448,  176, "ferris wheel" },  /* 🎡 */
    { "\xf0\x9f\x8e\xa2",  464,  176, "roller coaster" },  /* 🎢 */
    { "\xf0\x9f\x8e\xa3",    0,  192, "fishing pole and fish" },  /* 🎣 */
    { "\xf0\x9f\x8e\xa4",   16,  192, "microphone" },  /* 🎤 */
    { "\xf0\x9f\x8e\xa5",   32,  192, "movie camera" },  /* 🎥 */
    { "\xf0\x9f\x8e\xa6",   48,  192, "cinema" },  /* 🎦 */
    { "\xf0\x9f\x8e\xa7",   64,  192, "headphone" },  /* 🎧 */
    { "\xf0\x9f\x8e\xa8",   80,  192, "artist palette" },  /* 🎨 */
    { "\xf0\x9f\x8e\xa9",   96,  192, "top hat" },  /* 🎩 */
    { "\xf0\x9f\x8e\xaa",  112,  192, "circus tent" },  /* 🎪 */
    { "\xf0\x9f\x8e\xab",  128,  192, "ticket" },  /* 🎫 */
    { "\xf0\x9f\x8e\xac",  144,  192, "clapper board" },  /* 🎬 */
    { "\xf0\x9f\x8e\xad",  160,  192, "performing arts" },  /* 🎭 */
    { "\xf0\x9f\x8e\xae",  176,  192, "video game" },  /* 🎮 */
    { "\xf0\x9f\x8e\xaf",  192,  192, "direct hit" },  /* 🎯 */
    { "\xf0\x9f\x8e\xb0",  208,  192, "slot machine" },  /* 🎰 */
    { "\xf0\x9f\x8e\xb1",  224,  192, "billiards" },  /* 🎱 */
    { "\xf0\x9f\x8e\xb2",  240,  192, "game die" },  /* 🎲 */
    { "\xf0\x9f\x8e\xb3",  256,  192, "bowling" },  /* 🎳 */
    { "\xf0\x9f\x8e\xb4",  272,  192, "flower playing cards" },  /* 🎴 */
    { "\xf0\x9f\x8e\xb5",  288,  192, "musical note" },  /* 🎵 */
    { "\xf0\x9f\x8e\xb6",  304,  192, "multiple musical notes" },  /* 🎶 */
    { "\xf0\x9f\x8e\xb7",  320,  192, "saxophone" },  /* 🎷 */
    { "\xf0\x9f\x8e\xb8",  336,  192, "guitar" },  /* 🎸 */
    { "\xf0\x9f\x8e\xb9",  352,  192, "musical keyboard" },  /* 🎹 */
    { "\xf0\x9f\x8e\xba",  368,  192, "trumpet" },  /* 🎺 */
    { "\xf0\x9f\x8e\xbb",  384,  192, "violin" },  /* 🎻 */
    { "\xf0\x9f\x8e\xbc",  400,  192, "musical score" },  /* 🎼 */
    { "\xf0\x9f\x8e\xbd",  416,  192, "running shirt with sash" },  /* 🎽 */
    { "\xf0\x9f\x8e\xbe",  432,  192, "tennis racquet and ball" },  /* 🎾 */
    { "\xf0\x9f\x8e\xbf",  448,  192, "ski and ski boot" },  /* 🎿 */
    { "\xf0\x9f\x8f\x80",  464,  192, "basketball and hoop" },  /* 🏀 */
    { "\xf0\x9f\x8f\x82",    0,  208, "snowboarder" },  /* 🏂 */
    { "\xf0\x9f\x8f\x83\xe2\x80\x8d\xe2\x99\x80",   16,  208, "runner (woman)" },  /* 🏃‍♀ */
    { "\xf0\x9f\x8f\x83\xe2\x80\x8d\xe2\x99\x82",   32,  208, "runner (man)" },  /* 🏃‍♂ */
    { "\xf0\x9f\x8f\x84\xe2\x80\x8d\xe2\x99\x80",   48,  208, "surfer (woman)" },  /* 🏄‍♀ */
    { "\xf0\x9f\x8f\x84\xe2\x80\x8d\xe2\x99\x82",   64,  208, "surfer (man)" },  /* 🏄‍♂ */
    { "\xf0\x9f\x8f\x85",   80,  208, "sports medal" },  /* 🏅 */
    { "\xf0\x9f\x8f\x86",   96,  208, "trophy" },  /* 🏆 */
    { "\xf0\x9f\x8f\x87",  112,  208, "horse racing" },  /* 🏇 */
    { "\xf0\x9f\x8f\x88",  128,  208, "american football" },  /* 🏈 */
    { "\xf0\x9f\x8f\x89",  144,  208, "rugby football" },  /* 🏉 */
    { "\xf0\x9f\x8f\x8a\xe2\x80\x8d\xe2\x99\x80",  160,  208, "swimmer (woman)" },  /* 🏊‍♀ */
    { "\xf0\x9f\x8f\x8a\xe2\x80\x8d\xe2\x99\x82",  176,  208, "swimmer (man)" },  /* 🏊‍♂ */
    { "\xf0\x9f\x8f\x8b",  192,  208, "weight lifter" },  /* 🏋 */
    { "\xf0\x9f\x8f\x8c\xe2\x80\x8d\xe2\x99\x80",  208,  208, "golfer (woman)" },  /* 🏌‍♀ */
    { "\xf0\x9f\x8f\x8c\xe2\x80\x8d\xe2\x99\x82",  224,  208, "golfer (man)" },  /* 🏌‍♂ */
    { "\xf0\x9f\x8f\x8d",  240,  208, "racing motorcycle" },  /* 🏍 */
    { "\xf0\x9f\x8f\x8e",  256,  208, "racing car" },  /* 🏎 */
    { "\xf0\x9f\x8f\x8f",  272,  208, "cricket bat and ball" },  /* 🏏 */
    { "\xf0\x9f\x8f\x90",  288,  208, "volleyball" },  /* 🏐 */
    { "\xf0\x9f\x8f\x91",  304,  208, "field hockey stick and ball" },  /* 🏑 */
    { "\xf0\x9f\x8f\x92",  320,  208, "ice hockey stick and puck" },  /* 🏒 */
    { "\xf0\x9f\x8f\x93",  336,  208, "table tennis paddle and ball" },  /* 🏓 */
    { "\xf0\x9f\x8f\x95",  352,  208, "camping" },  /* 🏕 */
    { "\xf0\x9f\x8f\x96",  368,  208, "beach with umbrella" },  /* 🏖 */
    { "\xf0\x9f\x8f\x97",  384,  208, "building construction" },  /* 🏗 */
    { "\xf0\x9f\x8f\x98",  400,  208, "house buildings" },  /* 🏘 */
    { "\xf0\x9f\x8f\x99",  416,  208, "cityscape" },  /* 🏙 */
    { "\xf0\x9f\x8f\x9a",  432,  208, "derelict house building" },  /* 🏚 */
    { "\xf0\x9f\x8f\x9b",  448,  208, "classical building" },  /* 🏛 */
    { "\xf0\x9f\x8f\x9c",  464,  208, "desert" },  /* 🏜 */
    { "\xf0\x9f\x8f\x9d",    0,  224, "desert island" },  /* 🏝 */
    { "\xf0\x9f\x8f\x9e",   16,  224, "national park" },  /* 🏞 */
    { "\xf0\x9f\x8f\x9f",   32,  224, "stadium" },  /* 🏟 */
    { "\xf0\x9f\x8f\xa0",   48,  224, "house building" },  /* 🏠 */
    { "\xf0\x9f\x8f\xa1",   64,  224, "house with garden" },  /* 🏡 */
    { "\xf0\x9f\x8f\xa2",   80,  224, "office building" },  /* 🏢 */
    { "\xf0\x9f\x8f\xa3",   96,  224, "japanese post office" },  /* 🏣 */
    { "\xf0\x9f\x8f\xa4",  112,  224, "european post office" },  /* 🏤 */
    { "\xf0\x9f\x8f\xa5",  128,  224, "hospital" },  /* 🏥 */
    { "\xf0\x9f\x8f\xa6",  144,  224, "bank" },  /* 🏦 */
    { "\xf0\x9f\x8f\xa7",  160,  224, "automated teller machine" },  /* 🏧 */
    { "\xf0\x9f\x8f\xa8",  176,  224, "hotel" },  /* 🏨 */
    { "\xf0\x9f\x8f\xa9",  192,  224, "love hotel" },  /* 🏩 */
    { "\xf0\x9f\x8f\xaa",  208,  224, "convenience store" },  /* 🏪 */
    { "\xf0\x9f\x8f\xab",  224,  224, "school" },  /* 🏫 */
    { "\xf0\x9f\x8f\xac",  240,  224, "department store" },  /* 🏬 */
    { "\xf0\x9f\x8f\xad",  256,  224, "factory" },  /* 🏭 */
    { "\xf0\x9f\x8f\xae",  272,  224, "izakaya lantern" },  /* 🏮 */
    { "\xf0\x9f\x8f\xaf",  288,  224, "japanese castle" },  /* 🏯 */
    { "\xf0\x9f\x8f\xb0",  304,  224, "european castle" },  /* 🏰 */
    { "\xf0\x9f\x8f\xb5",  320,  224, "rosette" },  /* 🏵 */
    { "\xf0\x9f\x8f\xb7",  336,  224, "label" },  /* 🏷 */
    { "\xf0\x9f\x8f\xb8",  352,  224, "badminton racquet and shuttlecock" },  /* 🏸 */
    { "\xf0\x9f\x8f\xb9",  368,  224, "bow and arrow" },  /* 🏹 */
    { "\xf0\x9f\x8f\xba",  384,  224, "amphora" },  /* 🏺 */
    { "\xf0\x9f\x90\x80",  400,  224, "rat" },  /* 🐀 */
    { "\xf0\x9f\x90\x81",  416,  224, "mouse" },  /* 🐁 */
    { "\xf0\x9f\x90\x82",  432,  224, "ox" },  /* 🐂 */
    { "\xf0\x9f\x90\x83",  448,  224, "water buffalo" },  /* 🐃 */
    { "\xf0\x9f\x90\x84",  464,  224, "cow" },  /* 🐄 */
    { "\xf0\x9f\x90\x85",    0,  240, "tiger" },  /* 🐅 */
    { "\xf0\x9f\x90\x86",   16,  240, "leopard" },  /* 🐆 */
    { "\xf0\x9f\x90\x87",   32,  240, "rabbit" },  /* 🐇 */
    { "\xf0\x9f\x90\x88",   48,  240, "cat" },  /* 🐈 */
    { "\xf0\x9f\x90\x88\xe2\x80\x8d\xe2\xac\x9b",   64,  240, "cat black large square" },  /* 🐈‍⬛ */
    { "\xf0\x9f\x90\x89",   80,  240, "dragon" },  /* 🐉 */
    { "\xf0\x9f\x90\x8a",   96,  240, "crocodile" },  /* 🐊 */
    { "\xf0\x9f\x90\x8b",  112,  240, "whale" },  /* 🐋 */
    { "\xf0\x9f\x90\x8c",  128,  240, "snail" },  /* 🐌 */
    { "\xf0\x9f\x90\x8d",  144,  240, "snake" },  /* 🐍 */
    { "\xf0\x9f\x90\x8f",  160,  240, "ram" },  /* 🐏 */
    { "\xf0\x9f\x90\x90",  176,  240, "goat" },  /* 🐐 */
    { "\xf0\x9f\x90\x91",  192,  240, "sheep" },  /* 🐑 */
    { "\xf0\x9f\x90\x92",  208,  240, "monkey" },  /* 🐒 */
    { "\xf0\x9f\x90\x93",  224,  240, "rooster" },  /* 🐓 */
    { "\xf0\x9f\x90\x94",  240,  240, "chicken" },  /* 🐔 */
    { "\xf0\x9f\x90\x95",  256,  240, "dog" },  /* 🐕 */
    { "\xf0\x9f\x90\x95\xe2\x80\x8d\xf0\x9f\xa6\xba",  272,  240, "dog safety vest" },  /* 🐕‍🦺 */
    { "\xf0\x9f\x90\x96",  288,  240, "pig" },  /* 🐖 */
    { "\xf0\x9f\x90\x97",  304,  240, "boar" },  /* 🐗 */
    { "\xf0\x9f\x90\x98",  320,  240, "elephant" },  /* 🐘 */
    { "\xf0\x9f\x90\x99",  336,  240, "octopus" },  /* 🐙 */
    { "\xf0\x9f\x90\x9a",  352,  240, "spiral shell" },  /* 🐚 */
    { "\xf0\x9f\x90\x9b",  368,  240, "bug" },  /* 🐛 */
    { "\xf0\x9f\x90\x9c",  384,  240, "ant" },  /* 🐜 */
    { "\xf0\x9f\x90\x9d",  400,  240, "honeybee" },  /* 🐝 */
    { "\xf0\x9f\x90\x9e",  416,  240, "lady beetle" },  /* 🐞 */
    { "\xf0\x9f\x90\x9f",  432,  240, "fish" },  /* 🐟 */
    { "\xf0\x9f\x90\xa0",  448,  240, "tropical fish" },  /* 🐠 */
    { "\xf0\x9f\x90\xa1",  464,  240, "blowfish" },  /* 🐡 */
    { "\xf0\x9f\x90\xa2",    0,  256, "turtle" },  /* 🐢 */
    { "\xf0\x9f\x90\xa3",   16,  256, "hatching chick" },  /* 🐣 */
    { "\xf0\x9f\x90\xa4",   32,  256, "baby chick" },  /* 🐤 */
    { "\xf0\x9f\x90\xa5",   48,  256, "front-facing baby chick" },  /* 🐥 */
    { "\xf0\x9f\x90\xa6",   64,  256, "bird" },  /* 🐦 */
    { "\xf0\x9f\x90\xa7",   80,  256, "penguin" },  /* 🐧 */
    { "\xf0\x9f\x90\xa8",   96,  256, "koala" },  /* 🐨 */
    { "\xf0\x9f\x90\xa9",  112,  256, "poodle" },  /* 🐩 */
    { "\xf0\x9f\x90\xaa",  128,  256, "dromedary camel" },  /* 🐪 */
    { "\xf0\x9f\x90\xab",  144,  256, "bactrian camel" },  /* 🐫 */
    { "\xf0\x9f\x90\xac",  160,  256, "dolphin" },  /* 🐬 */
    { "\xf0\x9f\x90\xad",  176,  256, "mouse face" },  /* 🐭 */
    { "\xf0\x9f\x90\xae",  192,  256, "cow face" },  /* 🐮 */
    { "\xf0\x9f\x90\xaf",  208,  256, "tiger face" },  /* 🐯 */
    { "\xf0\x9f\x90\xb0",  224,  256, "rabbit face" },  /* 🐰 */
    { "\xf0\x9f\x90\xb1",  240,  256, "cat face" },  /* 🐱 */
    { "\xf0\x9f\x90\xb2",  256,  256, "dragon face" },  /* 🐲 */
    { "\xf0\x9f\x90\xb3",  272,  256, "spouting whale" },  /* 🐳 */
    { "\xf0\x9f\x90\xb4",  288,  256, "horse face" },  /* 🐴 */
    { "\xf0\x9f\x90\xb5",  304,  256, "monkey face" },  /* 🐵 */
    { "\xf0\x9f\x90\xb6",  320,  256, "dog face" },  /* 🐶 */
    { "\xf0\x9f\x90\xb7",  336,  256, "pig face" },  /* 🐷 */
    { "\xf0\x9f\x90\xb8",  352,  256, "frog face" },  /* 🐸 */
    { "\xf0\x9f\x90\xb9",  368,  256, "hamster face" },  /* 🐹 */
    { "\xf0\x9f\x90\xba",  384,  256, "wolf face" },  /* 🐺 */
    { "\xf0\x9f\x90\xbb",  400,  256, "bear face" },  /* 🐻 */
    { "\xf0\x9f\x90\xbb\xe2\x80\x8d\xe2\x9d\x84",  416,  256, "bear face snowflake" },  /* 🐻‍❄ */
    { "\xf0\x9f\x90\xbc",  432,  256, "panda face" },  /* 🐼 */
    { "\xf0\x9f\x90\xbd",  448,  256, "pig nose" },  /* 🐽 */
    { "\xf0\x9f\x90\xbe",  464,  256, "paw prints" },  /* 🐾 */
    { "\xf0\x9f\x90\xbf",    0,  272, "chipmunk" },  /* 🐿 */
    { "\xf0\x9f\x91\x80",   16,  272, "eyes" },  /* 👀 */
    { "\xf0\x9f\x91\x81",   32,  272, "eye" },  /* 👁 */
    { "\xf0\x9f\x91\x81\xe2\x80\x8d\xf0\x9f\x97\xa8",   48,  272, "eye left speech bubble" },  /* 👁‍🗨 */
    { "\xf0\x9f\x91\x82",   64,  272, "ear" },  /* 👂 */
    { "\xf0\x9f\x91\x83",   80,  272, "nose" },  /* 👃 */
    { "\xf0\x9f\x91\x84",   96,  272, "mouth" },  /* 👄 */
    { "\xf0\x9f\x91\x85",  112,  272, "tongue" },  /* 👅 */
    { "\xf0\x9f\x91\x86",  128,  272, "backhand point up" },  /* 👆 */
    { "\xf0\x9f\x91\x87",  144,  272, "backhand point down" },  /* 👇 */
    { "\xf0\x9f\x91\x88",  160,  272, "backhand point left" },  /* 👈 */
    { "\xf0\x9f\x91\x89",  176,  272, "backhand point right" },  /* 👉 */
    { "\xf0\x9f\x91\x8a",  192,  272, "fist" },  /* 👊 */
    { "\xf0\x9f\x91\x8b",  208,  272, "waving hand" },  /* 👋 */
    { "\xf0\x9f\x91\x8c",  224,  272, "OK hand" },  /* 👌 */
    { "\xf0\x9f\x91\x8d",  240,  272, "thumbs up" },  /* 👍 */
    { "\xf0\x9f\x91\x8e",  256,  272, "thumbs down" },  /* 👎 */
    { "\xf0\x9f\x91\x8f",  272,  272, "clapping hands" },  /* 👏 */
    { "\xf0\x9f\x91\x90",  288,  272, "open hands" },  /* 👐 */
    { "\xf0\x9f\x91\x91",  304,  272, "crown" },  /* 👑 */
    { "\xf0\x9f\x91\x92",  320,  272, "womans hat" },  /* 👒 */
    { "\xf0\x9f\x91\x93",  336,  272, "eyeglasses" },  /* 👓 */
    { "\xf0\x9f\x91\x94",  352,  272, "necktie" },  /* 👔 */
    { "\xf0\x9f\x91\x95",  368,  272, "t-shirt" },  /* 👕 */
    { "\xf0\x9f\x91\x96",  384,  272, "jeans" },  /* 👖 */
    { "\xf0\x9f\x91\x97",  400,  272, "dress" },  /* 👗 */
    { "\xf0\x9f\x91\x98",  416,  272, "kimono" },  /* 👘 */
    { "\xf0\x9f\x91\x99",  432,  272, "bikini" },  /* 👙 */
    { "\xf0\x9f\x91\x9a",  448,  272, "womans clothes" },  /* 👚 */
    { "\xf0\x9f\x91\x9b",  464,  272, "purse" },  /* 👛 */
    { "\xf0\x9f\x91\x9c",    0,  288, "handbag" },  /* 👜 */
    { "\xf0\x9f\x91\x9d",   16,  288, "pouch" },  /* 👝 */
    { "\xf0\x9f\x91\x9e",   32,  288, "mans shoe" },  /* 👞 */
    { "\xf0\x9f\x91\x9f",   48,  288, "athletic shoe" },  /* 👟 */
    { "\xf0\x9f\x91\xa0",   64,  288, "high-heeled shoe" },  /* 👠 */
    { "\xf0\x9f\x91\xa1",   80,  288, "womans sandal" },  /* 👡 */
    { "\xf0\x9f\x91\xa2",   96,  288, "womans boots" },  /* 👢 */
    { "\xf0\x9f\x91\xa3",  112,  288, "footprints" },  /* 👣 */
    { "\xf0\x9f\x91\xa4",  128,  288, "bust in silhouette" },  /* 👤 */
    { "\xf0\x9f\x91\xa5",  144,  288, "busts in silhouette" },  /* 👥 */
    { "\xf0\x9f\x91\xa6",  160,  288, "boy" },  /* 👦 */
    { "\xf0\x9f\x91\xa7",  176,  288, "girl" },  /* 👧 */
    { "\xf0\x9f\x91\xa8",  192,  288, "man" },  /* 👨 */
    { "\xf0\x9f\x91\xa8\xe2\x80\x8d\xe2\x9a\x95",  208,  288, "man staff of aesculapius" },  /* 👨‍⚕ */
    { "\xf0\x9f\x91\xa8\xe2\x80\x8d\xe2\x9a\x96",  224,  288, "man scales" },  /* 👨‍⚖ */
    { "\xf0\x9f\x91\xa8\xe2\x80\x8d\xe2\x9d\xa4\xe2\x80\x8d\xf0\x9f\x91\xa8",  240,  288, "couple with heart (men)" },  /* 👨‍❤‍👨 */
    { "\xf0\x9f\x91\xa8\xe2\x80\x8d\xe2\x9d\xa4\xe2\x80\x8d\xf0\x9f\x92\x8b\xe2\x80\x8d\xf0\x9f\x91\xa8",  256,  288, "kiss (men)" },  /* 👨‍❤‍💋‍👨 */
    { "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x8c\xbe",  272,  288, "man ear of rice" },  /* 👨‍🌾 */
    { "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x8d\xb3",  288,  288, "man cooking" },  /* 👨‍🍳 */
    { "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x8d\xbc",  304,  288, "man baby bottle" },  /* 👨‍🍼 */
    { "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x8e\x93",  320,  288, "man graduation cap" },  /* 👨‍🎓 */
    { "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x8e\xa8",  336,  288, "man artist palette" },  /* 👨‍🎨 */
    { "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x8f\xab",  352,  288, "man school" },  /* 👨‍🏫 */
    { "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x8f\xad",  368,  288, "man factory" },  /* 👨‍🏭 */
    { "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x92\xbb",  384,  288, "man personal computer" },  /* 👨‍💻 */
    { "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x92\xbc",  400,  288, "man briefcase" },  /* 👨‍💼 */
    { "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x94\xa7",  416,  288, "man wrench" },  /* 👨‍🔧 */
    { "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x94\xac",  432,  288, "man microscope" },  /* 👨‍🔬 */
    { "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x9a\x80",  448,  288, "man rocket" },  /* 👨‍🚀 */
    { "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x9a\x92",  464,  288, "man fire engine" },  /* 👨‍🚒 */
    { "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\xa6\xaf",    0,  304, "man probing cane" },  /* 👨‍🦯 */
    { "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\xa6\xb0",   16,  304, "man red hair" },  /* 👨‍🦰 */
    { "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\xa6\xb2",   32,  304, "man bald" },  /* 👨‍🦲 */
    { "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\xa6\xb3",   48,  304, "man white hair" },  /* 👨‍🦳 */
    { "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\xa6\xbc",   64,  304, "man motorized wheelchair" },  /* 👨‍🦼 */
    { "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\xa6\xbd",   80,  304, "man manual wheelchair" },  /* 👨‍🦽 */
    { "\xf0\x9f\x91\xa9",   96,  304, "woman" },  /* 👩 */
    { "\xf0\x9f\x91\xa9\xe2\x80\x8d\xe2\x9a\x95",  112,  304, "woman staff of aesculapius" },  /* 👩‍⚕ */
    { "\xf0\x9f\x91\xa9\xe2\x80\x8d\xe2\x9a\x96",  128,  304, "woman scales" },  /* 👩‍⚖ */
    { "\xf0\x9f\x91\xa9\xe2\x80\x8d\xe2\x9c\x88",  144,  304, "woman airplane" },  /* 👩‍✈ */
    { "\xf0\x9f\x91\xa9\xe2\x80\x8d\xe2\x9d\xa4\xe2\x80\x8d\xf0\x9f\x91\xa8",  160,  304, "couple with heart" },  /* 👩‍❤‍👨 */
    { "\xf0\x9f\x91\xa9\xe2\x80\x8d\xe2\x9d\xa4\xe2\x80\x8d\xf0\x9f\x91\xa9",  176,  304, "couple with heart (women)" },  /* 👩‍❤‍👩 */
    { "\xf0\x9f\x91\xa9\xe2\x80\x8d\xe2\x9d\xa4\xe2\x80\x8d\xf0\x9f\x92\x8b\xe2\x80\x8d\xf0\x9f\x91\xa8",  192,  304, "kiss" },  /* 👩‍❤‍💋‍👨 */
    { "\xf0\x9f\x91\xa9\xe2\x80\x8d\xe2\x9d\xa4\xe2\x80\x8d\xf0\x9f\x92\x8b\xe2\x80\x8d\xf0\x9f\x91\xa9",  208,  304, "kiss (women)" },  /* 👩‍❤‍💋‍👩 */
    { "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x8c\xbe",  224,  304, "woman ear of rice" },  /* 👩‍🌾 */
    { "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x8d\xb3",  240,  304, "woman cooking" },  /* 👩‍🍳 */
    { "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x8d\xbc",  256,  304, "woman baby bottle" },  /* 👩‍🍼 */
    { "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x8e\x93",  272,  304, "woman graduation cap" },  /* 👩‍🎓 */
    { "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x8e\xa8",  288,  304, "woman artist palette" },  /* 👩‍🎨 */
    { "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x8f\xab",  304,  304, "woman school" },  /* 👩‍🏫 */
    { "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x8f\xad",  320,  304, "woman factory" },  /* 👩‍🏭 */
    { "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb",  336,  304, "woman personal computer" },  /* 👩‍💻 */
    { "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbc",  352,  304, "woman briefcase" },  /* 👩‍💼 */
    { "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x94\xa7",  368,  304, "woman wrench" },  /* 👩‍🔧 */
    { "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x94\xac",  384,  304, "woman microscope" },  /* 👩‍🔬 */
    { "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x9a\x80",  400,  304, "woman rocket" },  /* 👩‍🚀 */
    { "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x9a\x92",  416,  304, "woman fire engine" },  /* 👩‍🚒 */
    { "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\xa6\xaf",  432,  304, "woman probing cane" },  /* 👩‍🦯 */
    { "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\xa6\xb0",  448,  304, "woman red hair" },  /* 👩‍🦰 */
    { "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\xa6\xb2",  464,  304, "woman bald" },  /* 👩‍🦲 */
    { "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\xa6\xb3",    0,  320, "woman white hair" },  /* 👩‍🦳 */
    { "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\xa6\xbc",   16,  320, "woman motorized wheelchair" },  /* 👩‍🦼 */
    { "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\xa6\xbd",   32,  320, "woman manual wheelchair" },  /* 👩‍🦽 */
    { "\xf0\x9f\x91\xab",   48,  320, "man and woman holding hands" },  /* 👫 */
    { "\xf0\x9f\x91\xac",   64,  320, "two men holding hands" },  /* 👬 */
    { "\xf0\x9f\x91\xad",   80,  320, "two women holding hands" },  /* 👭 */
    { "\xf0\x9f\x91\xae\xe2\x80\x8d\xe2\x99\x80",   96,  320, "police officer (woman)" },  /* 👮‍♀ */
    { "\xf0\x9f\x91\xae\xe2\x80\x8d\xe2\x99\x82",  112,  320, "police officer (man)" },  /* 👮‍♂ */
    { "\xf0\x9f\x91\xaf",  128,  320, "people with bunny ears" },  /* 👯 */
    { "\xf0\x9f\x91\xaf\xe2\x80\x8d\xe2\x99\x82",  144,  320, "woman with bunny ears (man)" },  /* 👯‍♂ */
    { "\xf0\x9f\x91\xb0",  160,  320, "person with veil" },  /* 👰 */
    { "\xf0\x9f\x91\xb0\xe2\x80\x8d\xe2\x99\x80",  176,  320, "bride with veil (woman)" },  /* 👰‍♀ */
    { "\xf0\x9f\x91\xb0\xe2\x80\x8d\xe2\x99\x82",  192,  320, "bride with veil (man)" },  /* 👰‍♂ */
    { "\xf0\x9f\x91\xb1\xe2\x80\x8d\xe2\x99\x80",  208,  320, "person with blond hair (woman)" },  /* 👱‍♀ */
    { "\xf0\x9f\x91\xb1\xe2\x80\x8d\xe2\x99\x82",  224,  320, "person with blond hair (man)" },  /* 👱‍♂ */
    { "\xf0\x9f\x91\xb2",  240,  320, "man with gua pi mao" },  /* 👲 */
    { "\xf0\x9f\x91\xb3\xe2\x80\x8d\xe2\x99\x80",  256,  320, "man with turban (woman)" },  /* 👳‍♀ */
    { "\xf0\x9f\x91\xb3\xe2\x80\x8d\xe2\x99\x82",  272,  320, "man with turban (man)" },  /* 👳‍♂ */
    { "\xf0\x9f\x91\xb4",  288,  320, "older man" },  /* 👴 */
    { "\xf0\x9f\x91\xb5",  304,  320, "older woman" },  /* 👵 */
    { "\xf0\x9f\x91\xb6",  320,  320, "baby" },  /* 👶 */
    { "\xf0\x9f\x91\xb7\xe2\x80\x8d\xe2\x99\x80",  336,  320, "construction worker (woman)" },  /* 👷‍♀ */
    { "\xf0\x9f\x91\xb7\xe2\x80\x8d\xe2\x99\x82",  352,  320, "construction worker (man)" },  /* 👷‍♂ */
    { "\xf0\x9f\x91\xb8",  368,  320, "princess" },  /* 👸 */
    { "\xf0\x9f\x91\xb9",  384,  320, "japanese ogre" },  /* 👹 */
    { "\xf0\x9f\x91\xba",  400,  320, "japanese goblin" },  /* 👺 */
    { "\xf0\x9f\x91\xbb",  416,  320, "ghost" },  /* 👻 */
    { "\xf0\x9f\x91\xbc",  432,  320, "baby angel" },  /* 👼 */
    { "\xf0\x9f\x91\xbd",  448,  320, "extraterrestrial alien" },  /* 👽 */
    { "\xf0\x9f\x91\xbe",  464,  320, "alien monster" },  /* 👾 */
    { "\xf0\x9f\x91\xbf",    0,  336, "imp" },  /* 👿 */
    { "\xf0\x9f\x92\x80",   16,  336, "skull" },  /* 💀 */
    { "\xf0\x9f\x92\x81",   32,  336, "information desk person" },  /* 💁 */
    { "\xf0\x9f\x92\x82\xe2\x80\x8d\xe2\x99\x80",   48,  336, "guardsman (woman)" },  /* 💂‍♀ */
    { "\xf0\x9f\x92\x82\xe2\x80\x8d\xe2\x99\x82",   64,  336, "guardsman (man)" },  /* 💂‍♂ */
    { "\xf0\x9f\x92\x83",   80,  336, "dancer" },  /* 💃 */
    { "\xf0\x9f\x92\x84",   96,  336, "lipstick" },  /* 💄 */
    { "\xf0\x9f\x92\x85",  112,  336, "nail polish" },  /* 💅 */
    { "\xf0\x9f\x92\x86\xe2\x80\x8d\xe2\x99\x80",  128,  336, "face massage (woman)" },  /* 💆‍♀ */
    { "\xf0\x9f\x92\x86\xe2\x80\x8d\xe2\x99\x82",  144,  336, "face massage (man)" },  /* 💆‍♂ */
    { "\xf0\x9f\x92\x87\xe2\x80\x8d\xe2\x99\x80",  160,  336, "haircut (woman)" },  /* 💇‍♀ */
    { "\xf0\x9f\x92\x87\xe2\x80\x8d\xe2\x99\x82",  176,  336, "haircut (man)" },  /* 💇‍♂ */
    { "\xf0\x9f\x92\x88",  192,  336, "barber pole" },  /* 💈 */
    { "\xf0\x9f\x92\x89",  208,  336, "syringe" },  /* 💉 */
    { "\xf0\x9f\x92\x8a",  224,  336, "pill" },  /* 💊 */
    { "\xf0\x9f\x92\x8b",  240,  336, "kiss mark" },  /* 💋 */
    { "\xf0\x9f\x92\x8c",  256,  336, "love letter" },  /* 💌 */
    { "\xf0\x9f\x92\x8d",  272,  336, "ring" },  /* 💍 */
    { "\xf0\x9f\x92\x8e",  288,  336, "gem stone" },  /* 💎 */
    { "\xf0\x9f\x92\x8f",  304,  336, "kiss" },  /* 💏 */
    { "\xf0\x9f\x92\x90",  320,  336, "bouquet" },  /* 💐 */
    { "\xf0\x9f\x92\x91",  336,  336, "couple with heart" },  /* 💑 */
    { "\xf0\x9f\x92\x92",  352,  336, "wedding" },  /* 💒 */
    { "\xf0\x9f\x92\x93",  368,  336, "beating heart" },  /* 💓 */
    { "\xf0\x9f\x92\x94",  384,  336, "broken heart" },  /* 💔 */
    { "\xf0\x9f\x92\x95",  400,  336, "two hearts" },  /* 💕 */
    { "\xf0\x9f\x92\x96",  416,  336, "sparkling heart" },  /* 💖 */
    { "\xf0\x9f\x92\x97",  432,  336, "growing heart" },  /* 💗 */
    { "\xf0\x9f\x92\x98",  448,  336, "heart with arrow" },  /* 💘 */
    { "\xf0\x9f\x92\x9a",  464,  336, "green heart" },  /* 💚 */
    { "\xf0\x9f\x92\x9b",    0,  352, "yellow heart" },  /* 💛 */
    { "\xf0\x9f\x92\x9c",   16,  352, "purple heart" },  /* 💜 */
    { "\xf0\x9f\x92\x9d",   32,  352, "heart with ribbon" },  /* 💝 */
    { "\xf0\x9f\x92\x9e",   48,  352, "revolving hearts" },  /* 💞 */
    { "\xf0\x9f\x92\x9f",   64,  352, "heart decoration" },  /* 💟 */
    { "\xf0\x9f\x92\xa0",   80,  352, "diamond shape with a dot inside" },  /* 💠 */
    { "\xf0\x9f\x92\xa1",   96,  352, "electric light bulb" },  /* 💡 */
    { "\xf0\x9f\x92\xa2",  112,  352, "anger" },  /* 💢 */
    { "\xf0\x9f\x92\xa3",  128,  352, "bomb" },  /* 💣 */
    { "\xf0\x9f\x92\xa4",  144,  352, "zzz" },  /* 💤 */
    { "\xf0\x9f\x92\xa5",  160,  352, "collision" },  /* 💥 */
    { "\xf0\x9f\x92\xa6",  176,  352, "sweat drops" },  /* 💦 */
    { "\xf0\x9f\x92\xa7",  192,  352, "droplet" },  /* 💧 */
    { "\xf0\x9f\x92\xa8",  208,  352, "dash" },  /* 💨 */
    { "\xf0\x9f\x92\xa9",  224,  352, "pile of poo" },  /* 💩 */
    { "\xf0\x9f\x92\xaa",  240,  352, "flexed biceps" },  /* 💪 */
    { "\xf0\x9f\x92\xab",  256,  352, "dizzy" },  /* 💫 */
    { "\xf0\x9f\x92\xac",  272,  352, "speech balloon" },  /* 💬 */
    { "\xf0\x9f\x92\xad",  288,  352, "thought balloon" },  /* 💭 */
    { "\xf0\x9f\x92\xae",  304,  352, "white flower" },  /* 💮 */
    { "\xf0\x9f\x92\xaf",  320,  352, "100" },  /* 💯 */
    { "\xf0\x9f\x92\xb0",  336,  352, "money bag" },  /* 💰 */
    { "\xf0\x9f\x92\xb1",  352,  352, "currency exchange" },  /* 💱 */
    { "\xf0\x9f\x92\xb2",  368,  352, "dollar" },  /* 💲 */
    { "\xf0\x9f\x92\xb3",  384,  352, "credit card" },  /* 💳 */
    { "\xf0\x9f\x92\xb4",  400,  352, "yen banknote" },  /* 💴 */
    { "\xf0\x9f\x92\xb5",  416,  352, "dollar banknote" },  /* 💵 */
    { "\xf0\x9f\x92\xb6",  432,  352, "euro banknote" },  /* 💶 */
    { "\xf0\x9f\x92\xb7",  448,  352, "pound banknote" },  /* 💷 */
    { "\xf0\x9f\x92\xb8",  464,  352, "money with wings" },  /* 💸 */
    { "\xf0\x9f\x92\xb9",    0,  368, "yen chart up" },  /* 💹 */
    { "\xf0\x9f\x92\xba",   16,  368, "seat" },  /* 💺 */
    { "\xf0\x9f\x92\xbb",   32,  368, "personal computer" },  /* 💻 */
    { "\xf0\x9f\x92\xbc",   48,  368, "briefcase" },  /* 💼 */
    { "\xf0\x9f\x92\xbd",   64,  368, "minidisc" },  /* 💽 */
    { "\xf0\x9f\x92\xbe",   80,  368, "floppy disk" },  /* 💾 */
    { "\xf0\x9f\x92\xbf",   96,  368, "optical disc" },  /* 💿 */
    { "\xf0\x9f\x93\x80",  112,  368, "dvd" },  /* 📀 */
    { "\xf0\x9f\x93\x81",  128,  368, "file folder" },  /* 📁 */
    { "\xf0\x9f\x93\x82",  144,  368, "open file folder" },  /* 📂 */
    { "\xf0\x9f\x93\x83",  160,  368, "page with curl" },  /* 📃 */
    { "\xf0\x9f\x93\x84",  176,  368, "page facing up" },  /* 📄 */
    { "\xf0\x9f\x93\x85",  192,  368, "calendar" },  /* 📅 */
    { "\xf0\x9f\x93\x86",  208,  368, "tear-off calendar" },  /* 📆 */
    { "\xf0\x9f\x93\x87",  224,  368, "card index" },  /* 📇 */
    { "\xf0\x9f\x93\x88",  240,  368, "chart up" },  /* 📈 */
    { "\xf0\x9f\x93\x89",  256,  368, "chart down" },  /* 📉 */
    { "\xf0\x9f\x93\x8a",  272,  368, "bar chart" },  /* 📊 */
    { "\xf0\x9f\x93\x8b",  288,  368, "clipboard" },  /* 📋 */
    { "\xf0\x9f\x93\x8c",  304,  368, "pushpin" },  /* 📌 */
    { "\xf0\x9f\x93\x8d",  320,  368, "round pushpin" },  /* 📍 */
    { "\xf0\x9f\x93\x8e",  336,  368, "paperclip" },  /* 📎 */
    { "\xf0\x9f\x93\x8f",  352,  368, "straight ruler" },  /* 📏 */
    { "\xf0\x9f\x93\x90",  368,  368, "triangular ruler" },  /* 📐 */
    { "\xf0\x9f\x93\x91",  384,  368, "bookmark tabs" },  /* 📑 */
    { "\xf0\x9f\x93\x92",  400,  368, "ledger" },  /* 📒 */
    { "\xf0\x9f\x93\x93",  416,  368, "notebook" },  /* 📓 */
    { "\xf0\x9f\x93\x94",  432,  368, "notebook with decorative cover" },  /* 📔 */
    { "\xf0\x9f\x93\x95",  448,  368, "closed book" },  /* 📕 */
    { "\xf0\x9f\x93\x96",  464,  368, "open book" },  /* 📖 */
    { "\xf0\x9f\x93\x97",    0,  384, "green book" },  /* 📗 */
    { "\xf0\x9f\x93\x98",   16,  384, "blue book" },  /* 📘 */
    { "\xf0\x9f\x93\x99",   32,  384, "orange book" },  /* 📙 */
    { "\xf0\x9f\x93\x9a",   48,  384, "books" },  /* 📚 */
    { "\xf0\x9f\x93\x9b",   64,  384, "name badge" },  /* 📛 */
    { "\xf0\x9f\x93\x9c",   80,  384, "scroll" },  /* 📜 */
    { "\xf0\x9f\x93\x9d",   96,  384, "memo" },  /* 📝 */
    { "\xf0\x9f\x93\x9e",  112,  384, "telephone receiver" },  /* 📞 */
    { "\xf0\x9f\x93\x9f",  128,  384, "pager" },  /* 📟 */
    { "\xf0\x9f\x93\xa0",  144,  384, "fax machine" },  /* 📠 */
    { "\xf0\x9f\x93\xa1",  160,  384, "satellite antenna" },  /* 📡 */
    { "\xf0\x9f\x93\xa2",  176,  384, "public address loudspeaker" },  /* 📢 */
    { "\xf0\x9f\x93\xa3",  192,  384, "cheering megaphone" },  /* 📣 */
    { "\xf0\x9f\x93\xa4",  208,  384, "outbox tray" },  /* 📤 */
    { "\xf0\x9f\x93\xa6",  224,  384, "package" },  /* 📦 */
    { "\xf0\x9f\x93\xa7",  240,  384, "email" },  /* 📧 */
    { "\xf0\x9f\x93\xa8",  256,  384, "incoming envelope" },  /* 📨 */
    { "\xf0\x9f\x93\xa9",  272,  384, "incoming envelope" },  /* 📩 */
    { "\xf0\x9f\x93\xaa",  288,  384, "closed mailbox with lowered flag" },  /* 📪 */
    { "\xf0\x9f\x93\xab",  304,  384, "closed mailbox with raised flag" },  /* 📫 */
    { "\xf0\x9f\x93\xac",  320,  384, "open mailbox with raised flag" },  /* 📬 */
    { "\xf0\x9f\x93\xad",  336,  384, "open mailbox with lowered flag" },  /* 📭 */
    { "\xf0\x9f\x93\xae",  352,  384, "postbox" },  /* 📮 */
    { "\xf0\x9f\x93\xaf",  368,  384, "postal horn" },  /* 📯 */
    { "\xf0\x9f\x93\xb0",  384,  384, "newspaper" },  /* 📰 */
    { "\xf0\x9f\x93\xb1",  400,  384, "mobile phone" },  /* 📱 */
    { "\xf0\x9f\x93\xb2",  416,  384, "incoming call" },  /* 📲 */
    { "\xf0\x9f\x93\xb3",  432,  384, "vibration mode" },  /* 📳 */
    { "\xf0\x9f\x93\xb4",  448,  384, "mobile phone off" },  /* 📴 */
    { "\xf0\x9f\x93\xb5",  464,  384, "no mobile phones" },  /* 📵 */
    { "\xf0\x9f\x93\xb6",    0,  400, "antenna with bars" },  /* 📶 */
    { "\xf0\x9f\x93\xb7",   16,  400, "camera" },  /* 📷 */
    { "\xf0\x9f\x93\xb8",   32,  400, "camera with flash" },  /* 📸 */
    { "\xf0\x9f\x93\xb9",   48,  400, "video camera" },  /* 📹 */
    { "\xf0\x9f\x93\xba",   64,  400, "television" },  /* 📺 */
    { "\xf0\x9f\x93\xbb",   80,  400, "radio" },  /* 📻 */
    { "\xf0\x9f\x93\xbc",   96,  400, "videocassette" },  /* 📼 */
    { "\xf0\x9f\x93\xbd",  112,  400, "film projector" },  /* 📽 */
    { "\xf0\x9f\x93\xbf",  128,  400, "prayer beads" },  /* 📿 */
    { "\xf0\x9f\x94\x80",  144,  400, "shuffle" },  /* 🔀 */
    { "\xf0\x9f\x94\x81",  160,  400, "repeat" },  /* 🔁 */
    { "\xf0\x9f\x94\x82",  176,  400, "repeat" },  /* 🔂 */
    { "\xf0\x9f\x94\x83",  192,  400, "arrows clockwise" },  /* 🔃 */
    { "\xf0\x9f\x94\x84",  208,  400, "arrows counterclockwise" },  /* 🔄 */
    { "\xf0\x9f\x94\x85",  224,  400, "dim" },  /* 🔅 */
    { "\xf0\x9f\x94\x86",  240,  400, "bright" },  /* 🔆 */
    { "\xf0\x9f\x94\x87",  256,  400, "speaker with cancellation stroke" },  /* 🔇 */
    { "\xf0\x9f\x94\x88",  272,  400, "speaker" },  /* 🔈 */
    { "\xf0\x9f\x94\x89",  288,  400, "speaker with one sound wave" },  /* 🔉 */
    { "\xf0\x9f\x94\x8a",  304,  400, "speaker with three sound waves" },  /* 🔊 */
    { "\xf0\x9f\x94\x8b",  320,  400, "battery" },  /* 🔋 */
    { "\xf0\x9f\x94\x8c",  336,  400, "electric plug" },  /* 🔌 */
    { "\xf0\x9f\x94\x8d",  352,  400, "search left" },  /* 🔍 */
    { "\xf0\x9f\x94\x8e",  368,  400, "search right" },  /* 🔎 */
    { "\xf0\x9f\x94\x8f",  384,  400, "lock with ink pen" },  /* 🔏 */
    { "\xf0\x9f\x94\x90",  400,  400, "closed lock with key" },  /* 🔐 */
    { "\xf0\x9f\x94\x91",  416,  400, "key" },  /* 🔑 */
    { "\xf0\x9f\x94\x92",  432,  400, "lock" },  /* 🔒 */
    { "\xf0\x9f\x94\x93",  448,  400, "open lock" },  /* 🔓 */
    { "\xf0\x9f\x94\x94",  464,  400, "bell" },  /* 🔔 */
    { "\xf0\x9f\x94\x95",    0,  416, "bell with cancellation stroke" },  /* 🔕 */
    { "\xf0\x9f\x94\x96",   16,  416, "bookmark" },  /* 🔖 */
    { "\xf0\x9f\x94\x97",   32,  416, "link" },  /* 🔗 */
    { "\xf0\x9f\x94\x98",   48,  416, "radio button" },  /* 🔘 */
    { "\xf0\x9f\x94\x99",   64,  416, "back" },  /* 🔙 */
    { "\xf0\x9f\x94\x9a",   80,  416, "end" },  /* 🔚 */
    { "\xf0\x9f\x94\x9b",   96,  416, "on with exclamation mark with left right arrow a" },  /* 🔛 */
    { "\xf0\x9f\x94\x9c",  112,  416, "soon" },  /* 🔜 */
    { "\xf0\x9f\x94\x9d",  128,  416, "top" },  /* 🔝 */
    { "\xf0\x9f\x94\x9e",  144,  416, "18+" },  /* 🔞 */
    { "\xf0\x9f\x94\x9f",  160,  416, "keycap ten" },  /* 🔟 */
    { "\xf0\x9f\x94\xa0",  176,  416, "capital letters" },  /* 🔠 */
    { "\xf0\x9f\x94\xa1",  192,  416, "lowercase letters" },  /* 🔡 */
    { "\xf0\x9f\x94\xa2",  208,  416, "numbers" },  /* 🔢 */
    { "\xf0\x9f\x94\xa3",  224,  416, "symbols" },  /* 🔣 */
    { "\xf0\x9f\x94\xa4",  240,  416, "letters" },  /* 🔤 */
    { "\xf0\x9f\x94\xa5",  256,  416, "fire" },  /* 🔥 */
    { "\xf0\x9f\x94\xa6",  272,  416, "electric torch" },  /* 🔦 */
    { "\xf0\x9f\x94\xa7",  288,  416, "wrench" },  /* 🔧 */
    { "\xf0\x9f\x94\xa8",  304,  416, "hammer" },  /* 🔨 */
    { "\xf0\x9f\x94\xa9",  320,  416, "nut and bolt" },  /* 🔩 */
    { "\xf0\x9f\x94\xaa",  336,  416, "hocho" },  /* 🔪 */
    { "\xf0\x9f\x94\xab",  352,  416, "pistol" },  /* 🔫 */
    { "\xf0\x9f\x94\xac",  368,  416, "microscope" },  /* 🔬 */
    { "\xf0\x9f\x94\xad",  384,  416, "telescope" },  /* 🔭 */
    { "\xf0\x9f\x94\xae",  400,  416, "crystal ball" },  /* 🔮 */
    { "\xf0\x9f\x94\xaf",  416,  416, "six pointed star with middle dot" },  /* 🔯 */
    { "\xf0\x9f\x94\xb0",  432,  416, "beginner" },  /* 🔰 */
    { "\xf0\x9f\x94\xb2",  448,  416, "black square button" },  /* 🔲 */
    { "\xf0\x9f\x94\xb3",  464,  416, "white square button" },  /* 🔳 */
    { "\xf0\x9f\x94\xb4",    0,  432, "large red circle" },  /* 🔴 */
    { "\xf0\x9f\x94\xb5",   16,  432, "large blue circle" },  /* 🔵 */
    { "\xf0\x9f\x94\xb6",   32,  432, "large orange diamond" },  /* 🔶 */
    { "\xf0\x9f\x94\xb7",   48,  432, "large blue diamond" },  /* 🔷 */
    { "\xf0\x9f\x94\xb8",   64,  432, "small orange diamond" },  /* 🔸 */
    { "\xf0\x9f\x94\xb9",   80,  432, "small blue diamond" },  /* 🔹 */
    { "\xf0\x9f\x94\xba",   96,  432, "up triangle" },  /* 🔺 */
    { "\xf0\x9f\x94\xbb",  112,  432, "down triangle" },  /* 🔻 */
    { "\xf0\x9f\x94\xbc",  128,  432, "small up triangle" },  /* 🔼 */
    { "\xf0\x9f\x94\xbd",  144,  432, "small down triangle" },  /* 🔽 */
    { "\xf0\x9f\x95\x89",  160,  432, "om" },  /* 🕉 */
    { "\xf0\x9f\x95\x8a",  176,  432, "dove of peace" },  /* 🕊 */
    { "\xf0\x9f\x95\x8b",  192,  432, "kaaba" },  /* 🕋 */
    { "\xf0\x9f\x95\x8c",  208,  432, "mosque" },  /* 🕌 */
    { "\xf0\x9f\x95\x8d",  224,  432, "synagogue" },  /* 🕍 */
    { "\xf0\x9f\x95\x8e",  240,  432, "menorah with nine branches" },  /* 🕎 */
    { "\xf0\x9f\x95\x90",  256,  432, "clock face one oclock" },  /* 🕐 */
    { "\xf0\x9f\x95\x91",  272,  432, "clock face two oclock" },  /* 🕑 */
    { "\xf0\x9f\x95\x92",  288,  432, "clock face three oclock" },  /* 🕒 */
    { "\xf0\x9f\x95\x93",  304,  432, "clock face four oclock" },  /* 🕓 */
    { "\xf0\x9f\x95\x94",  320,  432, "clock face five oclock" },  /* 🕔 */
    { "\xf0\x9f\x95\x95",  336,  432, "clock face six oclock" },  /* 🕕 */
    { "\xf0\x9f\x95\x96",  352,  432, "clock face seven oclock" },  /* 🕖 */
    { "\xf0\x9f\x95\x97",  368,  432, "clock face eight oclock" },  /* 🕗 */
    { "\xf0\x9f\x95\x98",  384,  432, "clock face nine oclock" },  /* 🕘 */
    { "\xf0\x9f\x95\x99",  400,  432, "clock face ten oclock" },  /* 🕙 */
    { "\xf0\x9f\x95\x9a",  416,  432, "clock face eleven oclock" },  /* 🕚 */
    { "\xf0\x9f\x95\x9b",  432,  432, "clock face twelve oclock" },  /* 🕛 */
    { "\xf0\x9f\x95\x9c",  448,  432, "clock face one-thirty" },  /* 🕜 */
    { "\xf0\x9f\x95\x9d",  464,  432, "clock face two-thirty" },  /* 🕝 */
    { "\xf0\x9f\x95\x9e",    0,  448, "clock face three-thirty" },  /* 🕞 */
    { "\xf0\x9f\x95\x9f",   16,  448, "clock face four-thirty" },  /* 🕟 */
    { "\xf0\x9f\x95\xa0",   32,  448, "clock face five-thirty" },  /* 🕠 */
    { "\xf0\x9f\x95\xa1",   48,  448, "clock face six-thirty" },  /* 🕡 */
    { "\xf0\x9f\x95\xa2",   64,  448, "clock face seven-thirty" },  /* 🕢 */
    { "\xf0\x9f\x95\xa3",   80,  448, "clock face eight-thirty" },  /* 🕣 */
    { "\xf0\x9f\x95\xa4",   96,  448, "clock face nine-thirty" },  /* 🕤 */
    { "\xf0\x9f\x95\xa5",  112,  448, "clock face ten-thirty" },  /* 🕥 */
    { "\xf0\x9f\x95\xa6",  128,  448, "clock face eleven-thirty" },  /* 🕦 */
    { "\xf0\x9f\x95\xa7",  144,  448, "clock face twelve-thirty" },  /* 🕧 */
    { "\xf0\x9f\x95\xaf",  160,  448, "candle" },  /* 🕯 */
    { "\xf0\x9f\x95\xb0",  176,  448, "mantelpiece clock" },  /* 🕰 */
    { "\xf0\x9f\x95\xb3",  192,  448, "hole" },  /* 🕳 */
    { "\xf0\x9f\x95\xb4",  208,  448, "man in business suit levitating" },  /* 🕴 */
    { "\xf0\x9f\x95\xb5",  224,  448, "detective" },  /* 🕵 */
    { "\xf0\x9f\x95\xb5\xe2\x80\x8d\xe2\x99\x80",  240,  448, "sleuth or spy (woman)" },  /* 🕵‍♀ */
    { "\xf0\x9f\x95\xb6",  256,  448, "dark sunglasses" },  /* 🕶 */
    { "\xf0\x9f\x95\xb7",  272,  448, "spider" },  /* 🕷 */
    { "\xf0\x9f\x95\xb8",  288,  448, "spider web" },  /* 🕸 */
    { "\xf0\x9f\x95\xb9",  304,  448, "joystick" },  /* 🕹 */
    { "\xf0\x9f\x95\xba",  320,  448, "man dancing" },  /* 🕺 */
    { "\xf0\x9f\x96\x87",  336,  448, "linked paperclips" },  /* 🖇 */
    { "\xf0\x9f\x96\x8a",  352,  448, "pen" },  /* 🖊 */
    { "\xf0\x9f\x96\x8b",  368,  448, "fountain pen" },  /* 🖋 */
    { "\xf0\x9f\x96\x8c",  384,  448, "paintbrush" },  /* 🖌 */
    { "\xf0\x9f\x96\x8d",  400,  448, "crayon" },  /* 🖍 */
    { "\xf0\x9f\x96\x90",  416,  448, "raised hand with fingers splayed" },  /* 🖐 */
    { "\xf0\x9f\x96\x95",  432,  448, "reversed hand with middle finger extended" },  /* 🖕 */
    { "\xf0\x9f\x96\x96",  448,  448, "raised hand with part between middle and ring fi" },  /* 🖖 */
    { "\xf0\x9f\x96\xa4",  464,  448, "black heart" },  /* 🖤 */
    { "\xf0\x9f\x96\xa5",    0,  464, "desktop computer" },  /* 🖥 */
    { "\xf0\x9f\x96\xa8",   16,  464, "printer" },  /* 🖨 */
    { "\xf0\x9f\x96\xb1",   32,  464, "three button mouse" },  /* 🖱 */
    { "\xf0\x9f\x96\xb2",   48,  464, "trackball" },  /* 🖲 */
    { "\xf0\x9f\x96\xbc",   64,  464, "frame with picture" },  /* 🖼 */
    { "\xf0\x9f\x97\x82",   80,  464, "card index dividers" },  /* 🗂 */
    { "\xf0\x9f\x97\x83",   96,  464, "card file box" },  /* 🗃 */
    { "\xf0\x9f\x97\x84",  112,  464, "file cabinet" },  /* 🗄 */
    { "\xf0\x9f\x97\x91",  128,  464, "wastebasket" },  /* 🗑 */
    { "\xf0\x9f\x97\x92",  144,  464, "spiral note pad" },  /* 🗒 */
    { "\xf0\x9f\x97\x93",  160,  464, "spiral calendar pad" },  /* 🗓 */
    { "\xf0\x9f\x97\x9c",  176,  464, "compression" },  /* 🗜 */
    { "\xf0\x9f\x97\x9d",  192,  464, "old key" },  /* 🗝 */
    { "\xf0\x9f\x97\x9e",  208,  464, "rolled-up newspaper" },  /* 🗞 */
    { "\xf0\x9f\x97\xa1",  224,  464, "dagger knife" },  /* 🗡 */
    { "\xf0\x9f\x97\xa3",  240,  464, "speaking head in silhouette" },  /* 🗣 */
    { "\xf0\x9f\x97\xaf",  256,  464, "right anger bubble" },  /* 🗯 */
    { "\xf0\x9f\x97\xb3",  272,  464, "ballot box with ballot" },  /* 🗳 */
    { "\xf0\x9f\x97\xba",  288,  464, "world map" },  /* 🗺 */
    { "\xf0\x9f\x97\xbc",  304,  464, "tokyo tower" },  /* 🗼 */
    { "\xf0\x9f\x97\xbd",  320,  464, "statue of liberty" },  /* 🗽 */
    { "\xf0\x9f\x97\xbe",  336,  464, "silhouette of japan" },  /* 🗾 */
    { "\xf0\x9f\x97\xbf",  352,  464, "moyai" },  /* 🗿 */
    { "\xf0\x9f\x98\x80",  368,  464, "grinning face" },  /* 😀 */
    { "\xf0\x9f\x98\x81",  384,  464, "grinning face with smiling eyes" },  /* 😁 */
    { "\xf0\x9f\x98\x82",  400,  464, "face with tears of joy" },  /* 😂 */
    { "\xf0\x9f\x98\x83",  416,  464, "smiling face with open mouth" },  /* 😃 */
    { "\xf0\x9f\x98\x84",  432,  464, "smiling face with open mouth and smiling eyes" },  /* 😄 */
    { "\xf0\x9f\x98\x85",  448,  464, "smiling face with open mouth and cold sweat" },  /* 😅 */
    { "\xf0\x9f\x98\x86",  464,  464, "smiling face with open mouth and tightly-closed " },  /* 😆 */
    { "\xf0\x9f\x98\x87",    0,  480, "smiling face with halo" },  /* 😇 */
    { "\xf0\x9f\x98\x88",   16,  480, "smiling face with horns" },  /* 😈 */
    { "\xf0\x9f\x98\x89",   32,  480, "winking face" },  /* 😉 */
    { "\xf0\x9f\x98\x8a",   48,  480, "smiling face with smiling eyes" },  /* 😊 */
    { "\xf0\x9f\x98\x8b",   64,  480, "face savouring delicious food" },  /* 😋 */
    { "\xf0\x9f\x98\x8c",   80,  480, "relieved face" },  /* 😌 */
    { "\xf0\x9f\x98\x8d",   96,  480, "smiling face with heart-shaped eyes" },  /* 😍 */
    { "\xf0\x9f\x98\x8f",  112,  480, "smirking face" },  /* 😏 */
    { "\xf0\x9f\x98\x90",  128,  480, "neutral face" },  /* 😐 */
    { "\xf0\x9f\x98\x91",  144,  480, "expressionless face" },  /* 😑 */
    { "\xf0\x9f\x98\x92",  160,  480, "unamused face" },  /* 😒 */
    { "\xf0\x9f\x98\x93",  176,  480, "face with cold sweat" },  /* 😓 */
    { "\xf0\x9f\x98\x94",  192,  480, "pensive face" },  /* 😔 */
    { "\xf0\x9f\x98\x95",  208,  480, "confused face" },  /* 😕 */
    { "\xf0\x9f\x98\x96",  224,  480, "confounded face" },  /* 😖 */
    { "\xf0\x9f\x98\x97",  240,  480, "kissing face" },  /* 😗 */
    { "\xf0\x9f\x98\x98",  256,  480, "face throwing a kiss" },  /* 😘 */
    { "\xf0\x9f\x98\x99",  272,  480, "kissing face with smiling eyes" },  /* 😙 */
    { "\xf0\x9f\x98\x9a",  288,  480, "kissing face with closed eyes" },  /* 😚 */
    { "\xf0\x9f\x98\x9b",  304,  480, "face with stuck-out tongue" },  /* 😛 */
    { "\xf0\x9f\x98\x9c",  320,  480, "face with stuck-out tongue and winking eye" },  /* 😜 */
    { "\xf0\x9f\x98\x9d",  336,  480, "face with stuck-out tongue and tightly-closed ey" },  /* 😝 */
    { "\xf0\x9f\x98\x9e",  352,  480, "disappointed face" },  /* 😞 */
    { "\xf0\x9f\x98\x9f",  368,  480, "worried face" },  /* 😟 */
    { "\xf0\x9f\x98\xa0",  384,  480, "angry face" },  /* 😠 */
    { "\xf0\x9f\x98\xa1",  400,  480, "pouting face" },  /* 😡 */
    { "\xf0\x9f\x98\xa2",  416,  480, "crying face" },  /* 😢 */
    { "\xf0\x9f\x98\xa4",  432,  480, "face with look of triumph" },  /* 😤 */
    { "\xf0\x9f\x98\xa5",  448,  480, "disappointed but relieved face" },  /* 😥 */
    { "\xf0\x9f\x98\xa6",  464,  480, "frowning face with open mouth" },  /* 😦 */
    { "\xf0\x9f\x98\xa7",    0,  496, "anguished face" },  /* 😧 */
    { "\xf0\x9f\x98\xa8",   16,  496, "fearful face" },  /* 😨 */
    { "\xf0\x9f\x98\xa9",   32,  496, "weary face" },  /* 😩 */
    { "\xf0\x9f\x98\xaa",   48,  496, "sleepy face" },  /* 😪 */
    { "\xf0\x9f\x98\xab",   64,  496, "tired face" },  /* 😫 */
    { "\xf0\x9f\x98\xac",   80,  496, "grimacing face" },  /* 😬 */
    { "\xf0\x9f\x98\xad",   96,  496, "loudly crying face" },  /* 😭 */
    { "\xf0\x9f\x98\xae",  112,  496, "face with open mouth" },  /* 😮 */
    { "\xf0\x9f\x98\xaf",  128,  496, "hushed face" },  /* 😯 */
    { "\xf0\x9f\x98\xb0",  144,  496, "face with open mouth and cold sweat" },  /* 😰 */
    { "\xf0\x9f\x98\xb1",  160,  496, "face screaming in fear" },  /* 😱 */
    { "\xf0\x9f\x98\xb2",  176,  496, "astonished face" },  /* 😲 */
    { "\xf0\x9f\x98\xb3",  192,  496, "flushed face" },  /* 😳 */
    { "\xf0\x9f\x98\xb4",  208,  496, "sleeping face" },  /* 😴 */
    { "\xf0\x9f\x98\xb5",  224,  496, "dizzy face" },  /* 😵 */
    { "\xf0\x9f\x98\xb6",  240,  496, "face without mouth" },  /* 😶 */
    { "\xf0\x9f\x98\xb6\xe2\x80\x8d\xf0\x9f\x8c\xab",  256,  496, "face without mouth fog" },  /* 😶‍🌫 */
    { "\xf0\x9f\x98\xb7",  272,  496, "face with medical mask" },  /* 😷 */
    { "\xf0\x9f\x98\xb8",  288,  496, "grinning cat face with smiling eyes" },  /* 😸 */
    { "\xf0\x9f\x98\xb9",  304,  496, "cat face with tears of joy" },  /* 😹 */
    { "\xf0\x9f\x98\xba",  320,  496, "smiling cat face with open mouth" },  /* 😺 */
    { "\xf0\x9f\x98\xbb",  336,  496, "smiling cat face with heart-shaped eyes" },  /* 😻 */
    { "\xf0\x9f\x98\xbc",  352,  496, "cat face with wry smile" },  /* 😼 */
    { "\xf0\x9f\x98\xbd",  368,  496, "kissing cat face with closed eyes" },  /* 😽 */
    { "\xf0\x9f\x98\xbe",  384,  496, "pouting cat face" },  /* 😾 */
    { "\xf0\x9f\x98\xbf",  400,  496, "crying cat face" },  /* 😿 */
    { "\xf0\x9f\x99\x80",  416,  496, "weary cat face" },  /* 🙀 */
    { "\xf0\x9f\x99\x81",  432,  496, "slightly frowning face" },  /* 🙁 */
    { "\xf0\x9f\x99\x82",  448,  496, "slightly smiling face" },  /* 🙂 */
    { "\xf0\x9f\x99\x83",  464,  496, "upside-down face" },  /* 🙃 */
    { "\xf0\x9f\x99\x85",    0,  512, "face with no good gesture" },  /* 🙅 */
    { "\xf0\x9f\x99\x86",   16,  512, "face with ok gesture" },  /* 🙆 */
    { "\xf0\x9f\x99\x87",   32,  512, "person bowing deeply" },  /* 🙇 */
    { "\xf0\x9f\x99\x88",   48,  512, "see-no-evil monkey" },  /* 🙈 */
    { "\xf0\x9f\x99\x89",   64,  512, "hear-no-evil monkey" },  /* 🙉 */
    { "\xf0\x9f\x99\x8a",   80,  512, "speak-no-evil monkey" },  /* 🙊 */
    { "\xf0\x9f\x99\x8b\xe2\x80\x8d\xe2\x99\x80",   96,  512, "happy person raising one hand (woman)" },  /* 🙋‍♀ */
    { "\xf0\x9f\x99\x8b\xe2\x80\x8d\xe2\x99\x82",  112,  512, "happy person raising one hand (man)" },  /* 🙋‍♂ */
    { "\xf0\x9f\x99\x8c",  128,  512, "person raising both hands in celebration" },  /* 🙌 */
    { "\xf0\x9f\x99\x8d\xe2\x80\x8d\xe2\x99\x80",  144,  512, "person frowning (woman)" },  /* 🙍‍♀ */
    { "\xf0\x9f\x99\x8d\xe2\x80\x8d\xe2\x99\x82",  160,  512, "person frowning (man)" },  /* 🙍‍♂ */
    { "\xf0\x9f\x99\x8e\xe2\x80\x8d\xe2\x99\x80",  176,  512, "person with pouting face (woman)" },  /* 🙎‍♀ */
    { "\xf0\x9f\x99\x8e\xe2\x80\x8d\xe2\x99\x82",  192,  512, "person with pouting face (man)" },  /* 🙎‍♂ */
    { "\xf0\x9f\x99\x8f",  208,  512, "person with folded hands" },  /* 🙏 */
    { "\xf0\x9f\x9a\x80",  224,  512, "rocket" },  /* 🚀 */
    { "\xf0\x9f\x9a\x81",  240,  512, "helicopter" },  /* 🚁 */
    { "\xf0\x9f\x9a\x82",  256,  512, "steam locomotive" },  /* 🚂 */
    { "\xf0\x9f\x9a\x83",  272,  512, "railway car" },  /* 🚃 */
    { "\xf0\x9f\x9a\x84",  288,  512, "high-speed train" },  /* 🚄 */
    { "\xf0\x9f\x9a\x85",  304,  512, "high-speed train with bullet nose" },  /* 🚅 */
    { "\xf0\x9f\x9a\x86",  320,  512, "train" },  /* 🚆 */
    { "\xf0\x9f\x9a\x87",  336,  512, "metro" },  /* 🚇 */
    { "\xf0\x9f\x9a\x88",  352,  512, "light rail" },  /* 🚈 */
    { "\xf0\x9f\x9a\x89",  368,  512, "station" },  /* 🚉 */
    { "\xf0\x9f\x9a\x8a",  384,  512, "tram" },  /* 🚊 */
    { "\xf0\x9f\x9a\x8b",  400,  512, "tram car" },  /* 🚋 */
    { "\xf0\x9f\x9a\x8c",  416,  512, "bus" },  /* 🚌 */
    { "\xf0\x9f\x9a\x8d",  432,  512, "oncoming bus" },  /* 🚍 */
    { "\xf0\x9f\x9a\x8e",  448,  512, "trolleybus" },  /* 🚎 */
    { "\xf0\x9f\x9a\x8f",  464,  512, "bus stop" },  /* 🚏 */
    { "\xf0\x9f\x9a\x90",    0,  528, "minibus" },  /* 🚐 */
    { "\xf0\x9f\x9a\x91",   16,  528, "ambulance" },  /* 🚑 */
    { "\xf0\x9f\x9a\x92",   32,  528, "fire engine" },  /* 🚒 */
    { "\xf0\x9f\x9a\x93",   48,  528, "police car" },  /* 🚓 */
    { "\xf0\x9f\x9a\x94",   64,  528, "oncoming police car" },  /* 🚔 */
    { "\xf0\x9f\x9a\x95",   80,  528, "taxi" },  /* 🚕 */
    { "\xf0\x9f\x9a\x96",   96,  528, "oncoming taxi" },  /* 🚖 */
    { "\xf0\x9f\x9a\x97",  112,  528, "automobile" },  /* 🚗 */
    { "\xf0\x9f\x9a\x98",  128,  528, "oncoming automobile" },  /* 🚘 */
    { "\xf0\x9f\x9a\x99",  144,  528, "recreational vehicle" },  /* 🚙 */
    { "\xf0\x9f\x9a\x9a",  160,  528, "delivery truck" },  /* 🚚 */
    { "\xf0\x9f\x9a\x9b",  176,  528, "articulated lorry" },  /* 🚛 */
    { "\xf0\x9f\x9a\x9c",  192,  528, "tractor" },  /* 🚜 */
    { "\xf0\x9f\x9a\x9d",  208,  528, "monorail" },  /* 🚝 */
    { "\xf0\x9f\x9a\x9e",  224,  528, "mountain railway" },  /* 🚞 */
    { "\xf0\x9f\x9a\x9f",  240,  528, "suspension railway" },  /* 🚟 */
    { "\xf0\x9f\x9a\xa0",  256,  528, "mountain cableway" },  /* 🚠 */
    { "\xf0\x9f\x9a\xa1",  272,  528, "aerial tramway" },  /* 🚡 */
    { "\xf0\x9f\x9a\xa2",  288,  528, "ship" },  /* 🚢 */
    { "\xf0\x9f\x9a\xa3\xe2\x80\x8d\xe2\x99\x80",  304,  528, "rowboat (woman)" },  /* 🚣‍♀ */
    { "\xf0\x9f\x9a\xa3\xe2\x80\x8d\xe2\x99\x82",  320,  528, "rowboat (man)" },  /* 🚣‍♂ */
    { "\xf0\x9f\x9a\xa4",  336,  528, "speedboat" },  /* 🚤 */
    { "\xf0\x9f\x9a\xa5",  352,  528, "horizontal traffic light" },  /* 🚥 */
    { "\xf0\x9f\x9a\xa6",  368,  528, "vertical traffic light" },  /* 🚦 */
    { "\xf0\x9f\x9a\xa7",  384,  528, "construction" },  /* 🚧 */
    { "\xf0\x9f\x9a\xa8",  400,  528, "police cars revolving light" },  /* 🚨 */
    { "\xf0\x9f\x9a\xaa",  416,  528, "door" },  /* 🚪 */
    { "\xf0\x9f\x9a\xab",  432,  528, "no entry" },  /* 🚫 */
    { "\xf0\x9f\x9a\xac",  448,  528, "smoking" },  /* 🚬 */
    { "\xf0\x9f\x9a\xad",  464,  528, "no smoking" },  /* 🚭 */
    { "\xf0\x9f\x9a\xae",    0,  544, "litter bin" },  /* 🚮 */
    { "\xf0\x9f\x9a\xaf",   16,  544, "no littering" },  /* 🚯 */
    { "\xf0\x9f\x9a\xb0",   32,  544, "potable water" },  /* 🚰 */
    { "\xf0\x9f\x9a\xb1",   48,  544, "non-potable water" },  /* 🚱 */
    { "\xf0\x9f\x9a\xb2",   64,  544, "bicycle" },  /* 🚲 */
    { "\xf0\x9f\x9a\xb3",   80,  544, "no bicycles" },  /* 🚳 */
    { "\xf0\x9f\x9a\xb4\xe2\x80\x8d\xe2\x99\x80",   96,  544, "bicyclist (woman)" },  /* 🚴‍♀ */
    { "\xf0\x9f\x9a\xb4\xe2\x80\x8d\xe2\x99\x82",  112,  544, "bicyclist (man)" },  /* 🚴‍♂ */
    { "\xf0\x9f\x9a\xb6\xe2\x80\x8d\xe2\x99\x80",  128,  544, "pedestrian (woman)" },  /* 🚶‍♀ */
    { "\xf0\x9f\x9a\xb6\xe2\x80\x8d\xe2\x99\x82",  144,  544, "pedestrian (man)" },  /* 🚶‍♂ */
    { "\xf0\x9f\x9a\xb7",  160,  544, "no pedestrians" },  /* 🚷 */
    { "\xf0\x9f\x9a\xb8",  176,  544, "children crossing" },  /* 🚸 */
    { "\xf0\x9f\x9a\xb9",  192,  544, "mens room" },  /* 🚹 */
    { "\xf0\x9f\x9a\xba",  208,  544, "womens room" },  /* 🚺 */
    { "\xf0\x9f\x9a\xbb",  224,  544, "restroom" },  /* 🚻 */
    { "\xf0\x9f\x9a\xbc",  240,  544, "baby" },  /* 🚼 */
    { "\xf0\x9f\x9a\xbd",  256,  544, "toilet" },  /* 🚽 */
    { "\xf0\x9f\x9a\xbe",  272,  544, "water closet" },  /* 🚾 */
    { "\xf0\x9f\x9a\xbf",  288,  544, "shower" },  /* 🚿 */
    { "\xf0\x9f\x9b\x80",  304,  544, "bath" },  /* 🛀 */
    { "\xf0\x9f\x9b\x81",  320,  544, "bathtub" },  /* 🛁 */
    { "\xf0\x9f\x9b\x82",  336,  544, "passport control" },  /* 🛂 */
    { "\xf0\x9f\x9b\x83",  352,  544, "customs" },  /* 🛃 */
    { "\xf0\x9f\x9b\x84",  368,  544, "baggage claim" },  /* 🛄 */
    { "\xf0\x9f\x9b\x85",  384,  544, "left luggage" },  /* 🛅 */
    { "\xf0\x9f\x9b\x8b",  400,  544, "couch and lamp" },  /* 🛋 */
    { "\xf0\x9f\x9b\x8c",  416,  544, "sleeping accommodation" },  /* 🛌 */
    { "\xf0\x9f\x9b\x8d",  432,  544, "shopping bags" },  /* 🛍 */
    { "\xf0\x9f\x9b\x8e",  448,  544, "bellhop bell" },  /* 🛎 */
    { "\xf0\x9f\x9b\x8f",  464,  544, "bed" },  /* 🛏 */
    { "\xf0\x9f\x9b\x90",    0,  560, "place of worship" },  /* 🛐 */
    { "\xf0\x9f\x9b\x91",   16,  560, "stop sign" },  /* 🛑 */
    { "\xf0\x9f\x9b\x92",   32,  560, "shopping trolley" },  /* 🛒 */
    { "\xf0\x9f\x9b\xa0",   48,  560, "hammer and wrench" },  /* 🛠 */
    { "\xf0\x9f\x9b\xa1",   64,  560, "shield" },  /* 🛡 */
    { "\xf0\x9f\x9b\xa2",   80,  560, "oil drum" },  /* 🛢 */
    { "\xf0\x9f\x9b\xa3",   96,  560, "motorway" },  /* 🛣 */
    { "\xf0\x9f\x9b\xa4",  112,  560, "railway track" },  /* 🛤 */
    { "\xf0\x9f\x9b\xa5",  128,  560, "motor boat" },  /* 🛥 */
    { "\xf0\x9f\x9b\xa9",  144,  560, "small airplane" },  /* 🛩 */
    { "\xf0\x9f\x9b\xab",  160,  560, "airplane departure" },  /* 🛫 */
    { "\xf0\x9f\x9b\xac",  176,  560, "airplane arriving" },  /* 🛬 */
    { "\xf0\x9f\x9b\xb0",  192,  560, "satellite" },  /* 🛰 */
    { "\xf0\x9f\x9b\xb3",  208,  560, "passenger ship" },  /* 🛳 */
    { "\xf0\x9f\x9b\xb4",  224,  560, "scooter" },  /* 🛴 */
    { "\xf0\x9f\x9b\xb5",  240,  560, "motor scooter" },  /* 🛵 */
    { "\xf0\x9f\x9b\xb6",  256,  560, "canoe" },  /* 🛶 */
    { "\xf0\x9f\xa4\x90",  272,  560, "zipper-mouth face" },  /* 🤐 */
    { "\xf0\x9f\xa4\x91",  288,  560, "money-mouth face" },  /* 🤑 */
    { "\xf0\x9f\xa4\x92",  304,  560, "face with thermometer" },  /* 🤒 */
    { "\xf0\x9f\xa4\x93",  320,  560, "nerd face" },  /* 🤓 */
    { "\xf0\x9f\xa4\x94",  336,  560, "thinking face" },  /* 🤔 */
    { "\xf0\x9f\xa4\x95",  352,  560, "face with head-bandage" },  /* 🤕 */
    { "\xf0\x9f\xa4\x96",  368,  560, "robot face" },  /* 🤖 */
    { "\xf0\x9f\xa4\x97",  384,  560, "hugging face" },  /* 🤗 */
    { "\xf0\x9f\xa4\x98",  400,  560, "sign of the horns" },  /* 🤘 */
    { "\xf0\x9f\xa4\x99",  416,  560, "call me hand" },  /* 🤙 */
    { "\xf0\x9f\xa4\x9a",  432,  560, "raised back of hand" },  /* 🤚 */
    { "\xf0\x9f\xa4\x9b",  448,  560, "left-facing fist" },  /* 🤛 */
    { "\xf0\x9f\xa4\x9c",  464,  560, "right-facing fist" },  /* 🤜 */
    { "\xf0\x9f\xa4\x9d",    0,  576, "handshake" },  /* 🤝 */
    { "\xf0\x9f\xa4\x9e",   16,  576, "hand with index and middle fingers crossed" },  /* 🤞 */
    { "\xf0\x9f\xa4\xa0",   32,  576, "face with cowboy hat" },  /* 🤠 */
    { "\xf0\x9f\xa4\xa1",   48,  576, "clown face" },  /* 🤡 */
    { "\xf0\x9f\xa4\xa2",   64,  576, "nauseated face" },  /* 🤢 */
    { "\xf0\x9f\xa4\xa3",   80,  576, "rolling on the floor laughing" },  /* 🤣 */
    { "\xf0\x9f\xa4\xa4",   96,  576, "drooling face" },  /* 🤤 */
    { "\xf0\x9f\xa4\xa5",  112,  576, "lying face" },  /* 🤥 */
    { "\xf0\x9f\xa4\xa6",  128,  576, "face palm" },  /* 🤦 */
    { "\xf0\x9f\xa4\xa7",  144,  576, "sneezing face" },  /* 🤧 */
    { "\xf0\x9f\xa4\xb0",  160,  576, "pregnant woman" },  /* 🤰 */
    { "\xf0\x9f\xa4\xb3",  176,  576, "selfie" },  /* 🤳 */
    { "\xf0\x9f\xa4\xb5",  192,  576, "person in tuxedo" },  /* 🤵 */
    { "\xf0\x9f\xa4\xb5\xe2\x80\x8d\xe2\x99\x80",  208,  576, "man in tuxedo (woman)" },  /* 🤵‍♀ */
    { "\xf0\x9f\xa4\xb5\xe2\x80\x8d\xe2\x99\x82",  224,  576, "man in tuxedo (man)" },  /* 🤵‍♂ */
    { "\xf0\x9f\xa4\xb6",  240,  576, "mother christmas" },  /* 🤶 */
    { "\xf0\x9f\xa4\xb7",  256,  576, "shrug" },  /* 🤷 */
    { "\xf0\x9f\xa4\xb7\xe2\x80\x8d\xe2\x99\x82",  272,  576, "shrug (man)" },  /* 🤷‍♂ */
    { "\xf0\x9f\xa4\xb8",  288,  576, "person cartwheeling" },  /* 🤸 */
    { "\xf0\x9f\xa4\xb8\xe2\x80\x8d\xe2\x99\x82",  304,  576, "person doing cartwheel (man)" },  /* 🤸‍♂ */
    { "\xf0\x9f\xa4\xb9\xe2\x80\x8d\xe2\x99\x80",  320,  576, "juggling (woman)" },  /* 🤹‍♀ */
    { "\xf0\x9f\xa4\xb9\xe2\x80\x8d\xe2\x99\x82",  336,  576, "juggling (man)" },  /* 🤹‍♂ */
    { "\xf0\x9f\xa4\xba",  352,  576, "fencer" },  /* 🤺 */
    { "\xf0\x9f\xa4\xbc",  368,  576, "wrestlers" },  /* 🤼 */
    { "\xf0\x9f\xa4\xbd\xe2\x80\x8d\xe2\x99\x80",  384,  576, "water polo (woman)" },  /* 🤽‍♀ */
    { "\xf0\x9f\xa4\xbd\xe2\x80\x8d\xe2\x99\x82",  400,  576, "water polo (man)" },  /* 🤽‍♂ */
    { "\xf0\x9f\xa4\xbe",  416,  576, "handball" },  /* 🤾 */
    { "\xf0\x9f\xa4\xbe\xe2\x80\x8d\xe2\x99\x80",  432,  576, "handball (woman)" },  /* 🤾‍♀ */
    { "\xf0\x9f\xa5\x80",  448,  576, "wilted flower" },  /* 🥀 */
    { "\xf0\x9f\xa5\x81",  464,  576, "drum with drumsticks" },  /* 🥁 */
    { "\xf0\x9f\xa5\x82\xf0\x9f\x8d\xb7",    0,  592, "clinking glasses wine glass" },  /* 🥂🍷 */
    { "\xf0\x9f\xa5\x83",   16,  592, "tumbler glass" },  /* 🥃 */
    { "\xf0\x9f\xa5\x84",   32,  592, "spoon" },  /* 🥄 */
    { "\xf0\x9f\xa5\x85",   48,  592, "goal net" },  /* 🥅 */
    { "\xf0\x9f\xa5\x87",   64,  592, "first place medal" },  /* 🥇 */
    { "\xf0\x9f\xa5\x88",   80,  592, "second place medal" },  /* 🥈 */
    { "\xf0\x9f\xa5\x89",   96,  592, "third place medal" },  /* 🥉 */
    { "\xf0\x9f\xa5\x8a",  112,  592, "boxing glove" },  /* 🥊 */
    { "\xf0\x9f\xa5\x8b",  128,  592, "martial arts uniform" },  /* 🥋 */
    { "\xf0\x9f\xa5\x90",  144,  592, "croissant" },  /* 🥐 */
    { "\xf0\x9f\xa5\x91",  160,  592, "avocado" },  /* 🥑 */
    { "\xf0\x9f\xa5\x92",  176,  592, "cucumber" },  /* 🥒 */
    { "\xf0\x9f\xa5\x93",  192,  592, "bacon" },  /* 🥓 */
    { "\xf0\x9f\xa5\x94",  208,  592, "potato" },  /* 🥔 */
    { "\xf0\x9f\xa5\x95",  224,  592, "carrot" },  /* 🥕 */
    { "\xf0\x9f\xa5\x96",  240,  592, "baguette bread" },  /* 🥖 */
    { "\xf0\x9f\xa5\x97",  256,  592, "green salad" },  /* 🥗 */
    { "\xf0\x9f\xa5\x98",  272,  592, "shallow pan of food" },  /* 🥘 */
    { "\xf0\x9f\xa5\x99",  288,  592, "stuffed flatbread" },  /* 🥙 */
    { "\xf0\x9f\xa5\x9a",  304,  592, "egg" },  /* 🥚 */
    { "\xf0\x9f\xa5\x9b",  320,  592, "glass of milk" },  /* 🥛 */
    { "\xf0\x9f\xa5\x9c",  336,  592, "peanuts" },  /* 🥜 */
    { "\xf0\x9f\xa5\x9d",  352,  592, "kiwifruit" },  /* 🥝 */
    { "\xf0\x9f\xa5\x9e",  368,  592, "pancakes" },  /* 🥞 */
    { "\xf0\x9f\xa6\x80",  384,  592, "crab" },  /* 🦀 */
    { "\xf0\x9f\xa6\x81",  400,  592, "lion face" },  /* 🦁 */
    { "\xf0\x9f\xa6\x82",  416,  592, "scorpion" },  /* 🦂 */
    { "\xf0\x9f\xa6\x83",  432,  592, "turkey" },  /* 🦃 */
    { "\xf0\x9f\xa6\x84",  448,  592, "unicorn face" },  /* 🦄 */
    { "\xf0\x9f\xa6\x85",  464,  592, "eagle" },  /* 🦅 */
    { "\xf0\x9f\xa6\x86",    0,  608, "duck" },  /* 🦆 */
    { "\xf0\x9f\xa6\x87",   16,  608, "bat" },  /* 🦇 */
    { "\xf0\x9f\xa6\x88",   32,  608, "shark" },  /* 🦈 */
    { "\xf0\x9f\xa6\x89",   48,  608, "owl" },  /* 🦉 */
    { "\xf0\x9f\xa6\x8a",   64,  608, "fox face" },  /* 🦊 */
    { "\xf0\x9f\xa6\x8b",   80,  608, "butterfly" },  /* 🦋 */
    { "\xf0\x9f\xa6\x8c",   96,  608, "deer" },  /* 🦌 */
    { "\xf0\x9f\xa6\x8d",  112,  608, "gorilla" },  /* 🦍 */
    { "\xf0\x9f\xa6\x8e",  128,  608, "lizard" },  /* 🦎 */
    { "\xf0\x9f\xa6\x8f",  144,  608, "rhinoceros" },  /* 🦏 */
    { "\xf0\x9f\xa6\x90",  160,  608, "shrimp" },  /* 🦐 */
    { "\xf0\x9f\xa6\x91",  176,  608, "squid" },  /* 🦑 */
    { "\xf0\x9f\xa7\x80",  192,  608, "cheese wedge" }  /* 🧀 */
};


const short EMOJI_DISPLAY_ORDER[EMOJI_COUNT] = {
     893,  896,  897,  894,  899,  898, 1085,  895,  958,  959,  902,  903,  900,  906,  916,  915,
     918,  917,  904,  919,  920,  921, 1068, 1074, 1071, 1067,  908,  909,  945,  946,  907,  910,
     935, 1087,  905,  912,  933, 1086,  943,  947, 1069, 1072, 1084, 1089,  944, 1082, 1070,  913,
     923,  957,   72,  937,  938,  941,  942,  929,  930,  931,  939,  928,  926,  936,  940,  914,
     922,  911,  932,  934,  927,  925,  924,  901,  630,  631,   64,  674, 1083,  624,  625,  626,
     628,  629, 1073,  950,  948,  949,  951,  952,  953,  956,  955,  954,  963,  964,  965,  646,
     658,  662,  656,  657,  653,  663,  655,  664,  154,  654,  155,  660,  659,  661,  869,  645,
     680,  667,  670,  676,  671,  673,  852,  677,  513,  886,  678,  669,  523, 1077,  866,  134,
     868,  524,  135, 1081, 1075, 1076,  520,  521,  518,  867,  519,   63,  525,  526,  133,  522,
    1078, 1079,  527,  968,  528, 1080,  973,  136,  637, 1091,  675,  514,  515,  511,  512,  517,
     516,  620,  550,  551,  552,  571,  573,  572,  576,  598,  600,  599,  613,  614,  618,  619,
     970,  969,  972,  971,  960,  961,  632,  967,  966,  962, 1088, 1096, 1097,  553,  577,  560,
     587,  562,  589,  554,  578,  557,  584,  558,  585,  566,  593,  563,  590,  565,  592,  567,
     594,  564,  591,  561,  588,  579,  568,  595,  569,  596,  607,  606,  854,  855,  634,  633,
     622,  621,  623,  617,  616,  615, 1092, 1094, 1093,  610,  612,  611, 1090,  586,  559,  627,
     336, 1095,  639,  638,  641,  640, 1029, 1028,  570,  597,  574,  601,  575,  602,  392,  391,
     635,  860,  853,  608,  609, 1102,  397,  123,  390,  404,  403,  394,  393, 1010, 1009,  401,
     400,  126,  125,  402, 1027, 1026, 1098, 1099, 1103, 1105, 1104, 1106, 1107, 1101, 1100, 1039,
    1046,  605,  603,  604,  649,  582,  556,  583,  651,  580,  555,  581,  885,  548,  549,  547,
     499,  463, 1147,  500,  466,  467,  487,  504, 1144,  495,  453,  454, 1135,  493,  450,  451,
     498, 1138, 1146,  492,  447,  448,  449,  501,  468,  469,  508,  460,  462,  461,  488,  489,
     470, 1149,  491,  446,  445,  503,  494,  452,  510, 1141,  505,  506,  486,  507,  509, 1137,
     465,  464,  481,  482,  483,  484,  485,  821, 1139, 1140, 1143,  502,  456,  480, 1148,  459,
     496,  455,  497,  457,  490,  477,  478,  479, 1142,  471,  472,  458, 1145,  473,  474,  475,
     476,  857,  858, 1136,  650,  679,  440,  261, 1108,  262,  263,  264,  260,  254,  255,  256,
     257,  258,  266,  267,   62,  268,  269,  270,  271,  272,  275,  276,  277,  278,  279,  280,
     281,  282,  283,  284,  285,  286,  287, 1132,  273, 1120,  274, 1123, 1124,  265,  259, 1121,
    1131,  253,  298, 1119, 1125, 1133, 1152,  290,  291, 1122,  288,  299,  289,  250,  251,  252,
    1128, 1129,  319, 1127,  318, 1126,  330,  317,  292,  293,  294,  295,  296,  297,  300,  302,
     303,  304,  305,  301, 1134, 1150, 1151,  306,  307,  308,  309,  310,  333,  316,  311,  312,
     313,  314,  315,  327, 1130,   61,  321,  322,  329,  323,  325,  326, 1111,  328,  320, 1112,
     801,  444,  220,  221,  222,  223,  888,  891,  218,  412,  413,  419,  420,  421,  422,  418,
     414,  415,  417,  423,  424,  425,  426,  427,  428,  429,  431,  432,  433,  434,  435,  436,
     438,  439,  652,  889,  890,  117,  823,  824,  116,  822,  119,  127,  208,  210,  416,  211,
     212,  213,  214,  216,   89,  357,  358,  359,  642,  367,  976,  977,  978,  979,  980,  981,
     982,  983,  984, 1003, 1004,  985,  986,  987,  988,  990,  991,  992,  993,  994,  995,  996,
     997,  998,  999, 1000, 1001, 1002,  406,  405, 1065, 1024, 1064,  989, 1056, 1057, 1055,  128,
    1015, 1012, 1013, 1051, 1014,   93,  122, 1066, 1011, 1063,  121, 1058, 1008,  131, 1059, 1060,
    1061,  691,  975, 1005, 1006, 1007, 1062,  974, 1048,   27,   40,   26,   37,   38,   39,  851,
     837,  849,  826,  838,  827,  839,  828,  840,  829,  841,  830,  842,  831,  843,  832,  844,
     833,  845,  834,  846,  835,  847,  836,  848,  224,  225,  226,  227,  228,  229,  230,  231,
     232,  233,  234,  235,  240,   53,  236,  237,  169,  238,  239,  219,   54,  109,  110,  241,
     242,  243,  244,  245,  246,  247,  248,  249,  207,  215,  209,   55,   60,  118,  146,   56,
     108,   57,  796,  672,  217,  334,  335,  337,  338,  143,  339,  340,  341,  342,  343,  344,
     345,  346,  347,  331,  332,  351,  356,  368,  350,  396,  395, 1114, 1115, 1116,  106,  107,
     389,  408,  398,  399,  387,  376,  407,  409,  410,  411,  442, 1117, 1118, 1113,  120,  124,
     360,  386,  388,  372,  802,  374,  805,  371,  859,  373,  375,   85,   87,   88,   86,  176,
     175,  377,  370,  874,  365,  531,  856,  532,  533,  534,  535,  536,  537,  538,  539,  540,
     541, 1047,  348,  542,  543,  544,  545,  546,  529,  530,  366,  349,  113,  758,  636,  647,
     648,  766,  767,  768,  769,  731,  732,  743,  779,  780,  385,  378,  379,  352,  353,  354,
     361,  364,  755,  380,  381,  382,  383,  384, 1109,  745,  746,   58,  727,  728,  729,  770,
     771,  692,  870,  871,   28,  872,  873,  694,  695,  696,  697,  362,  355,  757,  369,  754,
     751,  752,  753,  756,  772,  773,  850,  666,  797,  437,  717,  718,  719,  720,  721,  722,
     723,  716,  715,  700,  725,  701,  744,  883,  714,  781,  441,  681,  685,  686,  687,  688,
     689,  684,  690,  132,  735,  736,  737,  733,  734,  739,  738,  740,  741,  742,  887,  137,
     138,  863,  862,  864,  865,  726,  693,  698,  699,  875,  702,  703,  879,  880,  704,  705,
     706,  707,  708,  709,  710,  711,  861,  712,  713,  129,  876,  877,  878,  777,  778,  774,
     775,  776,  882,  799,  112,   92, 1053,  884,   94,  668,  443, 1054,  798,  800,   97,  881,
      95,  782,  114,   96,  803,  804,  730,  643,  644, 1016, 1049, 1045, 1036, 1038, 1040, 1052,
    1018,  104,  105,  892,  430, 1020, 1022,   91, 1032, 1033, 1034, 1035, 1037, 1041, 1042, 1043,
    1044,  100, 1031,  115, 1017, 1025, 1019, 1021, 1023, 1030,  749,  789,   65,   66,  165,   21,
     159,   22,  166,   23,  164,   20,   19,   18,   24,   25,  162,  163,  762,  763,  784,  785,
     786,  787,  788, 1050,   98,  820,  142,   71,   70,  141,   67,   68,   69,  825,  806,   73,
      74,   75,   76,   77,   78,   79,   80,   81,   82,   83,   84,  111,  759,  760,  761,   47,
      30,   34,   36,   48,   31,   35,  818,   32,  819,   33,   41,   42,   43,   29,  363,  764,
     765,  750,  747,  748,  101,  140,  156,  157,  158,   14,   15,  150,  151,  152,  153,  171,
     682,  683,   90,   99,  724,  807,  170,  130,   59,  139,  148,  149,  160,  161,  172,  144,
     145,  147,   12,   13,   16,    0,    1,    2,    3,    4,    5,    6,    7,    8,    9,   10,
      11,  790,  791,  792,  793,  794,  795,  177,  181,  178,  182,  183,  184,   17,  185,   44,
     186,  187,  179,  188,  180,  189,  190,  191,  192,  193,  201,  200,  195,  205,  203,  194,
     196,  206,  202,  198,  197,  173,  174,  204,  199,  810,  811,  103,  102,  167,  168,   50,
      49,   52,   51,   45,   46,  812,  813,  814,  815,  816,  817,  665,  783,  809,  808,  324,
    1110,    8,    0,    1,    1,    8,    8,    0,    0,    1,    1,    8,    8,    0,    8,    8,
       8,    8,    0,    0,    8,    0,    0,    0,    0,    0,   80,    1,    0,    0,    0,    2,
       0,    0,    0,    3,    4,    0,   32,    8,    8,    0,   32,    8,    0,    8,   32,    8,
       0,    8,    8,    8,    0,   32,  198,    2,   80,    8,   32,    8,    0,    0,    3,    2,
      80,    8,    0,    0,    0,    2,    0,    2,    1,    0,   80,    2,    0,    8,    1,    0,
       8,    8,    8,    0,    8
};

const EmojiSprite *emoji_find(const char *utf8) {
    int lo = 0, hi = EMOJI_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        int cmp = strcmp(EMOJI_TABLE[mid].utf8, utf8);
        if (cmp == 0) return &EMOJI_TABLE[mid];
        if (cmp < 0)  lo = mid + 1;
        else          hi = mid - 1;
    }
    return NULL;
}

/* Returns the byte length of the first UTF-8 codepoint at utf8.
 * Returns 0 if utf8 is NULL or the first byte is NUL. */
size_t utf8_first_codepoint_len(const char *utf8)
{
    if (!utf8) return 0;
    unsigned char c0 = (unsigned char)utf8[0];
    if (!c0)       return 0;
    if (c0 < 0x80) return 1;
    if (c0 < 0xE0) return 2;
    if (c0 < 0xF0) return 3;
    return 4;
}

/* Like emoji_find but accepts a non-null-terminated string of exactly len bytes.
 * Strip U+FE0F before calling.  Returns NULL if not found or len >= 32. */
const EmojiSprite *emoji_find_exact(const char *utf8, size_t len)
{
    if (!utf8 || len == 0 || len >= 32) return NULL;
    /* Fast path: byte after len is already NUL — no copy needed */
    if (utf8[len] == '\0') return emoji_find(utf8);
    char buf[32];
    memcpy(buf, utf8, len);
    buf[len] = '\0';
    return emoji_find(buf);
}

/* Tries s, then s cut back at each ZWJ from the end:
   👩‍❤‍💋‍👨 → 👩‍❤‍💋 → 👩‍❤ → 👩.  s is len bytes, not NUL-terminated. */
static const EmojiSprite *find_trimmed(const char *s, size_t len)
{
    char buf[32];
    if (len == 0 || len >= sizeof(buf)) return NULL;
    memcpy(buf, s, len);
    buf[len] = '\0';
    for (;;) {
        const EmojiSprite *es = emoji_find(buf);
        if (es) return es;
        size_t zwj = 0;
        int found = 0;
        for (size_t i = 0; i + 2 < len; i++) {
            if ((unsigned char)buf[i]   == 0xE2 &&
                (unsigned char)buf[i+1] == 0x80 &&
                (unsigned char)buf[i+2] == 0x8D) {
                zwj = i;
                found = 1;
            }
        }
        if (!found) return NULL;
        buf[zwj] = '\0';
        len = zwj;
    }
}

/* find_trimmed on head followed by rest. */
static const EmojiSprite *find_joined(const char *head, size_t headLen,
                                      const char *rest, size_t restLen)
{
    char buf[32];
    if (headLen + restLen >= sizeof(buf)) return NULL;
    memcpy(buf, head, headLen);
    memcpy(buf + headLen, rest, restLen);
    return find_trimmed(buf, headLen + restLen);
}

const EmojiSprite *emoji_find_fallback(const char *utf8) {
    const EmojiSprite *es = emoji_find(utf8);
    if (es) return es;

    size_t total = strlen(utf8);
    if (total == 0 || total >= 32) return NULL;

    /* Drop skin-tone modifiers (U+1F3FB-U+1F3FF = F0 9F 8F BB-BF): the
       sheet has none, and 👩🏽‍💻 should still find 👩‍💻. */
    char buf[32];
    size_t len = 0;
    for (size_t i = 0; i < total; i++) {
        if (i + 3 < total &&
            (unsigned char)utf8[i]   == 0xF0 && (unsigned char)utf8[i+1] == 0x9F &&
            (unsigned char)utf8[i+2] == 0x8F && (unsigned char)utf8[i+3] >= 0xBB &&
            (unsigned char)utf8[i+3] <= 0xBF) {
            i += 3;
            continue;
        }
        buf[len++] = utf8[i];
    }
    buf[len] = '\0';

    es = find_trimmed(buf, len);
    if (es) return es;

    /* First codepoint only: drops any remaining compound part. */
    size_t first = utf8_first_codepoint_len(buf);
    if (first == 0 || first > len) return NULL;
    es = emoji_find_exact(buf, first);
    if (es) return es;

    /* Newer than the sheet: show a look-alike (🥶 → 😬).  Keep the rest of
       the sequence, first on the look-alike's first codepoint, then on all
       of it, so 🧑‍💻 finds 👨‍💻 and 🧍‍♀ (look-alike 🚶‍♂) finds 🚶‍♀. */
    unsigned char c0 = (unsigned char)buf[0];
    unsigned long cp = first == 1 ? c0
                     : first == 2 ? (c0 & 0x1FUL)
                     : first == 3 ? (c0 & 0x0FUL) : (c0 & 0x07UL);
    for (size_t i = 1; i < first; i++)
        cp = (cp << 6) | ((unsigned char)buf[i] & 0x3FUL);
    const EmojiSprite *look = emoji_alias(cp);
    if (!look) return NULL;
    size_t lookFirst = utf8_first_codepoint_len(look->utf8);
    es = find_joined(look->utf8, lookFirst, buf + first, len - first);
    if (es) return es;
    es = find_joined(look->utf8, strlen(look->utf8), buf + first, len - first);
    return es ? es : look;
}
