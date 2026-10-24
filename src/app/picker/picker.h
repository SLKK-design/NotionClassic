/* picker.h — sprite loading, icon drawing, and picker infrastructure.
   Included by main.cpp (picker UI + block rendering) and Settings.cpp
   (PageListDef, library list).  No app-specific editor state here.    */
#pragma once

#include <Quickdraw.h>
#include <QDOffscreen.h>
#include <vector>
#include <string>

/* ------------------------------------------------------------------ */
/* Sprite geometry constants (shared across the boundary)              */
/* ------------------------------------------------------------------ */
enum { kPSCell  = 16,   /* sprite cell size: 192 px / 12 cols          */
       kSprCols = 12 }; /* columns in the icon sprite sheet            */

/* ------------------------------------------------------------------ */
/* Globals — defined in picker.cpp, used by main.cpp picker UI and     */
/* block-rendering code.                                               */
/* ------------------------------------------------------------------ */
extern const char**  gIconNames;   /* pointers into locked icon-order buffer */
extern int           gIconCount;   /* number of entries in gIconNames        */
extern GWorldPtr gEmojiGWorld;                   /* 1-bit offscreen GWorld for emoji     */
extern BitMap    gSpriteBM;                      /* points into locked sprite pixel data */

/* ------------------------------------------------------------------ */
/* Asset loading / teardown                                            */
/* ------------------------------------------------------------------ */
void LoadIconList();
void LoadSprite();
void LoadEmojiSprite();
void FreeGraphicsAssets();

/* Reload all assets after they were freed for a TLS handshake.
   Called by Settings.cpp ShowPagePanel.                               */
void ReloadListAssets();

/* ------------------------------------------------------------------ */
/* Icon drawing                                                         */
/* ------------------------------------------------------------------ */
/* Draw a native Notion icon PICT by slug name; cached — one Handle.  */
void DrawNotionIcon(const std::string& name, short x, short y,
                    short size = 40);

/* Resolve an icon slug to its sprite sheet index (-1 = not found).
   O(n) scan; call once at entry creation, not per draw.               */
int  FindIconSpriteIdx(const char* name);

/* Draw icon sprite by pre-resolved index.  O(1) per draw call.       */
void DrawListPageIconIdx(int sprIdx, short x, short y);

/* Draw a native Notion icon slug from the sprite sheet.  Convenience
   wrapper: calls FindIconSpriteIdx + DrawListPageIconIdx.             */
void DrawListPageIcon(const char* name, short x, short y);

/* Draw a 16 px emoji from the emoji sprite; sprX < 0 draws nothing. */
void DrawEmojiSpriteAt(short sprX, short sprY, short x, short y);

/* Page titles with emoji (Library rows). A title holds one sentinel per
   emoji; titleEmoji[k] is the k-th one's EMOJI_TABLE index + 1, -1 = not
   in the sprite, 0 = not known (a list cached before titles kept emoji). */
void  TitleEmojiFromSeqs(const char (*seqs)[32], int count, short* titleEmoji);
/* Draw title at (x, baseline) with each emoji's top at emojiTop.        */
void  DrawTitleText(const char* title, const short* titleEmoji,
                    short x, short baseline, short emojiTop);
short TitleTextWidth(const char* title);

/* ------------------------------------------------------------------ */
/* QuickDraw helper (needed by picker drawing code in main.cpp)        */
/* ------------------------------------------------------------------ */
/* Return destination BitMap for any GrafPort.  On Color QD, locks
   *outPM; caller must UnlockPixels(*outPM) when dstPM is non-nil.    */
BitMap* acquire_port_bitmap(GrafPtr port, PixMapHandle* outPM);

/* Open a PICT file from the assets/ folder, skip the 512-byte header,
   and return a locked Handle.  Caller must HUnlock+DisposeHandle.
   Returns nil on any error.  Also used by ShowAboutBox in main.cpp.  */
Handle load_pict_handle(ConstStr255Param filename);
