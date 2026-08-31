# Version Matrix

> One row per planned version, one column per capability area. Status
> values: **DONE** · **PARTIAL** · **PLANNED** · **EXPERIMENTAL** · **N/A**
> (capability not part of that release's scope). The authoritative
> definition of each release's content is [ROADMAP.md](ROADMAP.md); this
> matrix is the tracking view. Versions follow
> [VERSIONING.md](VERSIONING.md).

| Version | Compiler | Runtime | Memory | Packages | Omnip | OPI | Namurai | AI | MCP | AArch64 | Security | Benchmarks | Status |
|---------------|----------|-----------|-----------|-------------|-----------|-----------|---------|-----------|---------|---------|--------------|------------|----------------|
| `v7.1.0-rc` (legacy) | x86-64 JIT + embedded-standalone, Win64+SysV | helpers + 9 modules | ai buffers only (raw f32/i8) | site-packages `use` | Win-only v6.0.0 | live, fail-secure JWT | — | AVX2 FP32/INT8 kernels | — | — | fail-secure auth, W^X | time-only runner | **current tag** |
| `V01.00.000-beta-NN` | version single-sourcing; `check --json` v0 | unchanged | unchanged | toml corrected | unchanged | unchanged | — | unchanged | — | — | CI secret-scan | unchanged | **PLANNED (next)** |
| `V01.01.000-beta-NN` | lifetime checks; new memory builtins | allocator hooks, poison-on-free | **dual model v1** (explicit subset) | unchanged | unchanged | unchanged | — | unchanged | — | — | safety analysis + ASan lanes | allocation microbenchmarks | PLANNED |
| `V01.02.000-beta-NN` | **native ELF64 + PE32+ emitters**; legacy embed mode | static-link startup | unchanged | compiled format defined (unsigned) | unchanged | unchanged | — | unchanged | — | — | reproducible-build groundwork | binary-size + startup metrics | PLANNED |
| `V01.03.000-beta-NN` | unchanged | unchanged | unchanged | **format v1**, source packages, lockfile | unchanged | unchanged | — | unchanged | — | — | checksum groundwork | unchanged | PLANNED |
| `V01.04.000-beta-NN` | unchanged | unchanged | unchanged | format v1 consumed | **POSIX port**, full command set | unchanged | — | unchanged | — | — | token hygiene, TLS, checksums | unchanged | PLANNED |
| `V01.05.000-beta-NN` | unchanged | unchanged | unchanged | registry artifacts schema | client pins `/v1` contract | **API v1**, E2E, mirrors/private spec | — | unchanged | — | — | authz matrix, rate limits | unchanged | PLANNED |
| `V01.06.000-beta-NN` | AArch64 emitter + AAPCS64 | NEON kernels | unchanged | arch tags in packages | arch artifact selection | arch metadata | — | NEON FP32/INT8 | — | **ELF64 AArch64, Termux** | unchanged | arch-tagged runs | PLANNED |
| `V01.07.000-beta-NN` | unchanged | unchanged | unchanged | namurai as package | unchanged | unchanged | **ndarray core, linalg, RNG** | built on namurai | — | — | unchanged | numerical category | PLANNED |
| `V01.08.000-beta-NN` | unchanged | unchanged | unchanged | ai stack packages | unchanged | unchanged | dependency | **tensor/nn/optim/loss/training/inference/quant** | — | — | unchanged | AI category (MLP/CNN/transformer primitives) | PLANNED |
| `V01.09.000-beta-NN` | AST/semantic-graph export; `check --json` GA | unchanged | unchanged | mcp packages | unchanged | unchanged | unchanged | serves AI runtime | **MCP client/server, agents** | — | unchanged | unchanged | PLANNED |
| `V01.10.000-beta-NN` | unchanged | unchanged | unchanged | signed artifacts | signature verify + revocation | signature fields live, provenance | unchanged | unchanged | unchanged | — | **Ed25519 signing, verification, revocation** | unchanged | PLANNED |
| `V01.11.000-beta-NN` | **optimizer + register allocation** | unchanged | perf tuning | unchanged | unchanged | unchanged | optimized kernels | tuned kernels | unchanged | — | unchanged | **full metric lab, CI regression tracking** | PLANNED |
| `V02+` | per [FUTURE.md](FUTURE.md) | distributed? | advanced | ecosystem growth | enterprise | enterprise | GPU/accel backends | distributed AI | agent ecosystems | more archs | enterprise | standing lab | EXPLORATORY |

## Reading the matrix

- A capability marked PLANNED in row *N* and unchanged in row *N+1* simply
  carries forward; only the release that changes it names the change.
- The matrix is updated in the same commit as any roadmap change
  ([PROJECT_DISCIPLINE.md](PROJECT_DISCIPLINE.md)).
- The current tag row is re-verified against evidence whenever
  [CURRENT_STATE.md](CURRENT_STATE.md) is updated.
