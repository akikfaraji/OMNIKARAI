# Namurai — the Numerical Ecosystem (PLANNED — V01.07)

> Conceptually: **Namurai = the numerical array / mathematical ecosystem of
> Omnikarai.** Status: PLANNED (name subject to the naming review below).
> Seeds that exist today: the `math` module and the AVX2 FP32/INT8 AI
> kernels ([AI_ECOSYSTEM.md](AI_ECOSYSTEM.md)). Roadmap: V01.07
> ([ROADMAP.md](ROADMAP.md)).

## Design identity

- **Written primarily in Omnikarai**, not as a C shim: the array core,
  reductions and linalg are Omnikarai code; only the per-architecture
  kernels bottom out in hand-written SIMD. This is deliberate — Namurai is
  the first flagship consumer of the package system (V01.03), the memory
  model (V01.01) and the AArch64 kernels (V01.06), and its development
  *is* the stress test for all three.
- **An Omnikarai-native API — not a NumPy clone.** NumPy's API evolved for
  Python's object model; Omnikarai has different rules (int64/f64/bool/
  string/list/nil, indentation blocks, explicit buffers). The API is
  designed from the actual grammar; where NumPy's shape is genuinely the
  best design (broadcasting semantics, strided views) it is adopted as a
  *concept*, not copied as surface syntax.
- **Distributed as source Namurai and compiled Namurai** — the first real
  exercise of both distribution forms
  ([PACKAGE_ECOSYSTEM.md](PACKAGE_ECOSYSTEM.md)).

## Scope (eventual capability list)

| Area | Contents |
|------|----------|
| Arrays | multidimensional arrays, strided views, vectors, matrices; tensors where the AI stack needs them |
| Indexing | integer indexing, slices, steps, boolean masks (concept-level; exact syntax from grammar review) |
| Broadcasting | conservative, explicit-broadcast-first design; implicit rules documented and tested |
| Numeric types | i8–i64, f32/f64; complex numbers later; dtype fixed per array (no silent promotion — no silent-wrongness) |
| Random | seeded generators, standard distributions |
| Statistics | reductions, moments, correlation basics |
| Linear algebra | dot, matmul (extending today's cache-tiled AVX2 kernel), norms, decompositions (LU/QR/Cholesky), solvers |
| Signal | FFT and filter primitives |
| Parallel | SIMD-first; multi-threading only after the concurrency design exists ([FUTURE.md](FUTURE.md)) |
| Serialization | versioned binary format + checksums |
| Kernels | per-arch: AVX2 (x86-64), NEON (AArch64), scalar fallback everywhere |

## Staged plan

1. **ndarray core** — allocation via the V01.01 memory model (pools/arenas
   fit arrays naturally), strides, dtype discipline, indexing/slicing,
   print/repr.
2. **Reductions + elementwise** — sums, min/max, arithmetic; this alone
   unblocks most examples.
3. **Linalg basics** — dot/matmul on the existing kernels, then norms.
4. **Random + statistics** — seeded, testable.
5. **Decompositions/solvers + FFT** — reference implementations first,
   SIMD second (correctness before optimization).
6. **Serialization + package distribution** — source and compiled forms.

Every stage lands with reference tests against the C implementations
already maintained in `benchmarks/` and with benchmark entries in the
numerical category ([BENCHMARKS.md](BENCHMARKS.md)).

## Naming review (required before V01.07)

- The built-in module `ai` already exists; Namurai must compose with it,
  not compete with it (AI stack builds *on* Namurai arrays).
- Final decision (module name `namurai` vs alternatives, top-level module
  vs package namespace) happens at the V01.07 PROPOSAL gate per
  [PROJECT_DISCIPLINE.md](PROJECT_DISCIPLINE.md). This document uses
  "Namurai" as the working name throughout.
