/* blocks.h — Block data types, constants, and type helpers.
   Included by main.cpp and all extraction files that need Block/TextRun/UndoState.
   No app state; no globals.  */
#pragma once

#include <TextEdit.h>
#include <Quickdraw.h>
#include <string.h>
#include <stdint.h>
#include "utf8_mac_lite.h"   /* kEmojiSentinel */

/* ------------------------------------------------------------------ */
/* Compile-time limits                                                 */
/* ------------------------------------------------------------------ */
static const int kMaxRuns    = 12;
static const int kMaxEmoji   = 12;
static const int kMaxTextLen = 2048; /* ≤ 2047 chars; saved in ≤1500-char items (Notion caps an item at 2,000) */
static const int kMaxUrlLen  = 116; /* 116 → sizeof(TextRun)==128: array indexing uses LSL not MULS on 68k */
static const int kMaxBlocks  = 175;  /* 175 tested safe on SE/30 */

/* kEmojiSentinel now lives in core/helpers/utf8_mac_lite.h (included above). */

/* ------------------------------------------------------------------ */
/* BlockType enum                                                      */
/* ------------------------------------------------------------------ */
enum BlockType : unsigned char {
    BT_PARAGRAPH = 0, BT_HEADING_1, BT_HEADING_2, BT_HEADING_3,
    BT_BULLETED,      BT_NUMBERED,  BT_TODO,       BT_TOGGLE,
    BT_CODE,          BT_QUOTE,     BT_CALLOUT,
    BT_TOGGLE_H1,     BT_TOGGLE_H2, BT_TOGGLE_H3,
    BT_DIVIDER,       BT_TITLE,
    BT_CHILD_PAGE,    /* 16 — inline subpage link; id = target page UUID */
    BT_UNKNOWN = 0xFF
};

extern const char* const kBlockTypeNames[];  /* defined in block_ops.cpp */

static inline BlockType blockTypeFromStr(const char* s) {
    if (!s || !s[0]) return BT_UNKNOWN;
    switch ((unsigned)(((unsigned char)s[0] << 8) | (unsigned char)s[1])) {
        case ('p'<<8|'a'): return BT_PARAGRAPH;
        case ('h'<<8|'e'):
            switch (s[8]) {
                case '1': return BT_HEADING_1;
                case '2': return BT_HEADING_2;
                case '3': return BT_HEADING_3;
            } break;
        case ('b'<<8|'u'): return BT_BULLETED;
        case ('n'<<8|'u'): return BT_NUMBERED;
        case ('t'<<8|'o'):
            if (s[2] == '_') return BT_TODO;
            if (s[6] == '\0') return BT_TOGGLE;
            if (s[7] == 'h') switch (s[15]) {
                case '1': return BT_TOGGLE_H1;
                case '2': return BT_TOGGLE_H2;
                case '3': return BT_TOGGLE_H3;
            } break;
        case ('c'<<8|'o'): return BT_CODE;
        case ('q'<<8|'u'): return BT_QUOTE;
        case ('c'<<8|'a'): return BT_CALLOUT;
        case ('d'<<8|'i'): return BT_DIVIDER;
        case ('t'<<8|'i'): return BT_TITLE;
        case ('c'<<8|'h'): return BT_CHILD_PAGE;
    }
    return BT_UNKNOWN;
}

static inline const char* blockTypeToStr(BlockType t) {
    return (t <= BT_CHILD_PAGE) ? kBlockTypeNames[(int)t] : "";
}

/* ------------------------------------------------------------------ */
/* Style bit indices (used by ToggleRunStyleBit)                       */
/* ------------------------------------------------------------------ */
enum { kStyleBold = 0, kStyleItalic, kStyleUnderline, kStyleStrikethrough };

/* ------------------------------------------------------------------ */
/* TextRun — one styled span within a Block's text[]                  */
/* ------------------------------------------------------------------ */
struct TextRun {
    short textOffset;
    short textLen;
    char  url[kMaxUrlLen];
    bool  bold;
    bool  italic;
    bool  underline;
    bool  strikethrough;
    /* Not shown by TE, but kept so a save writes them back unchanged */
    bool    code;
    bool    urlCut;  /* link longer than url[]: never written back truncated */
    uint8_t color;   /* index into the Notion colour names, 0 = "default" */
    uint8_t textUrl; /* link text is the URL itself (kTextUrl*): edits keep url in step */
    TextRun() : textOffset(0), textLen(0), bold(false), italic(false),
                underline(false), strikethrough(false), code(false),
                urlCut(false), color(0), textUrl(0) { url[0] = '\0'; }
};
/* A page mention run: url "http://notioncl/page/<id>".  One unit — Notion
   renders the page's title from it, never our text. */
static inline bool IsMentionRun(const TextRun& r) {
    return strncmp(r.url, "http://notioncl/page/", 21) == 0;
}

/* 68k: sizeof==128 means runs[i] indexing compiles to LSL.L #7 not MULS (~30 cycles saved per access) */
static_assert(sizeof(TextRun) == 128, "TextRun must be 128 bytes: reduce kMaxUrlLen if this fails");

/* Why a block can't be edited here (Block.cutShort); higher wins.     */
enum { kCutNone = 0,
       kCutTooLong = 1,   /* too long even split into pieces: "Too long"  */
       kCutMention = 2 }; /* @person/date/… mention or equation we can't
                             write back: "Read only"                      */

/* ------------------------------------------------------------------ */
/* Block — one Notion block (paragraph, heading, todo, toggle, …)     */
/* ------------------------------------------------------------------ */
/* Every new Block (a fresh row) gets the next serial; copies, moves and
   memmoves carry it along.  Unlike the row index it never changes, and
   unlike the Notion id it's never empty or shared by a split block's
   pieces — so it names "this row" for undo and in-flight requests. */
inline uint32_t NextBlockSerial() { static uint32_t sNext = 0; return ++sNext; }

struct Block {
    int       pos;
    uint32_t  serial;
    char      id[37];
    char      parentId[37];
    BlockType type;

    char      text[kMaxTextLen];
    short     textLen;

    short     teCachedWidth;
    BlockType teCachedType;

    TextRun   runs[kMaxRuns];
    uint8_t   runCount;

    char      emojiSeqs[kMaxEmoji][32];
    uint8_t   emojiCount;

    short     emojiXPos[kMaxEmoji];   /* cached emoji x positions */
    bool      runsDirty;

    bool check, dirty, open, isChild;
    bool recreate;   /* re-create on save (insert new, trash old): retype,
                        move, indent/outdent, or a child of a re-created parent */
    bool leftParent;  /* moved to another parent since its Notion copy was
                         made: that copy isn't under parentId's            */
    bool inOldParent; /* its Notion copy sits under its parent's old copy,
                         which waits for the trash: it goes along with it  */
    bool kidsLeft;    /* blocks moved out of it: until they're re-created,
                         their only copy may still be in its Notion copy   */
    uint8_t cutShort; /* kCut*: didn't load as-is, never written back (0 = editable) */
    bool isCont;     /* continuation piece of the block above: same Notion block
                        (same id), split on load because it was too long */
    bool hasChildren; /* Notion has_children: nested blocks exist (loaded or not) */
    bool saveFirst;   /* re-created before other dirty blocks (children moved
                         out of a retyped toggle go before the toggle)       */
    bool kidsPartial; /* toggle: its children didn't all load (has_more, or
                         no room in pageElm) — can't be re-created whole     */
    int  depth;
    Rect rect, vrect;
    TEHandle textEdit;
    short    titleRight;
    char     iconName[32];   /* child_page: Notion icon slug, or "" */
    short    iconSprX;       /* child_page: emoji sprite X, or -1   */
    short    iconSprY;       /* child_page: emoji sprite Y          */

    Block() : pos(0), serial(NextBlockSerial()), textLen(0), teCachedWidth(0),
              teCachedType(BT_UNKNOWN), type(BT_UNKNOWN), runCount(0),
              emojiCount(0), runsDirty(false), check(false), dirty(false),
              open(false), isChild(false), recreate(false), leftParent(false),
              inOldParent(false), kidsLeft(false), cutShort(false), isCont(false), hasChildren(false),
              saveFirst(false), kidsPartial(false), depth(0),
              textEdit(nil), titleRight(0) {
        id[0]='\0'; parentId[0]='\0'; text[0]='\0';
        invalidateEmojiPos();
        SetRect(&rect, 0, 0, 0, 0); SetRect(&vrect, 0, 0, 0, 0);
        iconName[0] = '\0'; iconSprX = -1; iconSprY = -1;
    }

    /* Forget the cached emoji x positions (-1): text or layout changed */
    void invalidateEmojiPos() { memset(emojiXPos, 0xFF, sizeof(emojiXPos)); }

    void invalidateTE() {
        if (textEdit) { TEDispose(textEdit); textEdit = nil; }
        teCachedWidth = 0; teCachedType = BT_UNKNOWN;
    }

    bool addTE(Rect _rect, Rect _vrect, BlockType curType) {
        short newWidth = _rect.right - _rect.left;
        if (textEdit != nil && teCachedWidth == newWidth && teCachedType == curType) {
            rect  = _rect; vrect = _vrect;
            (*textEdit)->viewRect = _vrect;
            (*textEdit)->destRect = _rect;
            return false;
        }
        if (textEdit != nil) { TEDispose(textEdit); textEdit = nil; }
        teCachedWidth = 0; teCachedType = BT_UNKNOWN;
        rect=_rect; vrect=_vrect;
        textEdit = TEStyleNew(&rect, &vrect);
        if (textEdit == nil) return false;
        { FontInfo fi; GetFontInfo(&fi);
          (*textEdit)->lineHeight = fi.ascent + fi.descent + fi.leading;
          (*textEdit)->fontAscent = fi.ascent; }
        teCachedWidth = newWidth; teCachedType = curType;
        return true;
    }
};

/* Raise b.cutShort to why (never lowers it). */
static inline void MarkCut(Block& b, uint8_t why) { if (b.cutShort < why) b.cutShort = why; }

/* ------------------------------------------------------------------ */
/* UndoState — one level of undo (text + emoji + block type)          */
/* ------------------------------------------------------------------ */
struct UndoState {
    char      text[kMaxTextLen];
    short     textLen;
    char      emojiSeqs[kMaxEmoji][32];
    uint8_t   emojiCount;
    TextRun   runs[kMaxRuns];   /* restored with the text: styles stay on it */
    uint8_t   runCount;
    UndoState() : textLen(0), emojiCount(0), runCount(0) { text[0]='\0'; }
};

/* ------------------------------------------------------------------ */
/* Block type descriptor — one entry per BlockType value              */
/* ------------------------------------------------------------------ */
enum BlockDecor : unsigned char {
    DECOR_NONE = 0,
    DECOR_BULLET,     /* bulleted: filled oval */
    DECOR_NUMBER,     /* numbered: "1." counter */
    DECOR_CHECKBOX,   /* todo: check box */
    DECOR_TRIANGLE,   /* toggle: disclosure triangle */
    DECOR_QUOTEBAR    /* quote: left black bar */
};

enum CounterBehavior : unsigned char {
    CTR_IGNORE = 0,   /* leave listCounter unchanged (headings, title) */
    CTR_RESET,        /* reset listCounter to 0 */
    CTR_INCREMENT     /* increment listCounter (numbered list) */
};

struct BlockDesc {
    short           fontSize;       /* pt: 12, 14, 16, 20, 26 */
    bool            useHelvetica;   /* false=Geneva, true=Helvetica */
    bool            bold;
    short           leftBase;       /* base left-indent px (+ depth*20 applied at runtime) */
    bool            useActualDepth; /* use raw depth (not depth-1) for indent + decorator; set for list items and toggles so child items visually indent inside their parent */
    short           topMargin;      /* extra px added above block */
    short           lineHeightPct;  /* padding = 8*lhPct/100; 0=no padding */
    short           teRightInset;   /* px subtracted from right edge of TE rect */
    short           teTopInset;     /* px added to top of TE rect (code box offset) */
    bool            hasBox;         /* draw background box (code/callout) */
    bool            boxFill;        /* true=filled bg (code), false=border-only (callout) */
    bool            extraLineH;     /* lineHeight+=2 on new TE; rect.bottom-=2 */
    bool            hasChecked;     /* API: read "checked" field */
    bool            useTitle;       /* API: read "title" not "rich_text" */
    BlockDecor      decor;          /* mutually exclusive left decorator */
    CounterBehavior counter;        /* how this type interacts with the list counter */
};

/* Defined in block_render.cpp — one row per BlockType (indices 0–16). */
extern const BlockDesc kBlockDesc[17];

/* Convenience: look up descriptor for a BlockType (safe for BT_UNKNOWN). */
static inline const BlockDesc& blockDesc(BlockType t) {
    return kBlockDesc[(t <= BT_CHILD_PAGE) ? (int)t : 0];
}

/* ------------------------------------------------------------------ */
/* Block type predicates (derived from descriptor table)              */
/* ------------------------------------------------------------------ */
static inline bool isToggleType(BlockType t)    { return blockDesc(t).decor == DECOR_TRIANGLE; }
static inline bool isLineHeightType(BlockType t){ return blockDesc(t).extraLineH; }

static inline void promoteToggleHeading(Block& c, bool isToggleable) {
    if (!isToggleable) return;
    if      (c.type == BT_HEADING_1) c.type = BT_TOGGLE_H1;
    else if (c.type == BT_HEADING_2) c.type = BT_TOGGLE_H2;
    else if (c.type == BT_HEADING_3) c.type = BT_TOGGLE_H3;
}
