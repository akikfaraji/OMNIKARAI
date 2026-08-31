# Contributing to Omnikarai

> Welcome. The rules here are short because most of them live elsewhere:
> engineering principles in [VISION.md](VISION.md) and
> [PROJECT_DISCIPLINE.md](PROJECT_DISCIPLINE.md), release gates in
> [RELEASE_PROCESS.md](RELEASE_PROCESS.md), roadmap in [ROADMAP.md](ROADMAP.md).

## Ground rules

1. **Correctness before optimization.** A slower correct change beats a
   faster questionable one.
2. **No feature without tests.** If your change can't show a test, it
   isn't done. Tests are never weakened to pass.
3. **Docs must match implementation.** If you add behavior, update the
   doc; if you can't ship the behavior, don't ship the doc.
4. **Ideas go to the parking lot.** New ideas do not automatically become
   active development — propose via
   [PROJECT_DISCIPLINE.md](PROJECT_DISCIPLINE.md). Unassigned ideas land
   in [FUTURE.md](FUTURE.md).
5. **No secrets in the tree.** Tokens/keys live in environment variables
   or credential helpers, never in source, docs, scripts, commit messages
   or git config.
6. **Preserve history.** No force-pushes to shared branches; no rewriting
   others' commits.

## Development environment

```
git clone https://github.com/akikfaraji/OMNIKARAI
cd OMNIKARAI
make                # build bin/omnicc (AVX2 kernels)
make portable       # scalar fallback build
make asan           # AddressSanitizer + UBSan build
make test           # build + run the portable suite (21 unit + 9 stress)
```

Details, Windows notes and CI description:
[BUILDING.md](BUILDING.md). Platform matrix: [PLATFORM_SUPPORT.md](PLATFORM_SUPPORT.md).

## What a good change looks like

- **One logical change** per commit ([PROJECT_DISCIPLINE.md](PROJECT_DISCIPLINE.md)).
- Tests for the behavior (unit and/or stress; parser changes should also
  fuzz-seed the corpus once TD-08 lands its first lane).
- Docs updated in the same change: language → [LANGUAGE.md](LANGUAGE.md),
  modules → [MODULES.md](MODULES.md), architecture →
  [COMPILER.md](COMPILER.md), roadmap-affecting → [ROADMAP.md](ROADMAP.md)
  + [VERSION_MATRIX.md](VERSION_MATRIX.md).
- Commit messages in the established style:
  `type(scope): summary` — types: `feat`, `fix`, `docs`, `chore`, `test`,
  `ci`, `bench`, `release`.
- The full suite passes on both platforms (CI enforces it; run locally on
  yours at minimum).

## Where to start (good first areas)

- `tests/` — the suite is deliberately readable; extending coverage of
  error paths is open debt ([TECHNICAL_DEBT.md](TECHNICAL_DEBT.md) TD-08).
- `docs/` — inaccuracies are bugs; file them.
- `benchmarks/` — adding a benchmark with full metadata is always useful.
- `omnip/` — the POSIX port (TD-02) is the highest-value open ecosystem
  work; see [OMNIP.md](OMNIP.md).

## Review checklist (maintainers apply; contributors pre-check)

- [ ] tests included and green
- [ ] sanitizers clean for touched code
- [ ] docs updated and status labels honest
- [ ] no secrets, no generated artifacts
- [ ] commit message type/scope correct
- [ ] roadmap/matrix rows updated if scope changed

## License

By contributing you agree that your contributions are licensed under the
repository license (MIT — see `LICENSE`; third-party notices:
`THIRD-PARTY-NOTICES.md`).
