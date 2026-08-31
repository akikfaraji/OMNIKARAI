# Omnikarai Vision

> Status: **blueprint** (V01 generation). Items marked PLANNED below are
> design intent, not shipped capability. The evidence-based state of the
> project lives in [CURRENT_STATE.md](CURRENT_STATE.md).

Omnikarai is a native systems/AI programming language and ecosystem. It is
compiled by `omnicc`, a dependency-free C99 compiler that emits x86-64
machine code directly — no LLVM, no bytecode VM, no interpreter layer. The
long-term goal is a language where the same source can express a tight SIMD
kernel and a high-level data pipeline, and where the whole toolchain is
built so that **AI coding agents** can read, write, run, test and optimize
Omnikarai programs more reliably than they can work with typical legacy
codebases.

## Three pillars

### 1. Systems-level capability — *partially real today, far from complete*

- C/C++-class **performance target** for equivalent workloads. Today this is
  a target, not an achievement — see the performance principle below.
- **Direct memory control**: today only via the raw `ai` buffers
  (`ai.alloc`/`ai.free`, 64-byte aligned, bit-pattern access). A full
  explicit model (ownership, lifetimes, pools, arenas) is PLANNED —
  [MEMORY_MODEL.md](MEMORY_MODEL.md).
- **Predictable execution**: native code, no GC pause today, explicit
  resource handling in the runtime.
- **SIMD**: real today for AI/math kernels — the compiler emits
  `VFMADD231PS`, `VPMADDUBSW`, `VMAXPS` and fused softmax sequences, with
  scalar fallbacks for non-AVX2 CPUs (`make portable`).
- **Architecture-specific optimization**: x86-64 only today (Win64 + SysV
  ABIs). AArch64 is PLANNED — [AARCH64.md](AARCH64.md).
- **Native compilation**: real codegen exists (REX prefixes, ModRM/SIB,
  patching, relocations), but *freestanding* binary emission does not:
  `omnicc build` embeds the engine. Replacing that with true ELF64/PE32+
  emitters is a headline V01 milestone — [ROADMAP.md](ROADMAP.md).

### 2. High-level usability — *real today in syntax, thin in ecosystem*

- Python-shaped syntax (indentation blocks, `fn`/`set`/`match`, `for … in`)
  with C-shaped runtime semantics (int64 truncating division, no hidden
  boxing). See [LANGUAGE.md](LANGUAGE.md).
- High-level **libraries**: PLANNED — the package ecosystem
  ([PACKAGE_ECOSYSTEM.md](PACKAGE_ECOSYSTEM.md)) is the vehicle.
- **Easy package importing**: `use <pkg>` works against locally installed
  packages; distribution via Omnip/OPI is the V01 workstream
  ([OMNIP.md](OMNIP.md), [OPI.md](OPI.md)).
- **Developer-friendly APIs and excellent diagnostics**: the standard
  modules are documented ([MODULES.md](MODULES.md)); diagnostics are
  currently one-line text and are a known gap — machine-readable diagnostics
  are PLANNED ([AI_NATIVE_DESIGN.md](AI_NATIVE_DESIGN.md)).

### 3. AI-native development — *philosophy today, engineering plan in V01*

The language, compiler and tooling are designed so AI agents can generate,
test, debug and optimize Omnikarai programs efficiently: structured
diagnostics, machine-readable metadata, deterministic builds, benchmark
interfaces. Nothing in this paragraph is shipped yet beyond the ordinary
properties a small clean compiler already has. The concrete engineering
plan: [AI_NATIVE_DESIGN.md](AI_NATIVE_DESIGN.md). The future native AI
stack (tensors → training → inference → agents/MCP):
[AI_ECOSYSTEM.md](AI_ECOSYSTEM.md), [MCP.md](MCP.md).

## What Omnikarai is NOT

- **Not a Python host.** Omnikarai is not a language that "secretly executes
  Python". It is a native systems/AI language. Python, C, C++, Rust, Julia,
  NumPy and PyTorch are **comparison/benchmark targets** unless a specific
  interoperability feature is explicitly designed and shipped later.
- **Not LLVM-based.** The backend is hand-written and dependency-free. This
  is a constraint the project embraces, and it shapes the roadmap (an
  honest optimizer takes longer to build than wiring up LLVM — the plan
  accounts for that).
- **Not currently a verified C/C++ competitor.** No claim of parity is made
  anywhere in these documents.

## Performance principle (core project goal)

> Omnikarai should target performance and memory efficiency comparable to
> optimized C/C++ for equivalent workloads.

To keep ourselves honest, every performance statement must name which of
these four categories it belongs to:

| Category | Meaning | Example (today) |
|----------|---------|-----------------|
| **CURRENT** | What the implementation demonstrably does now | 30/30 tests pass; AVX2 dot product at 8 FP32 lanes/cycle in the kernel inner loop |
| **TARGET** | The design goal | C/C++-class performance for equivalent workloads |
| **BENCHMARKED** | Measured by the reproducible runner, published with full metadata | benchmark programs exist for fib, loops, primes, matmul, dot vs C/C++/Go/Java/JS/Python |
| **UNPROVEN** | Believed or hoped, not measured | "comparable to -O2 C" on register-heavy code — **UNPROVEN** and explicitly not claimed: omnicc has no optimizer today |

The benchmark system that turns UNPROVEN into BENCHMARKED (metrics beyond
time: peak memory, allocations, startup, binary size, SIMD utilization):
[BENCHMARKS.md](BENCHMARKS.md). Rules: never cherry-pick, always publish
runner metadata.

## The memory philosophy (long-term goal)

> A beginner should be able to use memory without understanding pointers,
> while an expert should still be able to manually control memory when
> necessary.

Both halves matter. The dual memory model — friendly default management
plus expert explicit control (ownership, lifetimes, alignment, pools,
arenas, zero-copy) — is designed in [MEMORY_MODEL.md](MEMORY_MODEL.md) as a
set of requirements, not invented syntax. Safety implications are analyzed
there.

## Ecosystem vision (summary)

| Component | Role | Document |
|-----------|------|----------|
| **Omnip** | package manager client / build & dependency tool | [OMNIP.md](OMNIP.md) |
| **OPI** | package registry / service infrastructure | [OPI.md](OPI.md) |
| **Packages** | source and compiled distribution forms | [PACKAGE_ECOSYSTEM.md](PACKAGE_ECOSYSTEM.md) |
| **Namurai** | numerical array/math ecosystem (the "NumPy of Omnikarai", but native) | [NAMURAI.md](NAMURAI.md) |
| **AI stack** | tensors, training, inference, quantization | [AI_ECOSYSTEM.md](AI_ECOSYSTEM.md) |
| **MCP/agents** | model-context-protocol clients/servers, agent loops | [MCP.md](MCP.md) |

## Non-goals

- Becoming a Python dialect or a transpiler target.
- Hiding the machine from experts to make beginners comfortable.
- Adding features because they exist in other languages (see
  [PROJECT_DISCIPLINE.md](PROJECT_DISCIPLINE.md)).
- Claiming performance that is not BENCHMARKED by the reproducible runner.

## Ultra Instinct (design philosophy, not a product promise)

"Omnikarai Ultra Instinct" names the long-term feeling that an AI coding
agent is *unusually powerful* in this ecosystem because the compiler,
runtime, package system and tooling expose rich semantic information and
optimization capabilities — "why would I code this somewhere else?" It is a
design compass for tooling decisions, not a shipped feature and not a
marketing claim. Details: [AI_NATIVE_DESIGN.md](AI_NATIVE_DESIGN.md).
