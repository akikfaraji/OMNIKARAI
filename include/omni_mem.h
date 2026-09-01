#ifndef OMNIKARAI_MEM_H
#define OMNIKARAI_MEM_H

/* ============================================================
 *  OMNIKARAI INTERNAL MEMORY ABSTRACTION — V01.00
 *
 *  NOT the public language API. This is the internal funnel for
 *  RUNTIME allocations — memory created while generated code runs
 *  (lists, AI buffers, strings, class instances) — kept strictly
 *  separate from compiler-internal allocations (AST, scopes, code
 *  buffers), which keep using plain malloc.
 *
 *  Purpose (docs/MEMORY_MODEL.md):
 *    V01.00  single funnel + live counters + poison-on-free debug
 *    V01.01  allocator hooks (arena/pool) behind these functions,
 *            poison-on-free default in debug builds, and the
 *            reviewed public expert-API surface
 *
 *  Debug mode: set OMNI_MEM_DEBUG=1 in the environment to poison
 *  every freed block with 0xDD before release (use-after-free and
 *  double-free become visible without ASan). Counters always track.
 * ============================================================ */

#include <stddef.h>

/* Plain runtime allocations (list nodes, strings, instances). */
void* omni_mem_alloc(size_t n);
void* omni_mem_alloc_zeroed(size_t n);
void* omni_mem_realloc(void* p, size_t n);
void  omni_mem_free(void* p);

/* Cache-line-aligned runtime allocations (AI/tensor buffers, 64B). */
void* omni_mem_alloc_aligned(size_t n, size_t align);
void  omni_mem_free_aligned(void* p);

/* Live counters — always maintained, cheap, monotonic per process.
   C-level only for now; the language-visible surface is a V01.01
   decision and deliberately NOT invented here. */
size_t omni_mem_live_bytes(void);
size_t omni_mem_alloc_count(void);
size_t omni_mem_free_count(void);

#endif /* OMNIKARAI_MEM_H */
