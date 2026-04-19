#pragma once

#define kSettingsDialogID 129

struct AppSettings {
    char pageID[64];
    char apiKey[256];
};

bool LoadSettings(AppSettings &settings); /* returns true if prefs file existed */
void SaveSettings(const AppSettings &settings);
/* modal=true: rename Cancel→Quit (used on first-launch and HTTP errors).
   modal=false: keep Cancel label (used when opened from the menu).
   title: window title string (C string).                                 */
bool ShowSettingsDialog(AppSettings &settings, bool modal = false,
                        const char* title = "Connection");

/* ------------------------------------------------------------------ */
/* DNS cache — OPT-2                                                   */
/* ------------------------------------------------------------------ */
struct CachedDNS {
    unsigned long ip;            /* resolved 32-bit IPv4 address      */
    unsigned long ticks_written; /* TickCount() when last cached       */
};
void LoadDNSCache(CachedDNS &out);
void SaveDNSCache(const CachedDNS &in);

/* ------------------------------------------------------------------ */
/* Persistent ECDSA verify cache — OPT-3                              */
/* Keyed on (Q_pub, msg_hash): both change when the cert rotates.     */
/* ------------------------------------------------------------------ */
#define PERSIST_ECDSA_SLOTS 2
struct PersistentECDSACacheEntry {
    unsigned char q_pub[64];    /* P-256 public key X|Y (32+32)      */
    unsigned char msg_hash[32]; /* hash of TBSCertificate             */
    unsigned char hash_len;     /* blen (typically 32 for SHA-256)    */
    unsigned char valid;        /* 1 = uECC_verify returned success   */
    unsigned char pad[2];
};
void LoadPersistECDSACache(PersistentECDSACacheEntry slots[PERSIST_ECDSA_SLOTS]);
void SavePersistECDSACache(const PersistentECDSACacheEntry slots[PERSIST_ECDSA_SLOTS]);

/* ------------------------------------------------------------------ */
/* Full-width layout preference                                        */
/* ------------------------------------------------------------------ */
bool LoadFullWidth();
void SaveFullWidth(bool value);

#ifdef __cplusplus
extern "C" {
#endif
/* C-callable wrappers for uecc_alt.c (which is pure C) */
void LoadPersistECDSASlot_c(int slot,
                              unsigned char q_pub_out[64],
                              unsigned char hash_out[32],
                              unsigned char *hash_len_out,
                              unsigned char *valid_out);
void SavePersistECDSASlot_c(int slot,
                              const unsigned char q_pub_in[64],
                              const unsigned char hash_in[32],
                              unsigned char hash_len_in);
/* Saves all slots in a single file write — avoids per-slot read-modify-write */
void SaveAllPersistECDSASlots_c(const unsigned char *q_pub_flat,
                                  const unsigned char *hash_flat,
                                  const unsigned char *hash_lens,
                                  const unsigned char *valids,
                                  int num_slots);
/* Loads all slots in a single file read — avoids per-slot re-read */
void LoadAllPersistECDSASlots_c(unsigned char *q_pub_flat,
                                  unsigned char *hash_flat,
                                  unsigned char *hash_lens,
                                  unsigned char *valids,
                                  int num_slots);
#ifdef __cplusplus
}
#endif
