/*
 * fast_alloc.c — fast pool allocator for mbedTLS ECC computation.
 *
 * Retro68's calloc/free are thin wrappers around NewPtrClear/DisposePtr.
 * Each MPI field-multiply in mbedTLS calls calloc+free for temporary buffers;
 * on Classic Mac that overhead dominates the Curve25519 scalar multiply,
 * making the TLS handshake ~20x slower than the arithmetic alone.
 *
 * This file overrides calloc/free/malloc/realloc for the whole application.
 * When g_fast_alloc_active == 1 (set by uecc_alt.c during mbedtls_ecp_mul),
 * allocations come from a static 16KB pool with a free-list.  At peak the
 * Curve25519 Montgomery ladder keeps < 1 KB live at once, so 16 KB is ample.
 * Outside ECC, calls fall through to NewPtr/DisposePtr as before.
 */

#include <stdlib.h>
#include <string.h>
#include <MacMemory.h>

/* --- Pool ---------------------------------------------------------------- */

#define POOL_SIZE  (64 * 1024)

static char g_pool[POOL_SIZE];
static size_t g_pool_top = 0;

/* Free-list header stored immediately before the user data. */
typedef struct fblk { struct fblk *next; size_t sz; } fblk_t;
#define HDR_SZ  ((size_t)((sizeof(fblk_t) + 3u) & ~3u))   /* 4-byte aligned */

static fblk_t *g_free_head = NULL;

/* Set to 1 whenever g_fast_alloc_depth > 0. */
volatile int g_fast_alloc_active = 0;

/* Nesting depth: begin/end calls may nest (outer handshake + inner ecp_mul). */
static int g_fast_alloc_depth = 0;

/* Diagnostics: peak pool usage and overflow count (NewPtr fallbacks). */
size_t g_pool_peak = 0;
unsigned int g_pool_overflow_count = 0;

/* OPT-6: yield callback for X25519 Montgomery ladder coverage via pool allocs */
static void (*g_fast_alloc_yield)(void) = NULL;
static unsigned int g_fast_alloc_yield_count = 0;

void fast_alloc_set_yield(void (*f)(void))
{
    g_fast_alloc_yield = f;
    g_fast_alloc_yield_count = 0;
}

/* fast_alloc_begin / fast_alloc_end support nesting.
 * The pool is reset only when depth goes from 0→1.
 * The pool is deactivated only when depth returns to 0. */
void fast_alloc_begin(void)
{
    if (g_fast_alloc_depth == 0) {
        g_pool_top  = 0;
        g_free_head = NULL;
    }
    g_fast_alloc_depth++;
    g_fast_alloc_active = 1;
}

void fast_alloc_end(void)
{
    if (g_fast_alloc_depth > 0)
        g_fast_alloc_depth--;
    if (g_fast_alloc_depth == 0) {
        g_fast_alloc_active = 0;
        if (g_pool_top > g_pool_peak)
            g_pool_peak = g_pool_top;
    }
}

/* ---- Internal helpers --------------------------------------------------- */

static inline int in_pool(const void *p)
{
    return (const char *)p >= g_pool &&
           (const char *)p <  g_pool + POOL_SIZE;
}

static void *pool_alloc(size_t sz)
{
    sz = (sz + 3u) & ~3u;          /* 4-byte align requested size */

    /* First-fit search of free list */
    fblk_t **pp = &g_free_head;
    while (*pp) {
        fblk_t *b = *pp;
        if (b->sz >= sz) {
            *pp = b->next;
            return (char *)b + HDR_SZ;
        }
        pp = &b->next;
    }

    /* Bump-allocate from pool */
    size_t need = HDR_SZ + sz;
    if (g_pool_top + need > POOL_SIZE)
        return NULL;            /* pool exhausted; caller falls back */

    fblk_t *b = (fblk_t *)(g_pool + g_pool_top);
    b->sz   = sz;
    b->next = NULL;
    g_pool_top += need;
    return (char *)b + HDR_SZ;
}

static void pool_free(void *p)
{
    fblk_t *b = (fblk_t *)((char *)p - HDR_SZ);
    b->next    = g_free_head;
    g_free_head = b;
}

/* ---- Public overrides --------------------------------------------------- */

void *calloc(size_t n, size_t sz)
{
    size_t total = n * sz;
    if (!total) return NULL;

    if (g_fast_alloc_active) {
        /* OPT-6: yield periodically so MacTCP processes SYN-ACK during X25519 */
        if (g_fast_alloc_yield && ((++g_fast_alloc_yield_count) & 63u) == 0)
            g_fast_alloc_yield();
        void *p = pool_alloc(total);
        if (p) {
            memset(p, 0, total);    /* calloc must zero */
            return p;
        }
        /* Pool full — fall through to system allocator */
        g_pool_overflow_count++;
    }
    return (void *)NewPtrClear((Size)total);
}

void free(void *p)
{
    if (!p) return;
    if (in_pool(p)) {
        pool_free(p);
        return;
    }
    DisposePtr((Ptr)p);
}

void *malloc(size_t sz)
{
    if (!sz) return NULL;

    if (g_fast_alloc_active) {
        void *p = pool_alloc(sz);
        if (p) return p;
        g_pool_overflow_count++;
    }
    return (void *)NewPtr((Size)sz);
}

void *realloc(void *p, size_t sz)
{
    if (!p)  return malloc(sz);
    if (!sz) { free(p); return NULL; }

    if (in_pool(p)) {
        /* Can't resize pool block in place — allocate fresh, copy, free old */
        fblk_t *b = (fblk_t *)((char *)p - HDR_SZ);
        void *np = malloc(sz);
        if (np) {
            size_t copy = b->sz < sz ? b->sz : sz;
            memcpy(np, p, copy);
        }
        pool_free(p);
        return np;
    }

    /* System pointer: try in-place resize first, then copy */
    Size old_sz = GetPtrSize((Ptr)p);
    SetPtrSize((Ptr)p, (Size)sz);
    if (MemError() == noErr) return p;

    void *np = NewPtr((Size)sz);
    if (np) {
        memcpy(np, p, (size_t)old_sz < sz ? (size_t)old_sz : sz);
        DisposePtr((Ptr)p);
    }
    return np;
}
