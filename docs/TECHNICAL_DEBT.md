# Technical Debt Register

> Real problems found in the repository, recorded so they cannot be
> forgotten or hidden. Nothing here is embarrassing enough to justify
> silence; silent-wrongness is the only unacceptable state
> ([PROJECT_DISCIPLINE.md](PROJECT_DISCIPLINE.md)). Debt items get a
> version target; "UNASSIGNED" items are queued for roadmap assignment.

| ID | Severity | Location | Problem | Impact | Recommended solution | Target version | Status |
|----|----------|----------|---------|--------|----------------------|----------------|--------|
| TD-01 | Medium | `omnikarai.toml` | Stale metadata: version said `6.02.24`, header typo (`[etadata]`), description claimed Windows-only, repo URL pointed at the old GitHub location | Misleads tooling and humans about the actual version and platform support | Fixed during the V01 documentation milestone (version `7.1.0`, corrected header/description/URL) | V01.00 | **FIXED** (this milestone) |
| TD-02 | High | `omnip/src/omnip.c` | omnip is Windows-only: `windows.h`, `winhttp.h`, `%LOCALAPPDATA%` paths | Package distribution impossible on Linux/macOS; ecosystem blocked for half the supported platforms | Portable platform layer (reuse `include/omni_platform.h` patterns); sockets/TLS abstraction; path resolution via XDG | V01.04 | PLANNED |
| TD-03 | High | `src/main.c` (standalone build) | No freestanding binary emitter: `omnicc build` copies the whole omnicc engine and appends the program **source** (`OMNISRC1` payload) | ~1 MB binaries; source ships inside "compiled" artifacts; undercuts the systems-language story and IP protection for compiled packages | From-scratch ELF64 + PE32+ emitters with statically linked runtime | V01.02 | PLANNED |
| TD-04 | High | `src/codegen.c` | No optimizer: no SSA, no scheduling, no register allocation beyond loop-scoped pinning | Register-heavy code not competitive with `-O2` C (UNPROVEN claim guarded in docs) | Optimization passes + general register allocation | V01.11 | PLANNED |
| TD-05 | Medium | `src/main.c`, `src/parser.c` | Diagnostics were one-line text on stderr: no codes, no columns, no JSON | Blocked the AI-native tooling goal | Structured model + stable codes + `check --json` (`omnikarai.diag.v0`) + caret text rendering — [DIAGNOSTICS.md](DIAGNOSTICS.md) | V01.00 (v0), V01.09 (GA) | **v0 SHIPPED**; GA remains V01.09 |
| TD-06 | Medium | `opi/` | No live end-to-end test against the running registry | Registry regressions reach production unnoticed | Contract tests + E2E lane against public instance in CI | V01.05 | PLANNED |
| TD-07 | Medium | `include/omni_version.h` (was 4+ locations) | Version string was duplicated in 4+ locations; the runner asserted two of them | Bumps were error-prone; drift already happened once (toml said 6.02.24) | Single-sourced `OMNI_VERSION` header + `tests/check_version.py` gate in `make test` and CI | V01.00 | **FIXED** (this milestone) |
| TD-08 | Medium | `tests/` | No fuzzing; error-handling coverage was thin (only `t15_assert` targeted failure paths) | Robustness against malformed input unproven | Fuzz lexer/parser with structured corpus | V01.00 (fuzz seed), V01.01+ (expand) | PARTIAL: error-path battery shipped (regression suite + JSON goldens); fuzzing still PLANNED |
| TD-09 | Low | `include/omni_platform.h` | macOS is unclaimed: POSIX port tested on Linux only | Untested platform claims would violate docs-matches-implementation | macOS CI lane or explicit "unsupported" labeling (current choice: labeled untested) | UNASSIGNED | DOCUMENTED |
| TD-10 | Low | repository | Local scratch files at root (`_r*.ok`, `_time*.ok`, `journal.txt`, `docs_internal/`) are ignored but present in working clones | Cosmetic noise for contributors | None needed (ignored); periodic local cleanup | — | ACCEPTED |
| TD-11 | Low | `benchmarks/` | Metrics limited to wall-clock time | Cannot verify memory-efficiency goals (VISION performance principle) | Metric expansion per BENCHMARKS.md | V01.02 (binary size/startup), V01.11 (full set) | PLANNED |
| TD-12 | Low | `docs/` (pre-V01 set) | Original 5-doc set accurate but not indexed into the V01 hierarchy | Navigation friction | Superseded by this documentation milestone (index: [README.md](README.md)) | V01.00 | **DONE** (this milestone) |
| TD-13 | High | `src/codegen.c` (param `scope_define`, inline expander) | Function parameters have no static types. V01.00 made them UNKNOWN so int/string value flow is correct (string args no longer int_to_str-corrupted), but pass-through returns (`fn id(s): return s`) and float-typed params remain mistyped | Silent pointer/float-bits output for those shapes | Typed-parameters design over the real grammar (interprocedural or declared), sequenced with the V01.01 grammar review | V01.01 | DOCUMENTED |
| TD-14 | Medium | `docs/LANGUAGE.md`, `src/parser.c` | The documented `const` keyword is not implemented (parser has no TOKEN_CONST statement) | Docs promised a feature that fails to parse | Docs corrected to honest state now; feature lands with the V01.01 grammar review (compile-time constants feed the existing fold paths) | V01.01 | DOCUMENTED (docs corrected this milestone) |
| TD-15 | High | `src/codegen.c` (loop register pinning) | Specific statement mixes (list.push loop after a realloc-move + string-concat loop + several prints) corrupt pinned/slot state → SIGSEGV or bogus realloc of a stale pointer | Deterministic crash on valid programs; pinning is documented fragile-by-nature | Contained hardening shipped (list.push syncs pinned registers); general fix is register allocation | V01.11 | PARTIALLY HARDENED |
| TD-16 | Medium | `src/codegen.c` | At the pre-V01 tag, const-folding operand corruption (`2 + 3 * 4` → 24) and integer-ops-on-double-bits float arithmetic produced silently wrong results | Silent wrong results are the worst failure class | Ephemeral-tracker invalidation + SSE2 double path + permanent regression suite (r02/r04) | V01.00 | **FIXED** (this milestone) |
| TD-17 | Medium | `src/main.c` | CLI exit codes were 0/1 only and unknown `--flags` were silently treated as file names | Tools could not distinguish usage errors from compile errors | Documented deterministic table (0 ok / 1 diagnostics / 2 usage-IO); unknown flags rejected — [DIAGNOSTICS.md](DIAGNOSTICS.md) | V01.00 | **FIXED** (this milestone) |
| TD-18 | High | `src/codegen.c` (call sites) | JIT call sites emit calls with ALTERNATING rsp parity (odd net push depth at ~half of all `call rax` sites) — a SysV/Win64 ABI alignment violation. Runtime functions compiled by aggressive optimizers (gcc 13 -O2 scalar tier) emit aligned SSE stores → SIGSEGV in ~half the unit suite on the runner, with a per-binary-varying crash set. Invisible under AVX2 builds (VEX/unaligned forms), gcc 14.2, and -O1/ASan | Correctness landmine: any future callee using aligned SSE faults; environment-dependent test failures | Defensive: `-mstackrealign` on all compiled tiers (shipped this iteration). Proper fix: track rsp parity in the emitter and pad odd call sites — lands with the register-allocator work | V01.11 (proper) | **MITIGATED** (stackrealign this iteration; emitter fix pending) |
| TD-19 | Medium | `src/codegen.c` (Win64 path) | `emit_mov_rcx_rax/rdx_rax/rdx_rcx` were used ~30 lines before their static definitions; gcc >= 14 hard-errors on implicit declarations, so the MinGW build failed. Also, JIT-debug asm used `long` outputs for 64-bit register reads — valid on LP64 Linux, "operand type mismatch" on LLP64 Windows | Windows CI could never reach a compile verdict | Definitions moved above the ABI block; jitdbg types widened to `long long` (both discovered on the first-ever valid Windows CI run) | V01.00.000-beta-02 | **FIXED** (this iteration) |

## Closed debt (kept for the record)

| ID | Was | Fixed how | Commit |
|----|-----|-----------|--------|
| — | Hardcoded JWT fallback `opi-dev-secret` | Fail-secure auth (`opi/api/_auth.js`); deployment without a real secret refuses to authenticate | `7152ba2` |
| — | ~7k lines dead/backup code in the source tree | Removed in the cleanup pass | `e329c57` |
| — | PowerShell-only test runners | Portable `tests/run_tests.py` | `30ce393` |
| — | Inlined-return codegen bug (stress03), div/mod on negatives, pinned-register save inside loop | Fixed and regression-tested | `337dd9e` |
| — | Missing LICENSE/docs/CI | MIT + docs + CI pipelines | `27d180a` |
| — | ASan-found OOB read in the `strncpy_s` shim | Fixed | `3c88ece` |

## Rules for this register

1. Every item names a location and an impact — no vague debt.
2. Severity: High = blocks a roadmap pillar or a supported platform;
   Medium = slows honest progress; Low = cosmetic or accepted.
3. Statuses: PLANNED / IN PROGRESS / FIXED / DOCUMENTED / ACCEPTED.
4. Items are never deleted; fixed items move to "Closed debt".
