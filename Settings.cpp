#include "Settings.h"

#include <Files.h>
#include <Folders.h>
#include <Dialogs.h>
#include <Windows.h>
#include <TextEdit.h>
#include <Scrap.h>
#include <Events.h>
#include <Fonts.h>
#include <string.h>

static const unsigned char kPrefsFileName[] = "\pNotionclassicPrefs";

/* ------------------------------------------------------------------ */
/* GetPrefsSpec — build an FSSpec pointing at the prefs file.         */
/* Returns noErr even if the file does not yet exist (fnfErr is ok).  */
/* ------------------------------------------------------------------ */
static OSErr GetPrefsSpec(FSSpec &spec)
{
    short vRefNum;
    long  dirID;
    OSErr err = FindFolder(kOnSystemDisk, kPreferencesFolderType,
                           kCreateFolder, &vRefNum, &dirID);
    if (err != noErr) return err;
    err = FSMakeFSSpec(vRefNum, dirID, kPrefsFileName, &spec);
    return (err == noErr || err == fnfErr) ? noErr : err;
}

/* ------------------------------------------------------------------ */
/* LoadSettings — fills empty strings if no prefs file exists yet     */
/* ------------------------------------------------------------------ */
bool LoadSettings(AppSettings &settings)
{
    settings.pageID[0] = '\0';
    settings.apiKey[0] = '\0';

    FSSpec spec;
    if (GetPrefsSpec(spec) != noErr) return false;

    short refNum;
    if (FSpOpenDF(&spec, fsRdPerm, &refNum) != noErr) return false;

    long count = (long)sizeof(AppSettings);
    FSRead(refNum, &count, &settings);
    FSClose(refNum);

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
    FSSpec spec;
    if (GetPrefsSpec(spec) != noErr) return;

    short refNum;
    if (FSpOpenDF(&spec, fsWrPerm, &refNum) != noErr) {
        /* File doesn't exist yet — create it */
        if (FSpCreate(&spec, 'CNot', 'pref', smSystemScript) != noErr) return;
        if (FSpOpenDF(&spec, fsWrPerm, &refNum) != noErr) return;
    }

    long count = (long)sizeof(AppSettings);
    FSWrite(refNum, &count, &settings);
    FSClose(refNum);
}

/* ------------------------------------------------------------------ */
/* DNS cache — OPT-2                                                   */
/* ------------------------------------------------------------------ */
static const long kDNSOffset       = (long)sizeof(AppSettings);
static const long kECDSAOffset     = kDNSOffset + (long)sizeof(CachedDNS);
static const long kFullWidthOffset = kECDSAOffset + (long)(sizeof(PersistentECDSACacheEntry) * PERSIST_ECDSA_SLOTS);

void LoadDNSCache(CachedDNS &out)
{
    out.ip = 0; out.ticks_written = 0;
    FSSpec spec;
    if (GetPrefsSpec(spec) != noErr) return;
    short ref;
    if (FSpOpenDF(&spec, fsRdPerm, &ref) != noErr) return;
    SetFPos(ref, fsFromStart, kDNSOffset);
    long count = (long)sizeof(CachedDNS);
    OSErr err = FSRead(ref, &count, &out);
    FSClose(ref);
    if (err != noErr || count != (long)sizeof(CachedDNS)) {
        out.ip = 0; out.ticks_written = 0;
    }
}

void SaveDNSCache(const CachedDNS &in)
{
    FSSpec spec;
    if (GetPrefsSpec(spec) != noErr) return;
    short ref;
    if (FSpOpenDF(&spec, fsWrPerm, &ref) != noErr) {
        if (FSpCreate(&spec, 'CNot', 'pref', smSystemScript) != noErr) return;
        if (FSpOpenDF(&spec, fsWrPerm, &ref) != noErr) return;
    }
    SetFPos(ref, fsFromStart, kDNSOffset);
    long count = (long)sizeof(CachedDNS);
    FSWrite(ref, &count, &in);
    FSClose(ref);
}

/* ------------------------------------------------------------------ */
/* Persistent ECDSA verify cache — OPT-3                              */
/* ------------------------------------------------------------------ */
void LoadPersistECDSACache(PersistentECDSACacheEntry slots[PERSIST_ECDSA_SLOTS])
{
    memset(slots, 0, sizeof(PersistentECDSACacheEntry) * PERSIST_ECDSA_SLOTS);
    FSSpec spec;
    if (GetPrefsSpec(spec) != noErr) return;
    short ref;
    if (FSpOpenDF(&spec, fsRdPerm, &ref) != noErr) return;
    SetFPos(ref, fsFromStart, kECDSAOffset);
    long count = (long)sizeof(PersistentECDSACacheEntry) * PERSIST_ECDSA_SLOTS;
    OSErr err = FSRead(ref, &count, slots);
    FSClose(ref);
    if (err != noErr || count != (long)sizeof(PersistentECDSACacheEntry) * PERSIST_ECDSA_SLOTS) {
        memset(slots, 0, sizeof(PersistentECDSACacheEntry) * PERSIST_ECDSA_SLOTS);
    }
}

void SavePersistECDSACache(const PersistentECDSACacheEntry slots[PERSIST_ECDSA_SLOTS])
{
    FSSpec spec;
    if (GetPrefsSpec(spec) != noErr) return;
    short ref;
    if (FSpOpenDF(&spec, fsWrPerm, &ref) != noErr) {
        if (FSpCreate(&spec, 'CNot', 'pref', smSystemScript) != noErr) return;
        if (FSpOpenDF(&spec, fsWrPerm, &ref) != noErr) return;
    }
    SetFPos(ref, fsFromStart, kECDSAOffset);
    long count = (long)sizeof(PersistentECDSACacheEntry) * PERSIST_ECDSA_SLOTS;
    FSWrite(ref, &count, slots);
    FSClose(ref);
}

/* ------------------------------------------------------------------ */
/* Full-width layout preference                                        */
/* ------------------------------------------------------------------ */
bool LoadFullWidth()
{
    FSSpec spec;
    if (GetPrefsSpec(spec) != noErr) return false;
    short ref;
    if (FSpOpenDF(&spec, fsRdPerm, &ref) != noErr) return false;
    SetFPos(ref, fsFromStart, kFullWidthOffset);
    unsigned char val = 0;
    long count = 1;
    OSErr err = FSRead(ref, &count, &val);
    FSClose(ref);
    return (err == noErr && count == 1 && val == 1);
}

void SaveFullWidth(bool value)
{
    FSSpec spec;
    if (GetPrefsSpec(spec) != noErr) return;
    short ref;
    if (FSpOpenDF(&spec, fsWrPerm, &ref) != noErr) {
        if (FSpCreate(&spec, 'CNot', 'pref', smSystemScript) != noErr) return;
        if (FSpOpenDF(&spec, fsWrPerm, &ref) != noErr) return;
    }
    /* HFS cannot SetFPos beyond EOF — extend the file with zeros if it is
       shorter than needed (happens when SaveSettings created a 320-byte file
       or when the file was brand new).                                        */
    long fileSize = 0;
    GetEOF(ref, &fileSize);
    if (fileSize < kFullWidthOffset + 1) {
        long needed = (kFullWidthOffset + 1) - fileSize;
        SetFPos(ref, fsFromLEOF, 0);
        Ptr buf = NewPtrClear(needed);
        if (buf) {
            FSWrite(ref, &needed, buf);
            DisposePtr(buf);
        }
    }
    SetFPos(ref, fsFromStart, kFullWidthOffset);
    unsigned char val = value ? 1 : 0;
    long count = 1;
    FSWrite(ref, &count, &val);
    FSClose(ref);
}

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
        memcpy(slots[i].msg_hash, hash_flat  + i * 32, 32);
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
        memcpy(hash_flat  + i * 32, slots[i].msg_hash, 32);
        hash_lens[i] = slots[i].hash_len;
        valids[i]    = slots[i].valid;
    }
}

extern "C" void LoadPersistECDSASlot_c(int slot,
                                         unsigned char q_pub_out[64],
                                         unsigned char hash_out[32],
                                         unsigned char *hash_len_out,
                                         unsigned char *valid_out)
{
    PersistentECDSACacheEntry slots[PERSIST_ECDSA_SLOTS];
    LoadPersistECDSACache(slots);
    if (slot >= 0 && slot < PERSIST_ECDSA_SLOTS) {
        memcpy(q_pub_out, slots[slot].q_pub, 64);
        memcpy(hash_out,  slots[slot].msg_hash, 32);
        *hash_len_out = slots[slot].hash_len;
        *valid_out    = slots[slot].valid;
    } else {
        memset(q_pub_out, 0, 64);
        memset(hash_out,  0, 32);
        *hash_len_out = 0;
        *valid_out    = 0;
    }
}

extern "C" void SavePersistECDSASlot_c(int slot,
                                         const unsigned char q_pub_in[64],
                                         const unsigned char hash_in[32],
                                         unsigned char hash_len_in)
{
    PersistentECDSACacheEntry slots[PERSIST_ECDSA_SLOTS];
    LoadPersistECDSACache(slots); /* read current */
    if (slot >= 0 && slot < PERSIST_ECDSA_SLOTS) {
        memcpy(slots[slot].q_pub,    q_pub_in, 64);
        memcpy(slots[slot].msg_hash, hash_in,  32);
        slots[slot].hash_len = hash_len_in;
        slots[slot].valid    = 1;
    }
    SavePersistECDSACache(slots);
}

/* ------------------------------------------------------------------ */
/* Helper: C string <-> Pascal string                                 */
/* ------------------------------------------------------------------ */
static void CStrToPStr(const char *cstr, Str255 pstr)
{
    int len = strlen(cstr);
    if (len > 254) len = 254;
    pstr[0] = (unsigned char)len;
    memcpy(pstr + 1, cstr, len);
}

static void PStrToCStr(const Str255 pstr, char *cstr, int maxLen)
{
    int len = (unsigned char)pstr[0];
    if (len >= maxLen) len = maxLen - 1;
    memcpy(cstr, pstr + 1, len);
    cstr[len] = '\0';
}

/* ------------------------------------------------------------------ */
/* ExtractPageID — take everything after the last '-', which is the   */
/* Notion page ID in any URL or slug.  No '-' → leave as-is.         */
/* ------------------------------------------------------------------ */
static void ExtractPageID(char *buf, int bufLen)
{
    char *last = strrchr(buf, '-');
    if (last && *(last + 1) != '\0') {
        strncpy(buf, last + 1, bufLen - 1);
        buf[bufLen - 1] = '\0';
    }
}

/* ------------------------------------------------------------------ */
/* DrawSettingsItem — custom draw proc for styled dialog text         */
/* ------------------------------------------------------------------ */
static pascal void DrawSettingsItem(DialogPtr dlg, short item)
{
    short  type; Handle h; Rect r;
    GetDialogItem(dlg, item, &type, &h, &r);
    SetPort(dlg);
    EraseRect(&r);

    short fNum = 0;
    GetFNum("\pGeneva", &fNum);
    FontInfo fi;

    switch (item) {
        case 3: /* "Notion Page" — 12pt bold */
            TextFont(fNum); TextSize(12); TextFace(bold);
            GetFontInfo(&fi);
            MoveTo(r.left, r.top + fi.ascent);
            DrawString("\pNotion Page");
            break;
        case 7: /* Page ID hint — 12pt plain */
            TextFont(fNum); TextSize(12); TextFace(0);
            GetFontInfo(&fi);
            MoveTo(r.left, r.top + fi.ascent);
            DrawString("\pEnter Your Page ID or URL");
            break;
        case 5: /* "Integration Token" — 12pt bold */
            TextFont(fNum); TextSize(12); TextFace(bold);
            GetFontInfo(&fi);
            MoveTo(r.left, r.top + fi.ascent);
            DrawString("\pIntegration Token");
            break;
        case 8: /* Token hint — 12pt plain */
            TextFont(fNum); TextSize(12); TextFace(0);
            GetFontInfo(&fi);
            MoveTo(r.left, r.top + fi.ascent);
            DrawString("\pCreate an integration at notion.so/my-integrations");
            break;
    }

    TextFont(0); TextSize(12); TextFace(0);
}

/* ------------------------------------------------------------------ */
/* ShowSettingsDialog                                                  */
/* Dialog DITL 129 item layout:                                       */
/*   1 = Button "Save"                                                */
/*   2 = Button "Cancel"                                              */
/*   3 = StaticText "Page ID:"                                        */
/*   4 = EditText  page ID value                                      */
/*   5 = StaticText "API Key:"                                        */
/*   6 = EditText  API key value                                      */
/* ------------------------------------------------------------------ */
bool ShowSettingsDialog(AppSettings &settings, bool modal, const char* title)
{
    DialogPtr dlg = GetNewDialog(kSettingsDialogID, NULL, (WindowPtr)-1);
    if (!dlg) return false;

    /* Install custom draw proc on all styled user items (3,5,7,8,9) */
    UserItemUPP drawUPP = NewUserItemUPP(DrawSettingsItem);
    { short itemNums[] = {3, 5, 7, 8};
      for (int i = 0; i < 4; i++) {
          short t; Handle ih; Rect ir;
          GetDialogItem(dlg, itemNums[i], &t, &ih, &ir);
          SetDialogItem(dlg, itemNums[i], userItem, (Handle)drawUPP, &ir);
      } }

    Handle h;
    short  type;
    Rect   r;
    Str255 pstr;

    /* Pre-fill Page ID field */
    GetDialogItem(dlg, 4, &type, &h, &r);
    CStrToPStr(settings.pageID, pstr);
    SetDialogItemText(h, pstr);

    /* Pre-fill API Key field */
    GetDialogItem(dlg, 6, &type, &h, &r);
    CStrToPStr(settings.apiKey, pstr);
    SetDialogItemText(h, pstr);

    SelectDialogItemText(dlg, 4, 0, 32767);

    /* In modal mode (first-launch / HTTP error) rename Cancel to Quit */
    if (modal) {
        short bt; Handle bh; Rect br;
        GetDialogItem(dlg, 2, &bt, &bh, &br);
        SetControlTitle((ControlHandle)bh, "\pQuit");
    }

    /* Set window title from caller-supplied C string */
    if (title && title[0]) {
        Str255 ptitle;
        int len = 0;
        while (len < 254 && title[len]) { ptitle[len + 1] = title[len]; len++; }
        ptitle[0] = (unsigned char)len;
        SetWTitle((WindowPtr)dlg, ptitle);
    }

    ShowWindow(dlg);

    /* Custom event loop for movableDBoxProc: keeps menus and app-switching live.
     * We inline all filter logic here so we never call the pascal-qualified
     * SettingsFilter from C++ (calling-convention mismatch on 68k).            */
    short item = 0;
    while (item != 1 && item != 2) {
        EventRecord e;
        WaitNextEvent(everyEvent, &e, 6, nil);

        /* ---- suspend/resume: keep TE cursor correct on app switch ---- */
        if (e.what == osEvt) {
            if (((e.message >> 24) & 0xFF) == suspendResumeMessage) {
                TEHandle te = ((DialogPeek)dlg)->textH;
                if (e.message & resumeFlag) { if (te) TEActivate(te); }
                else                        { if (te) TEDeactivate(te); }
            }
            continue;
        }

        /* ---- activate/deactivate TE ---- */
        if (e.what == activateEvt) {
            TEHandle te = ((DialogPeek)dlg)->textH;
            if (e.modifiers & activeFlag) { if (te) TEActivate(te); }
            else                          { if (te) TEDeactivate(te); }
            continue;
        }

        /* ---- keyboard ---- */
        if (e.what == keyDown || e.what == autoKey) {
            char c = (char)(e.message & charCodeMask);
            if (c == 0x0D || c == 0x03) { item = 1; break; }   /* Return / Enter */

            TEHandle te = ((DialogPeek)dlg)->textH;
            if (e.modifiers & cmdKey) {
                if ((c == 'v' || c == 'V') && te) {
                    Handle scrapH = NewHandle(0);
                    long   offset, len = GetScrap(scrapH, 'TEXT', &offset);
                    if (len > 0) {
                        HLock(scrapH);
                        char *buf = (char *)NewPtr(len);
                        long  cleanLen = 0;
                        if (buf) {
                            for (long i = 0; i < len; i++) {
                                char ch = (*scrapH)[i];
                                if (ch != '\r' && ch != '\n' && ch != '\t')
                                    buf[cleanLen++] = ch;
                            }
                        }
                        HUnlock(scrapH);
                        TESetSelect(0, 32767, te); TEDelete(te);
                        if (buf && cleanLen > 0) TEInsert(buf, cleanLen, te);
                        if (buf) DisposePtr(buf);
                    }
                    DisposeHandle(scrapH);
                    continue;
                }
                if ((c == 'a' || c == 'A') && te) { TESetSelect(0, 32767, te); continue; }
                if ((c == 'x' || c == 'X') && te) { TECut(te);  ZeroScrap(); TEToScrap(); continue; }
                if ((c == 'c' || c == 'C') && te) { TECopy(te); ZeroScrap(); TEToScrap(); continue; }
            }
            /* fall through to DialogSelect for regular typing */
        }

        /* ---- mouse ---- */
        if (e.what == mouseDown) {
            WindowPtr hitWin;
            short part = FindWindow(e.where, &hitWin);

            /* Menu bar: keep live so Application Menu lets user switch apps */
            if (part == inMenuBar) {
                long  mr    = MenuSelect(e.where);
                short mID   = (short)(mr >> 16);
                short mItem = (short)(mr & 0xFFFF);
                HiliteMenu(0);
                if (mID == 129 && mItem == 8) { item = 2; break; } /* Quit → Cancel */
                continue;
            }

            /* Drag the dialog itself */
            if (part == inDrag && hitWin == (WindowPtr)dlg) {
                Rect sb = qd.screenBits.bounds;   /* safer than GetGrayRgn on older systems */
                DragWindow((WindowPtr)dlg, e.where, &sb);
                continue;
            }

            /* Click in another app's window — just ignore (OS handles app switch
             * when user picks from the Application Menu above)                   */
            if (hitWin != (WindowPtr)dlg) continue;
        }

        /* ---- let Dialog Manager handle everything else ---- */
        DialogSelect(&e, &dlg, &item);
    }


    bool saved = false;
    if (item == 1) { /* Save */
        GetDialogItem(dlg, 4, &type, &h, &r);
        GetDialogItemText(h, pstr);
        /* Extract into a full Str255-sized buffer first so ExtractPageID
           sees the complete URL before we copy the short result into pageID */
        char tmpID[256];
        PStrToCStr(pstr, tmpID, sizeof(tmpID));
        ExtractPageID(tmpID, sizeof(tmpID));
        strncpy(settings.pageID, tmpID, sizeof(settings.pageID) - 1);
        settings.pageID[sizeof(settings.pageID) - 1] = '\0';

        GetDialogItem(dlg, 6, &type, &h, &r);
        GetDialogItemText(h, pstr);
        PStrToCStr(pstr, settings.apiKey, sizeof(settings.apiKey));

        SaveSettings(settings);
        saved = true;
    }

    DisposeDialog(dlg);
    DisposeUserItemUPP(drawUPP);
    return saved;
}
