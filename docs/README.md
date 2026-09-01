# Omnikarai — Documentation

Complete documentation set for the **Omnikarai V01 generation**: what the
project is, what actually works today, and the full roadmap. Start here.

| Document | Contents |
|----------|----------|
| **[VISION.md](VISION.md)** | The three pillars (systems / high-level / AI-native), performance principle, what Omnikarai is NOT |
| **[CURRENT_STATE.md](CURRENT_STATE.md)** | Evidence-based status: WORKING / PARTIALLY WORKING / EXPERIMENTAL / BROKEN / PLANNED |
| **[ROADMAP.md](ROADMAP.md)** | Master V01 roadmap: V01.00 → V01.11 feature releases, dependencies, blockers |
| **[VERSIONING.md](VERSIONING.md)** | The `VXX.YY.ZZZ-beta-NN` convention, legacy mapping, where versions live |
| **[ARCHITECTURE.md](ARCHITECTURE.md)** | Master system architecture (compiler / runtime / tooling / ecosystem), status-annotated |

## Project discipline & quality

| Document | Contents |
|----------|----------|
| [PROJECT_DISCIPLINE.md](PROJECT_DISCIPLINE.md) | Anti-scope-explosion charter: IDEA → PROPOSAL → … → RELEASE pipeline |
| [TECHNICAL_DEBT.md](TECHNICAL_DEBT.md) | Debt register with severity, location, target version, status |
| [VERSION_MATRIX.md](VERSION_MATRIX.md) | Version × capability tracking matrix |
| [FUTURE.md](FUTURE.md) | The parking lot: LONG-TERM / EXPLORATORY ideas, nothing committed |
| [GOVERNANCE.md](GOVERNANCE.md) | Decision model, legal/commercial flags |
| [COMPATIBILITY.md](COMPATIBILITY.md) | What is promised to keep working, when |
| [RELEASE_PROCESS.md](RELEASE_PROCESS.md) | Definition of Done and release gates |
| [CONTRIBUTING.md](CONTRIBUTING.md) | How to contribute; dev environment; review checklist |

## Language & implementation

| Document | Contents |
|----------|----------|
| [LANGUAGE.md](LANGUAGE.md) | Syntax reference: variables, functions, control flow, types, classes |
| [MODULES.md](MODULES.md) | Built-in modules: `time`, `datetime`, `math`, `os`, `io`, `sys`, `list`, `str`, `ai` |
| [COMPILER.md](COMPILER.md) | Compiler internals: lexer → parser → codegen → JIT / standalone build |
| [DIAGNOSTICS.md](DIAGNOSTICS.md) | Diagnostic codes, `check --json` schema (`omnikarai.diag.v0`), exit codes — tooling contract |
| [MEMORY_MODEL.md](MEMORY_MODEL.md) | Dual memory model: design requirements (syntax deliberately undecided) |
| [ABI.md](ABI.md) | Win64 + SysV calling conventions, payload format, package-ABI plan |

## Platform

| Document | Contents |
|----------|----------|
| [PLATFORM_SUPPORT.md](PLATFORM_SUPPORT.md) | Support matrix, SIMD tiers, platform abstraction layer |
| [AARCH64.md](AARCH64.md) | AArch64 port plan: AAPCS64, ELF64, NEON, Termux (PLANNED) |
| [BUILDING.md](BUILDING.md) | Building from source, CI, sanitizer and portable builds |

## Ecosystem

| Document | Contents |
|----------|----------|
| [PACKAGE_ECOSYSTEM.md](PACKAGE_ECOSYSTEM.md) | Source vs compiled packages; open-source / source-available / proprietary |
| [OMNIP.md](OMNIP.md) | Package manager client: current state + full responsibility checklist |
| [OPI.md](OPI.md) | Registry/service infrastructure: target architecture, API v1 plan |
| [PACKAGES.md](PACKAGES.md) | Current practical package mechanics + live registry API |
| [PACKAGE_SECURITY.md](PACKAGE_SECURITY.md) | Package trust design: signatures, verification, honest limits |
| [NAMURAI.md](NAMURAI.md) | Numerical array/math ecosystem (PLANNED) |
| [AI_ECOSYSTEM.md](AI_ECOSYSTEM.md) | Native AI stack: current `ai` module → full module map |
| [MCP.md](MCP.md) | MCP clients/servers, agent loops, model providers (PLANNED) |
| [AI_NATIVE_DESIGN.md](AI_NATIVE_DESIGN.md) | AI-agent-facing tooling + the "Ultra Instinct" philosophy |

## Security & measurement

| Document | Contents |
|----------|----------|
| [SECURITY.md](SECURITY.md) | Project security posture, principles, vulnerability reporting |
| [BENCHMARKS.md](BENCHMARKS.md) | Benchmark lab: categories, metrics, honesty rules |

## Reading paths

- **"What is this project?"** → [VISION.md](VISION.md) → [CURRENT_STATE.md](CURRENT_STATE.md)
- **"What ships next?"** → [ROADMAP.md](ROADMAP.md) → [VERSION_MATRIX.md](VERSION_MATRIX.md)
- **"I want to write a program"** → [LANGUAGE.md](LANGUAGE.md) → [MODULES.md](MODULES.md) → [BUILDING.md](BUILDING.md)
- **"I want to contribute"** → [CONTRIBUTING.md](CONTRIBUTING.md) → [PROJECT_DISCIPLINE.md](PROJECT_DISCIPLINE.md)
- **"Is this secure?"** → [SECURITY.md](SECURITY.md) → [PACKAGE_SECURITY.md](PACKAGE_SECURITY.md)
- **"How fast is it?"** → [BENCHMARKS.md](BENCHMARKS.md) (honest answer: BENCHMARKED for kernel-scale work, UNPROVEN vs -O2 C overall)

## Status summary (current: V01.00.000-beta-01)

- **Platforms:** Linux x86-64 (primary CI) and Windows x64 (MinGW CI).
- **Tests:** 21 unit + 9 stress + 13 regression programs + JSON diagnostics
  goldens + a version-consistency gate — all passing on every CI lane
  (gcc, ASan+UBSan, no-AVX2 portable, Win64 MinGW).
- **AI kernels:** AVX2 FP32 and INT8 quantized primitives emitted as
  native machine code, scalar fallbacks (`make portable`).
- **Known limitations** are stated in [CURRENT_STATE.md](CURRENT_STATE.md)
  and the root README — this project does not claim features it does not
  have.
