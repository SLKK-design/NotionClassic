/* content_events.cpp — Content-area and key event handlers extracted from main.cpp.
   Contains: doInContent, HandleKeyDown.                                */

/* Apple Extended Keyboard virtual key codes (hardware-specific, not charCode) */
enum {
    kVKeyUp       = 0x7E,
    kVKeyDown     = 0x7D,
    kVKeyPageUp   = 0x74,
    kVKeyPageDown = 0x79,
    kVKeyHome     = 0x73,
    kVKeyEnd      = 0x77
};

#include <Memory.h>
#include <Windows.h>
#include <Controls.h>
#include <Events.h>
#include <TextEdit.h>
#include <Quickdraw.h>
#include <Scrap.h>
#include <Menus.h>
#include <Sound.h>

#include <string>
#include <string.h>
using std::string;

#include "content_events.h"
#include "blocks.h"
#include "app_state.h"
#include "helpers.h"
#include "block_render.h"
#include "block_ops.h"
#include "navigation.h"
#include "page_lifecycle.h"
#include "page_fetch.h"
#include "picker_window.h"
#include "save_pipeline.h"
#include "debug_con.h"
#include "Settings.h"
#include "library_window.h"
#include "HttpClient.h"
#include "emoji_text.h"
#include "run_styles.h"
#include "menu.h"

/* ------------------------------------------------------------------ */
/* Extern globals — defined in main.cpp                               */
/* ------------------------------------------------------------------ */
extern WindowPtr        gWindow;
extern ControlHandle    gScrollBar;
extern ControlHandle    gSaveButton;
extern ControlActionUPP gScrollActionUPP;
extern int              gScrollOffset;
extern int              gCurRequest;
extern int              blockSelect;
extern TEHandle         textActive;
extern Block*           pageElm;
extern int              pageElmCount;
extern int              gMaxBlocks;
extern char             gPageIconName[32];
extern Rect             gIconRect;
extern short            gColLeft;
extern bool             gArrowsVisible;
extern Rect             gMoveUpArrowRect;
extern Rect             gMoveDownArrowRect;
extern Rect             gAddBlockRect;
extern AppState         gAppState;
extern AppSettings      gSettings;
extern HttpClient       gHttpClient;

/* Emoji clipboard — stores emoji sequences during copy for paste     */
static char    sClipEmoji[kMaxEmoji][32];
static uint8_t sClipEmojiCount = 0;

/* ------------------------------------------------------------------ */
/* Extern constants — defined in main.cpp                             */
/* ------------------------------------------------------------------ */
extern const short kScrollBarWidth;
extern const short kLineScrollAmount;
extern const short kSidebarWidth;

/* ------------------------------------------------------------------ */
/* Extern functions — defined in other TUs                            */
/* ------------------------------------------------------------------ */
bool HasSel();
void MarkDirty();
void doSave();
void doAddBlock(int typeIdx);
string BlocksURL(const char* id);

/* ------------------------------------------------------------------ */
/* Local constants (mirror main.cpp)                                  */
/* ------------------------------------------------------------------ */

/* Shared scratch for the key/click handlers — they never run nested, and
   within one handler each use ends before the next begins (checked per
   handler).  Two buffers instead of nine statics (~19 KB -> ~6 KB).    */
static char sScratchTE[kTEBufLen];      /* raw TE text, emoji pads included */
static char sScratchText[kMaxTextLen];  /* stripped block text */

/* ------------------------------------------------------------------ */
/* DoUndo — Cmd-Z: undo the last step, or redo it the second time.     */
/* Works from anywhere: the block it belongs to is selected again.     */
/* ------------------------------------------------------------------ */
static void DoUndo()
{
    int idx = UndoBlockIdx();
    if (idx < 0) { SysBeep(1); return; }
    if (!AllowEdit(idx, kEditText)) return;
    UndoSwap(idx);
    if (textActive) { TEDeactivate(textActive); textActive = nil; }
    Block& ub = pageElm[idx];
    /* A fresh TE takes the text with the right type font and run styles
       (TESetText resets char styles if called outside updateBlocks) */
    ub.invalidateTE();
    ub.dirty     = true;
    ub.runsDirty = true;
    MarkDirty();
    blockSelect = idx;
    updateBlocks();
    textActive = pageElm[idx].textEdit;
    if (textActive) {
        short tlen = (*textActive)->teLength;
        TESetSelect(tlen, tlen, textActive);
        TEActivate(textActive);
    }
}

/* ------------------------------------------------------------------ */
/* DivertBlockedKey — the selected block can't take this edit (AllowEdit
   refuses it, or it's full).  Typing or Return at its end carries on in
   a new paragraph below; anything else beeps and flashes why.
   Returns true when the key was handled here.                         */
/* ------------------------------------------------------------------ */
static bool DivertBlockedKey(EventRecord& e, char c)
{
    if (!HasSel() || !textActive) return false;
    bool inserts = (unsigned char)c >= 0x20 && c != 0x7F;
    bool edits   = inserts || c == 0x08 || c == 0x7F || c == '\t' || c == 0x0D || c == 0x03;
    if (!edits) return false;
    int   idx    = blockSelect;
    short selS   = (*textActive)->selStart, selE = (*textActive)->selEnd;
    bool  full   = inserts && selS == selE && pageElm[idx].textLen >= kMaxTextLen - 1;
    if (!pageElm[idx].cutShort && !full && !gAppState.readOnly) return false;

    bool atEnd = selS == selE && selE == (*textActive)->teLength;
    bool lastPiece = !(idx + 1 < pageElmCount && pageElm[idx + 1].isCont);
    if (atEnd && lastPiece && (inserts || c == 0x0D) && pageElm[idx].type != BT_TITLE
        && !gAppState.busy() && !gAppState.readOnly) {
        TEDeactivate(textActive);
        doAddBlock(0);
        if (blockSelect != idx && textActive) {
            if (inserts) HandleKeyDown(e);   /* type the key into the new block */
            return true;
        }
        if (textActive) TEActivate(textActive);   /* add refused (busy, array full) */
    }
    if (AllowEdit(idx, kEditText)) FlashTooLong();   /* only full: AllowEdit flashed the rest */
    return true;
}

/* ------------------------------------------------------------------ */
/* MarkDirtyAndRefresh — mark block dirty, rebuild, reactivate        */
/* ------------------------------------------------------------------ */
static inline void MarkDirtyAndRefresh(Block& b)
{
    b.dirty = true; b.runsDirty = true; MarkDirty();
    short sel = (*textActive)->selStart;
    updateBlocks();
    reactivateAt(sel);
}

/* ------------------------------------------------------------------ */
/* HandleKeyDown — keyDown / autoKey event handler                    */
/* ------------------------------------------------------------------ */
void HandleKeyDown(EventRecord& e)
{
    char  c    = (char)(e.message & charCodeMask);
    short vkey = (short)((e.message & keyCodeMask) >> 8);

    /* --- Scroll keys: intercept by virtual key code before any TE handling ---
       These fire regardless of whether a TE is active. */
    if (!(e.modifiers & cmdKey) && gScrollBar != nil) {
        short winH0    = gWindow->portRect.bottom - gWindow->portRect.top;
        short pageAmt0 = winH0 - kLineScrollAmount;
        if (textActive == nil) {
            short scrollBy0 = 0;
            if      (vkey == kVKeyUp)       scrollBy0 = -kLineScrollAmount;
            else if (vkey == kVKeyDown)     scrollBy0 =  kLineScrollAmount;
            else if (vkey == kVKeyPageUp)   scrollBy0 = -pageAmt0;
            else if (vkey == kVKeyPageDown) scrollBy0 =  pageAmt0;
            else if (vkey == kVKeyHome)     scrollBy0 = -32767;
            else if (vkey == kVKeyEnd)      scrollBy0 =  32767;
            if (scrollBy0 != 0) { ApplyScrollDelta(scrollBy0, false); return; }
        } else if (vkey == kVKeyPageUp || vkey == kVKeyPageDown) {
            /* Page Up/Down while editing — scroll but preserve TE */
            short scrollBy0 = (vkey == kVKeyPageUp) ? -pageAmt0 : pageAmt0;
            ApplyScrollDelta(scrollBy0, true);
            return;
        }
    }

    if (e.modifiers & cmdKey) {
        if ((c == 'x' || c == 'v') && HasSel() && !AllowEdit(blockSelect, kEditText))
            return;
        if (c == 0x1E || c == 0x1F) {
            /* Cmd+Up/Down — move current block */
            moveBlock(c == 0x1E ? -1 : 1);
        } else if (c == 'z') {
            DoUndo();
        } else if ((c == 'c' || c == 'C') && (e.modifiers & shiftKey) && gDebugWin) {
            /* Cmd+Shift+C: copy debug console to clipboard */
            CopyDebugToClipboard();
        } else if ((c == 'c' || c == 'x') && textActive) {
            /* Copy / Cut — also save emoji sequences for the selected range.
               A cut takes a partly selected mention whole. */
            if (c == 'x' && HasSel()) SnapSelectionForEdit(pageElm[blockSelect], textActive, 0);
            {
                Block& b = pageElm[blockSelect];
                short selS = (*textActive)->selStart;
                short selE = (*textActive)->selEnd;
                CharsHandle ch = TEGetText(textActive);
                HLock((Handle)ch);
                sClipEmojiCount = 0;
                /* Count sentinels before selS once, then track incrementally */
                int idx = CountSentinels(*ch, 0, selS);
                for (short j = selS; j < selE && j < (*textActive)->teLength; j++) {
                    if ((unsigned char)(*ch)[j] == (unsigned char)kEmojiSentinel) {
                        if (idx < b.emojiCount && sClipEmojiCount < kMaxEmoji) {
                            memcpy(sClipEmoji[sClipEmojiCount], b.emojiSeqs[idx], 32);
                            sClipEmojiCount++;
                        }
                        idx++;
                    }
                }
                HUnlock((Handle)ch);
            }
            if (c == 'x' && HasSel()) UndoBegin(blockSelect, false);   /* its own undo step */
            if (c == 'x') TECut(textActive);
            else          TECopy(textActive);
            ZeroScrap();
            TEToScrap();
            if (c == 'x' && HasSel()) {
                Block& b = pageElm[blockSelect];
                SyncBlockTextKeepRuns(b, textActive);
                MarkDirtyAndRefresh(b);
            }
        } else if (c == 'v' && textActive && HasSel()) {
            /* Paste — merge clipboard emoji sequences into destination block;
               never into the middle of a mention */
            SnapSelectionForEdit(pageElm[blockSelect], textActive, 0);
            UndoBegin(blockSelect, false);   /* its own undo step */
            TEFromScrap();
            KeepTypingSize(textActive, pageElm[blockSelect].type);
            TEPaste(textActive);
            {
                /* Emoji the paste brings in take the copied sequences */
                Block& b = pageElm[blockSelect];
                SyncBlockTextKeepRuns(b, textActive, sClipEmoji, sClipEmojiCount);
                MarkDirtyAndRefresh(b);
            }
        } else if (c == 0x1C || c == 0x1D) {
            /* Cmd+Left: Back,  Cmd+Right: Forward */
            UpdateMenus();
            DoMenuCommand(((long)kMenuPages << 16)
                          | (c == 0x1C ? kItemBack : kItemFwd));
        } else {
            UpdateMenus();
            DoMenuCommand(MenuKey(c));
        }
    } else if (textActive == nil && HasSel()
               && c == 0x08
               && pageElm[blockSelect].type == BT_CHILD_PAGE
               && !gAppState.busy()) {
        /* Backspace on a child_page block (no TE): delete it */
        doDeleteBlock();
        return;
    } else if (textActive == nil
               && HasSel()
               && (c == 0x1E || c == 0x1F)) {
        /* Arrow key from a divider (no TE) */
        int dest = (c == 0x1E) ? blockSelect - 1 : blockSelect + 1;
        if (dest >= 0 && dest < pageElmCount) {
            blockSelect = dest;
            textActive  = pageElm[dest].textEdit;
            if (textActive != nil) {
                if (c == 0x1E) {
                    short len = (*textActive)->teLength;
                    TESetSelect(len, len, textActive);
                } else {
                    TESetSelect(0, 0, textActive);
                }
            }
            updateBlocks();
        }
    } else if (textActive != nil) {

        /* Extended-keyboard keys send control chars that TEKey would insert
           as text — Home's 0x01 is even the emoji sentinel.  Home/End move
           the caret, Enter acts as Return, the rest (Help, Esc, F-keys) do
           nothing here. */
        if ((unsigned char)c < 0x20 && c != 0x08 && c != '\t' && c != 0x0D
            && !(c >= 0x1C && c <= 0x1F)) {
            if (c == 0x03) {
                c = 0x0D;
            } else {
                if (vkey == kVKeyHome || vkey == kVKeyEnd) {
                    short to = (vkey == kVKeyHome) ? 0 : (*textActive)->teLength;
                    TESetSelect(to, to, textActive);
                }
                return;
            }
        }

        /* Typing, Return, Tab, Delete on a block that didn't fully load or
           is full: continue in a new block, or refuse */
        /* Mentions and emoji are one unit: move/widen the selection off
           their insides before the key applies (see SnapSelectionForEdit) */
        if (HasSel() && ((unsigned char)c >= 0x20 || c == 0x08 || c == 0x0D))
            SnapSelectionForEdit(pageElm[blockSelect], textActive, c);

        if (DivertBlockedKey(e, c)) return;

        /* ---- Tab: indent block into the toggle above ---- */
        if (c == '\t') {
            doIndentBlock();
        } else

        /* ---- Enter: split block and create new one below ---- */
        if (c == 0x0D && HasSel() && !gAppState.busy()) {
            if (!AllowEdit(blockSelect, kEditSplit)) return;
            /* Title: Enter moves focus to first body block,
               or creates a new paragraph if none exist yet */
            if (pageElm[blockSelect].type == BT_TITLE) {
                if (textActive) TEDeactivate(textActive);
                doAddBlock(0);
                return;
            }
            /* The block is cut at the caret before the new one is added:
               when no new row can be added, the cut-off text would land
               back in this block and replace what stays */
            if (pageElmCount >= gMaxBlocks || gCurRequest < kPhaseFullyLoaded) {
                FlashTooLarge();
                return;
            }
            Block& cur    = pageElm[blockSelect];
            short splitPos = (*textActive)->selEnd;
            ClearUndo();   /* one block became two: the snapshot can't come back */

            CharsHandle ch = TEGetText(textActive);
            short teLen    = (*textActive)->teLength;
            /* static scratch: keeps kMaxTextLen off the 68k stack; safe because
               this key handler is non-reentrant (single cooperative thread). */
            HLock((Handle)ch);
            { short cpLen = teLen < kTEBufLen-1 ? teLen : kTEBufLen-1;
              memcpy(sScratchTE, *ch, cpLen); sScratchTE[cpLen] = '\0'; teLen = cpLen; }
            HUnlock((Handle)ch);

            /* Split emojiSeqs at the sentinel boundary */
            int beforeEmoji = CountSentinels(sScratchTE, 0, splitPos < teLen ? splitPos : teLen);
            char afterSeqs[kMaxEmoji][32];
            uint8_t afterSeqCount = 0;
            { uint8_t ae = (uint8_t)beforeEmoji;
              while (ae < cur.emojiCount && afterSeqCount < kMaxEmoji) {
                  memcpy(afterSeqs[afterSeqCount++], cur.emojiSeqs[ae++], 32); } }
            cur.emojiCount = (uint8_t)beforeEmoji;

            /* Update current block with text before cursor; the runs of
               the rest go along with it to the new block */
            static TextRun sTailRuns[kMaxRuns];
            uint8_t tailRunCount = 0;
            {
              StripEmojiPadInto(sScratchTE, (size_t)splitPos, sScratchText, kMaxTextLen);
              short blen = (short)strlen(sScratchText);
              SplitRunsAt(cur, blen, sTailRuns, &tailRunCount);
              memcpy(cur.text, sScratchText, blen+1); cur.textLen = blen; }
            cur.dirty = true;
            MarkDirty();

            /* New block type: continue lists, else paragraph */
            BlockType ct = cur.type;
            BlockType newType = (ct == BT_BULLETED ||
                                 ct == BT_NUMBERED ||
                                 ct == BT_TODO) ? ct : BT_PARAGRAPH;
            int typeIdx = (int)newType;

            /* Deactivate old block to erase its cursor before inserting */
            {
                Rect oldRect = cur.rect;
                SetPort(gWindow);
                TEDeactivate(textActive);
                TEUpdate(&oldRect, textActive);
            }

            doAddBlock(typeIdx);

            /* Populate new block with text after cursor */
            { short afterStart = splitPos < teLen ? splitPos : teLen;
              short afterTeLen  = teLen - afterStart;
              if (afterTeLen > 0 && HasSel()) {
                Block& nb = pageElm[blockSelect];
                {
                  StripEmojiPadInto(sScratchTE + afterStart, (size_t)afterTeLen,
                                    sScratchText, kMaxTextLen);
                  short alen = (short)strlen(sScratchText);
                  memcpy(nb.text, sScratchText, alen+1); nb.textLen = alen; }
                nb.emojiCount = afterSeqCount;
                memcpy(nb.emojiSeqs, afterSeqs, 32*afterSeqCount);
                memcpy(nb.runs, sTailRuns, sizeof(TextRun) * tailRunCount);
                nb.runCount  = tailRunCount;
                nb.dirty     = true;
                nb.runsDirty = true;
                /* A fresh TE takes the text with its runs' styles */
                if (textActive == nb.textEdit) textActive = nil;
                nb.invalidateTE();
                updateBlocks();
                textActive = pageElm[blockSelect].textEdit;
              }
            }
            if (textActive != nil) {
                TEActivate(textActive);
                TESetSelect(0, 0, textActive);
                TEUpdate(&pageElm[blockSelect].rect, textActive);
            }
            return;
        }

        /* ---- Up/Down arrow: jump between blocks at boundary ---- */
        if ((c == 0x1E || c == 0x1F) && HasSel()) {
            short sel    = (*textActive)->selStart;
            short nLines = (*textActive)->nLines;
            bool  jump   = false;
            int   dest   = blockSelect;

            if (c == 0x1E) {  /* up arrow */
                bool onFirstLine = (nLines <= 1)
                    || (sel <= (*textActive)->lineStarts[1]);
                if (onFirstLine && blockSelect > 0) {
                    dest = blockSelect - 1;
                    jump = true;
                }
            } else {           /* down arrow */
                bool onLastLine = (nLines <= 1)
                    || (sel >= (*textActive)->lineStarts[nLines - 1]);
                if (onLastLine && blockSelect < pageElmCount - 1) {
                    dest = blockSelect + 1;
                    jump = true;
                }
            }

            if (jump) {
                SetPort(gWindow);
                TEDeactivate(textActive);
                RedrawBlock(pageElm[blockSelect], textActive);
                blockSelect = dest;
                textActive  = pageElm[dest].textEdit;
                if (textActive != nil) {
                    TEActivate(textActive);
                    if (c == 0x1E) {
                        short len = (*textActive)->teLength;
                        TESetSelect(len, len, textActive);
                    } else {
                        TESetSelect(0, 0, textActive);
                    }
                    RedrawBlock(pageElm[dest], textActive);
                } else {
                    updateBlocks();  /* landed on a divider */
                }
                DrawControls(gWindow);
                return;
            }
        }

        /* ---- Backspace on empty block:
               non-paragraph → demote to paragraph first
               paragraph     → delete the block              ---- */
        if (c == 0x08
            && (*textActive)->teLength == 0
            && blockSelect > 0 && blockSelect < pageElmCount
            && pageElm[blockSelect].type != BT_TITLE
            && !gAppState.busy()) {
            Block& bd = pageElm[blockSelect];
            if (bd.type != BT_PARAGRAPH && !InGroup(blockSelect)) {
                /* RetypeBlock carries a toggle's children along (and may
                   finish later, once a closed toggle's children load) */
                RetypeBlock(blockSelect, BT_PARAGRAPH);   /* relays out itself */
                if (pageElm[blockSelect].type == BT_PARAGRAPH)
                    reactivateAt(0);
            } else {
                doDeleteBlock();
            }
            return;
        }

        /* ---- Normal key: feed to TE, sync block text ---- */

        /* "New Page" placeholder: clear title on first printable keypress */
        if (pageElm[blockSelect].type == BT_TITLE
            && strcmp(pageElm[blockSelect].text, "New Page") == 0
            && c >= 0x20 && c != 0x7F) {
            TESetSelect(0, (*textActive)->teLength, textActive);
            TEDelete(textActive);
            pageElm[blockSelect].text[0] = '\0';
            pageElm[blockSelect].textLen = 0;
        }

        short linesBefore = (*textActive)->nLines;
        if (linesBefore < 1) linesBefore = 1;
        /* Bold-default block types (toggles, headings): if TE had no text,
           apply bold to the newly inserted character so continuation style
           inherits bold for subsequent keystrokes. */
        bool needsBoldInit = ((*textActive)->teLength == 0)
                           && blockDesc(pageElm[blockSelect].type).bold;
        if ((unsigned char)c >= 0x20 && c != 0x7F)
            KeepTypingSize(textActive, pageElm[blockSelect].type);
        TEKey(c, textActive);
        if (needsBoldInit && (*textActive)->teLength > 0) {
            short cur = (*textActive)->selStart;
            TESetSelect(0, (*textActive)->teLength, textActive);
            TextStyle _ts; _ts.tsFont = 0; _ts.tsSize = 0; _ts.tsFace = bold;
            TESetStyle(doFace, &_ts, false, textActive);
            TESetSelect(cur, cur, textActive);
        }

        Block& b = pageElm[blockSelect];
        if (b.textEdit == textActive) {
            CharsHandle ch    = TEGetText(textActive);
            short len         = (*textActive)->teLength;
            short selStart    = (*textActive)->selStart;
            short selEnd      = (*textActive)->selEnd;
            short linesAfter  = (*textActive)->nLines;
            if (linesAfter < 1) linesAfter = 1;
            HLock((Handle)ch);
            { short cpLen = len < kTEBufLen-1 ? len : kTEBufLen-1;
              memcpy(sScratchTE, *ch, cpLen); sScratchTE[cpLen] = '\0'; len = cpLen; }
            HUnlock((Handle)ch);
            StripEmojiPadInto(sScratchTE, (size_t)len, sScratchText, kMaxTextLen);
            short sStrippedLen = (short)strlen(sScratchText);

            if (sStrippedLen != b.textLen ||
                memcmp(sScratchText, b.text, sStrippedLen) != 0) {
                UndoBegin(blockSelect, true);   /* before b changes: the typing step */
                SyncEmojiSeqs(b, sScratchText, sStrippedLen, selStart);
                short brkStart, brkLen;
                AdjustRunsForEdit(b, sScratchText, sStrippedLen, selStart, &brkStart, &brkLen);
                memcpy(b.text, sScratchText, sStrippedLen+1); b.textLen = sStrippedLen;
                if (brkLen > 0) ClearLinkUnderline(b, brkStart, brkLen);
                b.dirty     = true;
                b.runsDirty = true;
                MarkDirty();
                /* Keep window title bar in sync with page title */
                if (b.type == BT_TITLE) {
                    setWindowTitleChrome(b.text);
                }
            }

            /* ---- Markdown prefix auto-conversion ----
               On space: if the paragraph starts with a recognised prefix,
               strip it and retype the block.  Quietly not for a split
               block or one with nested blocks: RetypeBlock would refuse
               them after the prefix was gone. */
            if (c == ' ' && b.type == BT_PARAGRAPH && !gAppState.busy()
                && !InGroup(blockSelect) && !b.hasChildren) {
                struct Pfx { const char* s; BlockType t; };
                static const Pfx kPfx[] = {
                    { "- ",   BT_BULLETED  },
                    { "* ",   BT_BULLETED  },
                    { "1. ",  BT_NUMBERED  },
                    { "[] ",  BT_TODO      },
                    { "[ ] ", BT_TODO      },
                    { "### ", BT_HEADING_3 },
                    { "## ",  BT_HEADING_2 },
                    { "# ",   BT_HEADING_1 },
                    { "> ",   BT_QUOTE     },
                    { "```",  BT_CODE      },
                };
                short caretPos = (*textActive)->selStart;
                BlockType newType = BT_UNKNOWN;
                int pfxLen = 0;
                for (int pi = 0; pi < 10; pi++) {
                    int plen = (int)strlen(kPfx[pi].s);
                    if (caretPos == plen
                        && (size_t)len >= (size_t)plen
                        && memcmp(sScratchTE, kPfx[pi].s, plen) == 0) {
                        newType = kPfx[pi].t;
                        pfxLen  = plen;
                        break;
                    }
                }
                if (newType != BT_UNKNOWN) {
                    TESetSelect(0, pfxLen, textActive);
                    TEDelete(textActive);
                    /* The prefix is plain ASCII, so emojiSeqs stay valid and the
                       runs just shift left by its length */
                    SyncBlockTextKeepRuns(b, textActive);
                    /* Re-created as the new type on save: Notion can't
                       change a block's type in place */
                    RetypeBlock(blockSelect, newType);
                    reactivateAt(0);
                    return;
                }
            }

            if (linesAfter != linesBefore) {
                /* Line wrap changed — re-layout so blocks below shift */
                updateBlocks();
                textActive = pageElm[blockSelect].textEdit;
                TESetSelect(selStart, selEnd, textActive);
            } else {
                /* Same line count — redraw only this block, no flicker */
                SetPort(gWindow);
                TEUpdate(&b.rect, textActive);
                { Block& _bk = pageElm[blockSelect];
                  _bk.invalidateEmojiPos();
                  DrawBlockEmojis(textActive, sScratchTE, (short)strlen(sScratchTE), _bk);
                  DrawStrikethrough(textActive, _bk); }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* doInContent                                                         */
/* ------------------------------------------------------------------ */
void doInContent(EventRecord *event, WindowPtr win) {
    /* Con window: handle scrollbar clicks */
    if (gDebugWin && win == gDebugWin) {
        SetPort(gDebugWin);
        Point lp = event->where; GlobalToLocal(&lp);
        HandleDebugClick(win, lp);
        return;
    }
    if (gWindow != win) return;
    if (gCurRequest < kPhaseFullyLoaded) return;

    ControlHandle control;
    SetPort(win);
    GlobalToLocal(&event->where);

    short part = FindControl(event->where, win, &control);

    /* Icon click — open picker */
    if (gPageIconName[0] != '\0' && PtInRect(event->where, &gIconRect)) {
        ShowIconPicker();
        return;
    }

    /* Sidebar controls: + (add block), ▴ (move up), ▾ (move down) */
    if (event->where.h < gColLeft) {
        if (PtInRect(event->where, &gAddBlockRect)) {
            int addIdx = gHoverBlockIdx;
            if (addIdx > 0 && addIdx < pageElmCount) {
                blockSelect = addIdx;
                BlockType ct = pageElm[addIdx].type;
                int typeIdx = 0;
                if (ct == BT_BULLETED) typeIdx = 4;
                else if (ct == BT_NUMBERED) typeIdx = 5;
                else if (ct == BT_TODO) typeIdx = 6;
                doAddBlock(typeIdx);
                if (HasSel())
                    textActive = pageElm[blockSelect].textEdit;
                SetSidebarPending(kSPPlus, blockSelect);
                gArrowsVisible = true;
                drawSidebarControls();
            }
            return;
        }
        if (PtInRect(event->where, &gMoveUpArrowRect)) {
            if (gHoverBlockIdx > 0) blockSelect = gHoverBlockIdx;
            moveBlock(-1);
            SetSidebarPending(kSPUp, blockSelect);
            gArrowsVisible = true;
            drawSidebarControls();
            return;
        }
        if (PtInRect(event->where, &gMoveDownArrowRect)) {
            if (gHoverBlockIdx > 0) blockSelect = gHoverBlockIdx;
            moveBlock(1);
            SetSidebarPending(kSPDown, blockSelect);
            gArrowsVisible = true;
            drawSidebarControls();
            return;
        }
        /* Save button (kept hidden; detect via rect) */
        if (gAppState.hasDirtyBlocks || gAppState.isSaving) {
            Rect btnR;
            { const short bw = kSidebarWidth - 8;
              const short bl = (gColLeft - bw) / 2 + 1;
              SetRect(&btnR, bl, gWindow->portRect.bottom - 23,
                      bl + bw, gWindow->portRect.bottom - 7); }
            if (PtInRect(event->where, &btnR)) {
                Rect inner;
                SetRect(&inner, btnR.left + 1, btnR.top + 1,
                        btnR.right - 1, btnR.bottom - 2);
                InvertRect(&inner);
                while (StillDown()) {}
                Point lp; GetMouse(&lp);
                InvertRect(&inner);
                gAppState.lastSaveLabelIdx = -1;
                updateSaveButton();
                if (PtInRect(lp, &btnR)) doSave();
                return;
            }
        }
    }

    if (part == 0) {
        /* Sync leaving block's TE content before changing selection */
        SyncActiveBlock();

        /* Deactivate and redraw old block to erase its cursor */
        if (textActive != nil) {
            TEDeactivate(textActive);
            EraseRect(&pageElm[blockSelect].vrect);
            RedrawBlock(pageElm[blockSelect], textActive);
            textActive = nil;
        }

        /* Find clicked block and check toggle-triangle in one pass */
        int prevBlockSelect = blockSelect;
        int clickedPi = -1;
        for (int _pi = 0; _pi < pageElmCount; _pi++) {
            Block* p = &pageElm[_pi];
            if (event->where.v <= p->rect.top || event->where.v >= p->rect.bottom) continue;
            clickedPi = _pi;
            if (isToggleType(p->type)) {
                short tx = gColLeft + 6 + p->depth * 20;
                if (event->where.h >= tx && event->where.h <= tx + 14) {
                    /* Triangle hit */
                    if (p->open) {
                        int toggleIdx = _pi;
                        closeToggleAt(toggleIdx);
                        blockSelect = toggleIdx;
                        textActive  = nil;
                        updateBlocks();
                        return;
                    } else {
                        blockSelect = p->pos;
                        textActive  = nil;
                        /* From the cache (0) we draw below.  A load (1) that
                           already finished drew the page in afterToggleLoad —
                           drawing again here was a second flash. */
                        int r = OpenToggleAt(_pi);
                        if (r == 1 && !gAppState.togglePending) return;
                    }
                    blockSelect = p->pos;
                    textActive  = nil;
                    updateBlocks();
                    return;
                }
            }
            break;
        }
        if (clickedPi >= 0) blockSelect = clickedPi; /* pos == array index by invariant */

        /* If we moved away from a divider, redraw it to clear selection highlight */
        if (prevBlockSelect != blockSelect &&
            prevBlockSelect >= 0 && prevBlockSelect < pageElmCount &&
            (pageElm[prevBlockSelect].type == BT_DIVIDER ||
             pageElm[prevBlockSelect].type == BT_CHILD_PAGE)) {
            updateBlocks();
        }

        if (pageElm[blockSelect].type == BT_CHILD_PAGE) {
            const Block& cp = pageElm[blockSelect];
            short cpRowTop = cp.rect.top + 3, cpRowBot = cp.rect.top + 25;
            short cpRight  = (cp.titleRight > 0) ? cp.titleRight + 4 : cp.rect.right;
            if (event->where.h >= gColLeft + 6 && event->where.h <= cpRight &&
                event->where.v >= cpRowTop && event->where.v <= cpRowBot) {
                scheduleNavigateTo(cp.id);
                textActive = nil;
                updateBlocks();
            } else {
                doAddBlock(0);
            }
        } else if (pageElm[blockSelect].type == BT_TODO && !pageElm[blockSelect].isCont &&
            [&]{ const Block& _tb = pageElm[blockSelect];
                 int _ed = (_tb.isChild && _tb.depth > 0) ? _tb.depth - 1 : _tb.depth;
                 return event->where.h >= gColLeft + 2 + _ed*20
                     && event->where.h <= gColLeft + 18 + _ed*20; }()) {
            if (!AllowEdit(blockSelect, kEditText)) return;
            pageElm[blockSelect].check = !pageElm[blockSelect].check;
            for (int gi = blockSelect + 1; gi < GroupEnd(blockSelect); gi++)
                pageElm[gi].check = pageElm[blockSelect].check;   /* split block pieces */
            pageElm[blockSelect].dirty = true;
            MarkDirty();
            updateBlocks();
        } else {
            textActive = pageElm[blockSelect].textEdit;
            if (textActive != nil) {
                TEActivate(textActive);
                TEClick(event->where, 0, textActive);
                /* --- notioncl:// link dispatch ---
                   After TEClick, selStart==selEnd means a simple click (not a drag).
                   Walk runs to find which one the caret landed in, then dispatch. */
                if ((*textActive)->selStart == (*textActive)->selEnd) {
                    short caretPos = (*textActive)->selStart;
                    int tePos = 0;
                    TextRun snapRuns[kMaxRuns]; uint8_t snapRunCount = pageElm[blockSelect].runCount;
                    if (snapRunCount > kMaxRuns) snapRunCount = kMaxRuns;
                    char* snapText = sScratchText; short snapTextLen = pageElm[blockSelect].textLen;
                    if (snapTextLen > kMaxTextLen-1) snapTextLen = kMaxTextLen-1;  /* leave room for NUL */
                    memcpy(snapRuns, pageElm[blockSelect].runs, sizeof(TextRun)*snapRunCount);
                    memcpy(snapText, pageElm[blockSelect].text, snapTextLen);
                    snapText[snapTextLen] = '\0';
                    bool dispatched = false;
                    for (int ri = 0; ri < snapRunCount; ri++) {
                        const TextRun& run = snapRuns[ri];
                        int runLen = TELenForText(snapText, run.textOffset, run.textLen);
                        if (caretPos >= tePos && caretPos <= tePos + runLen && run.url[0] != '\0') {
                            if (strncmp(run.url, "http://notioncl/", 16) == 0) {
                                const char* cmd = run.url + 16;
                                if (strcmp(cmd, "icon-picker") == 0)       { ShowIconPicker();            dispatched = true; }
                                else if (strcmp(cmd, "emoji-picker") == 0) { ShowEmojiPicker();           dispatched = true; }
                                else if (strncmp(cmd, "page/", 5) == 0) {
                                    scheduleNavigateTo(cmd + 5);
                                    dispatched = true;
                                }
                                else if (strcmp(cmd, "connection") == 0)   {
                                    prepareForPageList();
                                    if (ShowSettingsDialog(gSettings, false, "Connection",
                                                          DoFetchPages, DoFetchPageChildren, DoLibMovePage))
                                        AfterPickerConfirm();
                                    else
                                        AfterPickerCancel();
                                    dispatched = true;
                                }
                            }
                            break;
                        }
                        tePos += runLen;
                    }
                    if (dispatched) return;
                }
                RedrawBlock(pageElm[blockSelect], textActive);
            } else {
                updateBlocks();
            }
            DrawControls(gWindow);
        }
    } else if (control == gScrollBar) {
        if (part == kControlIndicatorPart) {
            TrackControl(control, event->where, nil);
            gScrollOffset = GetControlValue(gScrollBar);
            updateBlocks();
        } else {
            TrackControl(control, event->where, gScrollActionUPP);
        }
    }
}
