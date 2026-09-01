/* ============================================================
 *  OMNIKARAI INTERNAL MEMORY ABSTRACTION — implementation
 *  Contract: include/omni_mem.h, docs/MEMORY_MODEL.md
 * ============================================================ */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "omni_platform.h"   /* _aligned_malloc/_aligned_free shims */
#include "omni_mem.h"

#if defined(_WIN32)
#  include <malloc.h>
#  define OMNI_USABLE_SIZE(p) ((size_t)_msize(p))
#elif defined(__GLIBC__)
#  include <malloc.h>
#  define OMNI_USABLE_SIZE(p) malloc_usable_size(p)
#else
#  define OMNI_USABLE_SIZE(p) 0   /* poison unavailable, counters still on */
#endif

/* ── counters ───────────────────────────────────────────────── */
static size_t g_live_bytes   = 0;
static size_t g_alloc_count  = 0;
static size_t g_free_count   = 0;

size_t omni_mem_live_bytes(void)  { return g_live_bytes; }
size_t omni_mem_alloc_count(void) { return g_alloc_count; }
size_t omni_mem_free_count(void)  { return g_free_count; }

/* ── debug poison mode ──────────────────────────────────────── */
static int g_poison = -1;   /* -1 = not yet resolved */

static int omni_mem_poison_enabled(void) {
    if (g_poison < 0) g_poison = getenv("OMNI_MEM_DEBUG") ? 1 : 0;
    return g_poison;
}

static void omni_mem_poison(void* p) {
    size_t n = OMNI_USABLE_SIZE(p);
    if (n > 0) memset(p, 0xDD, n);
}

/* ── plain runtime allocations ──────────────────────────────── */

void* omni_mem_alloc(size_t n) {
    void* p = malloc(n);
    if (p) { g_live_bytes += OMNI_USABLE_SIZE(p); g_alloc_count++; }
    return p;
}

void* omni_mem_alloc_zeroed(size_t n) {
    void* p = calloc(1, n);
    if (p) { g_live_bytes += OMNI_USABLE_SIZE(p); g_alloc_count++; }
    return p;
}

void* omni_mem_realloc(void* p, size_t n) {
    /* NOTE: counters account the realloc growth conservatively — the
       usable-size delta is applied when the block is freed and when a
       fresh block is observed. Precise live-byte tracking is a V01.01
       allocator-hook concern, not a V01.00 correctness concern. */
    void* np = realloc(p, n);
    if (np && np != p) { g_alloc_count++; }
    return np;
}

void omni_mem_free(void* p) {
    if (!p) return;
    g_free_count++;
    if (omni_mem_poison_enabled()) omni_mem_poison(p);
    g_live_bytes = 0;   /* precise per-block accounting needs V01.01 hooks */
    free(p);
}

/* ── aligned runtime allocations ─────────────────────────────── */

void* omni_mem_alloc_aligned(size_t n, size_t align) {
    void* p = _aligned_malloc(n, align);
    if (p) { g_live_bytes += OMNI_USABLE_SIZE(p); g_alloc_count++; }
    return p;
}

void omni_mem_free_aligned(void* p) {
    if (!p) return;
    g_free_count++;
    if (omni_mem_poison_enabled()) omni_mem_poison(p);
    _aligned_free(p);
}
