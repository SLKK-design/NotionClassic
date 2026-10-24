/* page_cache.cpp — page block content disk cache.

   File layout ("NotionclassicPageCache" in the Preferences folder):
     CacheFileHeader { magic 'NPgC', layout word, slot directory[4] }
     4 × 32 KB fixed slot payload areas at fixed offsets.

   Each payload is a compact serialization of pageElm (text + runs +
   emoji + ids only — no TE caches, rects, or runtime pointers), with a
   16-bit additive checksum in the slot directory to reject torn writes.

   Deliberately a SEPARATE file from NotionclassicPrefs: variable block
   data inside the prefs file would shift the fixed section offsets
   (see the PrefsHeader layout incident).                               */

#include "page_cache.h"
#include "blocks.h"
#include "app_state.h"     /* PageRequestPhase */
#include <Windows.h>
#include <stdio.h>
#include "Settings.h"      /* PageHeader */
#include "helpers.h"       /* SafeStrCopy */
#include "debug_con.h"
#include <Files.h>
#include <Folders.h>
#include <Memory.h>
#include <OSUtils.h>
#include <string.h>

extern Block*    pageElm;
extern int       pageElmCount;
extern int       gMaxBlocks;
extern int       gCurRequest;
extern bool      gHasMore;
extern char      gNextCursor[256];
extern bool      g_titleIsPlaceholder;
extern char      gPageIconName[32];
extern char      gPageIconColor[32];
extern WindowPtr gWindow;
extern bool      gSkipHeaderErase;

extern const char* EffPageID();
extern void MakeNewWindow(short procID);
extern void updateBlocks();
extern void addToRecent(const PageHeader& h);
extern void buildCurrentHeader(PageHeader& h);

bool gPageCacheBypassOnce = false;
bool gPageCacheNeedsSave  = false;

/* ------------------------------------------------------------------ */
/* File format                                                         */
/* ------------------------------------------------------------------ */
static const unsigned char kCacheFileName[] = "\pNotionclassicPageCache";
static const unsigned long kCacheMagic      = 0x4E506743UL; /* 'NPgC' */
static const int           kPageCacheSlots  = 4;
static const long          kSlotSize        = 32L * 1024L;
/* Cache hits skip the network fetch entirely, so bound how old a slot may
   be before forcing a refetch: edits made elsewhere stay invisible until
   then, and saving a block from a stale copy would overwrite them.  1 h
   keeps instant re-visits within a session while making that rare
   (was 24 h).  The clock restarts whenever the page is saved here. */
static const unsigned long kPageCacheTTLSecs = 60UL * 60UL;
/* Sanity bound for Block.depth restored from disk (Notion nesting is
   shallow; updateBlocks computes indents from it). */
static const int           kMaxBlockDepth   = 8;
static const unsigned long kCacheFormatVer  = 8; /* 2: run code/urlCut bits + colour byte; 3: textUrl byte; 4: cutShort flag; 5: isCont flag; 6: cut reason; 7: hasChildren; 8: kidsPartial */

/* Layout word: invalidates the whole file when block geometry or the
   serialization format changes between builds. */
static unsigned long CacheLayoutWord()
{
    return ((unsigned long)kMaxTextLen << 20)
         ^ ((unsigned long)kMaxRuns    << 16)
         ^ ((unsigned long)kMaxUrlLen  <<  8)
         ^ ((unsigned long)kMaxEmoji   <<  4)
         ^ kCacheFormatVer;
}

struct CacheSlotInfo {
    char           pageId[64];     /* EffPageID() at save time; "" = empty */
    unsigned long  savedDateTime;  /* GetDateTime secs — LRU eviction key  */
    long           payloadLen;     /* bytes used in the slot; 0 = empty    */
    unsigned short checksum;       /* 16-bit additive sum of payload       */
    unsigned short pad;
};

struct CacheFileHeader {
    unsigned long magic;
    unsigned long layout;
    CacheSlotInfo slots[kPageCacheSlots];
};

static long SlotDataOffset(int slot)
{
    return (long)sizeof(CacheFileHeader) + (long)slot * kSlotSize;
}

/* ------------------------------------------------------------------ */
/* File IO (same patterns as prefs_io.cpp, but app-level — keeps       */
/* src/core free of Notion-specific code)                              */
/* ------------------------------------------------------------------ */
static OSErr GetCacheSpec(FSSpec& spec)
{
    static FSSpec sCached;
    static bool   sValid = false;
    if (sValid) { spec = sCached; return noErr; }
    short vRefNum; long dirID;
    OSErr err = FindFolder(kOnSystemDisk, kPreferencesFolderType,
                           kCreateFolder, &vRefNum, &dirID);
    if (err != noErr) return err;
    err = FSMakeFSSpec(vRefNum, dirID, kCacheFileName, &spec);
    if (err == noErr || err == fnfErr) { sCached = spec; sValid = true; return noErr; }
    return err;
}

/* Ref-based primitives: SavePageCacheNow holds one refNum across its
   header-read + payload-write + header-write, halving File Manager
   open/close overhead on SCSI machines.                                */
static bool ReadAtRef(short ref, long offset, void* buf, long size)
{
    if (SetFPos(ref, fsFromStart, offset) != noErr) return false;
    long count = size;
    OSErr err = FSRead(ref, &count, buf);
    return (err == noErr || err == eofErr) && count == size;
}

static bool WriteAtRef(short ref, long offset, const void* buf, long size)
{
    /* HFS refuses SetFPos past EOF — extend the logical EOF first.
       NOTE: SetEOF does NOT zero the newly allocated space; unwritten slot
       gaps are guarded by payloadLen==0 directory entries + checksums,
       never by assuming zeroed bytes.                                   */
    long fileSize = 0;
    GetEOF(ref, &fileSize);
    if (offset + size > fileSize) {
        if (SetEOF(ref, offset + size) != noErr) return false;
    }
    if (SetFPos(ref, fsFromStart, offset) != noErr) return false;
    long count = size;
    return FSWrite(ref, &count, buf) == noErr && count == size;
}

static bool OpenCacheRW(const FSSpec* spec, short* ref)
{
    if (FSpOpenDF(spec, fsRdWrPerm, ref) == noErr) return true;
    if (FSpCreate(spec, 'CNot', 'cach', smSystemScript) != noErr) return false;
    return FSpOpenDF(spec, fsRdWrPerm, ref) == noErr;
}

static bool ReadAt(const FSSpec* spec, long offset, void* buf, long size)
{
    short ref;
    if (FSpOpenDF(spec, fsRdPerm, &ref) != noErr) return false;
    bool ok = ReadAtRef(ref, offset, buf, size);
    FSClose(ref);
    return ok;
}

/* Validate a just-read header; zeroed if missing or stale layout. */
static bool ValidateHeader(CacheFileHeader& hdr, bool readOk)
{
    if (!readOk || hdr.magic != kCacheMagic || hdr.layout != CacheLayoutWord()) {
        memset(&hdr, 0, sizeof(hdr));
        return false;
    }
    return true;
}

/* Read header; returns false (zeroed header) if missing or stale layout. */
static bool LoadHeader(const FSSpec* spec, CacheFileHeader& hdr)
{
    memset(&hdr, 0, sizeof(hdr));
    return ValidateHeader(hdr, ReadAt(spec, 0, &hdr, (long)sizeof(hdr)));
}

static unsigned short Checksum16(const unsigned char* p, long n)
{
    unsigned long sum = 0;
    for (long i = 0; i < n; i++) sum += p[i];
    return (unsigned short)(sum & 0xFFFF);
}

/* ------------------------------------------------------------------ */
/* Serialization helpers — memcpy keeps shorts alignment-safe          */
/* ------------------------------------------------------------------ */
static void PutS16(unsigned char* buf, long& off, short v)
{ memcpy(buf + off, &v, 2); off += 2; }

static short GetS16(const unsigned char* buf, long& off)
{ short v; memcpy(&v, buf + off, 2); off += 2; return v; }

/* Payload format:
     short blockCount
     char  iconName[32], iconColor[32]
     per block:
       u8 type | u8 flags(check|open|isChild) | u8 runCount | u8 emojiCount
       s16 depth | s16 pos | s16 textLen
       char id[37] | char parentId[37]
       textLen bytes of text
       runCount × { s16 off, s16 len, u8 style, u8 color, u8 textUrl, u8 urlLen, urlLen bytes }
       emojiCount × 32 bytes                                            */
static long SerializePage(unsigned char* buf, long cap)
{
    long off = 0;
    if (off + 2 + 64 > cap) return -1;
    PutS16(buf, off, (short)pageElmCount);
    memcpy(buf + off, gPageIconName,  32); off += 32;
    memcpy(buf + off, gPageIconColor, 32); off += 32;

    for (int i = 0; i < pageElmCount; i++) {
        const Block& b = pageElm[i];
        if (off + 4 + 6 + 74 + b.textLen > cap) return -1;
        buf[off++] = (unsigned char)b.type;
        buf[off++] = (unsigned char)((b.check   ? 1 : 0) |
                                     (b.open    ? 2 : 0) |
                                     (b.isChild ? 4 : 0) |
                                     (b.cutShort ? 8 : 0) |
                                     (b.cutShort == kCutMention ? 32 : 0) |
                                     (b.hasChildren ? 64 : 0) |
                                     (b.kidsPartial ? 128 : 0) |
                                     (b.isCont   ? 16 : 0));
        buf[off++] = b.runCount;
        buf[off++] = b.emojiCount;
        PutS16(buf, off, (short)b.depth);
        PutS16(buf, off, (short)b.pos);
        PutS16(buf, off, b.textLen);
        memcpy(buf + off, b.id,       37); off += 37;
        memcpy(buf + off, b.parentId, 37); off += 37;
        memcpy(buf + off, b.text, b.textLen); off += b.textLen;

        for (int r = 0; r < b.runCount; r++) {
            const TextRun& tr = b.runs[r];
            unsigned char urlLen = (unsigned char)strlen(tr.url);
            if (off + 8 + urlLen > cap) return -1;
            PutS16(buf, off, tr.textOffset);
            PutS16(buf, off, tr.textLen);
            buf[off++] = (unsigned char)((tr.bold          ? 1 : 0) |
                                         (tr.italic        ? 2 : 0) |
                                         (tr.underline     ? 4 : 0) |
                                         (tr.strikethrough ? 8 : 0) |
                                         (tr.code          ? 16 : 0) |
                                         (tr.urlCut        ? 32 : 0));
            buf[off++] = tr.color;
            buf[off++] = tr.textUrl;
            buf[off++] = urlLen;
            memcpy(buf + off, tr.url, urlLen); off += urlLen;
        }
        if (off + (long)b.emojiCount * 32 > cap) return -1;
        memcpy(buf + off, b.emojiSeqs, (long)b.emojiCount * 32);
        off += (long)b.emojiCount * 32;
    }
    return off;
}

/* Populate pageElm from a payload; false on any bounds/format violation.
   On failure the caller must reset pageElmCount.                       */
static bool DeserializePage(const unsigned char* buf, long len)
{
    long off = 0;
    if (len < 2 + 64) return false;
    short count = GetS16(buf, off);
    if (count < 1 || count > gMaxBlocks) return false;
    memcpy(gPageIconName,  buf + off, 32); off += 32;
    memcpy(gPageIconColor, buf + off, 32); off += 32;
    gPageIconName[31] = '\0'; gPageIconColor[31] = '\0';

    for (int i = 0; i < count; i++) {
        if (off + 4 + 6 + 74 > len) return false;
        Block& b = pageElm[i];
        b = Block();
        b.type = (BlockType)buf[off++];
        unsigned char flags = buf[off++];
        b.check   = (flags & 1) != 0;
        b.open    = (flags & 2) != 0;
        b.isChild = (flags & 4) != 0;
        b.cutShort = (flags & 32) ? kCutMention : (flags & 8) ? kCutTooLong : kCutNone;
        b.isCont   = (flags & 16) != 0;
        b.hasChildren = (flags & 64) != 0;
        b.kidsPartial = (flags & 128) != 0;
        b.runCount   = buf[off++];
        b.emojiCount = buf[off++];
        b.depth   = GetS16(buf, off);
        b.pos     = GetS16(buf, off);
        b.textLen = GetS16(buf, off);
        if (b.runCount > kMaxRuns || b.emojiCount > kMaxEmoji ||
            b.textLen < 0 || b.textLen >= kMaxTextLen) return false;
        /* The serializer writes blocks in array order (pos == index) and
           depth is small; updateBlocks() indexes pageElm[pos-1] raw, so a
           corrupt value that survives the additive checksum would read out
           of bounds. */
        if (b.pos != i || b.depth < 0 || b.depth > kMaxBlockDepth) return false;
        memcpy(b.id,       buf + off, 37); off += 37; b.id[36]       = '\0';
        memcpy(b.parentId, buf + off, 37); off += 37; b.parentId[36] = '\0';
        if (off + b.textLen > len) return false;
        memcpy(b.text, buf + off, b.textLen); off += b.textLen;
        b.text[b.textLen] = '\0';

        for (int r = 0; r < b.runCount; r++) {
            if (off + 8 > len) return false;
            TextRun& tr = b.runs[r];
            tr.textOffset = GetS16(buf, off);
            tr.textLen    = GetS16(buf, off);
            unsigned char style  = buf[off++];
            tr.color             = buf[off++];
            tr.textUrl           = buf[off++];
            unsigned char urlLen = buf[off++];
            tr.bold          = (style & 1) != 0;
            tr.italic        = (style & 2) != 0;
            tr.underline     = (style & 4) != 0;
            tr.strikethrough = (style & 8) != 0;
            tr.code          = (style & 16) != 0;
            tr.urlCut        = (style & 32) != 0;
            if (urlLen >= kMaxUrlLen || off + urlLen > len) return false;
            memcpy(tr.url, buf + off, urlLen); off += urlLen;
            tr.url[urlLen] = '\0';
            if (tr.textOffset < 0 || tr.textLen < 0 ||
                tr.textOffset + tr.textLen > b.textLen) return false;
        }
        if (off + (long)b.emojiCount * 32 > len) return false;
        memcpy(b.emojiSeqs, buf + off, (long)b.emojiCount * 32);
        off += (long)b.emojiCount * 32;
    }
    pageElmCount = count;
    return true;
}

/* ------------------------------------------------------------------ */
/* Slot selection                                                      */
/* ------------------------------------------------------------------ */
static int FindSlotForPage(const CacheFileHeader& hdr, const char* pid)
{
    for (int i = 0; i < kPageCacheSlots; i++)
        if (hdr.slots[i].payloadLen > 0 &&
            strncmp(hdr.slots[i].pageId, pid, sizeof(hdr.slots[i].pageId)) == 0)
            return i;
    return -1;
}

static int PickSaveSlot(const CacheFileHeader& hdr, const char* pid)
{
    int s = FindSlotForPage(hdr, pid);
    if (s >= 0) return s;                        /* overwrite own slot   */
    for (int i = 0; i < kPageCacheSlots; i++)
        if (hdr.slots[i].payloadLen == 0) return i;  /* first empty      */
    int oldest = 0;                              /* evict LRU            */
    for (int i = 1; i < kPageCacheSlots; i++)
        if (hdr.slots[i].savedDateTime < hdr.slots[oldest].savedDateTime)
            oldest = i;
    return oldest;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */
void SavePageCacheNow()
{
    if (gCurRequest != kPhaseFullyLoaded || gHasMore || pageElm == NULL || pageElmCount < 1)
        return;

    FSSpec spec;
    if (GetCacheSpec(spec) != noErr) return;

    /* One refNum for the whole header-read + payload + header sequence. */
    short ref;
    if (!OpenCacheRW(&spec, &ref)) return;

    CacheFileHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    ValidateHeader(hdr, ReadAtRef(ref, 0, &hdr, (long)sizeof(hdr)));
    hdr.magic  = kCacheMagic;
    hdr.layout = CacheLayoutWord();

    Ptr buf = NewPtr(kSlotSize);
    if (buf == NULL) { FSClose(ref); return; }  /* low memory — skip caching */

    long payloadLen = SerializePage((unsigned char*)buf, kSlotSize);
    const char* pid = EffPageID();
    int slot = PickSaveSlot(hdr, pid);

    if (payloadLen < 0) {
        /* Page too large for a slot — invalidate any stale copy of it. */
        if (FindSlotForPage(hdr, pid) >= 0) {
            memset(&hdr.slots[slot], 0, sizeof(hdr.slots[slot]));
            WriteAtRef(ref, 0, &hdr, (long)sizeof(hdr));
        }
        DEBUG_LOG("pageCache", "too big — not cached");
        DisposePtr(buf);
        FSClose(ref);
        return;
    }

    /* Payload first, header second: a torn write leaves a checksum
       mismatch, never a header pointing at garbage it vouches for. */
    bool ok = WriteAtRef(ref, SlotDataOffset(slot), buf, payloadLen);
    if (ok) {
        CacheSlotInfo& si = hdr.slots[slot];
        memset(si.pageId, 0, sizeof(si.pageId));
        SafeStrCopy(si.pageId, sizeof(si.pageId), pid);
        GetDateTime(&si.savedDateTime);
        si.payloadLen = payloadLen;
        si.checksum   = Checksum16((unsigned char*)buf, payloadLen);
        si.pad        = 0;
        ok = WriteAtRef(ref, 0, &hdr, (long)sizeof(hdr));
    }
    { char dbuf[48];
      snprintf(dbuf, sizeof(dbuf), "%s slot=%d %ldB %dblk",
               ok ? "saved" : "write failed", slot, payloadLen, pageElmCount);
      DEBUG_LOG("pageCache", dbuf); }
    DisposePtr(buf);
    FSClose(ref);
}

bool TryLoadPageFromCache()
{
    if (gPageCacheBypassOnce) {
        gPageCacheBypassOnce = false;
        DEBUG_LOG("pageCache", "bypassed (Reload)");
        return false;
    }
    if (pageElm == NULL || gMaxBlocks < 1) return false;

    FSSpec spec;
    if (GetCacheSpec(spec) != noErr) return false;

    CacheFileHeader hdr;
    if (!LoadHeader(&spec, hdr)) return false;

    const char* pid = EffPageID();
    int slot = FindSlotForPage(hdr, pid);
    if (slot < 0) return false;
    const CacheSlotInfo& si = hdr.slots[slot];
    if (si.payloadLen <= 0 || si.payloadLen > kSlotSize) return false;

    /* TTL: slots persist across sessions; a copy cached weeks ago would
       otherwise be served indefinitely (no fetch follows a hit), hiding
       edits made from other devices.  Stale slots fall through to the
       network path; the fresh fetch overwrites the slot afterwards. */
    {
        unsigned long now; GetDateTime(&now);
        if (now - si.savedDateTime > kPageCacheTTLSecs) {
            DEBUG_LOG("pageCache", "slot stale (TTL) — refetching");
            return false;
        }
    }

    Ptr buf = NewPtr(si.payloadLen);
    if (buf == NULL) return false;

    bool ok = ReadAt(&spec, SlotDataOffset(slot), buf, si.payloadLen)
           && Checksum16((unsigned char*)buf, si.payloadLen) == si.checksum
           && DeserializePage((unsigned char*)buf, si.payloadLen);
    DisposePtr(buf);

    if (!ok) {
        pageElmCount = 0;            /* discard any partial fill */
        DEBUG_LOG("pageCache", "slot corrupt — refetching");
        return false;
    }

    /* Mirror the network path's "final batch received" state. */
    gCurRequest          = kPhaseFullyLoaded;
    gHasMore             = false;
    gNextCursor[0]       = '\0';
    g_titleIsPlaceholder = false;

    { char dbuf[48];
      snprintf(dbuf, sizeof(dbuf), "hit slot=%d %dblk", slot, pageElmCount);
      DEBUG_LOG("pageCache", dbuf); }

    /* MakeNewWindow runs a full updateBlocks() + ValidRect internally —
       a second updateBlocks() pass here would double the most expensive
       UI operation in the app and re-flash the window white. */
    MakeNewWindow(documentProc);
    { PageHeader rh; buildCurrentHeader(rh); addToRecent(rh); }
    return true;
}
