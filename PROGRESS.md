# Omnikarai Progress

This file records what changed between releases. The first section
covers the V01 generation; the engineering-level audit log for the
legacy stabilization is in `docs_internal/FINDINGS.md`.

## V01.00.000-beta-01 — Foundation: versioning & diagnostics v0

Versioning & CLI

- Version single-sourced in `include/omni_version.h` (`OMNI_VERSION`);
  `sys.omni_ver()`, `sys.version()`, the CLI banner and `omnikarai.toml`
  all derive from it; `tests/check_version.py` gate runs in `make test`
  and CI (closes TD-07). Adopted the FRAZIYM `VXX.YY.ZZZ-beta-NN`
  convention; the legacy `7.1.0` line is history-only.
- `omnicc version --machine`: stable key=value output for tooling.
- `omnicc --help/-h/help` to stdout (exit 0); deterministic exit codes
  (0 ok / 1 diagnostics / 2 usage-IO); unknown `--flags` rejected
  instead of being treated as file names (closes TD-17).

Diagnostics v0 (docs/DIAGNOSTICS.md is the tooling contract)

- Structured model (severity, stable code, message, file, line, column,
  span, hint) with code registry `OMNI-<S><NNNN>` (0xxx CLI, 2xxx
  parser, 3xxx semantic, 9xxx internal).
- `omnicc check --json`: `omnikarai.diag.v0` document on stdout —
  golden-tested; runs full parse+codegen validation; one document per
  run on every termination path.
- Human text output now carries file/line/caret with readable messages
  ("expected ':' but found identifier 'x'"); internal token debug only
  behind --beta.

Compiler correctness (found by the new regression suite; all
pre-existing at the pre-V01 tag)

- Const-fold operand corruption: `2 + 3 * 4` compiled to 24. Root
  cause: missing ephemeral-tracker invalidation after raw RAX writes
  (fold path, const-RHS fast paths, literals, math constants) let a
  tracked reload be skipped (closes TD-16).
- Float arithmetic: `+ - * /` and comparisons on float operands now
  compile to SSE2 double code (were integer ALU ops over double bits);
  `infer_type` propagates FLOAT to print/args/returns; `%`/`**` on
  floats are loud type errors.
- String arguments into functions were corrupted by int_to_str on the
  pointer: parameters are no longer statically INT (typing itself is
  TD-13, V01.01).
- Bare `name = value` reassignment parsed but failed codegen with
  "unknown operator '='": now desugared like augmented assignment.
- list.push now syncs the realloc-moved pointer to pinned registers
  (stale-register crash class; full trigger recorded as TD-15).

Memory foundation (docs/MEMORY_MODEL.md)

- Internal runtime allocation funnel `omni_mem_*` (lists, AI buffers,
  int8, instances) with live counters and `OMNI_MEM_DEBUG=1`
  poison-on-free; allocator hooks (arena/pool) are the V01.01 step.
  No public memory syntax was invented.

Testing & CI

- Permanent regression suite: 13 programs (inline return, const-fold
  torture, signed div/mod, float literals/args/returns, ABI arg order,
  loop register preservation, INT8 API, string inference, shims,
  standalone build, exit codes incl. `sys.exit(7)`) + JSON diagnostics
  goldens; output AND exit status asserted.
- CI: secret-scan preflight job; version gate; regression lanes on
  gcc, ASan+UBSan, no-AVX2 portable and Win64 MinGW.

Known leftovers (honest): no fuzzing (TD-08); untyped parameters
(TD-13); `const` unimplemented (TD-14, docs corrected); pinned-register
crash trigger still reachable (TD-15, V01.11).

---

## Legacy: v7.1.0 stabilization (877e18f → da13c15)

The previous section boundary is preserved below.


### Security & trust

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
