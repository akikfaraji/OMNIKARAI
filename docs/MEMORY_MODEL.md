# Memory Model — Design Requirements (PLANNED)

> Status: **DESIGNED as requirements; syntax deliberately not invented.**
> The exact surface will be decided in V01.01 by inspecting the real
> grammar (`docs/LANGUAGE.md`, `src/parser.c`) — this document defines
> *what* the model must do, not pretend-syntax. Roadmap: V01.01
> ([ROADMAP.md](ROADMAP.md)).

## The long-term goal

> A beginner should be able to use memory without understanding pointers,
> while an expert should still be able to manually control memory when
> necessary.

Both halves are requirements, not preferences. Neither may be sacrificed
for the other, and the two modes must coexist in one program.

## What exists today (the honest baseline)

| Mechanism | State | Evidence |
|-----------|-------|----------|
| AI buffers | `ai.alloc(n)` returns a 64-byte-aligned, zeroed buffer; `ai.free(buf)` releases; `ai.set/get` (FP32 bit patterns), `ai.set_u8/get_u8/set_i8/get_i8` (bytes) | tests t16, t20 |
| Lists | runtime-managed, `list.set` desugared from `obj[i] = v` | `docs/LANGUAGE.md` |
| Strings | runtime-managed; concatenation and repetition built in | tests t07 |
| GC | **none** | by design; no pause unpredictability |
| User pointers/refs | **none exposed** | grammar has no pointer types |
| Manual alloc for general objects | **none** | only the `ai` module allocates |

So today Omnikarai sits at the "beginner-friendly" end by *default
behavior*, with raw-but-safe-ish byte-level control available only inside
AI buffers. The dual model below generalizes that into a coherent design.

## The dual model — requirements

### Mode A: user-friendly (default)

1. Allocation and lifetime of lists, strings, and ordinary values are
   managed by the implementation with **no user-visible allocator**.
2. No GC pause requirement: the implementation may use ownership, arena
   scoping, or reference strategies — the *observable* contract is
   "memory behaves, predictably", not a specific mechanism.
3. Out-of-bounds access is a diagnosed error, not silent corruption
   (lists are bounds-checked today; this must never regress).
4. A program written entirely in Mode A must be reviewable without
   understanding pointers.

### Mode B: technical / manual (expert)

The expert surface must eventually expose, at minimum, the concepts:

- **allocation / deallocation** — explicit, named operations
- **ownership** — one responsible binding per allocation; transfers
  expressible
- **lifetime** — scopes and end-of-life events the compiler can check
- **alignment** — explicit alignment requests (the AI path already uses
  64-byte alignment; generalize it)
- **raw memory / buffers** — byte-level view for FFI-grade work
- **pointers/references** — with the safety stance defined below
- **stack/heap decisions** — user-expressible placement hints the compiler
  may enforce or refuse (never silently reinterpret)
- **memory pools / arena allocation** — bulk lifetime management
- **zero-copy operations** — views/slices over existing buffers without
  duplication

The compiler must be able to **diagnose** (not guess): use-after-free,
double-free, leaks-by-escape where derivable — with the same honesty rule
as everywhere else: what is checked is documented; what is not checked is
documented as unchecked.

## Safety analysis (obligations the design must discharge)

| Risk | Stance |
|------|--------|
| Double free | diagnosed error in Mode B once ownership tracking ships; today `ai.free` twice is a tested path (t16) returning a status, not a crash — preserve that behavior |
| Use after free | debug builds poison freed memory (`poison-on-free` runtime hook, V01.01); release builds document behavior as undefined — no silent-wrongness |
| Dangling references from Mode A into Mode B buffers | prohibited; boundary crossings require explicit views with lifetimes |
| Bounds | Mode A: always checked. Mode B raw views: unchecked, but clearly named as raw |
| Alignment misuse | alignment requests validated; misaligned typed access diagnosed |
| Leaks | Mode A: implementation's responsibility. Mode B: diagnosable via ownership tracking where derivable; explicit leak-check tooling is a V01.11 benchmark-lab metric (allocation count) |

## Non-goals

- No hidden reference-counting overhead in Mode A.
- No promise that Mode B is safe — it is *controllable*; the docs say
  exactly which guarantees apply where.
- No invented syntax in this document. When syntax lands, it goes through
  the grammar review and lands in [LANGUAGE.md](LANGUAGE.md) plus tests —
  never in a roadmap document.

## Relationship to other documents

- Roadmap placement and release criteria: [ROADMAP.md](ROADMAP.md) V01.01.
- ABI consequences (buffer layouts, alignment guarantees):
  [ABI.md](ABI.md).
- Benchmark obligations (allocation counts, memory bandwidth):
  [BENCHMARKS.md](BENCHMARKS.md).
