# AI-Native Language Design

> Strategic goal: **the compiler and tooling should be built so that AI
> coding agents can understand, generate, test, debug and optimize
> Omnikarai programs more reliably than they can understand poorly
> structured legacy codebases.**
>
> Status: this is NOT achieved today, and nothing in this document claims
> otherwise. Today Omnikarai's agent-facing surface is an ordinary CLI +
> text diagnostics. Everything below is PLANNED with roadmap anchors.

## "Omnikarai Ultra Instinct" — design philosophy

> *Omnikarai should eventually make an AI coding agent feel unusually
> powerful because the compiler, language, runtime, package system and
> tooling expose rich semantic information and optimization capabilities.*

The test for any tooling decision: does it increase an agent's (and a
human's) ability to **know** what a program does without running it, and
to **verify** what it does after running it? The aspirational endpoint —
the feeling of *"why would I code this somewhere else?"* — is a compass,
not a feature list and not a marketing promise. It is never cited as a
reason to skip verification: agent-power without agent-checkable truth is
just confident wrongness, which this project forbids
([PROJECT_DISCIPLINE.md](PROJECT_DISCIPLINE.md)).

## Capability plan

| Capability | Purpose for agents | Status / anchor |
|------------|--------------------|-----------------|
| Machine-readable compiler errors (`check --json`: file, line, column, code, message) | fix errors without stderr scraping | PLANNED — V01.00 v0, V01.09 GA |
| Stable diagnostic codes | agents learn error taxonomy once | PLANNED — V01.00 |
| AST access (exportable) | program understanding, refactoring, analysis tools | PLANNED — V01.09 |
| Semantic graph (types, symbols) | type-aware edits, dead-code detection | PLANNED — V01.09 |
| Dependency graph (imports, packages) | impact analysis before edits | PLANNED — V01.09 |
| Type/symbol information dumps | hover-equivalent data for editors & agents | PLANNED — V01.09 |
| Automated test execution (runner as library-friendly CLI) | verify-behavior loop | EXISTS today (`tests/run_tests.py`); formalized with the runner |
| Profiling interface | measure, then optimize | PLANNED — V01.11 |
| Benchmark interface | compare before/after honestly | EXISTS in seed form (`benchmarks/run_benchmarks.py`); matured V01.11 |
| Optimization suggestions | compiler-recommended improvements | EXPLORATORY — [FUTURE.md](FUTURE.md) |
| Static analysis (beyond check) | catch bug classes pre-run | PLANNED incrementally (lifetime checks V01.01) |
| API documentation metadata (doc extraction) | agents read real signatures, not stale docs | PLANNED — V01.09 |
| Deterministic builds | same input → same artifact; agent edits are attributable | groundwork V01.02 |
| Structured project metadata (`omnikarai.toml` schema v1) | agents parse project intent | PLANNED — V01.03 |

## Design rules (how we keep this honest)

1. **Structured output mirrors real state.** JSON diagnostics describe
   exactly what the text diagnostics describe — never a rosier version.
2. **Machine-readable ≠ stable-by-fiat.** Schemas are versioned; breaking
   schema changes bump the schema version ([COMPATIBILITY.md](COMPATIBILITY.md)).
3. **Agents get no privileges.** An agent uses the same compiler, the same
   tests, the same gates as a human. No "agent mode" that skips
   verification.
4. **The human benefits too.** Every agent-facing feature must be useful
   to humans (editors, CI, code review) — this keeps the surface from
   drifting into gimmicks, and honors
   [VISION.md](VISION.md)'s dual audience.

## What "AI-native" does NOT mean here

- Not "executes Python" ([VISION.md](VISION.md) non-goals).
- Not "generates code without tests" — agents pass the same
  no-weakening test policy.
- Not "already achieved" — the current compiler is small, clean and
  inspectable, which *helps* agents, but none of the machinery above
  ships yet.
