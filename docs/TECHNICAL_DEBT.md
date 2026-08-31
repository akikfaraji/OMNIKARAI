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
| TD-05 | Medium | `src/main.c`, `src/parser.c` | Diagnostics are one-line text on stderr: no codes, no column spans, no JSON | Blocks AI-native tooling goal; harder error recovery for agents and humans | `check --json` structured diagnostics with stable codes | V01.00 (v0), V01.09 (GA) | PLANNED |
| TD-06 | Medium | `opi/` | No live end-to-end test against the running registry | Registry regressions reach production unnoticed | Contract tests + E2E lane against public instance in CI | V01.05 | PLANNED |
| TD-07 | Medium | `src/codegen.c`, `src/main.c`, `tests/run_tests.py` | Version string duplicated in 4+ locations; runner asserts two of them | Version bumps are error-prone; inconsistency risk (already happened: toml said 6.02.24) | Single-sourced version header consumed everywhere + consistency test | V01.00 | PLANNED |
| TD-08 | Medium | `tests/` | No fuzzing; error-handling coverage thin (only `t15_assert` targets failure paths) | Parser/codegen robustness against malformed input unproven | Fuzz lexer/parser with structured corpus; error-path test battery | V01.00 (fuzz seed), V01.01+ (expand) | PLANNED |
| TD-09 | Low | `include/omni_platform.h` | macOS is unclaimed: POSIX port tested on Linux only | Untested platform claims would violate docs-matches-implementation | macOS CI lane or explicit "unsupported" labeling (current choice: labeled untested) | UNASSIGNED | DOCUMENTED |
| TD-10 | Low | repository | Local scratch files at root (`_r*.ok`, `_time*.ok`, `journal.txt`, `docs_internal/`) are ignored but present in working clones | Cosmetic noise for contributors | None needed (ignored); periodic local cleanup | — | ACCEPTED |
| TD-11 | Low | `benchmarks/` | Metrics limited to wall-clock time | Cannot verify memory-efficiency goals (VISION performance principle) | Metric expansion per BENCHMARKS.md | V01.02 (binary size/startup), V01.11 (full set) | PLANNED |
| TD-12 | Low | `docs/` (pre-V01 set) | Original 5-doc set accurate but not indexed into the V01 hierarchy | Navigation friction | Superseded by this documentation milestone (index: [README.md](README.md)) | V01.00 | **DONE** (this milestone) |

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
