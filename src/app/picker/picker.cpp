/* picker.cpp — sprite loading, icon drawing, and picker infrastructure.
   Self-contained except for three filesystem globals from main.cpp and
   SetListEmojiGWorld() from Settings.cpp (both declared extern below). */

#include "picker.h"
#include "helpers.h"
#include "emoji_sprite.h"
#include "utf8_mac_lite.h"   /* kEmojiSentinel, StripVariationSelectors */
#include "Settings.h"        /* kTitleEmojiMax */
#include "debug_con.h"
#include <Memory.h>
#include <Files.h>
#include <Resources.h>
#include <LowMem.h>
#include <string.h>
using std::vector;
using std::string;

/* ------------------------------------------------------------------ */
/* Globals (extern-visible via picker.h)                               */
/* ------------------------------------------------------------------ */
const char**   gIconNames        = nil;  /* pointers into gIconOrderBuf */
int            gIconCount        = 0;
GWorldPtr      gEmojiGWorld      = nil;
BitMap         gSpriteBM;

/* Private globals                                                     */
static Handle  gIconOrderBuf      = nil;  /* raw file data, kept locked  */
static Handle  gSpriteHandle      = nil;
static Handle  gEmojiSpriteHandle = nil; /* non-nil = sprite ready     */
static char    gIconCacheName[64] = {0};
static Handle  gIconCacheHandle   = nil;

/* ------------------------------------------------------------------ */
/* Extern access to main.cpp filesystem globals                        */
/* ------------------------------------------------------------------ */
extern short gAppVRefNum; /* volume ref of folder containing the app   */
extern long  gAppDirID;   /* directory ID of folder containing the app */
extern long  gIconsDirID; /* cached dirID of the assets/ subfolder     */

/* Forward declaration — defined in Settings.cpp                       */
void SetListEmojiGWorld(void* gw);

/* ------------------------------------------------------------------ */
/* resolveIconsDirID                                                    */
/* ------------------------------------------------------------------ */
static bool resolveIconsDirID()
{
    if (gIconsDirID != 0) return true;
    CInfoPBRec pb; Str255 fn = "\passets";
    memset(&pb, 0, sizeof(pb));
    pb.dirInfo.ioNamePtr   = fn;
    pb.dirInfo.ioVRefNum   = gAppVRefNum;
    pb.dirInfo.ioDrDirID   = gAppDirID;
    pb.dirInfo.ioFDirIndex = 0;
    if (PBGetCatInfoSync(&pb) != noErr) return false;
    gIconsDirID = pb.dirInfo.ioDrDirID;
    return true;
}

/* ------------------------------------------------------------------ */
/* GetAppResource                                                       */
/* Named resource from the app's own file (not the System file), or   */
/* nil.  Detached, so the caller owns a plain Handle.                  */
/* ------------------------------------------------------------------ */
static Handle GetAppResource(ResType type, ConstStr255Param name)
{
    short oldRes = CurResFile();
    UseResFile(LMGetCurApRefNum());
    Handle h = Get1NamedResource(type, name);
    UseResFile(oldRes);
    if (h) DetachResource(h);
    return h;
}

/* ------------------------------------------------------------------ */
/* load_pict_handle                                                     */
/* Return a locked Handle of raw PICT bytes: the app's own PICT        */
/* resource of that name, else the file in assets/ minus its 512-byte  */
/* header.  Caller HUnlock+DisposeHandle.                              */
/* ------------------------------------------------------------------ */
Handle load_pict_handle(ConstStr255Param filename)
{
    Handle h = GetAppResource('PICT', filename);
    if (h) { HLock(h); return h; }
    if (!resolveIconsDirID()) return nil;
    FSSpec spec;
    if (FSMakeFSSpec(gAppVRefNum, gIconsDirID, filename, &spec) != noErr) return nil;
    short refNum;
    if (FSpOpenDF(&spec, fsRdPerm, &refNum) != noErr) return nil;
    long fileSize = 0;
    if (GetEOF(refNum, &fileSize) != noErr || fileSize <= 512) { FSClose(refNum); return nil; }
    if (SetFPos(refNum, fsFromStart, 512) != noErr)            { FSClose(refNum); return nil; }
    long pictSize = fileSize - 512;
    h = NewHandle(pictSize);
    if (!h) { FSClose(refNum); return nil; }
    HLock(h);
    OSErr err = FSRead(refNum, &pictSize, *h);
    FSClose(refNum);
    if (err != noErr) { HUnlock(h); DisposeHandle(h); return nil; }
    return h;
}

/* ------------------------------------------------------------------ */
/* acquire_port_bitmap                                                  */
/* ------------------------------------------------------------------ */
BitMap* acquire_port_bitmap(GrafPtr port, PixMapHandle* outPM)
{
    *outPM = nil;
    if (((CGrafPtr)port)->portVersion & (short)0xC000) {
        *outPM = ((CGrafPtr)port)->portPixMap;
        LockPixels(*outPM);
        return (BitMap*)(**outPM);
    }
    return &port->portBits;
}

/* ------------------------------------------------------------------ */
/* FreeGraphicsAssets                                                   */
/* ------------------------------------------------------------------ */
void FreeGraphicsAssets()
{
    if (gSpriteHandle) {
        HUnlock(gSpriteHandle); DisposeHandle(gSpriteHandle);
        gSpriteHandle = nil; gSpriteBM.baseAddr = nil;
    }
    if (gIconCacheHandle) {
        DisposeHandle(gIconCacheHandle); gIconCacheHandle = nil;
        gIconCacheName[0] = '\0';
    }
    if (gEmojiGWorld) {
        UnlockPixels(GetGWorldPixMap(gEmojiGWorld));
        DisposeGWorld(gEmojiGWorld);
        gEmojiGWorld = nil; gEmojiSpriteHandle = nil;
    }
}

/* ------------------------------------------------------------------ */
/* LoadIconList                                                         */
/* ------------------------------------------------------------------ */
void LoadIconList()
{
    /* Free previous data */
    if (gIconNames) { DisposePtr((Ptr)gIconNames); gIconNames = nil; }
    if (gIconOrderBuf) { HUnlock(gIconOrderBuf); DisposeHandle(gIconOrderBuf); gIconOrderBuf = nil; }
    gIconCount = 0;

    Handle h = GetAppResource('TEXT', (ConstStr255Param)"\piconorder.txt");
    if (!h) return;
    long textLen = GetHandleSize(h);
    SetHandleSize(h, textLen + 1);
    if (MemError() != noErr) { DisposeHandle(h); return; }
    HLock(h);
    (*h)[textLen] = '\0';

    /* Count lines */
    int lineCount = 0;
    { char *s = *h, *e = *h + textLen; while (s < e) { if (*s == '\n') lineCount++; s++; } }
    if (textLen > 0 && (*h)[textLen - 1] != '\n') lineCount++;

    /* Allocate pointer array (single flat allocation) */
    const char** names = (const char**)NewPtr((long)lineCount * (long)sizeof(char*));
    if (!names) { HUnlock(h); DisposeHandle(h); return; }

    /* NUL-terminate lines in-place, store pointers */
    int idx = 0;
    char *p = *h, *end = *h + textLen;
    while (p < end && idx < lineCount) {
        char *nl = p;
        while (nl < end && *nl != '\n' && *nl != '\r') nl++;
        if (nl > p) {
            char *e = nl;
            if (e > p && *(e-1) == '\r') e--;
            *e = '\0';
            names[idx++] = p;
        }
        p = nl + 1;
    }

    gIconOrderBuf = h;       /* keep locked — pointers are into this buffer */
    gIconNames    = names;
    gIconCount    = idx;
}

/* ------------------------------------------------------------------ */
/* LoadSprite                                                           */
/* Load iconsprite.pict into a locked Handle; point gSpriteBM at the  */
/* pixel data.  No GWorld needed — we CopyBits directly from the BM.  */
/* ------------------------------------------------------------------ */
void LoadSprite()
{
    if (gSpriteHandle != nil) return;
    Handle h = load_pict_handle((ConstStr255Param)"\piconsprite.pict");
    if (!h) return;
    /* Unlock, move high, re-lock so the permanent lock sits at the top
       of the heap rather than fragmenting the middle of free space.   */
    HUnlock(h);
    MoveHHi(h);
    HLock(h);
    /* picFrame at PICT offset 2 gives us the sprite dimensions        */
    Rect picFrame;
    memcpy(&picFrame, *h + 2, sizeof(Rect));
    short sprW = picFrame.right  - picFrame.left;
    short sprH = picFrame.bottom - picFrame.top;
    if (sprW <= 0 || sprH <= 0 || sprW > 2048 || sprH > 8192) {
        HUnlock(h); DisposeHandle(h); return;
    }
    /* Pixel data starts at PICT v1 BitsRect offset 39:
       picSize(2)+picFrame(8)+opcode(1)+rowBytes(2)+bounds(8)+
       srcRect(8)+dstRect(8)+mode(2) = 39                             */
    short rowBytes = ((sprW + 15) / 16) * 2;
    gSpriteBM.baseAddr = *h + 39;
    gSpriteBM.rowBytes = rowBytes;
    SetRect(&gSpriteBM.bounds, 0, 0, sprW, sprH);
    gSpriteHandle = h;  /* keep locked — baseAddr must stay valid      */
}

/* ------------------------------------------------------------------ */
/* LoadEmojiSprite                                                      */
/* Load emoji_sprite.pict (PICT v1 BitsRect) into a 1-bit GWorld.    */
/* Reads raw bitmap at offset 39 — same format as iconsprite.pict.    */
/* ------------------------------------------------------------------ */
void LoadEmojiSprite()
{
    if (gEmojiSpriteHandle != nil) return;
    Handle h = load_pict_handle((ConstStr255Param)"\pemoji_sprite.pict");
    if (!h) { DEBUG_LOG("LoadEmojiSprite", "no PICT"); return; }
    Rect picFrame;
    memcpy(&picFrame, *h + 2, sizeof(Rect));
    short sprW = picFrame.right  - picFrame.left;
    short sprH = picFrame.bottom - picFrame.top;
    if (sprW <= 0 || sprH <= 0 || sprW > 2048 || sprH > 8192) {
        HUnlock(h); DisposeHandle(h); return;
    }
    /* Create 1-bit GWorld and copy raw bitmap into its PixMap */
    GWorldPtr gw = nil;
    if (NewGWorld(&gw, 1, &picFrame, nil, nil, 0) != noErr || !gw) {
        DEBUG_LOGF("LoadEmojiSprite", "NewGWorld failed, MaxBlock %ld", MaxBlock());
        HUnlock(h); DisposeHandle(h); return;
    }
    PixMapHandle pm = GetGWorldPixMap(gw);
    LockPixels(pm);
    /* Raw bitmap at PICT v1 BitsRect offset 39 */
    short srcRowBytes = ((sprW + 15) / 16) * 2;
    if (GetHandleSize(h) < 39 + (long)srcRowBytes * sprH) {
        HUnlock(h); DisposeHandle(h); DisposeGWorld(gw); return;
    }
    char* srcBits = *h + 39;
    char* dstBits = GetPixBaseAddr(pm);
    short dstRowBytes = (**pm).rowBytes & 0x3FFF;
    for (short row = 0; row < sprH; row++)
        memcpy(dstBits + row * dstRowBytes, srcBits + row * srcRowBytes, srcRowBytes);
    HUnlock(h); DisposeHandle(h);
    gEmojiGWorld       = gw;
    gEmojiSpriteHandle = (Handle)1;
}

/* ------------------------------------------------------------------ */
/* FindIconSpriteIdx — resolve a Notion icon slug to its sprite index. */
/* Returns -1 when the name is empty or not in the sprite sheet.       */
/* ------------------------------------------------------------------ */
int FindIconSpriteIdx(const char* name)
{
    if (!name || !name[0]) return -1;
    if (gIconCount == 0) LoadIconList();
    for (int i = 0; i < gIconCount; i++)
        if (strcmp(gIconNames[i], name) == 0) return i;
    return -1;
}

/* ------------------------------------------------------------------ */
/* DrawListPageIcon / *Idx — blit a Notion icon slug from the sprite  */
/* sheet. Called by Settings.cpp PageListDef and library list rows.   */
/* ------------------------------------------------------------------ */

void DrawListPageIconIdx(int sprIdx, short x, short y)
{
    if (sprIdx < 0) return;
    if (gSpriteHandle == nil) LoadSprite();
    if (gSpriteHandle == nil) return;
    int col = sprIdx % kSprCols;
    int row = sprIdx / kSprCols;
    Rect srcR; SetRect(&srcR, col * kPSCell, row * kPSCell,
                               (col+1) * kPSCell, (row+1) * kPSCell);
    Rect dstR = srcR; OffsetRect(&dstR, x - srcR.left, y - srcR.top);
    GrafPtr port; GetPort(&port);
    PixMapHandle dstPM; BitMap* dstBM = acquire_port_bitmap(port, &dstPM);
    if (dstBM) CopyBits(&gSpriteBM, dstBM, &srcR, &dstR, srcCopy, nil);
    if (dstPM) UnlockPixels(dstPM);
}

void DrawListPageIcon(const char* name, short x, short y)
{
    DrawListPageIconIdx(FindIconSpriteIdx(name), x, y);
}

/* DrawEmojiSpriteAt — blit one 16 px emoji from the emoji sprite at   */
/* (sprX, sprY) to (x, y) in the current port. sprX < 0 draws nothing. */
void DrawEmojiSpriteAt(short sprX, short sprY, short x, short y)
{
    if (sprX < 0) return;
    LoadEmojiSprite();
    static bool sLogged = false;   /* once, not per row */
    if (!gEmojiGWorld) {
        if (!sLogged) DEBUG_LOG("DrawEmojiSpriteAt", "no emoji sprite");
        sLogged = true; return;
    }
    sLogged = false;
    Rect srcR; SetRect(&srcR, sprX, sprY, sprX + 16, sprY + 16);
    Rect dstR; SetRect(&dstR, x, y, x + 16, y + 16);
    PixMapHandle pm = GetGWorldPixMap(gEmojiGWorld);
    LockPixels(pm);
    GrafPtr port; GetPort(&port);
    PixMapHandle dstPM; BitMap* dstBM = acquire_port_bitmap(port, &dstPM);
    if (dstBM) CopyBits((BitMap*)*pm, dstBM, &srcR, &dstR, srcCopy, nil);
    if (dstPM) UnlockPixels(dstPM);
    UnlockPixels(pm);
}

/* ------------------------------------------------------------------ */
/* Page titles with emoji — Library rows draw them from the sprite.   */
/* ------------------------------------------------------------------ */
enum { kTitleEmojiAdv = EMOJI_TILE_W + 2 };   /* tile + 2 px gap */

void TitleEmojiFromSeqs(const char (*seqs)[32], int count, short* titleEmoji)
{
    memset(titleEmoji, 0, kTitleEmojiMax * sizeof(short));
    if (count > kTitleEmojiMax) count = kTitleEmojiMax;
    for (int k = 0; k < count; k++) {
        char stripped[32];
        const EmojiSprite* sp =
            emoji_find_fallback(StripVariationSelectors(seqs[k], stripped, sizeof(stripped)));
        titleEmoji[k] = sp ? (short)(sp - EMOJI_TABLE + 1) : -1;
    }
}

void DrawTitleText(const char* title, const short* titleEmoji,
                   short x, short baseline, short emojiTop)
{
    MoveTo(x, baseline);
    int start = 0, k = 0;
    for (int i = 0; ; i++) {
        unsigned char c = (unsigned char)title[i];
        if (c != 0 && c != (unsigned char)kEmojiSentinel) continue;
        if (i > start) DrawText(title, (short)start, (short)(i - start));
        if (c == 0) break;
        Point pen; GetPen(&pen);
        short idx = k < kTitleEmojiMax ? titleEmoji[k] : 0;
        k++;
        if (idx > 0 && idx <= EMOJI_COUNT)
            DrawEmojiSpriteAt(EMOJI_TABLE[idx - 1].x, EMOJI_TABLE[idx - 1].y,
                              pen.h, emojiTop);
        MoveTo(pen.h + kTitleEmojiAdv, baseline);
        start = i + 1;
    }
}

short TitleTextWidth(const char* title)
{
    short w = 0;
    int start = 0;
    for (int i = 0; ; i++) {
        unsigned char c = (unsigned char)title[i];
        if (c != 0 && c != (unsigned char)kEmojiSentinel) continue;
        if (i > start) w += TextWidth(title, (short)start, (short)(i - start));
        if (c == 0) break;
        w += kTitleEmojiAdv;
        start = i + 1;
    }
    return w;
}

/* ------------------------------------------------------------------ */
/* Page icon pack: the app's 'PIcn' 128 resource holds every 40 px     */
/* page icon in iconorder.txt order, each trimmed to its ink with its  */
/* offset in the 40 px square (layout in scripts/make_icon_res.py).    */
/* Only the one icon on screen is read; it stays cached for redraws.   */
/* ------------------------------------------------------------------ */
enum { kPageIconSize = 40, kPageIconRB = 6 };   /* 40 px rounded to even bytes */
static Handle        sIconPack = nil;   /* resource handle, never loaded whole */
static bool          sPackIconOK = false;
static unsigned char sPackHdr[4];       /* left, top, width, height       */
static unsigned char sPackBits[kPageIconRB * kPageIconSize];

static long ReadLong(const unsigned char* p)
{
    return ((long)p[0] << 24) | ((long)p[1] << 16) | ((long)p[2] << 8) | p[3];
}

static bool LoadPackedPageIcon(int idx)
{
    if (idx < 0) return false;
    if (!sIconPack) {
        short oldRes = CurResFile();
        UseResFile(LMGetCurApRefNum());
        SetResLoad(false);
        sIconPack = Get1Resource('PIcn', 128);
        SetResLoad(true);
        UseResFile(oldRes);
        if (!sIconPack) return false;
    }
    unsigned char ent[8];
    ReadPartialResource(sIconPack, 2 + 4L * idx, ent, 8);
    if (ResError() != noErr) return false;
    long start = ReadLong(ent);
    long len   = ReadLong(ent + 4) - start;
    unsigned char buf[4 + 5 * kPageIconSize];
    if (len < 4 || len > (long)sizeof(buf)) return false;
    ReadPartialResource(sIconPack, start, buf, len);
    if (ResError() != noErr) return false;
    int w = buf[2], h = buf[3], srcRB = (w + 7) >> 3;
    if (buf[0] + w > kPageIconSize || buf[1] + h > kPageIconSize ||
        4 + srcRB * h != len) return false;
    memset(sPackBits, 0, sizeof(sPackBits));
    for (int r = 0; r < h; r++)
        memcpy(sPackBits + r * kPageIconRB, buf + 4 + r * srcRB, srcRB);
    memcpy(sPackHdr, buf, 4);
    return true;
}

static void DrawPackedPageIcon(short x, short y, short size)
{
    int w = sPackHdr[2], h = sPackHdr[3];
    if (w == 0 || h == 0) return;
    BitMap bm;
    bm.baseAddr = (Ptr)sPackBits;
    bm.rowBytes = kPageIconRB;
    SetRect(&bm.bounds, 0, 0, w, h);
    Rect dst;
    SetRect(&dst, x + sPackHdr[0] * size / kPageIconSize,
                  y + sPackHdr[1] * size / kPageIconSize,
                  x + (sPackHdr[0] + w) * size / kPageIconSize,
                  y + (sPackHdr[1] + h) * size / kPageIconSize);
    GrafPtr port; GetPort(&port);
    PixMapHandle dstPM; BitMap* dstBM = acquire_port_bitmap(port, &dstPM);
    if (dstBM) CopyBits(&bm, dstBM, &bm.bounds, &dst, srcCopy, nil);
    if (dstPM) UnlockPixels(dstPM);
}

/* ------------------------------------------------------------------ */
/* DrawNotionIcon                                                       */
/* Draw a native Notion icon by slug name: from the app's page icon    */
/* pack, or else from a <name>.pict file in assets/.  Single-entry     */
/* cache avoids re-reading on every updateBlocks call.                 */
/* ------------------------------------------------------------------ */
void DrawNotionIcon(const string& name, short x, short y, short size)
{
    if (gAppVRefNum == 0 || name.empty()) return;
    if (strcmp(name.c_str(), gIconCacheName) != 0) {
        if (gIconCacheHandle != nil) { DisposeHandle(gIconCacheHandle); gIconCacheHandle = nil; }
        gIconCacheName[0] = '\0';
        sPackIconOK = LoadPackedPageIcon(FindIconSpriteIdx(name.c_str()));
        if (!sPackIconOK) {
            size_t nameLen = (name.length() <= 26) ? name.length() : 26;
            Str255 pfname;
            pfname[0] = (unsigned char)(nameLen + 5);
            memcpy(pfname + 1, name.c_str(), nameLen);
            memcpy(pfname + 1 + nameLen, ".pict", 5);
            Handle h = load_pict_handle(pfname);
            if (!h) return;              /* icon file not found — silent fail */
            HUnlock(h);
            gIconCacheHandle = h;
        }
        strncpy(gIconCacheName, name.c_str(), 63); gIconCacheName[63] = '\0';
    }
    if (sPackIconOK) { DrawPackedPageIcon(x, y, size); return; }
    Rect destRect;
    SetRect(&destRect, x, y, x + size, y + size);
    DrawPicture((PicHandle)gIconCacheHandle, &destRect);
}

/* ------------------------------------------------------------------ */
/* ReloadListAssets                                                     */
/* Reload sprite + emoji GWorld after FetchNotionPageList freed them   */
/* to reclaim memory for the TLS handshake.  Called by ShowPagePanel.  */
/* ------------------------------------------------------------------ */
void ReloadListAssets()
{
    LoadIconList();
    LoadSprite();
    LoadEmojiSprite();
    SetListEmojiGWorld(gEmojiGWorld);
}
