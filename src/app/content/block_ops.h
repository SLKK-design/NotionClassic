/* block_ops.h — Block array manipulation, toggle cache, and editing actions.
   Extracted from main.cpp.                                                   */
#pragma once
#include "blocks.h"
#include "HttpResponse.h"

/* ------------------------------------------------------------------ */
/* Toggle cache: a closed toggle's children, packed (PackRows) into a  */
/* movable handle — only the text, runs and emoji each row uses.       */
/* ------------------------------------------------------------------ */
struct ToggleCacheEntry { char id[37]; Handle rows; int count; };

/* Toggle cache globals — allocated once in main(); used across translation units. */
extern ToggleCacheEntry* gToggleCache;
extern int               gToggleCacheCount;

/* ------------------------------------------------------------------ */
/* Array helpers                                                       */
/* ------------------------------------------------------------------ */
bool HasSel();
void syncTextActive();
void reindexBlocks();
/* Row of the block with this serial (Block::serial), or -1 */
int  FindSerial(uint32_t serial);
/* Undo is the classic Mac toggle: one snapshot of one block, swapped
   with it by Cmd-Z, so a second Cmd-Z redoes.  A run of typing is one
   step; paste, cut and a style change each start their own.  It names
   its block by serial, so rows moving around don't affect it. */
void ClearUndo();
/* Snapshot pageElm[idx] before an edit.  typing: carry on with the open
   typing step of the same block instead of starting a new one. */
void UndoBegin(int idx, bool typing);
/* The block the snapshot belongs to, or -1 (none, or it's gone) */
int  UndoBlockIdx();
/* Swap the snapshot with block idx (UndoBlockIdx): undo, next time redo */
void UndoSwap(int idx);
/* Remove n rows with one tail shift */
void eraseBlocks(int first, int n);
/* Rows [first, first+n) packed into a movable handle (nil if no memory);
   UnpackRows inserts them at `at` with one tail shift (depth >= 0
   overrides each row's depth) and returns how many fit. */
Handle PackRows(int first, int n);
int    UnpackRows(Handle h, int count, int at, int depth);
void eraseBlockAt(int idx);
void insertBlockAt(int pos, const Block& blk);
void commitUIUpdate();
void reactivateAt(short sel);
int  DirectChildrenEnd(int parentIdx);
/* One past idx's whole subtree (nested levels included) */
int  SubtreeEnd(int idx);

/* ------------------------------------------------------------------ */
/* Toggle cache                                                        */
/* ------------------------------------------------------------------ */
void              ToggleCacheEvict(const char* id);
void              ToggleCacheClear();
ToggleCacheEntry* ToggleCacheFind(const char* id);
/* A row in some closed toggle's cache still has to be re-created */
bool              ToggleCacheHasRecreate();

/* Close the toggle at togglePos and cache its children. */
void closeToggleAt(int togglePos);

/* ------------------------------------------------------------------ */
/* HTTP helpers                                                        */
/* ------------------------------------------------------------------ */
bool handle_write_error(const HttpResponse& r, uint32_t* recreateSerial = nullptr);
/* What an edit is about to do to a block (see AllowEdit) */
enum EditKind {
    kEditText,      /* typing, paste, cut, undo, style, links, checkbox */
    kEditSplit,     /* Return: a new block after this one */
    kEditRetype,    /* another type (its whole split block) */
    kEditToToggle,  /* ... into a toggle */
    kEditIndent,    /* into the toggle above (re-created there) */
    kEditMove,      /* up or down */
    kEditDelete     /* the block, or one piece of a split block */
};
/* AllowEdit — the one check before an edit changes pageElm[idx].  When
   the block can't take it, beeps, flashes why where Save sits and returns
   false:  "Read only"  the page (Notion refused a write) or a block with
                        mentions we can't write back,
           "Too long"   the block didn't fully load (saving it would drop
                        the rest), or a split block's piece can't do this,
           "Too large"  a non-toggle with nested blocks we never load
                        would be re-created (they'd stay in the trash). */
bool AllowEdit(int idx, EditKind kind);
/* Delete safety net: a copy of the rows doDeleteBlock removed, put back by
   afterDeleteArchive when Notion doesn't confirm the trash. */
void KeepDeletedCopy(int first, int n);
void DropDeletedCopy();
bool RestoreDeletedBlocks();

/* Show a refused-edit/save label (kFlash*) where Save sits, with a beep. */
void Flash(uint8_t kind, bool beep = true);
/* Beep and show "Too long" (or "Read only") where Save sits. */
void FlashTooLong(bool readOnly = false);
/* Beep and show "Too large": nested blocks we can't carry along. */
void FlashTooLarge();
/* Beep and show "Not saved" (a save failed; Save stays, autosave retries).
   Safe from the save thread: only sets state, the main loop draws it.  */
void FlashNotSaved(bool beep = true);

/* Show pageElm[idx]'s children (a closed toggle): from the toggle cache
   (returns 0) or by starting a load (1, finishes in afterToggleLoad).
   -1 when another load or API call is in flight.                       */
int  OpenToggleAt(int idx);

/* Change pageElm[idx]'s block type.  A toggle's children move out to sit
   under it (toggle → other) or are re-created inside the new toggle
   (toggle → toggle), so they never go to Notion's trash with it.  A
   closed toggle is opened first; the change completes after its children
   load (FinishPendingRetype).                                          */
void RetypeBlock(int idx, BlockType newType);
/* Called by afterToggleLoad (loaded=false on failure) for the toggle. */
void FinishPendingRetype(uint32_t toggleSerial, bool loaded);

/* pageElm[idx] is a toggle with children (in Notion, cached or loaded). */
bool ToggleHasChildren(int idx);
/* Move the unit at idx (block + split pieces + loaded children) one
   sibling up (dir -1) or down (+1) within its parent.  Only one side of
   the swap is re-created on save — the neighbour when possible, so a
   closed toggle, a block with nested children or a read-only block moves
   without loading anything.  "Too large" only if neither side can be.
   beforeMove (optional) runs just before the swap when nothing had to
   load, so the rows' rects still show the old layout.                 */
typedef void (*MoveHook)(int s, int e, int nS, int nE, int dir);
void MoveUnit(int idx, int dir, MoveHook beforeMove = nullptr);

/* Close the toggles opened only to load children for a move/retype/indent,
   once everything is saved (main loop).  RecloseRenameId follows a
   toggle re-created under a new ID. */
void RecloseOpenedToggles();
void RecloseRenameId(const char* oldId, const char* newId);

/* Split blocks: a Notion block too long for one Block is loaded as a head
   piece followed by isCont pieces (same id); saved together as one.     */
int  GroupHead(int idx);          /* first piece of idx's group            */
int  FindBlockIdx(const char* id); /* row of the block (head piece) with id, or -1 */
int  GroupEnd(int head);          /* one past the group's last piece       */
bool InGroup(int idx);            /* idx belongs to a group of 2+ pieces   */
void MarkGroupCutShort(int idx, uint8_t why); /* every piece of idx's group: the strongest kCut* */
void parse_inserted_block_id(HttpResponse& resp, int elmIdx);

/* ------------------------------------------------------------------ */
/* Editing actions                                                     */
/* ------------------------------------------------------------------ */
void doAddBlock(int typeIdx);
void doDeleteBlock();
void doIndentBlock();
void doInsertPageLink();
void doNewPage(const char* parentIdOverride = nullptr);
