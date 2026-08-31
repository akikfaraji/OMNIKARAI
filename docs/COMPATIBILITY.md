# Compatibility Policy

> What Omnikarai promises to keep working, when, and how changes are
> announced. Status today: **no stability promise** (pre-V01) — stated
> honestly so nobody builds on sand. This document defines the promise
> machinery for when the promises start.

## Current status (TODAY)

- **Language surface**: may change between any two releases. The suite
  protects against *accidental* change (30/30 green), not against
  *deliberate* change.
- **CLI** (`run/build/dump/check/version`): kept stable in shape
  (the repository depends on it), flags may grow.
- **ABI**: none stable — see [ABI.md](ABI.md).
- **Package format**: v1 arrives V01.03; before that, site-packages
  layout and `<pkg>__fn` prefixing are de-facto but unpromised.
- **`ai` module API**: current functions (`ai.alloc/free/set/get/
  set_u8/set_i8/get_u8/get_i8/dot/dot_i8/matmul/relu/softmax`) are kept
  working through V01 — the AI stack extends, it does not break, this
  surface.

## Promise schedule (roadmap-anchored)

| From | What becomes promised | Scope |
|------|----------------------|-------|
| V01.00 | versioning scheme consistency; `omnicc version` output line format | mechanical |
| V01.02 | standalone binary format replaced by real executables; legacy `--embed-engine` mode kept one release | transition |
| V01.03 | package format v1 + `omnikarai.toml` schema v1 | format-level |
| V01.02/03 | versioned **Package ABI** (`abi_version`, arch, platform, kernel tier) | binary-level |
| V01.09 | `check --json` diagnostic schema v1 (versioned) | tooling |
| V01.10 | signed-artifact verification behavior | trust |

## Rules once a promise exists

1. **Generation rule.** Within a `VXX` generation ([VERSIONING.md](VERSIONING.md)),
   language and ABI compatibility is maintained; breaking changes require
   a new generation and a migration note.
2. **Feature rule.** Within a feature line (`V01.YY.*`), `ZZZ` patches
   never break; `YY` bumps may add (not remove) surface.
3. **Deprecation process.** (a) mark deprecated in docs + compiler
   warning; (b) keep working for at least the next two feature releases;
   (c) remove only in a new generation — each step in release notes.
4. **No silent behavior change.** If a program's *output* would change
   (formatting, rounding, error text consumers parse), that is a breaking
   change and follows rule 3 — even if the grammar didn't change.
5. **Test-protected.** Every promise above gets compatibility tests
   (fixture packages, golden outputs, schema round-trips). A promise
   without a test is not a promise.

## Compatibility matrix (target)

Maintained per release from V01.03 onward: package format versions
understood, ABI versions produced, JSON schema versions emitted, CLI
flags available. Lives in this file as the table grows.

## What is explicitly NOT promised

- Benchmark numbers across versions (hardware and builds differ —
  [BENCHMARKS.md](BENCHMARKS.md) metadata makes comparisons meaningful,
  not the numbers themselves).
- Internal compiler structures (codegen byte layouts, AST node shapes) —
  these are free to change; the *export* surfaces (V01.09) are what carry
  stability.
- The site-packages directory *location* per OS (may move with
  justification; moves are announced and both locations honored for one
  release).
