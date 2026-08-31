# Omnikarai — Master Architecture

> This is the **conceptual system architecture** for the V01 generation,
> annotated with what actually exists today. Compiler-internals detail
> lives in [COMPILER.md](COMPILER.md); the build goes in
> [BUILDING.md](BUILDING.md); the roadmap for the missing pieces is
> [ROADMAP.md](ROADMAP.md). Status markers: **[TODAY]** exists and is
> tested · **[PARTIAL]** exists but incomplete · **[PLANNED]** designed,
> not built · **[EXPLORATORY]** idea only ([FUTURE.md](FUTURE.md)).

## System overview

```
                              OMNIKARAI
                                  │
        ┌─────────────────────────┼─────────────────────────┐
        │                         │                         │
    COMPILER                  RUNTIME                    TOOLING
   [TODAY]                   [PARTIAL]                  [PARTIAL]
        │                         │                         │
        └─────────────────────────┼─────────────────────────┘
                                  │
                             PACKAGE ABI                     ← [PARTIAL]: formats defined, versioning PLANNED
                                  │
                ┌─────────────────┴─────────────────┐
                │                                   │
             OMNIP (client)                    OPI (registry)
            [PARTIAL: Win-only]                    [TODAY]
                │                                   │
                └─────────────────┬─────────────────┘
                                  │
                           PACKAGE ECOSYSTEM                     ← [PLANNED: format v1, source+compiled]
                                  │
      ┌───────────────────────────┼───────────────────────────┐
      │                           │                           │
   NAMURAI                        AI                          MCP
  [PLANNED]                  [PARTIAL: ai kernels]        [PLANNED]
      │                           │                           │
      └───────────────────────────┼───────────────────────────┘
                                  │
                             AI WORKLOADS                             ← [PLANNED]
```

Every box below states what is real, so this diagram can never silently
overstate the system (see [PROJECT_DISCIPLINE.md](PROJECT_DISCIPLINE.md)
rule: documentation matches implementation).

## COMPILER — [TODAY]

`omnicc` is a dependency-free C99 compiler (~7.5k lines, four sources) that
compiles `.ok` source files to x86-64 machine code:

```
prog.ok ──lexer.c──▶ tokens ──parser.c──▶ AST ──codegen.c──▶ x86-64 bytes
                                                             │
        main.c ──────────────────────────────────────────────┴─▶ run / build / dump / check
```

- Two calling conventions, chosen at omnicc build time: **Win64** and
  **SysV AMD64** (`include/abi.h`) — see [ABI.md](ABI.md).
- Real instruction encoding: REX prefixes, ModRM/SIB, jump patching,
  helper relocations. No LLVM, no bytecode VM.
- Loop-scoped register pinning for hot variables; ephemeral store→load
  elimination; small-function inlining. No general optimizer **[TODAY]** —
  planned V01.11.
- AVX2 FP32/INT8 AI kernels emitted directly as machine code, scalar
  fallbacks via `make portable`.
- W^X discipline: JIT memory RW → RX → unmap.

Full detail: [COMPILER.md](COMPILER.md).

### Execution models — [TODAY] and the gap

| Model | How it works | Honest cost |
|-------|--------------|-------------|
| JIT run (`omnicc run`) | compile to memory, execute in-process under W^X | none; primary mode |
| Standalone (`omnicc build`) | copy the omnicc binary, append `[source][magic OMNISRC1][len]`; the copy recompiles in-process at startup | engine embedded (~1 MB); source travels with the binary |
| **Native emitters** (ELF64/PE32+) | planned V01.02 — true freestanding binaries | closes both costs above |

## RUNTIME — [PARTIAL]

Runtime helpers are C functions compiled into omnicc; generated code calls
them with the host ABI. Provided today: printing, strings, lists, time,
the 9 standard modules ([MODULES.md](MODULES.md)), and the AI kernels.
Platform abstraction (`include/omni_platform.h`) covers filesystem, memory
status, W^X executable memory on POSIX and Windows. Missing until the
V01.01/V01.02 work: allocator hooks, static-link startup, debugger-grade
introspection.

## TOOLING — [PARTIAL]

Today: CLI (`run/build/dump/check/version`), portable test runner
(`tests/run_tests.py`), reproducible benchmark runner
(`benchmarks/run_benchmarks.py`), CI on both platforms. PLANNED
(V01.00/V01.09): structured JSON diagnostics, version single-sourcing,
AST/semantic-graph export for AI agents ([AI_NATIVE_DESIGN.md](AI_NATIVE_DESIGN.md)).
EXPLORATORY: LSP, debugger, profiler ([FUTURE.md](FUTURE.md)).

## PACKAGE ABI — [PARTIAL]

The contract between compiled artifacts and consumers. Today it consists
of: (a) the `OMNISRC1` standalone payload format, (b) site-packages
directory layout with `<pkg>__fn` symbol prefixing, (c) two ABIs (Win64,
SysV) selected at compiler build time. PLANNED: versioned package ABI with
architecture tags so compiled packages can declare compatibility — the
reason `V01.02` precedes `V01.03`. Detail: [ABI.md](ABI.md),
[PACKAGE_ECOSYSTEM.md](PACKAGE_ECOSYSTEM.md).

## OMNIP (client) / OPI (registry) — [PARTIAL] / [TODAY]

**Omnip** is the package manager client: install/remove/publish with a
pip-inspired directory-tree + RECORD model. **Windows-only at v7.1.0**
(WinHTTP + LOCALAPPDATA) — the POSIX port is roadmap V01.04.
**OPI** is the registry service: Node.js on Vercel + Neon Postgres, JWT
auth (fails secure), package publish/list/stats. The two are separate
components with an HTTP/JSON contract; OPI must never depend on compiler
internals. Details: [OMNIP.md](OMNIP.md), [OPI.md](OPI.md), current API
surface in [PACKAGES.md](PACKAGES.md).

## PACKAGE ECOSYSTEM — [PLANNED]

Source packages (inspectable) and compiled packages (implementation
private), with the open-source / source-available / proprietary triad and
its trust consequences. Design: [PACKAGE_ECOSYSTEM.md](PACKAGE_ECOSYSTEM.md);
security model: [PACKAGE_SECURITY.md](PACKAGE_SECURITY.md).

## NAMURAI / AI / MCP — [PLANNED] / [PARTIAL] / [PLANNED]

- **Namurai**: the numerical array/math ecosystem, written primarily in
  Omnikarai, per-architecture kernels. Seeds that exist today: the AVX2
  FP32/INT8 kernels and the `math` module.
  [NAMURAI.md](NAMURAI.md).
- **AI**: today the built-in `ai` module (alloc/free, FP32 + INT8 dot,
  matmul, ReLU, softmax). The layered stack (tensor → nn → training →
  inference → serving) is PLANNED: [AI_ECOSYSTEM.md](AI_ECOSYSTEM.md).
- **MCP**: MCP clients/servers, tool-calling, agent loops, vector search —
  PLANNED: [MCP.md](MCP.md).

## AI WORKLOADS — [PLANNED]

The end state: tensor math (Namurai), training and inference (AI stack),
and agent/MCP integration running natively in Omnikarai, benchmarked
against NumPy/PyTorch-class alternatives with published methodology
([BENCHMARKS.md](BENCHMARKS.md)).

## Standing invariants (apply to every layer)

1. No LLVM / no external compiler dependency.
2. Fail secure on security-relevant defaults (auth, verification).
3. W^X for executable memory.
4. Documentation matches implementation; unshipped things are labelled.
5. Platform semantics stay architecture-independent wherever possible
   ([PLATFORM_SUPPORT.md](PLATFORM_SUPPORT.md)).
6. The client (omnip) and the registry (opi) communicate only via
   versioned HTTP/JSON — never via compiler internals.
