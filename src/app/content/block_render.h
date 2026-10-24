/* block_render.h — Block rendering, sidebar controls, scroll sync, and
   window/status utilities extracted from main.cpp.
   Included by main.cpp and any translation unit that needs to trigger a
   full page redraw or manipulate the save-button/sidebar state.         */
#pragma once
#include "blocks.h"
#include <TextEdit.h>
#include <Events.h>

/* ------------------------------------------------------------------ */
/* Sidebar pending-click state (enum + globals shared with main.cpp)   */
/* The event loop sets these to give visual feedback on sidebar clicks. */
/* ------------------------------------------------------------------ */
enum SidebarPending { kSPNone = 0, kSPPlus, kSPUp, kSPDown };

extern SidebarPending gSidebarPending;
extern long           gSidebarPendingTick;
extern int            gSidebarPendingBlock;
extern int            gHoverBlockIdx;

inline void SetSidebarPending(SidebarPending kind, int blockIdx) {
    gSidebarPending      = kind;
    gSidebarPendingTick  = TickCount();
    gSidebarPendingBlock = blockIdx;
}

/* Set this to true before calling MakeNewWindow() or updateBlocks() when
   the cached header placeholder is already on screen.  updateBlocks() will
   skip erasing the title+icon area so cached pixels aren't whited-out and
   redrawn when HTTP content arrives.  Automatically cleared after one use. */
extern bool gSkipHeaderErase;

/* Set before updateBlocks() to force a full-window erase (clears the scrollbar
   column + bottom margins that the targeted erase skips). Cleared after one use. */
extern bool gFullEraseOnce;

/* ------------------------------------------------------------------ */
/* Functions                                                           */
/* ------------------------------------------------------------------ */

/* Sync the active TE handle into blockSelect's text buffer. */
void SyncActiveBlock();

/* Mark blockIdx as a toggle-child and fire the archive→re-insert API sequence. */
void beginIndentOperation(int blockIdx, const char* parentId, int depth);

/* Swap the selected block up (dir=-1) or down (dir=+1) and queue API reorder. */
void moveBlock(int dir);

/* Repaint the sidebar + / ▴ / ▾ controls for the hovered block. */
void drawSidebarControls();

/* Full layout + redraw of all visible blocks; updates gColLeft, scroll bar,
   sidebar controls, and gTotalContentHeight. */
void updateBlocks();

/* Erase the window and draw msg centred (Geneva 12). */
void DrawStatusText(const char* msg);

/* Status line below the placeholder title, header left intact.
   Falls back to DrawStatusText when no header is on screen. */
void DrawStatusBelowHeader(const char* msg);


/* Ensure scroll bar + save button controls exist; resets scroll state.
   Called from paths that skip MakeNewWindow (cached preview match). */
void EnsureControls();

/* Create (or reuse) the main content window with the given procID;
   also creates the scroll bar and save-button controls on first call. */
void MakeNewWindow(short procID);

/* Redraw the save / progress-bar area in the sidebar. */
void updateSaveButton();

/* Force an immediate redraw of the save button (resets throttle). */
void InvalidateSaveButton();

/* Redraw a single block's TE + emoji + strikethrough without full updateBlocks(). */
void RedrawBlock(Block& b, TEHandle te);

/* Recompute arrow/sidebar button visibility for the current hovered block. */
void updateArrowVisibility();

/* Scroll action proc — fires while mouse held on scroll bar arrows/page areas. */
pascal void scrollActionProc(ControlHandle control, short part);

/* Reposition scroll bar and save button after window grow/resize. */
void resizeControls();

/* Apply a scroll delta to the scroll bar; preserveTE keeps TE active after scroll. */
void ApplyScrollDelta(short delta, bool preserveTE);
