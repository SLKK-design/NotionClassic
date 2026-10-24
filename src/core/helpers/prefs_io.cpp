/* prefs_io.cpp — All persistent state read/write for Notion classic.
   Extracted from Settings.cpp.  No UI, no Toolbox dialog code; pure
   file I/O via HFS FSSpec.  All public symbols are declared in Settings.h.

   Prefs file layout (all offsets defined in one enum below):
     [0]                     PrefsHeader         (magic + layout word)
     [kSettingsOffset]       AppSettings         (pageID + apiKey)
     [kDNSOffset]            CachedDNS           (resolved IP + GetDateTime)
     [kECDSAOffset]          PersistentECDSACacheEntry × PERSIST_ECDSA_SLOTS
     [kFullWidthOffset]      1 byte              (full-width toggle)
     [kDebugModeOffset]     1 byte              (debug console toggle)
     [kPageListOffset]       CachedPageList      (api key prefix + page list)
     [kHistoryOffset]        NavHistoryData      (back/fwd stacks)
     [kRecentOffset]         RecentPagesData     (recent page headers)
     [kMidpointOffset]       PersistedECMidpoint (Q2 = 2^128·Q, OPT #7)
     [kTLSSessionOffset]     PersistedTLSSession (session ticket + master)
     [kSeedOffset]           PersistedRandomSeed (not in the layout word)

   The header's layout word is derived from the struct sizes, so any
   struct change automatically invalidates ALL cached blocks at once
   (instead of leaving a half-valid file where e.g. the TLS ticket
   reads fine but the ECDSA cache is garbage — which silently disables
   the shamir2 fast path). Settings are migrated across layout changes
   when possible.                                                       */

#include "Settings.h"
#include "helpers.h"

#include <Files.h>
#include <Folders.h>
#include <string.h>

static const unsigned char kPrefsFileName[] = "\pNotionclassicPrefs";

/* ------------------------------------------------------------------ */
/* GetPrefsSpec — build an FSSpec pointing at the prefs file.         */
/* Returns noErr even if the file does not yet exist (fnfErr is ok).  */
/* Result is cached after the first successful call.                   */
/* ------------------------------------------------------------------ */
static FSSpec  sCachedPrefsSpec;
static bool    sCachedPrefsSpecValid = false;

static OSErr GetPrefsSpec(FSSpec &spec)
{
    if (sCachedPrefsSpecValid) { spec = sCachedPrefsSpec; return noErr; }
    short vRefNum;
    long  dirID;
    OSErr err = FindFolder(kOnSystemDisk, kPreferencesFolderType,
                           kCreateFolder, &vRefNum, &dirID);
    if (err != noErr) return err;
    err = FSMakeFSSpec(vRefNum, dirID, kPrefsFileName, &spec);
    if (err == noErr || err == fnfErr) {
        sCachedPrefsSpec      = spec;
        sCachedPrefsSpecValid = true;
        return noErr;
    }
    return err;
}

/* ------------------------------------------------------------------ */
/* ReadFileAt — open, seek, read, close.  Returns false on any error. */
/* ------------------------------------------------------------------ */
static bool ReadFileAt(const FSSpec *spec, long offset, void *buf, long size)
{
    short ref;
    if (FSpOpenDF(spec, fsRdPerm, &ref) != noErr) return false;
    bool ok = false;
    if (SetFPos(ref, fsFromStart, offset) == noErr) {
        long count = size;
        OSErr err  = FSRead(ref, &count, buf);
        ok = (err == noErr || err == eofErr) && count == size;
    }
    FSClose(ref);
    return ok;
}

/* ------------------------------------------------------------------ */
/* WriteFileAt — open-or-create, optionally extend with zeros, seek,  */
/* write, close.  extend=true pads file to (offset+size) with zeros.  */
/* ------------------------------------------------------------------ */
static void ExtendFileWithZeros(short ref, long needed)
{
    char zero[256];
    memset(zero, 0, sizeof(zero));
    SetFPos(ref, fsFromLEOF, 0);
    while (needed > 0) {
        long chunk = needed < (long)sizeof(zero) ? needed : (long)sizeof(zero);
        if (FSWrite(ref, &chunk, zero) != noErr) break;
        needed -= chunk;
    }
}

static bool WriteFileAt(const FSSpec *spec, long offset, const void *buf,
                        long size, bool extend)
{
    short ref;
    if (FSpOpenDF(spec, fsWrPerm, &ref) != noErr) {
        if (FSpCreate(spec, 'CNot', 'pref', smSystemScript) != noErr) return false;
        if (FSpOpenDF(spec, fsWrPerm, &ref) != noErr) return false;
    }
    if (extend) {
        long fileSize = 0;
        GetEOF(ref, &fileSize);
        long needed = offset + size - fileSize;
        if (needed > 0) ExtendFileWithZeros(ref, needed);
    }
    bool ok = false;
    if (SetFPos(ref, fsFromStart, offset) == noErr) {
        long count = size;
        ok = (FSWrite(ref, &count, buf) == noErr);
    }
    FSClose(ref);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Version header — magic + layout word at offset 0.                  */
/* PrefsLayoutWord()/EnsurePrefsHeader are defined further down (they */
/* need the late-defined cache structs); declared here so the Load/   */
/* Save functions above them can validate the file first.             */
/* ------------------------------------------------------------------ */
struct PrefsHeader {
    unsigned long magic;    /* kPrefsMagic */
    unsigned long layout;   /* PrefsLayoutWord() — struct-size fingerprint */
};

enum {
    kSettingsOffset = (long)sizeof(PrefsHeader)
};

static OSErr GetPrefsSpecChecked(FSSpec &spec);

/* ------------------------------------------------------------------ */
/* ReadPref / WritePref — thin wrappers that fold GetPrefsSpecChecked  */
/* + ReadFileAt / WriteFileAt into a single call.                      */
/* ------------------------------------------------------------------ */
static bool ReadPref(long offset, void *buf, long size)
{
    FSSpec spec;
    if (GetPrefsSpecChecked(spec) != noErr) return false;
    return ReadFileAt(&spec, offset, buf, size);
}

static bool WritePref(long offset, const void *buf, long size, bool extend = false)
{
    FSSpec spec;
    if (GetPrefsSpecChecked(spec) != noErr) return false;
    return WriteFileAt(&spec, offset, buf, size, extend);
}

/* ------------------------------------------------------------------ */
/* LoadSettings — fills empty strings if no prefs file exists yet     */
/* ------------------------------------------------------------------ */
bool LoadSettings(AppSettings &settings)
{
    settings.pageID[0] = '\0';
    settings.apiKey[0] = '\0';

    if (!ReadPref(kSettingsOffset, &settings, (long)sizeof(AppSettings))) {
        /* Partial read (truncated floppy write): FSRead already overwrote the fields
           we cleared above; reset them so no garbage leaks into EffPageID/EffAPIKey. */
        settings.pageID[0] = '\0';
        settings.apiKey[0] = '\0';
        return false;
    }

    /* Ensure null termination after raw read */
    settings.pageID[sizeof(settings.pageID) - 1] = '\0';
    settings.apiKey[sizeof(settings.apiKey) - 1] = '\0';

    /* Sanitize: if either field contains non-printable bytes it was read from
       an uninitialised gap in the prefs file — treat it as empty. */
    for (int i = 0; settings.pageID[i]; i++) {
        unsigned char c = (unsigned char)settings.pageID[i];
        if (c < 0x20 || c > 0x7E) { settings.pageID[0] = '\0'; break; }
    }
    for (int i = 0; settings.apiKey[i]; i++) {
        unsigned char c = (unsigned char)settings.apiKey[i];
        if (c < 0x20 || c > 0x7E) { settings.apiKey[0] = '\0'; break; }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* SaveSettings                                                        */
/* ------------------------------------------------------------------ */
void SaveSettings(const AppSettings &settings)
{
    WritePref(kSettingsOffset, &settings, (long)sizeof(AppSettings));
}

/* ------------------------------------------------------------------ */
/* File layout — all offsets computed from struct sizes in one place. */
/* kFullWidthPad = 2 bytes (1 value + 1 alignment) so kPageListOffset */
/* lands on an even address for the short fields in NotionPageEntry.  */
/* ------------------------------------------------------------------ */
enum {
    kDNSOffset       = kSettingsOffset + (long)sizeof(AppSettings),
    kECDSAOffset     = kDNSOffset     + (long)sizeof(CachedDNS),
    kFullWidthOffset = kECDSAOffset   + (long)(sizeof(PersistentECDSACacheEntry) * PERSIST_ECDSA_SLOTS)
};

/* ------------------------------------------------------------------ */
/* DNS cache — OPT-2                                                   */
/* ------------------------------------------------------------------ */
void LoadDNSCache(CachedDNS &out)
{
    out.ip = 0; out.secs_written = 0;
    if (!ReadPref(kDNSOffset, &out, (long)sizeof(CachedDNS)))
        out.ip = 0;
}

void SaveDNSCache(const CachedDNS &in)
{
    WritePref(kDNSOffset, &in, (long)sizeof(CachedDNS), true);
}

/* ------------------------------------------------------------------ */
/* Persistent ECDSA verify cache — OPT-3                              */
/* ------------------------------------------------------------------ */
void LoadPersistECDSACache(PersistentECDSACacheEntry slots[PERSIST_ECDSA_SLOTS])
{
    memset(slots, 0, sizeof(PersistentECDSACacheEntry) * PERSIST_ECDSA_SLOTS);
    long ecdsaSz = (long)sizeof(PersistentECDSACacheEntry) * PERSIST_ECDSA_SLOTS;
    if (!ReadPref(kECDSAOffset, slots, ecdsaSz))
        memset(slots, 0, (size_t)ecdsaSz);
}

void SavePersistECDSACache(const PersistentECDSACacheEntry slots[PERSIST_ECDSA_SLOTS])
{
    /* extend=true: kECDSAOffset may be beyond EOF if prefs file was written by an
       older build that didn't include this section. Without extending, SetFPos fails
       silently and the ECDSA data is never written. */
    WritePref(kECDSAOffset, slots,
              (long)sizeof(PersistentECDSACacheEntry) * PERSIST_ECDSA_SLOTS, true);
}

/* ------------------------------------------------------------------ */
/* Full-width layout preference                                        */
/* ------------------------------------------------------------------ */
bool LoadFullWidth()
{
    unsigned char val = 0;
    ReadPref(kFullWidthOffset, &val, 1);
    return (val == 1);
}

void SaveFullWidth(bool value)
{
    unsigned char val = value ? 1 : 0;
    WritePref(kFullWidthOffset, &val, 1, true);
}

/* ------------------------------------------------------------------ */
/* Debug-mode preference (stored at kFullWidthOffset + 1)              */
/* ------------------------------------------------------------------ */
enum { kDebugModeOffset = kFullWidthOffset + 1 };

bool LoadDebugMode()
{
    unsigned char val = 0;
    ReadPref(kDebugModeOffset, &val, 1);
    return (val == 1);
}

void SaveDebugMode(bool value)
{
    unsigned char val = value ? 1 : 0;
    WritePref(kDebugModeOffset, &val, 1, true);
}

/* ------------------------------------------------------------------ */
/* Page list disk cache                                               */
/* ------------------------------------------------------------------ */

/* Stored in the prefs file immediately after the FullWidth byte.
   Aligned to an even offset so short fields in NotionPageEntry land
   on even addresses in memory when read back on 68k.                */
struct CachedPageList {
    char         apiKeyPrefix[8]; /* first 8 chars of API key — invalidates on token change */
    NotionPageList list;
};

/* Navigation history */
struct NavHistoryData {
    PageHeader back[kNavHistMax];
    int        backCount;
    PageHeader fwd[kNavHistMax];
    int        fwdCount;
};

/* Recent pages list */
struct RecentPagesData {
    PageHeader pages[kMaxRecentPages];
    int        count;
};

/* All offsets after FullWidth — defined here once all struct sizes are known */
enum {
    kPageListOffset    = kFullWidthOffset + 2,
    kHistoryOffset     = kPageListOffset  + (long)sizeof(CachedPageList),
    kRecentOffset      = kHistoryOffset   + (long)sizeof(NavHistoryData),
    kMidpointOffset    = kRecentOffset    + (long)sizeof(RecentPagesData),
    kTLSSessionOffset  = kMidpointOffset  + (long)sizeof(PersistedECMidpoint),
    kSeedOffset        = kTLSSessionOffset + (long)sizeof(PersistedTLSSession)
};

/* ------------------------------------------------------------------ */
/* Version header implementation (declared above LoadSettings).       */
/* ------------------------------------------------------------------ */
static const unsigned long kPrefsMagic = 0x50724673UL;  /* 'PrFs' */

static unsigned long PrefsLayoutWord()
{
    /* Each field gets a distinct base on a 4-bit grid so a size change in one
       struct can't XOR-cancel a change in another. PersistedECMidpoint sits at
       <<26 (not <<9, which was off-grid and overlapped three neighbours). */
    return ((unsigned long)sizeof(PersistedECMidpoint) << 26)
         ^ ((unsigned long)sizeof(AppSettings)        << 22)
         ^ ((unsigned long)sizeof(CachedDNS)          << 18)
         ^ ((unsigned long)(sizeof(PersistentECDSACacheEntry) * PERSIST_ECDSA_SLOTS) << 14)
         ^ ((unsigned long)sizeof(CachedPageList)     << 10)
         ^ ((unsigned long)sizeof(NavHistoryData)     << 6)
         ^ ((unsigned long)sizeof(RecentPagesData)    << 2)
         ^  (unsigned long)sizeof(PersistedTLSSession);
}

/* Printable-ASCII rule used by LoadSettings, applied to one field. */
static bool FieldLooksValid(const char *s, long max)
{
    if (!s[0]) return false;
    for (long i = 0; i < max && s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c > 0x7E) return false;
    }
    return true;
}

/* EnsurePrefsHeader — checked once per launch.
   On magic/layout mismatch (legacy file or struct change): salvage the
   settings if recognizable, truncate everything, write a fresh header,
   and re-save the salvaged settings. All caches (DNS/ECDSA/page list/
   history/recent/TLS) are discarded — they rebuild on the next run.   */
static bool sPrefsChecked  = false;
static bool sPrefsHeaderOK = false;

static bool EnsurePrefsHeader(const FSSpec *spec)
{
    if (sPrefsChecked) return sPrefsHeaderOK;
    sPrefsChecked = true;

    short ref;
    if (FSpOpenDF(spec, fsRdWrPerm, &ref) != noErr) {
        if (FSpCreate(spec, 'CNot', 'pref', smSystemScript) != noErr)
            return false;
        if (FSpOpenDF(spec, fsRdWrPerm, &ref) != noErr)
            return false;
    }

    PrefsHeader h;
    memset(&h, 0, sizeof(h));
    long count = (long)sizeof(h);
    bool good = false;
    if (SetFPos(ref, fsFromStart, 0) == noErr &&
        FSRead(ref, &count, &h) == noErr &&
        count == (long)sizeof(h) &&
        h.magic == kPrefsMagic && h.layout == PrefsLayoutWord())
    {
        good = true;
    }

    if (!good) {
        /* Salvage settings. Header-format files (magic ok, layout changed)
           store AppSettings at kSettingsOffset; legacy files (no header)
           stored them at offset 0. Reading a header-format file at 0 would
           salvage the header bytes as "settings" and lose the key.        */
        long keyOff = (h.magic == kPrefsMagic) ? (long)kSettingsOffset : 0;
        AppSettings old;
        memset(&old, 0, sizeof(old));
        long oc = (long)sizeof(old);
        SetFPos(ref, fsFromStart, keyOff);
        FSRead(ref, &oc, &old);   /* partial read OK */
        old.pageID[sizeof(old.pageID) - 1] = '\0';
        old.apiKey[sizeof(old.apiKey) - 1] = '\0';
        if (!FieldLooksValid(old.pageID, (long)sizeof(old.pageID)))
            old.pageID[0] = '\0';
        bool haveKey = FieldLooksValid(old.apiKey, (long)sizeof(old.apiKey));
        if (!haveKey) old.apiKey[0] = '\0';

        /* Wipe stale layout, write fresh header (+ salvaged settings). */
        SetEOF(ref, 0);
        h.magic  = kPrefsMagic;
        h.layout = PrefsLayoutWord();
        count = (long)sizeof(h);
        if (SetFPos(ref, fsFromStart, 0) == noErr &&
            FSWrite(ref, &count, &h) == noErr)
        {
            good = true;
            if (haveKey || old.pageID[0]) {
                long sc = (long)sizeof(AppSettings);
                FSWrite(ref, &sc, &old);   /* lands at kSettingsOffset */
            }
        }
    }

    FSClose(ref);
    sPrefsHeaderOK = good;
    return good;
}

/* GetPrefsSpec + header validation — used by every Load/Save here. */
static OSErr GetPrefsSpecChecked(FSSpec &spec)
{
    OSErr err = GetPrefsSpec(spec);
    if (err != noErr) return err;
    if (!EnsurePrefsHeader(&spec)) return ioErr;
    return noErr;
}

static void BuildApiKeyPrefix(const char* apiKey, char out[8])
{
    memset(out, 0, 8);
    int n = apiKey ? (int)strlen(apiKey) : 0;
    if (n > 8) n = 8;
    if (n > 0) memcpy(out, apiKey, n);
}

void SavePageListCache(const char* apiKey, const NotionPageList& list)
{
    CachedPageList buf;
    memset(&buf, 0, sizeof(buf));
    BuildApiKeyPrefix(apiKey, buf.apiKeyPrefix);
    buf.list = list;
    WritePref(kPageListOffset, &buf, (long)sizeof(CachedPageList), true);
}

bool LoadPageListCache(const char* apiKey, NotionPageList& out)
{
    out.count = 0;
    CachedPageList buf;
    memset(&buf, 0, sizeof(buf));
    if (!ReadPref(kPageListOffset, &buf, (long)sizeof(CachedPageList))) return false;
    char prefix[8];
    BuildApiKeyPrefix(apiKey, prefix);
    bool ok = (memcmp(buf.apiKeyPrefix, prefix, 8) == 0) &&
              (buf.list.count > 0) && (buf.list.count <= kMaxNotionPages);
    if (ok) {
        out = buf.list;
        /* Harden against a corrupt-but-plausible prefs file: force-terminate
           every fixed char field before these are used as C strings. */
        for (int i = 0; i < out.count; i++) {
            out.pages[i].id[sizeof(out.pages[i].id) - 1]                     = '\0';
            out.pages[i].title[sizeof(out.pages[i].title) - 1]              = '\0';
            out.pages[i].iconName[sizeof(out.pages[i].iconName) - 1]        = '\0';
            out.pages[i].pageParentId[sizeof(out.pages[i].pageParentId) - 1] = '\0';
        }
    }
    return ok;
}

/* ------------------------------------------------------------------ */
/* Navigation history                                                  */
/* ------------------------------------------------------------------ */
void SaveNavHistory(const PageHeader back[], int backCount,
                   const PageHeader fwd[],  int fwdCount)
{
    NavHistoryData buf;
    memset(&buf, 0, sizeof(buf));
    int bc = backCount < kNavHistMax ? backCount : kNavHistMax;
    int fc = fwdCount  < kNavHistMax ? fwdCount  : kNavHistMax;
    memcpy(buf.back, back, sizeof(PageHeader) * bc); buf.backCount = bc;
    memcpy(buf.fwd,  fwd,  sizeof(PageHeader) * fc); buf.fwdCount  = fc;
    WritePref(kHistoryOffset, &buf, (long)sizeof(NavHistoryData), true);
}

void LoadNavHistory(PageHeader back[], int& backCount, PageHeader fwd[], int& fwdCount)
{
    backCount = 0; fwdCount = 0;
    NavHistoryData buf;
    memset(&buf, 0, sizeof(buf));
    if (!ReadPref(kHistoryOffset, &buf, (long)sizeof(NavHistoryData))) return;
    int bc = buf.backCount;
    int fc = buf.fwdCount;
    if (bc < 0 || bc > kNavHistMax) bc = 0;
    if (fc < 0 || fc > kNavHistMax) fc = 0;
    memcpy(back, buf.back, sizeof(PageHeader) * bc); backCount = bc;
    memcpy(fwd,  buf.fwd,  sizeof(PageHeader) * fc); fwdCount  = fc;
}

/* ------------------------------------------------------------------ */
/* Recent pages list                                                   */
/* ------------------------------------------------------------------ */
void SaveRecentPages(const PageHeader recent[], int count)
{
    RecentPagesData buf;
    memset(&buf, 0, sizeof(buf));
    int n = count < kMaxRecentPages ? count : kMaxRecentPages;
    memcpy(buf.pages, recent, sizeof(PageHeader) * n);
    buf.count = n;
    WritePref(kRecentOffset, &buf, (long)sizeof(RecentPagesData), true);
}

void LoadRecentPages(PageHeader recent[], int& count)
{
    count = 0;
    RecentPagesData buf;
    memset(&buf, 0, sizeof(buf));
    if (!ReadPref(kRecentOffset, &buf, (long)sizeof(RecentPagesData))) return;
    int n = buf.count;
    if (n < 0 || n > kMaxRecentPages) n = 0;
    memcpy(recent, buf.pages, sizeof(PageHeader) * n);
    count = n;
}

/* ------------------------------------------------------------------ */
/* EC midpoint persistence — OPT #7                                    */
/* ------------------------------------------------------------------ */
void LoadECMidpoint(PersistedECMidpoint &out)
{
    memset(&out, 0, sizeof(out));
    if (!ReadPref(kMidpointOffset, &out, (long)sizeof(PersistedECMidpoint)))
        memset(&out, 0, sizeof(out));
}

void SaveECMidpoint(const PersistedECMidpoint &in)
{
    WritePref(kMidpointOffset, &in, (long)sizeof(PersistedECMidpoint), true);
}

/* ------------------------------------------------------------------ */
/* TLS session ticket persistence — OPT-5                             */
/* ------------------------------------------------------------------ */
#include <DateTimeUtils.h>

void SaveTLSSession(const PersistedTLSSession& sess)
{
    WritePref(kTLSSessionOffset, &sess, (long)sizeof(PersistedTLSSession), true);
}

bool LoadTLSSession(PersistedTLSSession& sess)
{
    memset(&sess, 0, sizeof(sess));
    if (!ReadPref(kTLSSessionOffset, &sess, (long)sizeof(PersistedTLSSession)))
        return false;
    if (sess.magic != 0x544C5353UL) return false;   /* 'TLSS' */
    if (sess.blob_len == 0 || sess.blob_len > kTLSBlobMax) return false;
    /* Expire after 23 hours */
    unsigned long now = 0;
    GetDateTime(&now);
    if (sess.saved_datetime == 0 || now - sess.saved_datetime > 23UL * 3600UL) return false;
    return true;
}

/* ------------------------------------------------------------------ */
/* Random seed                                                         */
/* ------------------------------------------------------------------ */
static const unsigned long kSeedMagic = 0x53656564UL;  /* 'Seed' */

bool LoadRandomSeed(unsigned char seed[kRandomSeedLen])
{
    PersistedRandomSeed s;
    if (!ReadPref(kSeedOffset, &s, (long)sizeof(s))) return false;
    if (s.magic != kSeedMagic) return false;
    memcpy(seed, s.seed, kRandomSeedLen);
    memset(&s, 0, sizeof(s));
    return true;
}

void SaveRandomSeed(const unsigned char seed[kRandomSeedLen])
{
    PersistedRandomSeed s;
    s.magic = kSeedMagic;
    memcpy(s.seed, seed, kRandomSeedLen);
    WritePref(kSeedOffset, &s, (long)sizeof(s), true);
    memset(&s, 0, sizeof(s));
}

/* ------------------------------------------------------------------ */
/* C-callable wrappers for uecc_alt.c (pure C, cannot call C++ directly) */
/* ------------------------------------------------------------------ */
extern "C" void SaveAllPersistECDSASlots_c(const unsigned char *q_pub_flat,
                                             const unsigned char *hash_flat,
                                             const unsigned char *hash_lens,
                                             const unsigned char *valids,
                                             int num_slots)
{
    PersistentECDSACacheEntry slots[PERSIST_ECDSA_SLOTS];
    int n = num_slots < PERSIST_ECDSA_SLOTS ? num_slots : PERSIST_ECDSA_SLOTS;
    memset(slots, 0, sizeof(slots));
    for (int i = 0; i < n; i++) {
        memcpy(slots[i].q_pub,    q_pub_flat + i * 64, 64);
        memcpy(slots[i].msg_hash, hash_flat  + i * 64, 32);
        slots[i].hash_len = hash_lens[i];
        slots[i].valid    = valids[i];
    }
    SavePersistECDSACache(slots);
}

extern "C" void LoadAllPersistECDSASlots_c(unsigned char *q_pub_flat,
                                             unsigned char *hash_flat,
                                             unsigned char *hash_lens,
                                             unsigned char *valids,
                                             int num_slots)
{
    PersistentECDSACacheEntry slots[PERSIST_ECDSA_SLOTS];
    LoadPersistECDSACache(slots);
    int n = num_slots < PERSIST_ECDSA_SLOTS ? num_slots : PERSIST_ECDSA_SLOTS;
    for (int i = 0; i < n; i++) {
        memcpy(q_pub_flat + i * 64, slots[i].q_pub,    64);
        memcpy(hash_flat  + i * 64, slots[i].msg_hash, 32);
        hash_lens[i] = slots[i].hash_len;
        valids[i]    = slots[i].valid;
    }
}

extern "C" void LoadECMidpoint_c(unsigned char q_pub_out[64],
                                  unsigned char q2_native_out[64],
                                  unsigned char *valid_out)
{
    PersistedECMidpoint mid;
    LoadECMidpoint(mid);
    memcpy(q_pub_out,     mid.q_pub,     64);
    memcpy(q2_native_out, mid.q2_native, 64);
    *valid_out = mid.valid;
}

extern "C" void SaveECMidpoint_c(const unsigned char q_pub_in[64],
                                  const unsigned char q2_native_in[64])
{
    PersistedECMidpoint mid;
    memset(&mid, 0, sizeof(mid));
    memcpy(mid.q_pub,     q_pub_in,     64);
    memcpy(mid.q2_native, q2_native_in, 64);
    mid.valid = 1;
    SaveECMidpoint(mid);
}
