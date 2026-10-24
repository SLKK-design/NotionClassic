/* picker_geom.h — inline grid geometry helpers shared by icon and emoji pickers.
   Header-only so the compiler can inline on 68k.                              */
#pragma once

#include <Quickdraw.h>
#include "emoji_sprite.h"   /* EMOJI_TILE_W/H for EmojiSrcRect */

/* ------------------------------------------------------------------ */
/* GridMetrics — describes a single picker grid's geometry              */
/* ------------------------------------------------------------------ */
struct GridMetrics {
    short cell;     /* display cell size (tile + 2*pad) */
    short pad;      /* padding around tile inside cell   */
    short inset;    /* window inset (left + top)         */
    short tile;     /* sprite tile size (kPSCell / EMOJI_TILE_W) */
    short cols;     /* display columns                   */
    short visRows;  /* visible rows                      */
    short sprCols;  /* columns in the source sprite sheet */
};

/* ------------------------------------------------------------------ */
/* Rect helpers — inline to avoid call overhead on 68k                 */
/* ------------------------------------------------------------------ */

/* Outer cell rect: the full cell including padding */
static inline Rect CellRect(const GridMetrics& m, int row, int col) {
    short x = col * m.cell + m.inset, y = row * m.cell + m.inset;
    Rect r; SetRect(&r, x, y, x + m.cell, y + m.cell);
    return r;
}

/* Tile rect: the inner tile area where the sprite is drawn */
static inline Rect TileRect(const GridMetrics& m, int row, int col) {
    short x = col * m.cell + m.pad + m.inset;
    short y = row * m.cell + m.pad + m.inset;
    Rect r; SetRect(&r, x, y, x + m.tile, y + m.tile);
    return r;
}

/* Selection frame rect: 1px inset from cell edges */
static inline Rect SelFrameRect(const GridMetrics& m, int row, int col) {
    short x = col * m.cell + m.inset + 1;
    short y = row * m.cell + m.inset + 1;
    Rect r; SetRect(&r, x, y, x + m.cell - 2, y + m.cell - 2);
    return r;
}

/* Paint the round-rect selection background behind a frame rect */
static inline void PaintSelBg(Rect frame) {
    InsetRect(&frame, -2, -2);
    PaintRoundRect(&frame, 8, 8);
}

/* Source rect for an emoji tile at atlas pixel (sx,sy) — fixed tile size. */
static inline Rect EmojiSrcRect(short sx, short sy) {
    Rect r; SetRect(&r, sx, sy, sx + EMOJI_TILE_W, sy + EMOJI_TILE_H);
    return r;
}

/* Source rect for a sprite index in the sprite sheet */
static inline Rect SpriteSrcRect(const GridMetrics& m, int sprIdx) {
    int sc = sprIdx % m.sprCols, sr = sprIdx / m.sprCols;
    Rect r; SetRect(&r, sc * m.tile, sr * m.tile,
                        (sc + 1) * m.tile, (sr + 1) * m.tile);
    return r;
}

/* Source x,y for a sprite index (for memcpy-based GWorld blitting) */
static inline void SpriteSrcXY(const GridMetrics& m, int sprIdx,
                                short* sx, short* sy) {
    *sx = (short)((sprIdx % m.sprCols) * m.tile);
    *sy = (short)((sprIdx / m.sprCols) * m.tile);
}

/* Destination x,y for a cell in the grid (for GWorld blitting) */
static inline void TileDestXY(const GridMetrics& m, int row, int col,
                               short* dx, short* dy) {
    *dx = (short)(col * m.cell + m.pad + m.inset);
    *dy = (short)(row * m.cell + m.pad + m.inset);
}
