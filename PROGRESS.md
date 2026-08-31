# Omnikarai Stabilization Progress — v7.1.0

This file records what changed between the last pre-stabilization commit
(`877e18f "Bugfix -23"`, 2026-05-25) and the v7.1.0 release candidate.
The engineering-level audit log with per-finding evidence is
`docs_internal/FINDINGS.md`.

## Security & trust

- MIT LICENSE added; third-party notices documented (previously unlicensed).
- Package registry auth fails secure: hardcoded `opi-dev-secret` JWT
  fallback removed everywhere; deployments without `JWT_SECRET` refuse to
  authenticate.
- All 20 committed `.exe` binaries, stale scratch outputs, and the personal
  `journal.txt` removed from the repository (~7k dead LOC deleted with them:
  `codegen_backup.c`, `codegen_test.c`).

## Cross-platform port

- Full POSIX backend in `include/omni_platform.h` (file ops, directory
  iteration, memory status, exec memory via mmap/mprotect, path shims).
- Generated code targets Win64 **and** SysV ABIs (`include/abi.h`):
  argument registers, shadow space, pinned-register sets, XMM float args.
- Linux is the primary CI platform; Windows x64 (MinGW) CI keeps the
  Win64 path exercised.

## Compiler correctness (audit findings → fixes)

- print/sysV arg0 bug (every print returned garbage on SysV).
- `sqrt(144.0)` class of bugs: float literals corrupted by unconditional
  CVTSI2SD; math entry now converts per static type.
- `-7 / 4` class of bugs: SAR/AND/Barrett fast paths replaced by
  CQO+IDIV (truncation semantics, verified on negatives).
- Inlined `return` inside `if` no longer emits a physical `ret` into the
  caller's stream (stress03 now passes).
- For-loop register pinning no longer corrupts callee-saved registers
  (the JIT host crashed with SIGSEGV on Linux).
- `ai.free` returns a defined status; `sys.version` reports the real
  platform; the ghost `numrai` module is gone from banner, tables, docs.

## AI module

- INT8 API added: `ai.set_i8 / set_u8 / get_i8 / get_u8` — the only
  correct way to populate buffers consumed by `ai.dot_i8`.

## Toolchain

- Standalone builds redesigned: `omnicc build` emits a self-copy with the
  program source embedded; the artifact recompiles in-process at startup
  and genuinely runs without omnicc installed. The previous from-scratch
  PE emitter could never resolve its runtime references and was removed.
- Portable test runner `tests/run_tests.py` (Python) replaces the
  PowerShell-only runners; 21 unit + 9 stress tests, all passing.
- CI: linux.yml (gcc / ASan+UBSan / portable), windows.yml (MinGW).
- Reproducible benchmark runner `benchmarks/run_benchmarks.py`.
- ASan+UBSan clean on the full suite; gcc `-fanalyzer` reports 0 CWE
  findings; an OOB read in the `strncpy_s` shim (masked by the MSVC CRT on
  Windows) was caught and fixed by the sanitizer build.

## Versions

- `v6.02.24` → **v7.1.0** everywhere (banner, `sys.version`, `sys.omni_ver`,
  tests, docs).
