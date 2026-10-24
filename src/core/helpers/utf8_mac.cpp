/* utf8_mac.cpp — UTF-8 → Mac Roman conversion and the JSON text writers.
   Extracted from main.cpp; no app state; no Mac Toolbox dependencies.  */
#include <string.h>
#include <stdint.h>
#include <string>

#include "utf8_mac.h"   /* kEmojiSentinel (via utf8_mac_lite.h) */

using std::string;

/* ------------------------------------------------------------------ */
/* StripVariationSelectors                                             */
/* ------------------------------------------------------------------ */
const char* StripVariationSelectors(const char* s, char* buf, size_t bufMax) {
    bool hasVS = false;
    for (size_t i = 0; s[i] && i+2 < bufMax; i++)
        if ((unsigned char)s[i]==0xef && (unsigned char)s[i+1]==0xb8 && (unsigned char)s[i+2]==0x8f) { hasVS=true; break; }
    if (!hasVS) return s;
    size_t di = 0;
    for (size_t i = 0; s[i] && di+2 < bufMax; ) {
        if ((unsigned char)s[i]==0xef && s[i+1] && (unsigned char)s[i+1]==0xb8 && s[i+2] && (unsigned char)s[i+2]==0x8f) { i += 3; }
        else { buf[di++] = s[i++]; }
    }
    buf[di] = '\0';
    return buf;
}

/* ------------------------------------------------------------------ */
/* Mac Roman ↔ Unicode table                                           */
/* kMrToUnicode[i] maps Mac Roman byte (i + 0x80) → Unicode codepoint.*/
/* Used directly for decode; scanned linearly for encode (inverse).    */
/* ------------------------------------------------------------------ */
static const unsigned short kMrToUnicode[128] = {
    /* 0x80 */ 0x00C4,0x00C5,0x00C7,0x00C9,0x00D1,0x00D6,0x00DC,0x00E1,
    /* 0x88 */ 0x00E0,0x00E2,0x00E4,0x00E3,0x00E5,0x00E7,0x00E9,0x00E8,
    /* 0x90 */ 0x00EA,0x00EB,0x00ED,0x00EC,0x00EE,0x00EF,0x00F1,0x00F3,
    /* 0x98 */ 0x00F2,0x00F4,0x00F6,0x00F5,0x00FA,0x00F9,0x00FB,0x00FC,
    /* 0xA0 */ 0x2020,0x00B0,0x00A2,0x00A3,0x00A7,0x2022,0x00B6,0x00DF,
    /* 0xA8 */ 0x00AE,0x00A9,0x2122,0x00B4,0x00A8,0x2260,0x00C6,0x00D8,
    /* 0xB0 */ 0x221E,0x00B1,0x2264,0x2265,0x00A5,0x00B5,0x2202,0x2211,
    /* 0xB8 */ 0x220F,0x03C0,0x222B,0x00AA,0x00BA,0x03A9,0x00E6,0x00F8,
    /* 0xC0 */ 0x00BF,0x00A1,0x00AC,0x221A,0x0192,0x2248,0x2206,0x00AB,
    /* 0xC8 */ 0x00BB,0x2026,0x00A0,0x00C0,0x00C3,0x00D5,0x0152,0x0153,
    /* 0xD0 */ 0x2013,0x2014,0x201C,0x201D,0x2018,0x2019,0x00F7,0x25CA,
    /* 0xD8 */ 0x00FF,0x0178,0x2044,0x20AC,0x2039,0x203A,0xFB01,0xFB02,
    /* 0xE0 */ 0x2021,0x00B7,0x201A,0x201E,0x2030,0x00C2,0x00CA,0x00C1,
    /* 0xE8 */ 0x00CB,0x00C8,0x00CD,0x00CE,0x00CF,0x00CC,0x00D3,0x00D4,
    /* 0xF0 */ 0x0000,0x00D2,0x00DA,0x00DB,0x00D9,0x0131,0x02C6,0x02DC,
    /* 0xF8 */ 0x00AF,0x02D8,0x02D9,0x02DA,0x00B8,0x02DD,0x02DB,0x02C7
};

/* ------------------------------------------------------------------ */
/* Special Unicode codepoints involved in emoji sequences             */
/* ------------------------------------------------------------------ */
static const unsigned int kCP_ZWJ      = 0x200D;  /* Zero-Width Joiner        */
static const unsigned int kCP_Keycap   = 0x20E3;  /* Combining Enclosing Keycap */
static const unsigned int kCP_VS_First = 0xFE00;  /* Variation Selector-1     */
static const unsigned int kCP_VS_Last  = 0xFE0F;  /* Variation Selector-16    */

/* Returns true if cp should be appended to the preceding emoji
   sequence rather than emitted as a new character or sentinel. */
static inline bool isEmojiCombiner(unsigned int cp) {
    return cp == kCP_ZWJ ||
           (cp >= kCP_VS_First && cp <= kCP_VS_Last) ||
           cp == kCP_Keycap;
}

/* ------------------------------------------------------------------ */
/* Unicode → Mac Roman: two-stage lookup.                             */
/* kLatin1ToMR[96]: direct for U+00A0–U+00FF (O(1), covers ~80/127). */
/* kExtToMR[47]:    sorted pairs for above-Latin1 (O(log 47) ≈ 6).   */
/* Derived from kMrToUnicode — do not edit independently.             */
/* ------------------------------------------------------------------ */
static const unsigned char kLatin1ToMR[96] = {
    /* U+00A0 */ 0xCA, 0xC1, 0xA2, 0xA3, 0x00, 0xB4, 0x00, 0xA4,
    /* U+00A8 */ 0xAC, 0xA9, 0xBB, 0xC7, 0xC2, 0x00, 0xA8, 0xF8,
    /* U+00B0 */ 0xA1, 0xB1, 0x00, 0x00, 0xAB, 0xB5, 0xA6, 0xE1,
    /* U+00B8 */ 0xFC, 0x00, 0xBC, 0xC8, 0x00, 0x00, 0x00, 0xC0,
    /* U+00C0 */ 0xCB, 0xE7, 0xE5, 0xCC, 0x80, 0x81, 0xAE, 0x82,
    /* U+00C8 */ 0xE9, 0x83, 0xE6, 0xE8, 0xED, 0xEA, 0xEB, 0xEC,
    /* U+00D0 */ 0x00, 0x84, 0xF1, 0xEE, 0xEF, 0xCD, 0x85, 0x00,
    /* U+00D8 */ 0xAF, 0xF4, 0xF2, 0xF3, 0x86, 0x00, 0x00, 0xA7,
    /* U+00E0 */ 0x88, 0x87, 0x89, 0x8B, 0x8A, 0x8C, 0xBE, 0x8D,
    /* U+00E8 */ 0x8F, 0x8E, 0x90, 0x91, 0x93, 0x92, 0x94, 0x95,
    /* U+00F0 */ 0x00, 0x96, 0x98, 0x97, 0x99, 0x9B, 0x9A, 0xD6,
    /* U+00F8 */ 0xBF, 0x9D, 0x9C, 0x9E, 0x9F, 0x00, 0x00, 0xD8,
};

struct CpMr { unsigned short cp; unsigned char mr; };
static const CpMr kExtToMR[] = {
    {0x0131,0xF5},{0x0152,0xCE},{0x0153,0xCF},{0x0178,0xD9},{0x0192,0xC4},
    {0x02C6,0xF6},{0x02C7,0xFF},{0x02D8,0xF9},{0x02D9,0xFA},{0x02DA,0xFB},
    {0x02DB,0xFE},{0x02DC,0xF7},{0x02DD,0xFD},{0x03A9,0xBD},{0x03C0,0xB9},
    {0x2013,0xD0},{0x2014,0xD1},{0x2018,0xD4},{0x2019,0xD5},{0x201A,0xE2},
    {0x201C,0xD2},{0x201D,0xD3},{0x201E,0xE3},{0x2020,0xA0},{0x2021,0xE0},
    {0x2022,0xA5},{0x2026,0xC9},{0x2030,0xE4},{0x2039,0xDC},{0x203A,0xDD},
    {0x2044,0xDA},{0x20AC,0xDB},{0x2122,0xAA},{0x2202,0xB6},{0x2206,0xC6},
    {0x220F,0xB8},{0x2211,0xB7},{0x221A,0xC3},{0x221E,0xB0},{0x222B,0xBA},
    {0x2248,0xC5},{0x2260,0xAD},{0x2264,0xB2},{0x2265,0xB3},{0x25CA,0xD7},
    {0xFB01,0xDE},{0xFB02,0xDF}
};
static const int kExtToMRCount = (int)(sizeof(kExtToMR)/sizeof(kExtToMR[0]));

static inline unsigned char cpToMacRoman(unsigned int cp)
{
    if (cp >= 0x00A0 && cp <= 0x00FF)
        return kLatin1ToMR[cp - 0x00A0];
    int lo = 0, hi = kExtToMRCount - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if      (kExtToMR[mid].cp == (unsigned short)cp) return kExtToMR[mid].mr;
        else if (kExtToMR[mid].cp  < (unsigned short)cp)  lo = mid + 1;
        else                                               hi = mid - 1;
    }
    return 0;
}

/* Append n UTF-8 bytes to an emoji's sequence, as far as its 31 bytes go */
static void AppendToEmoji(char* seq, const char* src, size_t n) {
    size_t have = strlen(seq);
    if (have + n > 31) n = 31 - have;
    memcpy(seq + have, src, n);
    seq[have + n] = '\0';
}

/* ------------------------------------------------------------------ */
/* utf8_to_macroman_step / _into — fixed-array variants (zero heap)   */
/* ------------------------------------------------------------------ */
short utf8_to_macroman_into(const char* __restrict__ utf8,
                             char* __restrict__ textDst, short textCap,
                             char (* __restrict__ emojiDst)[32],
                             uint8_t* __restrict__ emojiCount,
                             uint8_t emojiCap)
{
    size_t used;
    return utf8_to_macroman_step(utf8, utf8 ? strlen(utf8) : 0, textDst, textCap,
                                 emojiDst, emojiCount, emojiCap, &used);
}

short utf8_to_macroman_step(const char* __restrict__ utf8, size_t utf8len,
                            char* __restrict__ textDst, short textCap,
                            char (* __restrict__ emojiDst)[32],
                            uint8_t* __restrict__ emojiCount,
                            uint8_t emojiCap, size_t* consumed)
{
    if (emojiCount) *emojiCount = 0;
    *consumed = 0;
    if (!utf8 || textCap <= 0) return 0;
    int outLen = 0;
    size_t i = 0;
    bool afterZWJ = false;
    while (i < utf8len && outLen < textCap) {
        unsigned char b = (unsigned char)utf8[i];
        unsigned int cp;
        if (b < 0x80) {
            textDst[outLen++] = (char)b; i++; afterZWJ = false; continue;
        } else if (b < 0xC2) {
            i++; continue;
        } else if (b < 0xE0) {
            if (i + 1 >= utf8len) { i = utf8len; break; }   /* truncated: drop */
            if (((unsigned char)utf8[i+1] & 0xC0) != 0x80) { i++; continue; }  /* bad continuation: skip lead, reprocess next */
            cp = ((b & 0x1F) << 6) | ((unsigned char)utf8[i+1] & 0x3F);
            i += 2;
        } else if (b < 0xF0) {
            if (i + 2 >= utf8len) { i = utf8len; break; }   /* truncated: drop */
            if (((unsigned char)utf8[i+1] & 0xC0) != 0x80 ||
                ((unsigned char)utf8[i+2] & 0xC0) != 0x80) { i++; continue; }  /* bad continuation */
            cp = ((b & 0x0F) << 12) | (((unsigned char)utf8[i+1] & 0x3F) << 6) | ((unsigned char)utf8[i+2] & 0x3F);
            i += 3;
        } else {
            size_t seqLen = (i + 4 <= utf8len) ? 4 : utf8len - i;
            if (emojiDst && emojiCount) {
                uint8_t ec = *emojiCount;
                bool merge = afterZWJ && ec > 0 && outLen > 0 &&
                             (unsigned char)textDst[outLen-1] == (unsigned char)kEmojiSentinel;
                if (!merge && ec >= emojiCap) break;   /* no slot left: stop before it */
                if (merge) {
                    AppendToEmoji(emojiDst[ec-1], utf8 + i, seqLen);
                } else {
                    emojiDst[ec][0] = '\0';
                    AppendToEmoji(emojiDst[ec], utf8 + i, seqLen);
                    (*emojiCount)++;
                    textDst[outLen++] = kEmojiSentinel;
                }
            } else {
                textDst[outLen++] = kEmojiSentinel;
            }
            afterZWJ = false;
            i += seqLen; continue;
        }
        unsigned char mr = cpToMacRoman(cp);
        if (mr != 0) {
            textDst[outLen++] = (char)mr; afterZWJ = false;
        } else if (isEmojiCombiner(cp)) {
            size_t seqLen = (b < 0xE0) ? 2 : 3;
            bool prevIsSentinel = outLen > 0 &&
                                  (unsigned char)textDst[outLen-1] == (unsigned char)kEmojiSentinel;
            if (emojiDst && emojiCount && *emojiCount > 0 && prevIsSentinel)
                AppendToEmoji(emojiDst[*emojiCount - 1], utf8 + i - seqLen, seqLen);
            afterZWJ = (cp == kCP_ZWJ);
        } else {
            size_t seqLen = (b < 0xE0) ? 2 : 3;
            if (emojiDst && emojiCount && *emojiCount >= emojiCap) {
                i -= seqLen; break;                     /* no slot left: stop before it */
            }
            if (emojiDst && emojiCount) {
                emojiDst[*emojiCount][0] = '\0';
                AppendToEmoji(emojiDst[*emojiCount], utf8 + i - seqLen, seqLen);
                (*emojiCount)++;
            }
            textDst[outLen++] = kEmojiSentinel;
            afterZWJ = false;
        }
    }
    *consumed = i;
    return outLen;
}

/* ------------------------------------------------------------------ */
/* json_escape                                                         */
/* ------------------------------------------------------------------ */
void append_json_text(string& out, const char* mac, size_t len,
                      const char emojiSeqs[][32], uint8_t emojiCount, uint8_t& emojiIdx) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)mac[i];
        if (c == (unsigned char)kEmojiSentinel) {         /* UTF-8 bytes >= 0x80: no escaping */
            if (emojiIdx < emojiCount) out += emojiSeqs[emojiIdx++];
            continue;
        }
        if (c >= 0x80) {                                  /* Mac Roman -> UTF-8 */
            unsigned int cp = kMrToUnicode[c - 0x80];
            if (cp == 0) { out += '?'; continue; }
            if (cp < 0x800) {
                out += (char)(0xC0 | (cp >> 6));
                out += (char)(0x80 | (cp & 0x3F));
            } else {
                out += (char)(0xE0 | (cp >> 12));
                out += (char)(0x80 | ((cp >> 6) & 0x3F));
                out += (char)(0x80 | (cp & 0x3F));
            }
            continue;
        }
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20)  { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else                out += (char)c;
    }
}

string json_escape(const string& s) {
    string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";   /* code blocks keep their tabs */
        else if (c < 0x20)  {                /* other control chars: \u00XX, not dropped */
            static const char hex[] = "0123456789abcdef";
            out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15];
        }
        else                out += (char)c;
    }
    return out;
}
