# Platform Support

> Status matrix for supported platforms. Claims here must match CI — an
> untested platform is listed as untested, never as supported
> ([PROJECT_DISCIPLINE.md](PROJECT_DISCIPLINE.md)). See also
> [AARCH64.md](AARCH64.md) for the next architecture.

## Current support matrix (TODAY)

| Platform | Architecture | Status | ABI | Toolchain | Evidence |
|----------|--------------|--------|-----|-----------|----------|
| Linux | x86-64 | **supported** (primary) | SysV AMD64 | gcc / clang, C99 | CI `linux.yml`; 30/30 tests |
| Windows | x86-64 | **supported** | Win64 | MinGW-w64 gcc | CI `windows.yml` |
| macOS | x86-64 / arm64 | **untested** — not claimed | (POSIX path exists, unverified) | — | no CI lane; deliberately not labeled supported |
| Android/Termux | AArch64 | **diagnostics tier** (V01.00.x) — native planned (V01.06) | AAPCS64 (planned) | Termux clang | CI `linux.yml` arm64 lane; build + `check --json` + version gate green, `run`/`build` refuse with `OMNI-E0005`; [AARCH64.md](AARCH64.md), [BUILDING.md](BUILDING.md) |

## SIMD tiers (TODAY)

| Tier | Builds via | Contains | Runtime requirement |
|------|------------|----------|---------------------|
| AVX2+FMA (default) | `make` | VFMADD231PS, VPMADDUBSW, VMAXPS, fused softmax | Haswell (2013) or newer |
| Scalar fallback | `make portable` | same operations, scalar kernels | any x86-64 |

The compiler itself never requires AVX2; only the default language runtime
kernels do. Both tiers run the full test suite in CI (separate lanes).

## Platform abstraction layer

`include/omni_platform.h` is the single choke point for OS differences:
filesystem operations, directory iteration, memory status, and W^X
executable-memory management (mmap/mprotect on POSIX,
VirtualAlloc/VirtualProtect on Windows). New platform work (macOS
verification, AArch64) extends this layer first — no OS `#ifdef`s outside
it.

## Site-packages paths (TODAY)

| Platform | Path |
|----------|------|
| Windows | `%LOCALAPPDATA%\Programs\omnikarai\site-packages\<name>\` |
| POSIX | `$XDG_DATA_HOME/omnikarai/site-packages/<name>/` (default `~/.local/share/omnikarai/site-packages/`) |

(omnip, the installer that populates these, is currently Windows-only —
[OMNIP.md](OMNIP.md); roadmap V01.04.)

## Planned matrix evolution

| Milestone | Change | Roadmap |
|-----------|--------|---------|
| V01.02 | freestanding binaries per platform (ELF64, PE32+) | [ROADMAP.md](ROADMAP.md) |
| V01.04 | omnip on POSIX → Linux package workflow complete | [ROADMAP.md](ROADMAP.md) |
| V01.06 | AArch64: ELF64 AArch64 + AAPCS64 + NEON; Termux as supported test environment | [AARCH64.md](AARCH64.md) |
| UNASSIGNED | macOS verification lane (CI) | [TECHNICAL_DEBT.md](TECHNICAL_DEBT.md) TD-09 |

## Architecture-independence rule

Language semantics are architecture-independent wherever possible. Where
they cannot be (e.g., integer widths are already fixed at 64-bit; endianness
assumptions in buffer bit-pattern access), the divergence is documented in
the language reference and kept identical across architectures. No program
should observe *which* backend compiled it, only *how fast* it runs.
