# AArch64 Port Plan (PLANNED — V01.06)

> Status: **PLANNED**. Nothing in this document exists in the tree yet.
> Roadmap placement and rationale: [ROADMAP.md](ROADMAP.md) V01.06.
> Prerequisite: the native ELF64 emitter machinery from V01.02, refactored
> to be architecture-agnostic.

## Objective

Omnikarai compiles and runs natively on AArch64: Linux servers, and —
explicitly — **Android/Termux as a first-class test environment**, since
that is the maintainer's daily environment. The user-facing claim when
this ships: "the same `.ok` program compiles and runs on x86-64 Linux,
x86-64 Windows and AArch64 Linux/Termux."

## Work breakdown (DESIGNED at this level; details land with the release)

### 1. Backend selection and structure

- New emitter module alongside the x86-64 backend; `include/abi.h` gains an
  architecture dimension. The one-pass codegen structure is preserved
  (this is a second backend, not a rewrite —
  [PROJECT_DISCIPLINE.md](PROJECT_DISCIPLINE.md) forbids aesthetic
  rewrites, and the x86-64 backend keeps its own tests green).
- AArch64 fixed-width 32-bit instruction encoding (no unaligned
  instruction streams), literal pools for constants, branch
  distance resolution for the fixed-width format.

### 2. Calling convention: AAPCS64

- Arguments X0–X7, return X0; callee-saved X19–X28, X29 (FP), X30 (LR);
  SP-alignment discipline. Distinct from both Win64 and SysV x86-64 —
  the ABI table in [ABI.md](ABI.md) gains its third row.
- Pinned-variable strategy re-derived for the 32 general-purpose registers
  (more callee-saved registers than x86-64 → simpler pinning).

### 3. Object/output: ELF64 AArch64

- Reuses the V01.02 ELF writer with `e_machine = EM_AARCH64 (183)`,
  AArch64 relocation types, and the AAPCS64 runtime startup path.
- Termux targets a clang/`ld` environment: static runtime linking with
  bionic libc linkage validated there specifically.

### 4. SIMD kernels: NEON

- FP32 dot/matmul/ReLU/softmax and INT8 dot ported to NEON
  (`FMLA`, `SDOT`/`UDOT` for INT8), with the same scalar fallback contract
  as the x86 `make portable` tier ([PLATFORM_SUPPORT.md](PLATFORM_SUPPORT.md)).
- Kernel-level tests compare NEON vs scalar outputs element-wise — the
  same no-silent-wrongness rule as x86.

### 5. Runtime/platform layer

- `include/omni_platform.h` AArch64 paths audited (it is already POSIX on
  Linux, so most pieces carry over; memory-status and executable-memory
  details verified on device).

### 6. CI and testing

- Native AArch64 CI lane (GitHub Actions arm64 runner) running the full
  30+-test suite; QEMU fallback lane for PRs if needed.
- Termux smoke-run script documented in this document when the release
  ships — the maintainer's phone becomes a legitimate test bench.
- Benchmarks gain `arch: aarch64` metadata ([BENCHMARKS.md](BENCHMARKS.md)).

## Explicitly out of scope for V01.06

- Windows-on-ARM, iOS, 32-bit ARM — parked in [FUTURE.md](FUTURE.md).
- Advanced NEON autovectorization of user code (optimizer territory,
  V01.11+).

## Risks

| Risk | Mitigation |
|------|------------|
| Fixed-width encoding makes jump-patching harder than x86 | literal-pool + branch-resolution design reviewed before codegen starts (PROPOSAL stage gate) |
| Termux bionic differences (no glibcisms) | kernel/portable layer already uses POSIX-only constructs; bionic conformance tested on device early |
| Two-backend maintenance cost | shared instruction-selection interfaces kept minimal; x86-64 suite must stay green in every CI run |
