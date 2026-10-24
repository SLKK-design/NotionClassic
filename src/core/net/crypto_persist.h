/* crypto_persist.h — persistence types for the TLS/crypto fast paths.
   These are core/net concerns (DNS cache, ECDSA verify cache, EC midpoint,
   TLS session ticket) that happen to be stored in the app prefs file via
   prefs_io.cpp.  Kept here, not in the app's Settings.h, so core/net can use
   them without reaching up into the app layer.

   The Load/Save implementations live in core/helpers/prefs_io.cpp. */
#pragma once

/* ------------------------------------------------------------------ */
/* DNS cache — OPT-2                                                   */
/* ------------------------------------------------------------------ */
struct CachedDNS {
    unsigned long ip;            /* resolved 32-bit IPv4 address      */
    unsigned long secs_written;  /* GetDateTime() when last cached: real
                                    time, unlike TickCount() which restarts
                                    at every boot and made an old entry
                                    look fresh                          */
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
/* EC midpoint persistence — OPT #7 (cached-midpoint Straus verify)   */
/* Stores Q2 = 2^128·Q for the server cert pub key Q, so the 128      */
/* Jacobian doublings (~0.5-1.2s) only run when the cert rotates.     */
/* ------------------------------------------------------------------ */
struct PersistedECMidpoint {
    unsigned char q_pub[64];     /* big-endian X|Y the midpoint belongs to */
    unsigned char q2_native[64]; /* 2^128·Q, raw uECC native word format   */
    unsigned char valid;
    unsigned char pad[3];
};
void LoadECMidpoint(PersistedECMidpoint &out);
void SaveECMidpoint(const PersistedECMidpoint &in);

/* ------------------------------------------------------------------ */
/* TLS session ticket persistence — OPT-5                             */
/* Stores the full mbedtls_ssl_session_save blob so resumed           */
/* handshakes skip SKE+ECDHE even after an app restart.               */
/* (A hand-rolled master+ticket+ciphersuite copy reconstructed an     */
/* incomplete session and every cross-restart resume was rejected.)   */
/* ------------------------------------------------------------------ */
enum { kTLSBlobMax = 2048 };
struct PersistedTLSSession {
    unsigned long  magic;            /* 'TLSS' = 0x544C5353              */
    unsigned short blob_len;         /* bytes used in blob[], 0=none     */
    unsigned short pad;
    unsigned long  saved_datetime;   /* GetDateTime at save time         */
    unsigned char  blob[kTLSBlobMax];/* mbedtls_ssl_session_save output  */
};
void SaveTLSSession(const PersistedTLSSession& sess);
bool LoadTLSSession(PersistedTLSSession& sess);  /* false = not found/expired */

/* ------------------------------------------------------------------ */
/* Random seed                                                         */
/* 32 bytes from the random generator, saved at every launch and mixed */
/* into the next launch's seeding, so each launch builds on the timing */
/* jitter of all the earlier ones (see mac_entropy.c). Stored after    */
/* the TLS session and left out of the layout word: an older prefs     */
/* file just has no seed yet, and its caches stay valid.               */
/* ------------------------------------------------------------------ */
enum { kRandomSeedLen = 32 };
struct PersistedRandomSeed {
    unsigned long magic;             /* 'Seed' = 0x53656564              */
    unsigned char seed[kRandomSeedLen];
};
bool LoadRandomSeed(unsigned char seed[kRandomSeedLen]);   /* false = none */
void SaveRandomSeed(const unsigned char seed[kRandomSeedLen]);

/* ------------------------------------------------------------------ */
/* C-callable wrappers for uecc_alt.c (which is pure C)               */
/* ------------------------------------------------------------------ */
#ifdef __cplusplus
extern "C" {
#endif
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
/* EC midpoint (OPT #7) wrappers for uecc_alt.c */
void LoadECMidpoint_c(unsigned char q_pub_out[64],
                       unsigned char q2_native_out[64],
                       unsigned char *valid_out);
void SaveECMidpoint_c(const unsigned char q_pub_in[64],
                       const unsigned char q2_native_in[64]);
#ifdef __cplusplus
}
#endif
