/*
 * uecc_alt.c - micro-ecc backed P-256/P-384 alternative implementations for mbedTLS.
 *
 * Replaces:
 *   mbedtls_ecdh_gen_public       (MBEDTLS_ECDH_GEN_PUBLIC_ALT)
 *   mbedtls_ecdh_compute_shared   (MBEDTLS_ECDH_COMPUTE_SHARED_ALT)
 *   mbedtls_ecdsa_verify          (MBEDTLS_ECDSA_VERIFY_ALT)
 *
 * ECDH uses P-256 only (TLS ECDHE-ECDSA cipher suite).
 * ECDSA verify handles P-256 and P-384 via micro-ecc.
 * P-384 is needed to verify intermediate certs signed by P-384 roots
 * (e.g. Google WE1 intermediate signed by GTS Root R4 / secp384r1).
 * Other curves fall back to mbedTLS's own ecp_muladd.
 */

#include <string.h>
#include "mbedtls/ecdh.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/bignum.h"
#include "mbedtls/error.h"

/* Configure micro-ecc: P-256 only, 32-bit words, max speed */
#define uECC_SUPPORTS_secp160r1  0
#define uECC_SUPPORTS_secp192r1  0
#define uECC_SUPPORTS_secp224r1  0
#define uECC_SUPPORTS_secp256r1  1
#define uECC_SUPPORTS_secp256k1  0
#define uECC_OPTIMIZATION_LEVEL  2
#define uECC_SQUARE_FUNC         1
#define uECC_WORD_SIZE           4
#define uECC_VLI_NATIVE_LITTLE_ENDIAN 0

#include "uECC.h"

/* OPT-3: C-callable persistent ECDSA cache functions (implemented in Settings.cpp) */
extern void LoadPersistECDSASlot_c(int slot,
                                    unsigned char q_pub_out[64],
                                    unsigned char hash_out[32],
                                    unsigned char *hash_len_out,
                                    unsigned char *valid_out);
extern void SavePersistECDSASlot_c(int slot,
                                    const unsigned char q_pub_in[64],
                                    const unsigned char hash_in[32],
                                    unsigned char hash_len_in);
extern void SaveAllPersistECDSASlots_c(const unsigned char *q_pub_flat,
                                        const unsigned char *hash_flat,
                                        const unsigned char *hash_lens,
                                        const unsigned char *valids,
                                        int num_slots);
extern void LoadAllPersistECDSASlots_c(unsigned char *q_pub_flat,
                                        unsigned char *hash_flat,
                                        unsigned char *hash_lens,
                                        unsigned char *valids,
                                        int num_slots);

/* OPT-3: tracks whether any persistent cache slot was updated this session */
static int g_persist_ecdsa_dirty = 0;
/* OPT-3: tracks whether persistent cache has been loaded into in-memory cache */
static int g_persist_ecdsa_loaded = 0;

#define P256_BYTES 32
#define MAX_ECC_BYTES 32   /* P-256 only */

/* Last ECDH group ID actually used — readable from C++ for diagnostics */
int g_last_ecdh_grp_id = 0;

/* Hooks into fast_alloc.c — activate pool allocator around mbedtls_ecp_mul */
extern void fast_alloc_begin(void);
extern void fast_alloc_end(void);

/* ------------------------------------------------------------------ */
/* RNG bridge: convert mbedTLS f_rng to micro-ecc RNG signature        */
/* ------------------------------------------------------------------ */
static int (*g_f_rng)(void *, unsigned char *, size_t) = NULL;
static void *g_p_rng = NULL;

static int uecc_rng_bridge(uint8_t *dest, unsigned size)
{
    if (g_f_rng == NULL) return 0;
    return (g_f_rng(g_p_rng, dest, (size_t)size) == 0) ? 1 : 0;
}

static void set_rng(int (*f_rng)(void *, unsigned char *, size_t), void *p_rng)
{
    g_f_rng = f_rng;
    g_p_rng = p_rng;
    uECC_set_rng(uecc_rng_bridge);
}

/* ------------------------------------------------------------------ */
/* Helpers: convert between mbedTLS MPI and big-endian byte arrays     */
/* ------------------------------------------------------------------ */
static int mpi_to_be32(const mbedtls_mpi *m, uint8_t *buf)
{
    return mbedtls_mpi_write_binary(m, buf, P256_BYTES);
}

static int be32_to_mpi(mbedtls_mpi *m, const uint8_t *buf)
{
    return mbedtls_mpi_read_binary(m, buf, P256_BYTES);
}

/* ------------------------------------------------------------------ */
/* Pre-computed ephemeral ECDH key cache                               */
/* Call uecc_precompute_ecdh_key() before the TCP connect so the slow  */
/* uECC_make_key() runs before the server starts its handshake timer.  */
/* ------------------------------------------------------------------ */
static uint8_t g_pre_priv[P256_BYTES];
static uint8_t g_pre_pub[P256_BYTES * 2];
static int     g_pre_ready = 0;

/* Cached last server ephemeral pub key + resulting shared secret.
 * Cloudflare rotates ephemeral keys on a timer (~60s), not per-connection,
 * so consecutive requests within that window reuse the same server key.
 * When the cache hits, uECC_shared_secret() (the main bottleneck on slow
 * 68k hardware) is skipped entirely. */
static uint8_t g_cached_server_pub[P256_BYTES * 2];
static uint8_t g_cached_shared[P256_BYTES];
static int     g_cached_valid = 0;

/* Cached ECDSA P-256 verify results — 2 slots.
 * TLS 1.2 ECDHE_ECDSA calls mbedtls_ecdsa_verify twice per handshake:
 *   slot 0: cert chain verify  (Q = WE1 intermediate pub, hash = server cert hash — CONSTANT)
 *   slot 1: ServerKeyExchange  (Q = server cert pub, hash includes nonces  — changes each connection)
 * Two slots prevent the SKE verify from evicting the cert verify hit.
 * Round-robin insertion: g_ecdsa_cache_next alternates 0/1. */
#define ECDSA_CACHE_SLOTS 2
static uint8_t g_ecdsa_cache_pub [ECDSA_CACHE_SLOTS][P256_BYTES * 2];
static uint8_t g_ecdsa_cache_hash[ECDSA_CACHE_SLOTS][64];
static size_t  g_ecdsa_cache_blen[ECDSA_CACHE_SLOTS];
static int     g_ecdsa_cache_valid[ECDSA_CACHE_SLOTS];
static int     g_ecdsa_cache_next = 0;   /* next slot to evict */

/* Cached X25519 compute_shared result.
 * Cloudflare rotates ephemeral X25519 keys on a timer (~60s), so consecutive
 * retries within that window hit the same server pubkey.  Cache the result so
 * compute_shared is instant on the second attempt (saves ~4s). */
static uint8_t g_x25519_cached_q[P256_BYTES];       /* server u-coordinate */
static uint8_t g_x25519_cached_shared[P256_BYTES];  /* d * Q result */
static int     g_x25519_cached_valid = 0;

/* Pre-computed X25519 ephemeral key (d, d*G).
 * Computed before TCP connect so gen_public is instant during the handshake,
 * leaving only compute_shared (~9.5s) inside the server's timeout window. */
static uint8_t g_x25519_pre_priv[P256_BYTES];
static uint8_t g_x25519_pre_pub[P256_BYTES];   /* u-coordinate only */
static int     g_x25519_pre_ready = 0;

int uecc_pre_ready(void) { return g_pre_ready; }
int uecc_x25519_pre_ready(void) { return g_x25519_pre_ready; }

/* OPT-3: load persistent ECDSA cache from prefs into the in-memory slots.
 * Single file read via LoadAllPersistECDSASlots_c (avoids per-slot re-read). */
void uecc_load_persist_ecdsa_cache(void)
{
    unsigned char hash_lens[ECDSA_CACHE_SLOTS];
    unsigned char valids[ECDSA_CACHE_SLOTS];
    int slot;
    LoadAllPersistECDSASlots_c(
        (unsigned char *)g_ecdsa_cache_pub,
        (unsigned char *)g_ecdsa_cache_hash,
        hash_lens, valids, ECDSA_CACHE_SLOTS);
    for (slot = 0; slot < ECDSA_CACHE_SLOTS; slot++) {
        g_ecdsa_cache_blen[slot]  = (size_t)hash_lens[slot];
        g_ecdsa_cache_valid[slot] = valids[slot];
    }
    g_persist_ecdsa_loaded = 1;
    g_persist_ecdsa_dirty  = 0;
}

/* OPT-3: flush in-memory cache back to prefs file in a single write */
void uecc_save_persist_ecdsa_cache(void)
{
    unsigned char hash_lens[ECDSA_CACHE_SLOTS];
    unsigned char valids[ECDSA_CACHE_SLOTS];
    int slot;
    for (slot = 0; slot < ECDSA_CACHE_SLOTS; slot++) {
        hash_lens[slot] = (unsigned char)g_ecdsa_cache_blen[slot];
        valids[slot]    = (unsigned char)g_ecdsa_cache_valid[slot];
    }
    SaveAllPersistECDSASlots_c(
        (const unsigned char *)g_ecdsa_cache_pub,
        (const unsigned char *)g_ecdsa_cache_hash,
        hash_lens, valids, ECDSA_CACHE_SLOTS);
    g_persist_ecdsa_dirty = 0;
}

/* OPT-3: returns 1 if in-memory cache was updated and needs flushing */
int uecc_persist_ecdsa_dirty(void) { return g_persist_ecdsa_dirty; }

/* OPT-5: pre-size MPI limb arrays to avoid realloc during scalar mult */
static void PreGrowMpi(mbedtls_mpi *X)
{
    mbedtls_mpi_grow(X, 8); /* 8 x 32-bit limbs = 256 bits */
}

int uecc_precompute_ecdh_key(int (*f_rng)(void *, unsigned char *, size_t),
                              void *p_rng)
{
    set_rng(f_rng, p_rng);
    if (!uECC_make_key(g_pre_pub, g_pre_priv, uECC_secp256r1()))
        return MBEDTLS_ERR_ECP_RANDOM_FAILED;
    g_pre_ready = 1;
    return 0;
}

int uecc_precompute_x25519_key(int (*f_rng)(void *, unsigned char *, size_t),
                                void *p_rng)
{
    mbedtls_ecp_group grp;
    mbedtls_mpi       d;
    mbedtls_ecp_point Q;
    int ret;

    mbedtls_ecp_group_init(&grp);
    mbedtls_mpi_init(&d);
    mbedtls_ecp_point_init(&Q);

    ret = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519);
    if (ret) goto cleanup;
    ret = mbedtls_ecp_gen_privkey(&grp, &d, f_rng, p_rng);
    if (ret) goto cleanup;

    /* OPT-5: pre-size all MPIs that grow during the Montgomery ladder */
    PreGrowMpi(&d);
    PreGrowMpi(&Q.MBEDTLS_PRIVATE(X));
    PreGrowMpi(&Q.MBEDTLS_PRIVATE(Y));
    PreGrowMpi(&Q.MBEDTLS_PRIVATE(Z));
    fast_alloc_begin();
    ret = mbedtls_ecp_mul(&grp, &Q, &d, &grp.G, f_rng, p_rng);
    fast_alloc_end();
    if (ret) goto cleanup;

    ret = mbedtls_mpi_write_binary(&d,                        g_x25519_pre_priv, P256_BYTES);
    if (ret == 0)
        ret = mbedtls_mpi_write_binary(&Q.MBEDTLS_PRIVATE(X), g_x25519_pre_pub,  P256_BYTES);
    if (ret == 0)
        g_x25519_pre_ready = 1;

cleanup:
    mbedtls_ecp_point_free(&Q);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_group_free(&grp);
    return ret;
}

/* ------------------------------------------------------------------ */
/* ECDH: generate ephemeral key pair                                   */
/* ------------------------------------------------------------------ */
int mbedtls_ecdh_gen_public(mbedtls_ecp_group *grp, mbedtls_mpi *d,
                             mbedtls_ecp_point *Q,
                             int (*f_rng)(void *, unsigned char *, size_t),
                             void *p_rng)
{
    int ret;

    if (grp->id != MBEDTLS_ECP_DP_SECP256R1) {
        if (grp->id == MBEDTLS_ECP_DP_CURVE25519 && g_x25519_pre_ready) {
            /* Use pre-computed X25519 key — gen_public is now instant */
            ret = be32_to_mpi(d, g_x25519_pre_priv);
            if (ret == 0) ret = be32_to_mpi(&Q->MBEDTLS_PRIVATE(X), g_x25519_pre_pub);
            if (ret == 0) ret = mbedtls_mpi_lset(&Q->MBEDTLS_PRIVATE(Z), 1);
            memset(g_x25519_pre_priv, 0, P256_BYTES);
            g_x25519_pre_ready = 0;
            return ret;
        }
        /* Fallback: compute d*G with pool allocator */
        ret = mbedtls_ecp_gen_privkey(grp, d, f_rng, p_rng);
        if (ret != 0) return ret;
        fast_alloc_begin();
        ret = mbedtls_ecp_mul(grp, Q, d, &grp->G, f_rng, p_rng);
        fast_alloc_end();
        return ret;
    }

    uint8_t priv[P256_BYTES];
    uint8_t pub[P256_BYTES * 2];

    if (g_pre_ready) {
        /* Use pre-computed key, clear cache so it's single-use */
        memcpy(priv, g_pre_priv, P256_BYTES);
        memcpy(pub,  g_pre_pub,  P256_BYTES * 2);
        memset(g_pre_priv, 0, P256_BYTES);
        g_pre_ready = 0;
    } else {
        set_rng(f_rng, p_rng);
        if (!uECC_make_key(pub, priv, uECC_secp256r1())) {
            return MBEDTLS_ERR_ECP_RANDOM_FAILED;
        }
    }

    ret = be32_to_mpi(d, priv);
    if (ret == 0) ret = be32_to_mpi(&Q->MBEDTLS_PRIVATE(X), pub);
    if (ret == 0) ret = be32_to_mpi(&Q->MBEDTLS_PRIVATE(Y), pub + P256_BYTES);
    if (ret == 0) ret = mbedtls_mpi_lset(&Q->MBEDTLS_PRIVATE(Z), 1);

    memset(priv, 0, sizeof(priv));
    return ret;
}

/* ------------------------------------------------------------------ */
/* ECDH: compute shared secret                                         */
/* ------------------------------------------------------------------ */
int mbedtls_ecdh_compute_shared(mbedtls_ecp_group *grp, mbedtls_mpi *z,
                                 const mbedtls_ecp_point *Q,
                                 const mbedtls_mpi *d,
                                 int (*f_rng)(void *, unsigned char *, size_t),
                                 void *p_rng)
{
    int ret;

    g_last_ecdh_grp_id = (int)grp->id;

    if (grp->id != MBEDTLS_ECP_DP_SECP256R1) {
        /* X25519 (or other non-P256) path — use mbedTLS ecp_mul with pool.
         * Cache the server pubkey + result: Cloudflare reuses ephemeral keys
         * for ~60s, so a retry within that window skips the 4s ecp_mul. */
        uint8_t q_bytes[P256_BYTES];
        if (mbedtls_mpi_write_binary(&Q->MBEDTLS_PRIVATE(X), q_bytes, P256_BYTES) == 0 &&
            g_x25519_cached_valid &&
            memcmp(q_bytes, g_x25519_cached_q, P256_BYTES) == 0) {
            /* Cache hit: return stored shared secret */
            return be32_to_mpi(z, g_x25519_cached_shared);
        }

        mbedtls_ecp_point P;
        mbedtls_ecp_point_init(&P);
        /* OPT-5: pre-size result point MPIs */
        PreGrowMpi(&P.MBEDTLS_PRIVATE(X));
        PreGrowMpi(&P.MBEDTLS_PRIVATE(Y));
        PreGrowMpi(&P.MBEDTLS_PRIVATE(Z));
        fast_alloc_begin();
        ret = mbedtls_ecp_mul(grp, &P, d, Q, f_rng, p_rng);
        fast_alloc_end();
        if (ret == 0) {
            if (mbedtls_ecp_is_zero(&P)) {
                ret = MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
            } else {
                ret = mbedtls_mpi_copy(z, &P.MBEDTLS_PRIVATE(X));
                /* Populate cache for next connection */
                if (ret == 0) {
                    memcpy(g_x25519_cached_q, q_bytes, P256_BYTES);
                    mbedtls_mpi_write_binary(&P.MBEDTLS_PRIVATE(X),
                                             g_x25519_cached_shared, P256_BYTES);
                    g_x25519_cached_valid = 1;
                }
            }
        }
        mbedtls_ecp_point_free(&P);
        return ret;
    }

    uint8_t pub[P256_BYTES * 2];
    uint8_t priv[P256_BYTES];
    uint8_t secret[P256_BYTES];

    ret = mpi_to_be32(&Q->MBEDTLS_PRIVATE(X), pub);
    if (ret == 0) ret = mpi_to_be32(&Q->MBEDTLS_PRIVATE(Y), pub + P256_BYTES);
    if (ret == 0) ret = mpi_to_be32(d, priv);
    if (ret != 0) goto cleanup;

    /* Fast path: if the server reused its ephemeral key from the last
     * connection, return the cached shared secret without calling
     * uECC_shared_secret() (the main bottleneck on slow 68k hardware). */
    if (g_cached_valid &&
        memcmp(pub, g_cached_server_pub, P256_BYTES * 2) == 0) {
        memcpy(secret, g_cached_shared, P256_BYTES);
        ret = be32_to_mpi(z, secret);
        goto cleanup;
    }

    set_rng(f_rng, p_rng);
    if (!uECC_shared_secret(pub, priv, secret, uECC_secp256r1())) {
        ret = MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
        goto cleanup;
    }

    /* Cache server key + shared secret for next connection */
    memcpy(g_cached_server_pub, pub,    P256_BYTES * 2);
    memcpy(g_cached_shared,     secret, P256_BYTES);
    g_cached_valid = 1;

    ret = be32_to_mpi(z, secret);

cleanup:
    memset(priv,   0, sizeof(priv));
    memset(secret, 0, sizeof(secret));
    return ret;
}

/* ------------------------------------------------------------------ */
/* ECDSA: verify signature                                             */
/* P-256: delegates to uECC_verify which uses Shamir's trick          */
/*        (u1*G + u2*Q in one pass, ~2x faster than separate mults).  */
/* Other curves: mbedTLS ecp_muladd fallback.                         */
/* ------------------------------------------------------------------ */
int mbedtls_ecdsa_verify(mbedtls_ecp_group *grp,
                          const unsigned char *buf, size_t blen,
                          const mbedtls_ecp_point *Q,
                          const mbedtls_mpi *r,
                          const mbedtls_mpi *s)
{
    int ret;

    if (grp->id == MBEDTLS_ECP_DP_SECP256R1) {
        /* Pack Q and (r,s) into micro-ecc byte format, call uECC_verify */
        uint8_t pub[P256_BYTES * 2];
        uint8_t sig[P256_BYTES * 2];

        ret = mbedtls_mpi_write_binary(&Q->MBEDTLS_PRIVATE(X), pub,             P256_BYTES);
        if (ret) return ret;
        ret = mbedtls_mpi_write_binary(&Q->MBEDTLS_PRIVATE(Y), pub + P256_BYTES, P256_BYTES);
        if (ret) return ret;
        ret = mbedtls_mpi_write_binary(r, sig,             P256_BYTES);
        if (ret) return ret;
        ret = mbedtls_mpi_write_binary(s, sig + P256_BYTES, P256_BYTES);
        if (ret) return ret;

        /* 2-slot cache check: look for (Q, hash) match in either slot.
         * Slot 0 holds cert-chain verify, slot 1 holds SKE verify.
         * Using 2 slots prevents SKE (different Q, changing hash) from
         * evicting the cert verify entry (constant Q + hash). */
        if (blen <= sizeof(g_ecdsa_cache_hash[0])) {
            int slot;
            for (slot = 0; slot < ECDSA_CACHE_SLOTS; slot++) {
                if (g_ecdsa_cache_valid[slot] &&
                    blen == g_ecdsa_cache_blen[slot] &&
                    memcmp(pub, g_ecdsa_cache_pub[slot],  P256_BYTES * 2) == 0 &&
                    memcmp(buf, g_ecdsa_cache_hash[slot], blen)           == 0) {
                    return 0;   /* cache hit */
                }
            }
        }

        int ok = uECC_verify(pub, buf, (unsigned)blen, sig, uECC_secp256r1());

        /* Populate cache on success.
         * Primary key = Q (public key): if a slot for this Q already exists,
         * update it in-place (so cert verify with constant Q persists across
         * connections while SKE verify with the same Q but changing hash also
         * gets its own stable slot).  Only evict a different Q when all slots
         * are full (round-robin). */
        if (ok && blen <= sizeof(g_ecdsa_cache_hash[0])) {
            int slot = -1, i;
            for (i = 0; i < ECDSA_CACHE_SLOTS; i++) {
                if (!g_ecdsa_cache_valid[i]) { slot = i; break; }  /* empty slot */
                if (memcmp(pub, g_ecdsa_cache_pub[i], P256_BYTES * 2) == 0) {
                    slot = i; break;  /* same Q: update in-place */
                }
            }
            if (slot < 0) {
                slot = g_ecdsa_cache_next;  /* all slots occupied, evict oldest */
                g_ecdsa_cache_next = (g_ecdsa_cache_next + 1) % ECDSA_CACHE_SLOTS;
            }
            memcpy(g_ecdsa_cache_pub[slot],  pub, P256_BYTES * 2);
            memcpy(g_ecdsa_cache_hash[slot], buf, blen);
            g_ecdsa_cache_blen[slot]  = blen;
            g_ecdsa_cache_valid[slot] = 1;
            g_persist_ecdsa_dirty = 1; /* OPT-3: mark for flush in SslClose */
        }

        return ok ? 0 : MBEDTLS_ERR_ECP_VERIFY_FAILED;
    }

    /* Non-P256 fallback: mbedTLS scalar mults via ecp_muladd */
    {
        mbedtls_mpi e, s_inv, u1, u2;
        mbedtls_ecp_point R;
        size_t n_size   = (grp->nbits + 7) / 8;
        size_t use_size = blen > n_size ? n_size : blen;

        mbedtls_mpi_init(&e);  mbedtls_mpi_init(&s_inv);
        mbedtls_mpi_init(&u1); mbedtls_mpi_init(&u2);
        mbedtls_ecp_point_init(&R);

        if (mbedtls_mpi_cmp_int(r, 1) < 0 ||
            mbedtls_mpi_cmp_mpi(r, &grp->N) >= 0 ||
            mbedtls_mpi_cmp_int(s, 1) < 0 ||
            mbedtls_mpi_cmp_mpi(s, &grp->N) >= 0) {
            ret = MBEDTLS_ERR_ECP_VERIFY_FAILED; goto cleanup;
        }

        MBEDTLS_MPI_CHK(mbedtls_mpi_read_binary(&e, buf, use_size));
        if (use_size * 8 > grp->nbits)
            MBEDTLS_MPI_CHK(mbedtls_mpi_shift_r(&e, use_size * 8 - grp->nbits));
        if (mbedtls_mpi_cmp_mpi(&e, &grp->N) >= 0)
            MBEDTLS_MPI_CHK(mbedtls_mpi_sub_mpi(&e, &e, &grp->N));

        MBEDTLS_MPI_CHK(mbedtls_mpi_inv_mod(&s_inv, s, &grp->N));
        MBEDTLS_MPI_CHK(mbedtls_mpi_mul_mpi(&u1, &e, &s_inv));
        MBEDTLS_MPI_CHK(mbedtls_mpi_mod_mpi(&u1, &u1, &grp->N));
        MBEDTLS_MPI_CHK(mbedtls_mpi_mul_mpi(&u2, r,  &s_inv));
        MBEDTLS_MPI_CHK(mbedtls_mpi_mod_mpi(&u2, &u2, &grp->N));
        MBEDTLS_MPI_CHK(mbedtls_ecp_muladd(grp, &R, &u1, &grp->G, &u2, Q));

        if (mbedtls_ecp_is_zero(&R)) {
            ret = MBEDTLS_ERR_ECP_VERIFY_FAILED; goto cleanup;
        }
        MBEDTLS_MPI_CHK(mbedtls_mpi_mod_mpi(&R.MBEDTLS_PRIVATE(X),
                                              &R.MBEDTLS_PRIVATE(X), &grp->N));
        ret = mbedtls_mpi_cmp_mpi(&R.MBEDTLS_PRIVATE(X), r) == 0
              ? 0 : MBEDTLS_ERR_ECP_VERIFY_FAILED;

cleanup:
        mbedtls_ecp_point_free(&R);
        mbedtls_mpi_free(&e); mbedtls_mpi_free(&s_inv);
        mbedtls_mpi_free(&u1); mbedtls_mpi_free(&u2);
        return ret;
    }
}
