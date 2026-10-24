/* rich_text_json.cpp — Notion rich_text and block JSON serialisation.
   Extracted from main.cpp. */
#include <string.h>
#include <stdint.h>
#include <string>

#include "rich_text_json.h"
#include "utf8_mac.h"   /* json_escape, append_json_text */
#include "run_styles.h" /* RunColorName */

using std::string;

/* ------------------------------------------------------------------ */
/* build_block_json                                                    */
/* ------------------------------------------------------------------ */
void build_block_json(BlockType type, const char* richText,
                      bool checked, string& out) {
    const char* rt = richText ? richText
                              : "\"rich_text\":[{\"text\":{\"content\":\"\"}}]";
    out.clear();
    switch (type) {
        case BT_TODO:
            out = "{\"type\":\"to_do\",\"to_do\":{"; out += rt;
            out += ",\"checked\":"; out += checked ? "true" : "false"; out += "}}";
            break;
        case BT_DIVIDER:
            out = "{\"type\":\"divider\",\"divider\":{}}";
            break;
        case BT_CALLOUT:
            out = "{\"type\":\"callout\",\"callout\":{"; out += rt; out += "}}";
            break;
        case BT_CODE:
            out = "{\"type\":\"code\",\"code\":{"; out += rt;
            out += ",\"language\":\"plain text\"}}";
            break;
        case BT_TOGGLE_H1: case BT_TOGGLE_H2: case BT_TOGGLE_H3: {
            const char* at = blockTypeToStr(type) + 7; /* skip "toggle_" */
            out = "{\"type\":\""; out += at; out += "\",\""; out += at;
            out += "\":{"; out += rt; out += ",\"is_toggleable\":true}}";
            break;
        }
        default: {
            const char* tn = blockTypeToStr(type);
            out = "{\"type\":\""; out += tn; out += "\",\""; out += tn;
            out += "\":{"; out += rt; out += "}}";
        }
    }
}

/* ------------------------------------------------------------------ */
/* build_children_body                                                 */
/* ------------------------------------------------------------------ */
void build_children_body(const string& blockJson,
                         const char*   afterId,
                         bool          atStart,
                         string&        out) {
    out = "{\"children\":[";
    out += blockJson; out += "]";
    if (atStart) {
        out += ",\"position\":{\"type\":\"start\"}";
    } else if (afterId && afterId[0]) {
        out += ",\"position\":{\"type\":\"after_block\",\"after_block\":{\"id\":\"";
        out += afterId; out += "\"}}";
    }
    out += "}";
}

/* ------------------------------------------------------------------ */
/* Rich text items                                                     */
/* ------------------------------------------------------------------ */
/* Notion rejects a rich_text item over 2,000 characters.  One Mac Roman
   byte is one character; the (at most kMaxEmoji) emoji add a few more,
   so 1,500 bytes per item leaves ample room.                           */
static const short kRichTextChunk = 1500;

/* Append one rich_text item for b.text[off, off+len) styled as run. */
static void AppendRichItem(string& result, const Block& b, const TextRun& run,
                           short off, short len, uint8_t& emojiIdx) {
    /* Page mention runs: serialize as mention type so Notion preserves the link.
       The text goes straight into result (escaped UTF-8, no temporaries). */
    if (IsMentionRun(run)) {
        const char* pageId = run.url + 21; /* UUID after "http://notioncl/page/" */
        result += "{\"type\":\"mention\",\"mention\":{\"type\":\"page\",\"page\":{\"id\":\"";
        result += pageId;
        result += "\"}},\"plain_text\":\"";
        append_json_text(result, b.text + off, (size_t)len, b.emojiSeqs, b.emojiCount, emojiIdx);
    } else {
        result += "{\"type\":\"text\",\"text\":{\"content\":\"";
        append_json_text(result, b.text + off, (size_t)len, b.emojiSeqs, b.emojiCount, emojiIdx);
        result += "\"";
        /* Keep the link; skip in-app notioncl URLs and links that didn't
           fit in run.url (writing those back would corrupt them). */
        if (run.url[0] && !run.urlCut && strncmp(run.url, "http://notioncl/", 16) != 0) {
            result += ",\"link\":{\"url\":\"";
            result += json_escape(run.url); result += "\"}";
        }
        result += "}";
    }
    result += ",\"annotations\":{\"bold\":";
    result += run.bold          ? "true" : "false"; result += ",\"italic\":";
    result += run.italic        ? "true" : "false"; result += ",\"underline\":";
    result += run.underline     ? "true" : "false"; result += ",\"strikethrough\":";
    result += run.strikethrough ? "true" : "false";
    result += ",\"code\":"; result += run.code ? "true" : "false";
    result += ",\"color\":\""; result += RunColorName(run.color); result += "\"}}";
}

/* Append b's runs as rich_text items (comma-separated after *first). */
static void AppendRichItems(string& result, const Block& b, bool& first) {
    /* §1: Validate: sum of run textLen must equal b.textLen */
    bool valid = b.runCount > 0;
    if (valid) {
        short total = 0;
        for (int _ri = 0; _ri < b.runCount; _ri++) total += b.runs[_ri].textLen;
        valid = (total == b.textLen);
    }
    /* Plain fallback: one unstyled run over the whole text */
    TextRun plain;
    plain.textLen = b.textLen;
    const TextRun* runs = valid ? b.runs : &plain;
    int runCount = valid ? b.runCount : 1;

    uint8_t emojiIdx = 0;
    for (int ri = 0; ri < runCount; ri++) {
        const TextRun& run = runs[ri];
        /* Page mentions stay one item; text runs split at kRichTextChunk */
        bool mention = IsMentionRun(run);
        short off = run.textOffset, left = run.textLen;
        do {
            short len = (!mention && left > kRichTextChunk) ? kRichTextChunk : left;
            if (!first) result += ",";
            first = false;
            AppendRichItem(result, b, run, off, len, emojiIdx);
            off += len; left -= len;
        } while (left > 0);
    }
}

/* Size a body for n pieces up front: ~200 bytes of item JSON per run, and
   text grows by up to ~1/4 (UTF-8 for Mac Roman accents, escapes). */
size_t RichJsonEstimate(const Block* pieces, int n) {
    size_t est = 160;
    for (int i = 0; i < n; i++)
        est += (size_t)pieces[i].runCount * 200 + (size_t)pieces[i].textLen * 5 / 4;
    return est;
}

/* Pieces of a split block (head + isCont pieces) form one rich_text array */
void AppendRichArray(string& out, const Block* pieces, int n) {
    out += "[";
    bool first = true;
    for (int i = 0; i < n; i++) AppendRichItems(out, pieces[i], first);
    out += "]";
}

/* The block object for `type`, with the pieces' rich text written straight
   into out (n == 0: one empty text item).  One pass, no intermediate
   strings — a split block's body can be 50+ KB. */
void AppendBlockJson(BlockType type, bool checked, const Block* pieces, int n, string& out) {
    if (type == BT_DIVIDER) { out += "{\"type\":\"divider\",\"divider\":{}}"; return; }
    const char* tn = blockTypeToStr(type);
    bool toggleHeading = (type == BT_TOGGLE_H1 || type == BT_TOGGLE_H2 || type == BT_TOGGLE_H3);
    if (toggleHeading) tn += 7;   /* skip "toggle_": a heading_N with is_toggleable */
    out += "{\"type\":\""; out += tn; out += "\",\""; out += tn; out += "\":{\"rich_text\":";
    if (n > 0) AppendRichArray(out, pieces, n);
    else       out += "[{\"text\":{\"content\":\"\"}}]";
    if (type == BT_TODO)      { out += ",\"checked\":"; out += checked ? "true" : "false"; }
    if (type == BT_CODE)        out += ",\"language\":\"plain text\"";
    if (toggleHeading)          out += ",\"is_toggleable\":true";
    out += "}}";
}

/* A /children append request for the pieces as one new block */
void AppendChildrenBody(BlockType type, bool checked, const Block* pieces, int n,
                        const char* afterId, bool atStart, string& out) {
    out += "{\"children\":[";
    AppendBlockJson(type, checked, pieces, n, out);
    out += "]";
    if (atStart) {
        out += ",\"position\":{\"type\":\"start\"}";
    } else if (afterId && afterId[0]) {
        out += ",\"position\":{\"type\":\"after_block\",\"after_block\":{\"id\":\"";
        out += afterId; out += "\"}}";
    }
    out += "}";
}

