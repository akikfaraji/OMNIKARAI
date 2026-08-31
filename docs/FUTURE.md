# FUTURE — The Parking Lot

Everything in this file is **LONG-TERM / EXPLORATORY**. Nothing here is
committed, scheduled, designed in detail, or promised. Items are promoted
out of this file only through the pipeline in
[PROJECT_DISCIPLINE.md](PROJECT_DISCIPLINE.md) (IDEA → PROPOSAL → TECHNICAL
REVIEW → ROADMAP → VERSION ASSIGNMENT).

If an idea is not in [ROADMAP.md](ROADMAP.md) with a priority and status,
this file is where it lives.

## V02+ platform candidates (EXPLORATORY)

| Idea | One-line sketch | Why not now |
|------|-----------------|-------------|
| GPU backend | emit compute kernels for GPU targets (SPIR-V/PTX-class) | CPU x86-64 correctness and the optimizer come first; a second backend doubles the correctness surface |
| WebAssembly backend | run Omnikarai in browsers/sandboxes | depends on a real object-file emitter existing first (V01 native-executable milestone) |
| Advanced SIMD autovectorization | compiler auto-vectorizes user loops (SSE/AVX/NEON) | requires SSA + loop analysis; current codegen is one-pass |
| Distributed computing | multi-node data/task execution runtime | depends on the AI/runtime stack (Namurai, ai.distributed) existing |
| Advanced AI compilation | graph-level fusion, quantization passes in the compiler | depends on ai.tensor-level IR, which depends on Namurai |
| Accelerator support | NPUs, DSPs, other accelerators | no portable abstraction layer exists yet |
| Embedded systems targets | freestanding/no-libc profiles, small MCUs | current runtime leans on libc; needs a no-libc profile design |
| Advanced concurrency | threads, channels, async model in-language | today the model is single-threaded processes; needs a memory-model design first (V01.01) |

## Tooling candidates (EXPLORATORY)

| Idea | One-line sketch |
|------|-----------------|
| Language Server (LSP) | completions, hover, go-to-definition for editors |
| Debugger | source-level stepping over generated machine code |
| Profiler | sampling/instrumentation profiler with source mapping |
| IDE integration | editor plugins beyond plain LSP (project views, benchmarks) |
| Cloud tooling | hosted builds, remote package builds, signed artifact service |
| Enterprise tooling | private registry management UIs, audit logs, SSO |
| Package ecosystem growth tooling | scorecards, doc generation, dependency dashboards |

## Explicitly out of scope, standing policy

- Becoming a Python host or transpile target (see [VISION.md](VISION.md)).
- Any feature that exists only to imitate another language.
- Billing/pricing implementation before real usage and cost data exist
  (see the commercial notes in [GOVERNANCE.md](GOVERNANCE.md)).

## How to move something out of this file

Open a proposal per [PROJECT_DISCIPLINE.md](PROJECT_DISCIPLINE.md). The
bar: a written problem, a smallest-change design, a cost/risk estimate, and
a verification plan. If it cannot meet that bar, it stays here — and that
is a fine, permanent outcome for most ideas.
