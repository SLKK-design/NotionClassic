/* run_styles.cpp — Text run annotation parsing and TE style application.
   Extracted from main.cpp.  */
#include <TextEdit.h>
#include <Memory.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include "run_styles.h"
#include "emoji_text.h"  /* DrawStrikethrough */
#include "helpers.h"
#include "block_ops.h"   /* FlashTooLong */

/* MarkDirty is static in main.cpp; declare as extern so ToggleRunStyleBit can call it */
extern void MarkDirty();

/* Notion annotation colours, indexed by TextRun.color (0 = default). */
static const char* const kRunColors[] = {
    "default", "gray", "brown", "orange", "yellow", "green", "blue", "purple", "pink", "red",
    "gray_background", "brown_background", "orange_background", "yellow_background",
    "green_background", "blue_background", "purple_background", "pink_background",
    "red_background"
};
static const int kRunColorCount = (int)(sizeof(kRunColors) / sizeof(kRunColors[0]));

/* TextRun.textUrl: the URL is this prefix + the link text (0 = not a text URL) */
static const char* const kTextUrlPrefix[] = { 0, "", "https://", "http://", "mailto:" };
static const int kTextUrlCount = (int)(sizeof(kTextUrlPrefix) / sizeof(kTextUrlPrefix[0]));

static bool IsUrlChars(const char* text, int len) {
    if (len <= 0) return false;
    for (int i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)text[i];
        if (ch <= ' ' || ch >= 0x7F) return false;   /* also rejects kEmojiSentinel */
    }
    return true;
}

/* Rewrite r.url from its (edited) text; keeps the old url if it won't fit. */
static void SyncTextUrl(TextRun& r, const char* text) {
    if (!r.textUrl || r.textUrl >= kTextUrlCount || r.urlCut) return;
    if (!IsUrlChars(text, r.textLen)) { r.textUrl = 0; return; }
    const char* pre = kTextUrlPrefix[r.textUrl];
    int pl = (int)strlen(pre);
    if (pl + r.textLen >= kMaxUrlLen) return;
    memcpy(r.url, pre, pl);
    memcpy(r.url + pl, text, r.textLen);
    r.url[pl + r.textLen] = '\0';
}

/* ------------------------------------------------------------------ */
/* parseAnnotations                                                    */
/* ------------------------------------------------------------------ */
void parseAnnotations(TextRun& run, gason::JsonValue ann) {
    run.bold          = (ann("bold").getTag()          == gason::JSON_TRUE);
    run.italic        = (ann("italic").getTag()        == gason::JSON_TRUE);
    run.underline     = (ann("underline").getTag()     == gason::JSON_TRUE);
    run.strikethrough = (ann("strikethrough").getTag() == gason::JSON_TRUE);
    run.code          = (ann("code").getTag()          == gason::JSON_TRUE);
    run.color         = 0;
    bool ok = false;
    const char* c = ann("color").toString(&ok);
    if (ok && c)
        for (int i = 1; i < kRunColorCount; i++)
            if (strcmp(c, kRunColors[i]) == 0) { run.color = (uint8_t)i; break; }
}

const char* RunColorName(uint8_t color) {
    return color < kRunColorCount ? kRunColors[color] : kRunColors[0];
}

/* ------------------------------------------------------------------ */
/* parseRunLink                                                        */
/* ------------------------------------------------------------------ */
static void SetRunUrl(TextRun& run, const char* url) {
    if (!url || !url[0]) return;
    SafeStrCopy(run.url, kMaxUrlLen, url);
    run.urlCut = strlen(url) >= (size_t)kMaxUrlLen;
}

void parseRunLink(TextRun& run, gason::JsonValue item, const char* text) {
    bool ok = false;
    const char* itemType = item("type").toString(&ok);
    if (ok && itemType && strcmp(itemType, "text") == 0) {
        gason::JsonValue u = item("text")("link")("url");
        if (u.getTag() == gason::JSON_STRING) SetRunUrl(run, u.toString(&ok));
        /* Link whose text is the URL (pasted/auto-linked): remember which
           prefix makes url, so editing the text can update the link. */
        if (run.url[0] && !run.urlCut && IsUrlChars(text, run.textLen)) {
            int ul = (int)strlen(run.url);
            for (int k = 1; k < kTextUrlCount; k++) {
                int pl = (int)strlen(kTextUrlPrefix[k]);
                if (pl + run.textLen == ul && strncmp(run.url, kTextUrlPrefix[k], pl) == 0
                    && memcmp(run.url + pl, text, run.textLen) == 0) { run.textUrl = (uint8_t)k; break; }
            }
        }
        return;
    }
    if (ok && itemType && strcmp(itemType, "mention") == 0) {
        const char* mtype = item("mention")("type").toString(&ok);
        if (ok && mtype && strcmp(mtype, "page") == 0) {
            const char* pid = item("mention")("page")("id").toString(&ok);
            if (ok && pid && pid[0]) {
                snprintf(run.url, kMaxUrlLen, "http://notioncl/page/%s", pid);
                return;
            }
        }
    }
    gason::JsonValue h = item("href");
    if (h.getTag() == gason::JSON_STRING) SetRunUrl(run, h.toString(&ok));
}

bool RichTextItemLossy(gason::JsonValue item) {
    bool ok = false;
    const char* t = item("type").toString(&ok);
    if (!ok || !t) return true;
    if (strcmp(t, "text") == 0) return false;
    if (strcmp(t, "mention") == 0) {
        const char* m = item("mention")("type").toString(&ok);
        return !(ok && m && strcmp(m, "page") == 0);
    }
    return true;
}

bool BlockLoadedPartially(const Block& c) {
    if (CountSentinels(c.text, 0, c.textLen) > c.emojiCount) return true;
    for (int i = 0; i < c.emojiCount; i++)
        if (strlen(c.emojiSeqs[i]) >= 31) return true;
    for (int i = 0; i < c.runCount; i++)
        if (c.runs[i].urlCut) return true;
    return false;
}

bool SameRunExtras(const TextRun& a, const TextRun& b) {
    /* Two page mentions never merge, even of the same page: "@Page@Page"
       is two mentions, and one run would save it as one. */
    if (IsMentionRun(a) || IsMentionRun(b)) return &a == &b;
    return a.code == b.code && a.color == b.color && a.urlCut == b.urlCut
        && a.textUrl == b.textUrl && strcmp(a.url, b.url) == 0;
}

static void CopyRunExtras(TextRun& dst, const TextRun& src) {
    SafeStrCopy(dst.url, kMaxUrlLen, src.url);
    dst.code = src.code; dst.color = src.color; dst.urlCut = src.urlCut;
    dst.textUrl = src.textUrl;
}

/* ------------------------------------------------------------------ */
/* AdjustRunsForEdit / ClearLinkUnderline                              */
/* ------------------------------------------------------------------ */
/* The edit that turned blk.text into newText: old [p, oldLen-s) became
   new [p, newLen-s).  caret (a text index) is where it ended and the text
   after it is unchanged — that pins the edit down when it sits next to an
   identical char (" " typed before " end", one of two emoji side by side). */
static void EditSpan(const Block& blk, const char* newText, int newLen, int caret,
                     int* pOut, int* sOut) {
    int oldLen = blk.textLen;
    int s = 0;
    while (s < oldLen && s < newLen - caret
           && blk.text[oldLen - 1 - s] == newText[newLen - 1 - s]) s++;
    int p = 0;
    while (p < oldLen - s && p < newLen - s && blk.text[p] == newText[p]) p++;
    *pOut = p; *sOut = s;
}

void SyncEmojiSeqs(Block& blk, const char* newText, short newLen, short teCaret,
                   const char (*ins)[32], int insCount) {
    int p, s;
    EditSpan(blk, newText, newLen, TextIndexForTE(newText, newLen, teCaret), &p, &s);
    int head  = CountSentinels(newText, 0, p);
    int mid   = CountSentinels(newText, p, newLen - s);
    int tail  = CountSentinels(newText, newLen - s, newLen);
    int first = CountSentinels(blk.text, 0, blk.textLen) - tail;   /* old index of the tail's first */
    static char sSeqs[kMaxEmoji][32];
    int n = 0;
    for (int k = 0; k < head && k < blk.emojiCount && n < kMaxEmoji; k++)
        memcpy(sSeqs[n++], blk.emojiSeqs[k], 32);
    for (int k = 0; k < mid && n < kMaxEmoji; k++) {
        if (k < insCount) memcpy(sSeqs[n++], ins[k], 32);
        else              sSeqs[n++][0] = '\0';   /* unknown: dropped on save */
    }
    for (int k = first; k < first + tail && k < blk.emojiCount && n < kMaxEmoji; k++)
        memcpy(sSeqs[n++], blk.emojiSeqs[k], 32);
    memcpy(blk.emojiSeqs, sSeqs, 32 * n);
    blk.emojiCount = (uint8_t)n;
    blk.invalidateEmojiPos();
}

void AdjustRunsForEdit(Block& blk, const char* newText, short newLen, short teCaret,
                       short* outStart, short* outLen) {
    *outStart = 0; *outLen = 0;
    int oldLen = blk.textLen;
    if (blk.runCount == 0) return;
    /* One plain run (most paragraphs): the edit just changes its length —
       no diff over up to 2 KB per keystroke */
    if (blk.runCount == 1 && !blk.runs[0].url[0]) {
        blk.runs[0].textOffset = 0;
        blk.runs[0].textLen    = newLen;
        return;
    }

    /* The edit: old [p, oldLen-s) became new [p, newLen-s) */
    int p, s;
    EditSpan(blk, newText, newLen, TextIndexForTE(newText, newLen, teCaret), &p, &s);
    int del = oldLen - p - s;
    int ins = newLen - p - s;

    /* Run that owns the char before the edit (or the first run at p==0);
       chosen before the delete so a deleted run's style still applies. */
    int ownerIdx = 0;
    bool atEdge = (p == 0);   /* edit sits at the owner's start or end */
    { int pos = 0;
      for (int ri = 0; ri < blk.runCount; ri++) {
          if (p > 0 && blk.runs[ri].textLen > 0 && pos + blk.runs[ri].textLen >= p) {
              ownerIdx = ri; atEdge = (pos + blk.runs[ri].textLen == p); break;
          }
          pos += blk.runs[ri].textLen;
      } }
    TextRun owner = blk.runs[ownerIdx];

    /* Delete [p, p+del) from the runs */
    if (del > 0) {
        int pos = 0;
        for (int ri = 0; ri < blk.runCount; ri++) {
            TextRun& r = blk.runs[ri];
            int rs = pos, re = pos + r.textLen;
            pos = re;
            int a = rs > p ? rs : p, b = re < p + del ? re : p + del;
            if (b > a) r.textLen = (short)(r.textLen - (b - a));
        }
    }

    if (ins > 0) {
        bool breakOut = owner.url[0] &&
            atEdge && (strncmp(owner.url, "http://notioncl/", 16) == 0 || newText[p] == ' ');
        /* Strictly inside a link or mention the text joins it: splitting a
           mention would write it twice (typing there is refused upstream). */
        /* Find the run that ends at p (or starts there, for p == 0) */
        int at = 0, pos = 0;
        for (at = 0; at < blk.runCount; at++) {
            if (p > 0 && pos + blk.runs[at].textLen >= p && blk.runs[at].textLen > 0) break;
            if (p == 0) break;
            pos += blk.runs[at].textLen;
        }
        if (at >= blk.runCount) at = blk.runCount - 1;
        if (breakOut && blk.runCount < kMaxRuns) {
            /* Split the owner at p and put a link-free run in between */
            TextRun& r = blk.runs[at];
            int head = (p == 0) ? 0 : p - pos;
            int tail = r.textLen - head;
            int insAt = at + (head > 0 ? 1 : 0);
            int extra = (head > 0 && tail > 0) ? 2 : 1;
            if (blk.runCount + extra > kMaxRuns) extra = 0;
            if (extra) {
                memmove(&blk.runs[insAt + extra], &blk.runs[insAt],
                        sizeof(TextRun) * (blk.runCount - insAt));
                blk.runCount += extra;
                if (extra == 2) {
                    blk.runs[at].textLen = (short)head;
                    blk.runs[insAt + 1] = blk.runs[at];
                    blk.runs[insAt + 1].textLen = (short)tail;
                }
                TextRun nr = owner;
                nr.url[0] = '\0'; nr.urlCut = false; nr.textUrl = 0;
                nr.textLen = (short)ins;
                blk.runs[insAt] = nr;
                /* TE gave the new text the link's display underline;
                   only a real (annotated) underline should stay. */
                if (!owner.underline) { *outStart = (short)p; *outLen = (short)ins; }
            } else {
                blk.runs[at].textLen = (short)(blk.runs[at].textLen + ins);
            }
        } else {
            blk.runs[at].textLen = (short)(blk.runs[at].textLen + ins);
        }
    }

    /* Drop emptied runs, merge identical neighbours (kMaxRuns is small),
       recompute offsets */
    int w = 0, off = 0;
    for (int ri = 0; ri < blk.runCount; ri++) {
        const TextRun& r = blk.runs[ri];
        if (r.textLen <= 0) continue;
        if (w > 0) {
            TextRun& prev = blk.runs[w - 1];
            if (prev.bold == r.bold && prev.italic == r.italic && prev.underline == r.underline
                && prev.strikethrough == r.strikethrough && SameRunExtras(prev, r)) {
                prev.textLen = (short)(prev.textLen + r.textLen);
                off += r.textLen;
                continue;
            }
        }
        if (w != ri) blk.runs[w] = blk.runs[ri];
        blk.runs[w].textOffset = (short)off;
        off += blk.runs[w].textLen;
        w++;
    }
    blk.runCount = (uint8_t)w;
    /* Links whose text is the URL follow the edited text */
    for (int ri = 0; ri < w; ri++)
        if (blk.runs[ri].textUrl) SyncTextUrl(blk.runs[ri], newText + blk.runs[ri].textOffset);
    if (w == 0 && newLen > 0) {
        blk.runs[0] = owner;
        blk.runs[0].url[0] = '\0'; blk.runs[0].urlCut = false; blk.runs[0].textUrl = 0;
        blk.runs[0].textOffset = 0; blk.runs[0].textLen = newLen;
        blk.runCount = 1;
    }
}

void SnapSelectionForEdit(const Block& blk, TEHandle te, char key) {
    if (!te) return;
    if (blk.emojiCount > 0) SnapSelectionToEmoji(te, key);
    bool any = false;   /* most blocks have no mention: nothing to do */
    for (int ri = 0; ri < blk.runCount && !any; ri++) any = IsMentionRun(blk.runs[ri]);
    if (!any) return;
    short teS = (*te)->selStart, teE = (*te)->selEnd;
    int s = TextIndexForTE(blk.text, blk.textLen, teS);
    int e = TextIndexForTE(blk.text, blk.textLen, teE);
    for (int ri = 0; ri < blk.runCount; ri++) {
        const TextRun& r = blk.runs[ri];
        if (!IsMentionRun(r)) continue;
        int rs = r.textOffset, re = rs + r.textLen;
        if (s == e) {
            if      (key == 0x08 && rs < s && s <= re)  { s = rs; e = re; }   /* Backspace: whole */
            else if (key == 0x7F && rs <= s && s < re)  { s = rs; e = re; }   /* Delete: whole */
            else if (rs < s && s < re)                  { s = e = re; }       /* type after it */
        } else {
            if (rs < s && s < re) s = rs;   /* widen to cover it */
            if (rs < e && e < re) e = re;
        }
    }
    short nS = TELenForText(blk.text, 0, (short)s);
    short nE = TELenForText(blk.text, 0, (short)e);
    if (nS != teS || nE != teE) TESetSelect(nS, nE, te);
}

void SyncBlockTextKeepRuns(Block& b, TEHandle te, const char (*ins)[32], int insCount) {
    static char sNew[kMaxTextLen];
    CharsHandle ch = TEGetText(te);
    short len = (*te)->teLength;   /* padded: strip first, kMaxTextLen bounds it */
    HLock((Handle)ch);
    StripEmojiPadInto(*ch, (size_t)len, sNew, kMaxTextLen);
    HUnlock((Handle)ch);
    short nl = (short)strlen(sNew);
    short bs, bl;
    SyncEmojiSeqs(b, sNew, nl, (*te)->selStart, ins, insCount);
    AdjustRunsForEdit(b, sNew, nl, (*te)->selStart, &bs, &bl);
    memcpy(b.text, sNew, nl + 1);
    b.textLen = nl;
}

void SplitRunsAt(Block& b, int pos, TextRun* tail, uint8_t* tailCount) {
    int keep = 0, nt = 0;
    for (int ri = 0; ri < b.runCount; ri++) {
        /* Fields read from the array, never from a struct copy (see the
           m68k -Os miscompile note in SplitPiece) */
        int rs = b.runs[ri].textOffset;
        int re = rs + b.runs[ri].textLen;
        if (re <= pos) {                 /* wholly before the cut: stays */
            if (keep != ri) b.runs[keep] = b.runs[ri];
            keep++;
            continue;
        }
        tail[nt] = b.runs[ri];
        tail[nt].textOffset = (short)(rs > pos ? rs - pos : 0);
        if (rs < pos) {                  /* straddles the cut: both halves */
            tail[nt].textLen = (short)(re - pos);
            tail[nt].textUrl = 0;        /* each half is only part of the URL */
            if (keep != ri) b.runs[keep] = b.runs[ri];
            b.runs[keep].textLen = (short)(pos - rs);
            b.runs[keep].textUrl = 0;
            keep++;
        }
        nt++;
    }
    b.runCount = (uint8_t)keep;
    *tailCount = (uint8_t)nt;
}

void ClearLinkUnderline(const Block& blk, short start, short len) {
    TEHandle te = blk.textEdit;
    if (!te || len <= 0) return;
    short teS = TELenForText(blk.text, 0, start);
    short teE = teS + TELenForText(blk.text, start, len);
    short selS = (*te)->selStart, selE = (*te)->selEnd;
    TESetSelect(teS, teE, te);
    short mode = doFace;
    TextStyle ts;
    if (TEContinuousStyle(&mode, &ts, te) && (ts.tsFace & underline)) {
        ts.tsFont = 0; ts.tsSize = 0; ts.tsFace = underline;
        TESetStyle(doFace + doToggle, &ts, true, te);
    }
    TESetSelect(selS, selE, te);
}

/* ------------------------------------------------------------------ */
/* ApplyRunStyles                                                      */
/* ------------------------------------------------------------------ */
bool ApplyRunStyles(TEHandle te, const Block& blk) {
    if (blk.runCount == 0) return false;
    bool anyStyle = false;
    for (int i = 0; i < blk.runCount; i++) {
        const TextRun& r = blk.runs[i];
        if (r.bold || r.italic || r.underline || r.url[0]) { anyStyle = true; break; }
    }
    if (!anyStyle) return false;

    short savedStart = (*te)->selStart;
    short savedEnd   = (*te)->selEnd;
    int tePos = 0;
    for (int ri = 0; ri < blk.runCount; ri++) {
        const TextRun& run = blk.runs[ri];
        int runStart = tePos;
        tePos += TELenForText(blk.text, run.textOffset, run.textLen);
        int runEnd = tePos;
        TESetSelect(runStart, runEnd, te);
        TextStyle ts;
        ts.tsFont  = 0; ts.tsSize = 0;
        ts.tsFace  = (Style)((run.bold      ? bold      : 0)
                           | (run.italic    ? italic    : 0)
                           | (run.underline ? underline : 0)
                           | (run.url[0]    ? underline : 0));
        TESetStyle(doFace, &ts, false, te);
    }
    TESetSelect(savedStart, savedEnd, te);
    return true;
}

/* ------------------------------------------------------------------ */
/* RebuildRunsFromTE                                                   */
/* ------------------------------------------------------------------ */
void RebuildRunsFromTE(Block& blk) {
    if (!blk.runsDirty) return;
    TEHandle te = blk.textEdit;
    if (!te) return;

    int bn = blk.textLen;
    if (bn > kMaxTextLen) bn = kMaxTextLen;  /* clamp: oldRunIdx[] is kMaxTextLen wide */
    if (bn == 0) {
        blk.runCount = 1;
        blk.runs[0] = TextRun();
        return;
    }

    TextRun savedRuns[kMaxRuns];
    uint8_t savedCount = blk.runCount;
    if (savedCount > kMaxRuns) savedCount = kMaxRuns;
    memcpy(savedRuns, blk.runs, sizeof(TextRun) * savedCount);
    blk.runCount = 0;

    /* Carry forward strikethrough, url, code and colour from the pre-edit
       runs — TE only knows bold/italic/underline; the rest live in
       TextRun and are not in the TE style scrap.  Map each char
       position in blk.text → old run index for best-effort recovery. */
    /* static: kMaxTextLen-sized arrays don't belong on the small 68k stack;
       safe because nothing here yields or recurses. */
    static int8_t oldRunIdx[kMaxTextLen];   /* run index (kMaxRuns 12): one byte */
    memset(oldRunIdx, -1, sizeof(int8_t) * bn);
    { int ci = 0;
      for (int ri = 0; ri < savedCount; ri++) {
          for (int j = 0; j < savedRuns[ri].textLen && ci < bn; j++, ci++)
              oldRunIdx[ci] = ri;
      }
    }

    short teLen = (*te)->teLength;
    struct TStyle { bool b, i, u; };
    TStyle teS_buf[256];
    TStyle* teS = (teLen <= 256) ? teS_buf : (TStyle*)NewPtrClear(sizeof(TStyle) * teLen);
    if (!teS) { blk.runCount = 0; return; }
    short selS = (*te)->selStart, selE = (*te)->selEnd;
    TESetSelect(0, teLen, te);
    StScrpHandle scrap = TEGetStyleScrapHandle(te);
    TESetSelect(selS, selE, te);
    if (scrap && *scrap) {
        HLock((Handle)scrap);
        short nRuns = (*scrap)->scrpNStyles;
        for (int s = 0; s < nRuns; s++) {
            long start = (*scrap)->scrpStyleTab[s].scrpStartChar;
            long end   = (s + 1 < nRuns)
                         ? (*scrap)->scrpStyleTab[s+1].scrpStartChar
                         : (long)teLen;
            Style face = (*scrap)->scrpStyleTab[s].scrpFace;
            bool b = (face & bold)      != 0;
            bool iv = (face & italic)   != 0;
            bool u = (face & underline) != 0;
            for (long pp = start; pp < end && pp < teLen; pp++)
                teS[pp] = {b, iv, u};
        }
        HUnlock((Handle)scrap);
        DisposeHandle((Handle)scrap);
    }

    bool curB = false, curI = false, curU = false, curS = false;
    const TextRun plain;
    const TextRun* curEx = &plain;
    int  curRi = -1;          /* old run curEx came from (-1: plain) */
    bool first = true;
    bool tailPlain = false;   /* overflow tail mixed in a mention/link: no url */
    int tep = 0;
    int runStart = 0;

    for (int ci = 0; ci < bn; ci++) {
        bool b  = (tep < teLen) ? teS[tep].b : false;
        bool iv = (tep < teLen) ? teS[tep].i : false;
        bool u  = (tep < teLen) ? teS[tep].u : false;
        int  ri = oldRunIdx[ci];
        const TextRun& ex = (ri >= 0) ? savedRuns[ri] : plain;
        bool s  = ex.strikethrough;
        if (ex.url[0]) u = ex.underline;   /* TE underline on links is display-only */

        /* Same old run: extras can't differ (skips the url compares that
           otherwise ran for every char of every save) */
        bool change = first || b != curB || iv != curI || u != curU || s != curS
                            || (ri != curRi && !SameRunExtras(ex, *curEx));
        /* Last slot: the rest of the text joins it (losing some styling at
           the end) instead of being dropped — dropped runs no longer match
           the text and the save would strip every style in the block.  A
           mention can't hold extra text (Notion would drop it), so a tail
           that mixes in a mention or a different link becomes plain text. */
        if (change && !first && blk.runCount >= kMaxRuns - 1) {
            if (IsMentionRun(*curEx) || IsMentionRun(ex) || strcmp(ex.url, curEx->url) != 0)
                tailPlain = true;
            change = false;
        }
        if (change) {
            if (!first) {
                TextRun& nr = blk.runs[blk.runCount++];
                nr.textOffset = (short)runStart; nr.textLen = (short)(ci - runStart);
                nr.bold = curB; nr.italic = curI;
                nr.underline = curU; nr.strikethrough = curS;
                CopyRunExtras(nr, *curEx);
            }
            runStart = ci;
            curB = b; curI = iv; curU = u; curS = s;
            curEx = &ex; curRi = ri;
            first = false;
        }
        tep += ((unsigned char)blk.text[ci] == (unsigned char)kEmojiSentinel) ? kEmojiTECells : 1;
    }
    if (!first && bn > runStart) {   /* a slot is always left (see above) */
        TextRun& nr = blk.runs[blk.runCount++];
        nr.textOffset = (short)runStart; nr.textLen = (short)(bn - runStart);
        nr.bold = curB; nr.italic = curI;
        nr.underline = curU; nr.strikethrough = curS;
        CopyRunExtras(nr, *curEx);
        if (tailPlain) { nr.url[0] = '\0'; nr.urlCut = false; nr.textUrl = 0; }
    }
    if (blk.runCount == 0 && bn > 0) {
        blk.runs[0] = TextRun();
        blk.runs[0].textOffset = 0; blk.runs[0].textLen = (short)bn;
        blk.runCount = 1;
    }
    if (teS != teS_buf) DisposePtr((Ptr)teS);
    blk.runsDirty = false;
}

/* ------------------------------------------------------------------ */
/* ToggleRunStyleBit                                                   */
/* ------------------------------------------------------------------ */
void ToggleRunStyleBit(Block& blk, int which) {
    TEHandle te = blk.textEdit;
    if (!te) return;
    short selStart = (*te)->selStart;
    short selEnd   = (*te)->selEnd;
    if (selStart >= selEnd) return;

    int n = blk.textLen;
    if (n == 0) return;

    if (blk.runCount == 0) {
        blk.runs[0] = TextRun();
        blk.runs[0].textOffset = 0; blk.runs[0].textLen = (short)n;
        /* Inherit the block type's default bold so Cmd-B correctly toggles
           for types that render bold by default (toggle, headings). */
        blk.runs[0].bold = blockDesc(blk.type).bold;
        blk.runCount = 1;
    }

    /* ri = old run index per char, so url/code/colour follow their text
       when the runs are rebuilt below (-1 = no run: plain). */
    /* 2 bytes a char (4 KB static, not 12): style bits + old run index */
    struct CS { bool b:1, i:1, u:1, s:1; int8_t ri; };
    static CS cs[kMaxTextLen];
    for (int ci = 0; ci < n; ci++) cs[ci] = {false, false, false, false, -1};
    TextRun oldRuns[kMaxRuns];
    int oldCount = blk.runCount;
    memcpy(oldRuns, blk.runs, sizeof(TextRun) * oldCount);
    for (int ri = 0; ri < oldCount; ri++) {
        const TextRun& r = oldRuns[ri];
        for (int ci = r.textOffset; ci < r.textOffset + r.textLen && ci < n; ci++)
            cs[ci] = {r.bold, r.italic, r.underline, r.strikethrough, (int8_t)ri};
    }
    const TextRun plain;

    int txtStart = n, txtEnd = n;
    { int tep = 0;
      for (int ci = 0; ci < n; ci++) {
          int cw = ((unsigned char)blk.text[ci] == (unsigned char)kEmojiSentinel) ? kEmojiTECells : 1;
          if (txtStart == n && tep + cw > selStart) txtStart = ci;
          if (txtEnd   == n && tep >= selEnd)       { txtEnd = ci; break; }
          tep += cw;
      }
    }
    if (txtStart >= n || txtStart >= txtEnd) return;
    /* A mention is one unit: style all of it or none (a half-styled one
       would be two runs, i.e. two mentions) */
    for (int ri = 0; ri < oldCount; ri++) {
        const TextRun& r = oldRuns[ri];
        if (!IsMentionRun(r)) continue;
        int rs = r.textOffset, re = rs + r.textLen;
        if (rs < txtEnd && re > txtStart) {
            if (rs < txtStart) txtStart = rs;
            if (re > txtEnd)   txtEnd   = re;
        }
    }

    bool allHave = true;
    for (int ci = txtStart; ci < txtEnd; ci++) {
        bool has = (which==kStyleBold ? cs[ci].b : which==kStyleItalic ? cs[ci].i :
                    which==kStyleUnderline ? cs[ci].u : cs[ci].s);
        if (!has) { allHave = false; break; }
    }
    bool adding = !allHave;

    for (int ci = txtStart; ci < txtEnd; ci++) {
        switch (which) {
            case kStyleBold:          cs[ci].b = adding; break;
            case kStyleItalic:        cs[ci].i = adding; break;
            case kStyleUnderline:     cs[ci].u = adding; break;
            case kStyleStrikethrough: cs[ci].s = adding; break;
        }
    }

    /* Same style and extras (url, code, colour) as the run being built */
    auto sameAs = [&](const CS& a, const CS& b) {
        return a.b == b.b && a.i == b.i && a.u == b.u && a.s == b.s &&
               (a.ri == b.ri || SameRunExtras(a.ri >= 0 ? oldRuns[a.ri] : plain,
                                              b.ri >= 0 ? oldRuns[b.ri] : plain));
    };
    /* More runs than a block holds: refuse, leaving text and styles as
       they were (dropping runs would make them stop matching the text,
       and the save would then strip every style, link and mention). */
    { int need = (n > 0) ? 1 : 0;
      for (int ci = 1; ci < n; ci++) if (!sameAs(cs[ci], cs[ci - 1])) need++;
      if (need > kMaxRuns) { FlashTooLong(); return; } }

    blk.runCount = 0;
    if (n > 0) {
        CS curStyle = cs[0];
        int runStart = 0;
        for (int ci = 1; ci <= n; ci++) {
            bool same = (ci < n) && sameAs(cs[ci], curStyle);
            if (!same && blk.runCount < kMaxRuns) {
                TextRun& nr = blk.runs[blk.runCount++];
                nr.textOffset = (short)runStart; nr.textLen = (short)(ci - runStart);
                nr.bold=curStyle.b; nr.italic=curStyle.i;
                nr.underline=curStyle.u; nr.strikethrough=curStyle.s;
                CopyRunExtras(nr, curStyle.ri >= 0 ? oldRuns[curStyle.ri] : plain);
                if (ci < n) { runStart = ci; curStyle = cs[ci]; }
            }
        }
    }

    if (which != kStyleStrikethrough) {
        if (!ApplyRunStyles(te, blk) && !adding) {
            /* ApplyRunStyles skips when all runs are plain (anyStyle=false), but we
               still need to clear the TE face when removing the last style bit. */
            TextStyle _ts; _ts.tsFont = 0; _ts.tsSize = 0; _ts.tsFace = 0;
            TESetStyle(doFace, &_ts, false, te);
        }
    }

    TEUpdate(&blk.vrect, te);
    DrawStrikethrough(te, blk);
    blk.dirty = true;
    MarkDirty();
}
