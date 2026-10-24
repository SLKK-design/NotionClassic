/* emoji_text.cpp — Emoji sentinel/TE helpers and drawing utilities.
   Section (0) is pure char-buffer logic (the padding model); the rest
   works on TextEdit and QuickDraw. */
#include <TextEdit.h>
#include <Quickdraw.h>
#include <Memory.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "emoji_text.h"
#include "utf8_mac_lite.h"   /* StripVariationSelectors */
#include "picker.h"          /* gEmojiGWorld, LoadEmojiSprite, acquire_port_bitmap */

#include "emoji_sprite.h"

/* Globals owned by main.cpp needed for drawing */
extern WindowPtr  gWindow;
extern TEHandle   textActive;
extern SInt16     gFontHelvetica;
extern SInt16     gFontGeneva;

/* ================================================================== */
/* (0) Sentinel & padding primitives                                  */
/*     Pure logic over char buffers — the "padding model".            */
/* ================================================================== */

static inline bool IsSentinel(char c) {
    return (unsigned char)c == (unsigned char)kEmojiSentinel;
}

static inline short sMin(short a, short b) { return a < b ? a : b; }
static inline short sMax(short a, short b) { return a > b ? a : b; }

/* True if any of the first `len` chars is a sentinel. */
static bool ContainsSentinel(const char* text, short len) {
    for (short i = 0; i < len; i++)
        if (IsSentinel(text[i])) return true;
    return false;
}

/* True if buf[i] is a padding space: one of the 2 TextToTE puts right after
   a sentinel (the "S__" pad).  A space BEFORE an emoji is the user's: until
   Oct 2026 a leading pad made those strippable too, so "Hello 😀" lost its
   space on the next edit.  Returns false for non-space chars. */
static bool IsStrippablePadSpace(const char* buf, int len, int i) {
    if (buf[i] != ' ') return false;
    for (int d = -2; d <= -1; d++) {
        int j = i + d;
        if (j >= 0 && j < len && IsSentinel(buf[j])) return true;
    }
    return false;
}

/* One past the emoji cell whose sentinel is te[s]: its pad spaces */
static int CellEnd(const char* te, int len, int s) {
    int e = s + 1;
    while (e < s + kEmojiTECells && e < len && te[e] == ' ') e++;
    return e;
}

/* The sentinel of the emoji cell that TE position t ends or sits inside
   (s < t <= its end), or -1 */
static int CellBefore(const char* te, int len, int t) {
    for (int s = t - 1; s >= 0 && s > t - 1 - kEmojiTECells; s--) {
        if (IsSentinel(te[s])) return (t <= CellEnd(te, len, s)) ? s : -1;
        if (te[s] != ' ') break;
    }
    return -1;
}

/* ================================================================== */
/* (1) Stored text  <->  TE text  (padding transforms)                */
/* ================================================================== */

/* Stored text (sentinels) -> TE-displayable text (padded sentinels). */
void TextToTE(const char* text, short textLen, char* out, short outMax, short& outLen) {
    outLen = 0;

    if (!ContainsSentinel(text, textLen)) {
        short n = sMin(textLen, (short)(outMax - 1));
        memcpy(out, text, n); out[n] = '\0'; outLen = n;
        return;
    }

    for (short i = 0; i < textLen && outLen < outMax - 4; i++) {
        if (IsSentinel(text[i])) {
            /* No pad before it: every TE position is then TELenForText of the
               stored text, and a user's space before an emoji survives */
            out[outLen++] = text[i];                   /* sentinel */
            out[outLen++] = ' '; out[outLen++] = ' ';  /* 2 trailing pad spaces */
        } else {
            out[outLen++] = text[i];
        }
    }
    out[outLen] = '\0';
}

/* TE text (padded) -> stored text: drop the pad spaces around sentinels. */
void StripEmojiPadInto(const char* buf, size_t len, char* dst, size_t dstMax) {
    size_t di = 0;
    for (size_t i = 0; i < len && di < dstMax - 1; i++) {
        if (IsStrippablePadSpace(buf, (int)len, (int)i)) continue;
        dst[di++] = buf[i];
    }
    dst[di] = '\0';
}

/* Blocks above body size get the smaller cells; 10pt keeps the gap
   after the 16px tile about as tight as a paragraph's */
static const short kBodySize = 12;
static const short kPadSize  = 10;

bool ShrinkEmojiPads(TEHandle te, const char* teText, short teLen, BlockType type) {
    const BlockDesc& d = blockDesc(type);
    if (d.fontSize <= kBodySize || teLen == 0) return false;
    short selS = (*te)->selStart, selE = (*te)->selEnd;
    /* The whole text at the block's size first: a reused TE's TESetText
       gives all of it the style of its old first char, maybe a pad */
    TextStyle ts; ts.tsFont = d.useHelvetica ? gFontHelvetica : gFontGeneva;
    ts.tsSize = d.fontSize; ts.tsFace = 0;
    TESetSelect(0, teLen, te);
    TESetStyle(doFont + doSize, &ts, false, te);
    ts.tsFont = gFontGeneva; ts.tsSize = kPadSize;
    for (short i = 0; i + 1 < teLen; i++) {
        if (!IsSentinel(teText[i])) continue;
        TESetSelect(i, i + 2, te);   /* sentinel + first pad space */
        TESetStyle(doFont + doSize, &ts, false, te);
    }
    TESetSelect(selS, selE, te);
    return true;
}

void SnapSelectionToEmoji(TEHandle te, char key) {
    int len = (*te)->teLength;
    int a = (*te)->selStart, b = (*te)->selEnd;
    const char* t = *(*te)->hText;   /* nothing below moves memory */
    if (a == b) {
        int s = CellBefore(t, len, a);
        if (key == 0x08) {                          /* Backspace: the emoji before */
            if (s < 0) return;
            a = s; b = CellEnd(t, len, s);
        } else if (key == 0x7F) {                   /* Delete: the emoji after/around */
            if (s < 0 || a == CellEnd(t, len, s))
                s = (a < len && IsSentinel(t[a])) ? a : -1;
            if (s < 0) return;
            a = s; b = CellEnd(t, len, s);
        } else {                                    /* inside a pad: go after it */
            if (s < 0 || a == CellEnd(t, len, s)) return;
            a = b = CellEnd(t, len, s);
        }
    } else {                                        /* edges inside a cell widen */
        int s = CellBefore(t, len, a);
        if (s >= 0 && a < CellEnd(t, len, s)) a = s;
        s = CellBefore(t, len, b);
        if (s >= 0) b = CellEnd(t, len, s);
    }
    if (a != (*te)->selStart || b != (*te)->selEnd) TESetSelect(a, b, te);
}

void KeepTypingSize(TEHandle te, BlockType type) {
    const BlockDesc& d = blockDesc(type);
    if (d.fontSize <= kBodySize) return;
    short p = (*te)->selStart;
    if (p != (*te)->selEnd || p > (*te)->teLength) return;
    short src = (p == 0) ? 0 : p - 1;   /* the char TE takes the style from */
    if (src >= (*te)->teLength) return;
    const char* t = *(*te)->hText;
    if (!IsSentinel(t[src]) && !(src > 0 && IsSentinel(t[src - 1]))) return;
    TextStyle ts; short lh, asc;
    TEGetStyle(src, &ts, &lh, &asc, te);   /* keep its face (bold) */
    ts.tsFont = d.useHelvetica ? gFontHelvetica : gFontGeneva;
    ts.tsSize = d.fontSize;
    TESetStyle(doFont + doSize + doFace, &ts, false, te);   /* null scrap */
}

/* ================================================================== */
/* (2) TE  ->  Block sync                                              */
/* ================================================================== */

static void SyncTEInto(TEHandle te, char* dst, short maxLen) {
    CharsHandle ch = TEGetText(te);
    short len = (*te)->teLength;   /* padded length: strip first, dstMax bounds the result */
    HLock((Handle)ch);
    StripEmojiPadInto(*ch, (size_t)len, dst, maxLen);
    HUnlock((Handle)ch);
}

void SyncBlockText(Block& b) {
    SyncTEInto(textActive, b.text, kMaxTextLen);
    b.textLen = (short)strlen(b.text);
}

/* ================================================================== */
/* (3) Drawing                                                        */
/* ================================================================== */

/* The text arrows U+2190-2193 aren't emoji and aren't in the Mac font:
   draw them as plain line arrows in the slot.  dir: 0 left, 1 up,
   2 right, 3 down; -1 when seq isn't one of them. */
static int TextArrowDir(const char* seq) {
    const unsigned char* u = (const unsigned char*)seq;
    if (u[0] == 0xE2 && u[1] == 0x86 && u[2] >= 0x90 && u[2] <= 0x93 && !u[3])
        return u[2] - 0x90;
    return -1;
}

static void DrawTextArrow(int dir, short left, short mid) {
    short y = mid;                     /* same middle as an emoji in the slot */
    short x = left + 7;                /* middle of the 16 px slot            */
    switch (dir) {
    case 0: MoveTo(x - 5, y); LineTo(x + 5, y);
            MoveTo(x - 2, y - 3); LineTo(x - 5, y); LineTo(x - 2, y + 3); break;
    case 2: MoveTo(x - 5, y); LineTo(x + 5, y);
            MoveTo(x + 2, y - 3); LineTo(x + 5, y); LineTo(x + 2, y + 3); break;
    case 1: MoveTo(x, y + 5); LineTo(x, y - 5);
            MoveTo(x - 3, y - 2); LineTo(x, y - 5); LineTo(x + 3, y - 2); break;
    case 3: MoveTo(x, y - 5); LineTo(x, y + 5);
            MoveTo(x - 3, y + 2); LineTo(x, y + 5); LineTo(x + 3, y + 2); break;
    }
}

void DrawBlockEmojis(TEHandle teh, const char* teText, short teLen,
                     Block& blk) {
    if (!ContainsSentinel(teText, teLen)) return;
    LoadEmojiSprite();
    if (!gEmojiGWorld) return;

    PixMapHandle dstPM; BitMap* dstBM = acquire_port_bitmap((GrafPtr)gWindow, &dstPM);
    if (!dstBM) { if (dstPM) UnlockPixels(dstPM); return; }

    static const short kEmojiSrc = EMOJI_TILE_W;
    static const short kEmojiDst = 16;
    short lineHt = (*teh)->lineHeight;
    if (lineHt < 8) lineHt = 12;
    uint8_t emojiIdx = 0;

    PixMapHandle spritePM = GetGWorldPixMap(gEmojiGWorld);
    LockPixels(spritePM);

    for (short i = 0; i < teLen; ++i) {
        if (!IsSentinel(teText[i])) continue;

        Point pt = TEGetPoint(i, teh);
        short baseline = pt.v;
        short left     = pt.h;

        const EmojiSprite* es = nullptr;
        int arrow = -1;
        if (emojiIdx < blk.emojiCount) {
            static char sVSBuf[128];
            const char* stripped = StripVariationSelectors(blk.emojiSeqs[emojiIdx], sVSBuf, sizeof(sVSBuf));
            es = emoji_find_fallback(stripped);
            if (!es) arrow = TextArrowDir(stripped);
        }
        if (arrow >= 0) {
            /* Erase the font's missing-glyph box under it */
            Rect slotR; SetRect(&slotR, left, baseline - lineHt, left + kEmojiDst, baseline);
            EraseRect(&slotR);
            DrawTextArrow(arrow, left, baseline - lineHt / 2 - 2);
            emojiIdx++; continue;
        }
        if (!es) { emojiIdx++; continue; }

        /* Cache the x-position per emoji slot so it stays put across TE
           reflows (left can drift by a few px between TE states). */
        short imgLeft;
        if (emojiIdx < kMaxEmoji && blk.emojiXPos[emojiIdx] >= 0) {
            imgLeft = blk.emojiXPos[emojiIdx];
        } else {
            imgLeft = left + 1;  /* nudge right: glyph sat 1px left of the sentinel slot */
            if (emojiIdx < kMaxEmoji) blk.emojiXPos[emojiIdx] = imgLeft;
        }
        emojiIdx++;

        short centerV = baseline - lineHt / 2;
        short imgTop  = centerV - kEmojiDst / 2 - 2;

        /* Erase only this emoji's footprint — never touches neighbors. */
        Rect slotR; SetRect(&slotR, imgLeft, baseline - lineHt, imgLeft + kEmojiDst, baseline);
        EraseRect(&slotR);

        Rect srcR; SetRect(&srcR, es->x, es->y, es->x + kEmojiSrc, es->y + kEmojiSrc);
        Rect dstR; SetRect(&dstR, imgLeft, imgTop, imgLeft + kEmojiDst, imgTop + kEmojiDst);
        CopyBits((BitMap*)*spritePM, dstBM, &srcR, &dstR, srcOr, nil);
    }

    UnlockPixels(spritePM);
    if (dstPM) UnlockPixels(dstPM);
}

void DrawStrikethrough(TEHandle te, const Block& blk) {
    bool any = false;
    for (int i = 0; i < blk.runCount; i++) if (blk.runs[i].strikethrough) { any = true; break; }
    if (!any) return;

    static const Pattern k30Gray = {{0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55}};

    short lineHt = (*te)->lineHeight;
    short strike = lineHt / 2 + 1;
    short teLen  = (*te)->teLength;
    short nLines = (*te)->nLines;
    short tePos  = 0;

    PenPat(&k30Gray);
    for (int ri = 0; ri < blk.runCount; ri++) {
        const TextRun& run = blk.runs[ri];

        short runStart = tePos;
        tePos += TELenForText(blk.text, run.textOffset, run.textLen);
        short runEnd   = tePos;
        if (!run.strikethrough) continue;

        /* Per-TE-line scan: 2 TEGetPoint calls per line instead of 1/char.
           lineStarts[ln] is the absolute text offset of each TE line start. */
        for (short ln = 0; ln < nLines; ln++) {
            short lineStart = (*te)->lineStarts[ln];
            short lineEnd   = (ln + 1 < nLines) ? (*te)->lineStarts[ln + 1] : teLen;
            if (lineEnd   <= runStart) continue;
            if (lineStart >= runEnd)   break;

            short segS = sMax(lineStart, runStart);   /* clamp segment to line ∩ run */
            short segE = sMin(lineEnd,   runEnd);
            if (segS >= segE) continue;

            Point ptA = TEGetPoint(segS, te);
            Point ptB = TEGetPoint(segE, te);
            short y = ptA.v - strike;
            MoveTo(ptA.h, y); LineTo(ptB.h - 1, y);
        }
    }
    PenPat(&qd.black);
}
