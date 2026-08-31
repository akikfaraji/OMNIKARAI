# Current State — evidence-based

> Last verified against the tree at tag `v7.1.0-rc` (commit `da13c15`).
> Every claim below cites its evidence: file, test or workflow. If a claim
> cannot cite evidence, it does not belong in this document. Companion
> documents: [TECHNICAL_DEBT.md](TECHNICAL_DEBT.md) (problems),
> [ROADMAP.md](ROADMAP.md) (future), [VISION.md](VISION.md) (goals).

## Version

- Legacy version string `7.1.0`, tag `v7.1.0-rc`
  (`src/codegen.c`, `src/main.c`, `omnikarai.toml`).
- **Next planned version:** `V01.00.000-beta-01` under the convention in
  [VERSIONING.md](VERSIONING.md).

## WORKING (verified by tests/CI in this tree)

| Capability | Evidence |
|------------|----------|
| x86-64 native codegen, no LLVM | `src/codegen.c` (~5.3k lines); `omnicc dump` shows real encodings |
| Win64 **and** SysV AMD64 calling conventions | `include/abi.h`; same 30-test suite green on both platforms in CI |
| JIT execution with W^X | `src/main.c` run path; mmap/mprotect vs VirtualAlloc/VirtualProtect |
| Standalone builds (engine-embedding) | `OMNISRC1` payload in `src/main.c`; PHASE-31 clean-clone check |
| 9 built-in modules: time, datetime, math, os, io, sys, list, str, ai | `docs/MODULES.md`; tests t08–t20 |
| Language: fn/set/const/if/elif/else/while/for-in/match/class/use | `docs/LANGUAGE.md`; tests t01–t07, t14, t21 |
| AI kernels: FP32 dot/matmul/ReLU/softmax (AVX2), INT8 dot (VPMADDUBSW), scalar fallbacks | tests t16–t20; `make portable` lane in CI |
| Package loading from site-packages (`use <pkg>`, `<pkg>__fn` prefixing) | `docs/PACKAGES.md`; `test_pkg/`, `test_multipkg/` fixtures |
| opi registry: register/login/tokens/publish/list/stats, JWT **fails secure** | `opi/api/` (`_auth.js` removed the `opi-dev-secret` fallback); audit finding #10 closed |
| omnip client v6.0.0: publish/install, RECORD-based clean uninstall | `omnip/src/omnip.c` (853 lines) — **Windows-only**, see PARTIALLY |
| Portable test suite: 21 unit + 9 stress, 30/30 green | `tests/run_tests.py`; CI |
| CI on Linux (gcc, ASan+UBSan, portable) and Windows (MinGW) | `.github/workflows/linux.yml`, `windows.yml` |
| Reproducible benchmark runner, multi-language (C/C++/Go/Java/JS/Python) | `benchmarks/run_benchmarks.py` |
| MIT license + third-party notices | `LICENSE`, `THIRD-PARTY-NOTICES.md` |
| Honest documentation set | `docs/` (5 docs + index) |

## PARTIALLY WORKING

| Area | Works | Missing | Evidence |
|------|-------|---------|----------|
| omnip | publish/install/uninstall on Windows | POSIX build entirely (WinHTTP, LOCALAPPDATA hardwired); no lockfile/rollback yet | `omnip/src/omnip.c` header, `docs/PACKAGES.md` |
| Standalone executables | run on any same-platform machine | true freestanding binaries (engine embedded, source travels inside the file) | `src/main.c` payload layout |
| opi | core publish/list/auth flow | live E2E test, mirrors, private registries, signatures | `opi/api/`; debt register |
| Benchmarks | time measurements across 6 languages | peak memory, allocations, startup, binary size, SIMD-utilization metrics | `benchmarks/` |
| Platform layer | Linux + Windows x86-64 | macOS untested (POSIX port targeted Linux); AArch64 not started | `include/omni_platform.h`; CI matrix |

## EXPERIMENTAL

- **JIT host register pinning**: correct and tested today (a real
  SIGSEGV-class bug was found and fixed during the Linux port — pinned
  register save moved before `loop_top`), but the mechanism is subtle and
  documented as fragile-by-nature until a real register allocator exists
  (V01.11).
- **Compiled packages**: the format exists as fields in omnip's model;
  trust/verification does not exist. Do not distribute compiled packages
  as trusted artifacts before V01.10
  ([PACKAGE_SECURITY.md](PACKAGE_SECURITY.md)).

## BROKEN (nothing silently broken; these are known and registered)

- No user-facing breakage known at this tag: 30/30 tests green on both
  platforms. Known *limitations* and debt are tracked, not hidden:
  [TECHNICAL_DEBT.md](TECHNICAL_DEBT.md) (top items: stale metadata —
  fixed while writing these docs — Windows-only omnip, missing native
  emitters, missing optimizer, text-only diagnostics, no opi E2E).

## PLANNED (not started; roadmap-linked)

Memory model (V01.01) · native ELF64/PE32+ emitters (V01.02) · package
format v1 (V01.03) · omnip POSIX port (V01.04) · OPI API v1 (V01.05) ·
AArch64 + Termux (V01.06) · Namurai (V01.07) · AI stack (V01.08) · MCP +
agent tooling (V01.09) · package signing/trust (V01.10) · optimizer +
benchmark lab (V01.11). See [ROADMAP.md](ROADMAP.md).

## WHAT SHOULD BE PRESERVED (explicitly)

1. The hand-written, dependency-free codegen approach — it is the project's
   identity and its honest differentiator.
2. The honest documentation tone (limitations stated plainly).
3. The test suite's no-weakening policy and its platform-aware runner.
4. The fail-secure opi auth posture.
5. The full git history, including pre-stabilization — tags are permanent.
6. The `omnicc` CLI shape (`run/build/dump/check/version`) — tools and
   scripts depend on it.

## WHAT SHOULD BE REPLACED (explicitly)

1. Engine-embedding standalone builds → native emitters (V01.02).
2. Windows-only omnip → portable client (V01.04).
3. Loop-pinning-only register strategy → real allocation (V01.11).
4. Ad-hoc version constants scattered in four files → single-sourced
   version (V01.00).
5. Text-only diagnostics → structured diagnostics (V01.00/V01.09).

## Audit-method note

The original independent audit's claim that "115 tracked `.ok` files are
test outputs" was **wrong**: `.ok` is the Omnikarai **source-file
extension**; the files in `tests/` are the test *programs* and the runner
compiles and executes them. Corrections like this are recorded in
[CURRENT_STATE.md](CURRENT_STATE.md) when found.
