# Omnikarai Roadmap — V01 Generation

> This is a **proposed roadmap, not a schedule**. Dates are deliberately
> absent; releases ship when their release criteria pass
> ([RELEASE_PROCESS.md](RELEASE_PROCESS.md)). Ordering follows technical
> dependencies, documented below. Status/priority vocabulary and the idea
> pipeline: [PROJECT_DISCIPLINE.md](PROJECT_DISCIPLINE.md). Versions follow
> [VERSIONING.md](VERSIONING.md).

## Where we are

The repository at tag `v7.1.0-rc` (legacy numbering) is a stabilized,
tested x86-64 compiler for Linux and Windows with 9 built-in modules,
AVX2 FP32/INT8 AI kernels, a JIT execution path, engine-embedding
standalone builds, a Windows-only package client (omnip), a live registry
service (opi), CI on both platforms and an honest documentation set.
Evidence and known gaps: [CURRENT_STATE.md](CURRENT_STATE.md),
[TECHNICAL_DEBT.md](TECHNICAL_DEBT.md).

```
current state ──▶ V01 foundation ──▶ V01 ecosystem ──▶ V01 AI ──▶ V01 security ──▶ V01 performance ──▶ V02+
  v7.1.0-rc       V01.00–V01.02      V01.03–V01.05    V01.06–V01.09  V01.10           V01.11            FUTURE.md
```

## Ordering: proposed vs adopted (and why)

The original proposed sequence was V01.00 core → V01.01 memory → V01.02
packages → V01.03 omnip → V01.04 opi → V01.05 AArch64 → V01.06 Namurai →
V01.07 AI → V01.08 MCP → V01.09 security → V01.10 performance. After
inspecting the repository, three dependencies forced changes:

1. **Native executable emitters** are missing: `omnicc build` currently
   embeds the whole engine and the program *source* in an `OMNISRC1`
   payload. Compiled packages, honest binary sizes, IP protection and the
   AArch64 backend (which needs ELF-emission machinery) all depend on true
   ELF64/PE32+ output. → inserted as its own release **V01.02**.
2. **AArch64** moved after the emitters (it reuses them) → **V01.06**.
3. **Machine-readable diagnostics** were pulled *earlier* than proposed:
   they are small, they unblock the AI-native goal immediately, and they
   are required by the MCP/AI-tooling work. → foundation release
   **V01.00** and expanded in **V01.09**.

The adopted feature releases: **V01.00 → V01.11**.

| Version | Theme | Priority |
|---------------|----------------------------------------------|------|
| V01.00.x | Foundation: versioning adoption, diagnostics v0 | P0 |
| V01.01.x | Memory model | P0 |
| V01.02.x | Native executable emitters (ELF64 / PE32+) | P0 |
| V01.03.x | Package system (format v1, source packages) | P1 |
| V01.04.x | Omnip: cross-platform package client | P1 |
| V01.05.x | OPI: registry API v1 | P1 |
| V01.06.x | AArch64 (Linux / Termux) | P1 |
| V01.07.x | Namurai numerical foundation | P1 |
| V01.08.x | AI ecosystem foundation | P1 |
| V01.09.x | MCP & AI tooling | P2 |
| V01.10.x | Package security & trust | P0* |
| V01.11.x | Performance & optimization | P2 |

\* P0 *before compiled packages are trusted at scale*, though it ships
after the package system exists (you cannot sign what you cannot
distribute).

Dependency sketch:

```
V01.00 ─▶ V01.01 ─▶ V01.02 ─▶ V01.03 ─▶ V01.04 ─▶ V01.05
                        │                          │
                        └────────▶ V01.06          └─▶ V01.10 (trust)
                                       │
                                       ▼
                       V01.07 ─▶ V01.08 ─▶ V01.09
```

---

## V01.00.x — Foundation: versioning & diagnostics v0

- **Objective.** Make the repo fully self-describing under the new
  versioning convention and take the first AI-native tooling step.
- **Prerequisites.** None (first release of the generation).
- **Features.** In-code version strings switched to `V01.00.000-beta-NN`
  (`src/codegen.c` banner + `omni_sys_omni_ver`, `src/main.c`,
  `omnikarai.toml`); `omnicc version` gains a stable machine-parsable line;
  `omnicc check --json` emits structured parse/check diagnostics
  (file, line, column, code, message).
- **API changes.** `sys.version` reports the new scheme. No other language
  change.
- **Compiler changes.** Version single-sourcing (one header consumed
  everywhere); JSON diagnostic emitter alongside text.
- **Runtime changes.** None.
- **Package changes.** `omnikarai.toml` schema unchanged (v1 lands V01.03).
- **Tests.** Version-consistency test (all locations agree); JSON
  diagnostics golden tests; existing 30 tests stay green.
- **Benchmarks.** Unchanged.
- **Security.** Secret-scan preflight step added to CI (no `github_pat_` /
  `ghp_` patterns in tree).
- **Release criteria.** CI green on Linux + Windows; version test proves
  single-sourcing; docs updated to match.

## V01.01.x — Memory model

- **Objective.** Ship the dual memory model: friendly default plus explicit
  expert control. Design first (requirements already in
  [MEMORY_MODEL.md](MEMORY_MODEL.md)); syntax is decided by inspecting the
  real grammar, not invented blindly.
- **Prerequisites.** V01.00.
- **Features.** Minimum-viable explicit subset: allocation/deallocation
  builtins outside the `ai` module, ownership/lifetime annotations (design
  complete even if subset ships), alignment control, pools or arenas (one,
  not both), zero-copy buffer views. Beginner path: current behavior
  unchanged.
- **API changes.** New builtins/module; nothing existing breaks.
- **Compiler changes.** Lifetime-aware checks in `check`; codegen support
  for new builtins.
- **Runtime changes.** Allocator hooks; poison-on-free debug mode.
- **Tests.** New memory test battery; ASan/UBSan CI lane extended to cover
  them; double-free/use-after-free regression tests.
- **Benchmarks.** Allocation/deallocation microbenchmarks added
  ([BENCHMARKS.md](BENCHMARKS.md) memory category).
- **Security.** Safety analysis documented (dangling refs, double-free,
  leaks); bounds-checked defaults where cheap.
- **Release criteria.** Expert subset usable and tested; beginner path
  unchanged; safety doc reviewed.

## V01.02.x — Native executable emitters (ELF64 / PE32+)

- **Objective.** `omnicc build` produces true freestanding binaries.
- **Prerequisites.** V01.01 (runtime allocator hooks make static runtime
  linking sane), though the emitter design can proceed in parallel.
- **Features.** From-scratch ELF64 (Linux x86-64) and PE32+ (Windows x64)
  writers; runtime statically linked into output; libc linkage via standard
  crt startup; section/segment layout; relocations resolved at link time
  into the file; legacy `--embed-engine` mode retained one release.
- **API changes.** None (CLI flags only).
- **Compiler changes.** New object/executable emission stage after codegen;
  helper-call scheme adapted from host-process relocations to
  static-placement.
- **Runtime changes.** Startup path (`_start`/`mainCRTStartup`)
  responsibilities.
- **Package changes.** Unblocks compiled-package format (V01.03) and
  honest binary-size metrics.
- **Tests.** Built binaries run on clean machines without omnicc; ELF/PE
  structure validation tests; smoke-run of the full 30-test suite against
  file-output binaries, not only JIT.
- **Benchmarks.** Binary-size and startup-time metrics become honest
  (engine no longer embedded).
- **Security.** Reproducible-build groundwork (deterministic layout, no
  timestamps); W^X rules unchanged (JIT paths unaffected).
- **Release criteria.** Hello-world standalone < 200 KB on both OSes;
  full test suite green via standalone binaries; CI produces both.

## V01.03.x — Package system

- **Objective.** Define and ship package format v1 with source packages
  working end-to-end locally.
- **Prerequisites.** V01.02 (compiled-package feasibility; binary size
  honesty).
- **Features.** `omnikarai.toml` schema v1 (name, version per
  [VERSIONING.md](VERSIONING.md), deps with constraints, build, arch,
  abi); package directory layout; source-package build/install; local
  dependency resolution; lockfile (hash-pinned); namespace prefixing
  formalized (`<pkg>__fn`, current behavior documented and kept);
  compiled-package *format* defined (signing fields reserved, trust lands
  V01.10) and marked EXPERIMENTAL.
- **Distribution forms.** Source vs compiled distinction, and the
  open-source / source-available / proprietary triad, are documented in
  [PACKAGE_ECOSYSTEM.md](PACKAGE_ECOSYSTEM.md).
- **Tests.** Multi-package fixture projects (extend `test_pkg/`,
  `test_multipkg/`); lockfile reproducibility tests.
- **Release criteria.** A fresh clone can build+install a local source
  package and `use` it.

## V01.04.x — Omnip: cross-platform package client

- **Objective.** omnip becomes the official cross-platform client
  ([OMNIP.md](OMNIP.md)).
- **Prerequisites.** V01.03 (format v1 exists).
- **Features.** POSIX port (replace WinHTTP/localappdata with portable
  sockets + filesystem paths); commands: install, remove, update, search,
  publish, rollback; lockfile consumption; cache; arch/platform artifact
  selection; publishing source packages; compiled-package publish
  EXPERIMENTAL until V01.10.
- **Tests.** E2E against a local test registry; uninstall-cleanliness
  (RECORD) tests on both OSes.
- **Security.** Token storage hygiene (0600 perms); checksums on download;
  TLS required.
- **Release criteria.** Same command set works on Linux and Windows CI.

## V01.05.x — OPI: registry API v1

- **Objective.** Stabilize the client/registry contract
  ([OPI.md](OPI.md)); decouple OPI from compiler internals permanently.
- **Prerequisites.** V01.04 (client ready to pin the contract).
- **Features.** Versioned API (`/v1/…`): packages, versions, artifacts
  (source; compiled schema), dependency metadata, publisher accounts and
  ownership transfer, download stats, signature fields (consumed V01.10);
  mirrors and private-registry specifications; backup/restore runbook.
- **Tests.** Contract tests + live E2E against the public instance; the
  current missing E2E gap (see [TECHNICAL_DEBT.md](TECHNICAL_DEBT.md)) is
  closed here.
- **Security.** Rate limits; authz matrix; audit logging of publish
  events.
- **Release criteria.** omnip V01.04 operates a full publish/install
  lifecycle against the public registry in CI.

## V01.06.x — AArch64

- **Objective.** Native AArch64 support; the maintainer's Android/Termux
  environment becomes a first-class test environment
  ([AARCH64.md](AARCH64.md), [PLATFORM_SUPPORT.md](PLATFORM_SUPPORT.md)).
- **Prerequisites.** V01.02 (ELF emitter machinery refactored
  arch-agnostic).
- **Features.** AArch64 instruction emitter; AAPCS64 calling convention;
  ELF64 AArch64 output; NEON FP32/INT8 kernels with scalar fallbacks;
  language semantics stay architecture-independent (documented divergences
  only where unavoidable, e.g. pointer sizes already 64-bit).
- **Tests.** Full suite on aarch64 (native runner or QEMU CI); Termux
  smoke run documented.
- **Benchmarks.** Same suites, arch metadata recorded.
- **Release criteria.** 30+ tests green on x86-64 and AArch64 in CI.

## V01.07.x — Namurai numerical foundation

- **Objective.** The numerical array/math ecosystem of Omnikarai, written
  primarily in Omnikarai ([NAMURAI.md](NAMURAI.md)).
- **Prerequisites.** V01.03 (distribution), V01.06 (arch kernels worth
  having).
- **Features.** Ndarray core (strides, dtypes, indexing, slicing,
  broadcasting); numeric types (i8–i64, f32/f64; complex later);
  reductions; linalg basics (dot, matmul — building on the existing AVX2
  kernels); RNG; statistics basics; serialization; source + compiled
  distribution; per-arch optimized kernels.
- **Tests.** Numerical reference tests against C reference implementations
  already used by benchmarks.
- **Benchmarks.** Numerical category: vector ops, matmul, reductions, FFT
  (target list in [BENCHMARKS.md](BENCHMARKS.md)).
- **Release criteria.** `use namurai` (final name per naming review) runs
  the numerical benchmark category end-to-end in Omnikarai.

## V01.08.x — AI ecosystem foundation

- **Objective.** Native AI stack layered on Namurai
  ([AI_ECOSYSTEM.md](AI_ECOSYSTEM.md)).
- **Prerequisites.** V01.07.
- **Features.** Conceptual modules `ai.tensor`, `ai.nn`, `ai.optim`,
  `ai.loss`, `ai.dataset`, `ai.training`, `ai.inference`, `ai.quant`,
  `ai.serialization`, `ai.metrics` (final names fixed by a naming review
  against the existing built-in `ai` module); training loop CPU/SIMD
  first; quantization beyond INT8 dot; model serialization format;
  profiling hooks.
- **Tests.** Small-network training/inference golden tests.
- **Benchmarks.** AI category: tensor ops, small MLP/CNN/transformer
  primitives (the existing `t_bench_mlp` seed grows up).
- **Release criteria.** Train a small network from Omnikarai source,
  quantize, run inference — all native, benchmarked, no Python involved.

## V01.09.x — MCP & AI tooling

- **Objective.** First-class support for agent workloads and for AI agents
  as users ([MCP.md](MCP.md), [AI_NATIVE_DESIGN.md](AI_NATIVE_DESIGN.md)).
- **Prerequisites.** V01.08 (AI runtime exists to serve); V01.00
  (diagnostics v0).
- **Features.** MCP client/server packages (stdio + HTTP transports,
  tools/resources/prompts, structured tool calls); model-provider
  interface with routing; streaming; structured output; embeddings +
  vector-search primitives; agent memory; `omnicc` AST/semantic-graph
  export; `omnicc check --json` GA with codes stable enough for tools.
- **Release criteria.** An MCP server written in Omnikarai serves tools to
  a real MCP client; an agent loop runs natively.

## V01.10.x — Package security & trust

- **Objective.** Packages — especially compiled ones — carry verifiable
  trust ([PACKAGE_SECURITY.md](PACKAGE_SECURITY.md)).
- **Prerequisites.** V01.05 (registry contract), V01.04 (client).
- **Features.** Ed25519 package signatures; publisher identity and
  provenance; dependency/ABI/arch verification at install; checksums
  everywhere; revocation mechanism; capability declarations (advisory);
  reproducible-build support for compiled packages; vulnerability
  reporting and response process.
- **Honest limits.** Documented explicitly: static analysis cannot
  mathematically prove arbitrary native code harmless; guarantees are
  defined as what verification *does* check.
- **Release criteria.** omnip refuses unsigned/tampered packages; a
  compiled proprietary package passes the trust pipeline end-to-end.

## V01.11.x — Performance & optimization

- **Objective.** Move the headline claim from TARGET/UNPROVEN to
  BENCHMARKED ([VISION.md](VISION.md), [BENCHMARKS.md](BENCHMARKS.md)).
- **Prerequisites.** V01.02 (honest binary metrics); ideally after V01.07
  so kernels benefit.
- **Features.** Optimization passes (ephemeral-pass generalization,
  register allocation beyond loop pinning, light scheduling); benchmark
  lab maturity: full metric set (runtime, peak memory, allocations,
  startup, compile time, binary size, throughput/latency, SIMD
  utilization, scaling); CI perf-regression tracking against recorded
  baselines; categories: general computing, memory, numerical, AI.
- **Rules.** Never cherry-pick; runner metadata always published; C/C++/
  Rust/Python/NumPy (and where relevant Julia/PyTorch) comparisons on
  equivalent workloads.
- **Release criteria.** A published, reproducible results table for the
  full category set on reference hardware.

## V02+ (long-term / exploratory)

GPU backend, WebAssembly, distributed computing, advanced AI compilation,
accelerators, embedded targets, advanced concurrency, LSP, debugger,
profiler, cloud/enterprise tooling — tracked in [FUTURE.md](FUTURE.md).
Nothing there is committed.

## Current blockers (top of register)

1. omnip is Windows-only → V01.04.
2. Standalone builds embed the engine → V01.02.
3. No optimizer; C/C++ parity UNPROVEN → V01.11.
4. Text-only diagnostics → V01.00 / V01.09.
5. opi has no live E2E test → V01.05.

Full register with severity and status:
[TECHNICAL_DEBT.md](TECHNICAL_DEBT.md).

## Important architectural decisions (standing)

- No LLVM; hand-written backends per architecture (cost accepted in the
  optimizer schedule).
- Win64 and SysV ABIs selected at omnicc compile time
  ([ABI.md](ABI.md)).
- W^X for JIT memory (RW → RX → unmap).
- opi fails secure on missing JWT secret.
- Omnip = client, OPI = registry; never coupled to compiler internals.
- Docs must match implementation; plans are labelled.
