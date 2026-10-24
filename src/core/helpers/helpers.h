/* helpers.h — shared drawing, dialog, and string utilities
   Included by both main.cpp and Settings.cpp.
   No app-specific state; pure Mac Toolbox primitives only.  */
#pragma once

#include <Quickdraw.h>
#include <Dialogs.h>
#include <Controls.h>
#include <Events.h>
#include <Scrap.h>
#include <TextEdit.h>
#include <Resources.h>
#include <string.h>
#include <ctype.h>

/* ------------------------------------------------------------------ */
/* Drawing primitives                                                  */
/* ------------------------------------------------------------------ */

/* Up/down arrow triangle (sidebar controls, library row move arrows).
   cx,cy = tip position. pointUp=true: tip at top, base below.
   Base scales with height: half-base = height-1.                      */
void DrawArrowTriangle(short cx, short cy, bool pointUp,
                       short height, const Pattern* fill);

/* Disclosure triangle: open = pointing down (▼), closed = pointing right (▶).
   cx,cy = tip position. Geometry: open ▼ 7 wide × 4 tall, closed ▶ 4 wide × 7 tall.
   outline param is ignored (kept for API compatibility); fill always applies. */
void DrawDisclosureTriangle(short cx, short cy, bool open,
                            const Pattern* fill, bool outline = false);

/* Magnifying glass icon for picker search bars.
   Drawn at fixed x=3..13 relative to current port; gy sets y offset. */
void DrawMagnifyingGlass(short gy, short gx = 3);

/* Blit a cached 16×16 SICN resource with srcOr at (x, y).             */
void DrawSICN(short resID, short x, short y);
void DrawPageIcon(short x, short y);    /* SICN 200 */
void DrawCheckedBox(short x, short y);  /* SICN 201 */

/* 2px-wide + cross used by sidebar add-block and library add-page buttons.
   cx,cy = visual centre; fill sets the pen pattern.                  */
void DrawPlusCross(short cx, short cy, const Pattern* fill);

/* Progress bar: fills [r.left..r.left+filledPx] black, rest ltGray.
   Caller is responsible for the guard and dedup check.                */
void DrawProgressBar(GrafPtr port, Rect r, short filledPx);

/* Custom button frame: 1px border on top/left/right with rounded top
   corners, label centred. r is the full visual rect of the button
   (including any border overhang above the logical hit area).         */
void DrawRoundButton(GrafPtr port, Rect r, ConstStr255Param label);

/* Word-wrap msg centred horizontally inside bounds, starting at 1/3
   from the top. Sets Geneva 12 before drawing. Clips to bounds.
   lineW controls the soft wrap width in characters (default 60).      */
void DrawCenteredText(GrafPtr port, Rect bounds, const char* msg, short lineW = 60);

/* ------------------------------------------------------------------ */
/* Font helpers                                                        */
/* ------------------------------------------------------------------ */

/* Set Geneva font at the given size and face style.
   Caches the font ID internally — avoids repeated GetFNum trap calls. */
void SetGenevaFont(short size = 12, short face = 0);

/* ------------------------------------------------------------------ */
/* Dialog helpers                                                      */
/* ------------------------------------------------------------------ */

/* Return the control handle for a dialog item (casts Handle for you). */
ControlHandle GetDialogControl(DialogPtr dlg, short itemID);

/* Fill r with the bounding rect of a dialog item.                    */
void GetDialogItemRect(DialogPtr dlg, short itemID, Rect& r);

/* Enable / disable a dialog button by item number.                   */
void EnableDialogButton(DialogPtr dlg, short itemID);
void DisableDialogButton(DialogPtr dlg, short itemID);

/* ------------------------------------------------------------------ */
/* String utilities                                                    */
/* ------------------------------------------------------------------ */

/* Safe strncpy: always NUL-terminates dst, handles NULL src.         */
void SafeStrCopy(char* dst, int dstLen, const char* src);

/* Paste from clipboard into a TE field, stripping CR/LF/tab.
   Replaces the entire TE content (select-all + delete + insert).    */
void PasteCleanIntoTE(TEHandle te);

/* C ↔ Pascal string conversions.                                     */
void CStrToPStr(const char* cstr, unsigned char* pstr);   /* pstr = Str255 */
void PStrToCStr(const unsigned char* pstr, char* cstr, int maxLen);

/* Lowercase a buffer in place (ASCII only).                          */
inline void LowerCaseInPlace(char* buf, int len)
{ for (int i = 0; i < len; i++) buf[i] = (char)tolower((unsigned char)buf[i]); }

/* Copy a 36-char Notion UUID into a char[37] field.
   Bounded copy: a full 36-char UUID fills dst[0..35] and dst[36]='\0';
   a short/empty/absent src (e.g. a missing "id" in a server response)
   stops at its NUL and zero-pads — never reads past src. */
inline void setId(char* dst, const char* src)
{ if (src && src[0]) { strncpy(dst, src, 36); dst[36] = '\0'; } else dst[0] = '\0'; }

/* Convert a 32-char hex UUID to 36-char 8-4-4-4-12 hyphenated form.
   out must be at least 37 bytes. If src is already hyphenated or an
   unexpected length, it is copied as-is (safe pass-through).         */
void addHyphensToId(const char* src, char* out, int outLen);

/* ------------------------------------------------------------------ */
/* Dialog event helpers                                                */
/* ------------------------------------------------------------------ */

/* Install a draw-proc UPP into each of the listed dialog item slots. */
void SetUserItems(DialogPtr dlg, const short* items, int count, UserItemUPP drawUPP);

/* Handle an inDrag mouseDown: drags dlg if hitWin matches.           */
bool HandleDialogDrag(DialogPtr dlg, EventRecord& e, WindowPtr hitWin);

/* ------------------------------------------------------------------ */
/* Scroll helpers                                                      */
/* ------------------------------------------------------------------ */

/* Map a scrollbar part code to a signed delta.
   part 20=inUpButton, 21=inDownButton, 22=inPageUp, 23=inPageDown.  */
short ScrollPartDelta(short part, short lineAmt, short pageAmt);


/* Draw the 3-px default-button ring around dialog item itemID.
   Call once after ShowWindow and again after every DrawDialog.         */
void DrawDefaultButtonRing(DialogPtr dlg, short itemID);

/* Create a standard vertical scroll bar (procID=16).
   val/minVal/maxVal default to 0; picker passes pre-computed values. */
ControlHandle MakeScrollBar(GrafPtr port, const Rect& r,
                            short val = 0, short minVal = 0, short maxVal = 0);

/* Dispatch a scroll-bar click to the right TrackControl variant.
   Returns the control value after tracking.                          */
short TrackScrollBar(ControlHandle bar, Point lp, short part,
                     ControlActionUPP actionUPP);
