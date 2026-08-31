# Release Process

> A version is **stable** when every gate below passes — not when the
> calendar says so. Versions and tags follow [VERSIONING.md](VERSIONING.md);
> the matrix of what ships where is [VERSION_MATRIX.md](VERSION_MATRIX.md).

## Definition of Done (release gates)

A version cannot be considered stable until **all** of:

1. **Build passes** — `make` (AVX2) and `make portable` (scalar), on every
   supported platform ([PLATFORM_SUPPORT.md](PLATFORM_SUPPORT.md)).
2. **Tests pass** — full suite (21 unit + 9 stress, growing per roadmap)
   on all supported platforms and all CI lanes.
3. **Regression tests pass** — every previously-fixed bug keeps its
   regression test and that test passes (no weakening, ever).
4. **Sanitizers pass** — ASan+UBSan lane clean where applicable.
5. **Supported architectures pass** — x86-64 Linux + Windows today;
   AArch64 from V01.06 ([AARCH64.md](AARCH64.md)).
6. **Documentation matches implementation** — every doc in `docs/`
   audited against the tree for this release; status labels honest;
   version strings consistent everywhere ([VERSIONING.md](VERSIONING.md)
   single-source list).
7. **Benchmarks are reproducible** — the runner runs end-to-end and
   results (if published) carry full metadata
   ([BENCHMARKS.md](BENCHMARKS.md)).
8. **Security review complete** for relevant changes — auth/verification/
   memory-safety changes get an explicit review note
   ([SECURITY.md](SECURITY.md)).
9. **Package compatibility verified** — from format v1 (V01.03) onward:
   fixture packages (`test_pkg/`, `test_multipkg/`) install and load.
10. **Release notes written** — what changed, what's fixed, known
    limitations, upgrade notes. Notes that could not be written honestly
    mean the release is not ready.

## Version lifecycle

```
VXX.YY.ZZZ-beta-01 ─▶ ... ─▶ VXX.YY.ZZZ-beta-NN ─▶ gates pass ─▶ VXX.YY.ZZZ (stable)
        │                        │
        └── fixes bump beta-NN   └── fixes during beta bump ZZZ, restart beta-NN
```

- Prerelease iterations are cut freely; **stable promotion is rare and
  gate-driven**.
- Never re-tag a published version (rule 4 of [VERSIONING.md](VERSIONING.md)).

## Mechanics (checklist)

1. `git status` clean; on `main`; correct remote
   (`git remote -v` → `dest` = `https://github.com/akikfaraji/OMNIKARAI.git`).
2. Bump version atomically in all single-source locations
   ([VERSIONING.md](VERSIONING.md) table) + `tests/run_tests.py`
   expectations.
3. Full gate run (above).
4. Annotated tag `vVXX.YY.ZZZ(-beta-NN)` on the release commit.
5. Push branch + tag together; CI green on the release commit.
6. GitHub release from the tag with the release notes.
7. Update [CURRENT_STATE.md](CURRENT_STATE.md) and
   [VERSION_MATRIX.md](VERSION_MATRIX.md) status column in the same push.

## Emergency releases

- Security fixes: same gates, prioritized; a security advisory note in
  the release notes is mandatory ([SECURITY.md](SECURITY.md)).
- If a gate cannot pass, the release does not ship — a delayed release is
  a schedule problem; a broken release is a trust problem.

## Commit discipline at release time

- The release commit contains only release mechanics (version bumps,
  notes, matrix updates).
- Feature/fix work lands in its own commits before the release commit is
  cut ([PROJECT_DISCIPLINE.md](PROJECT_DISCIPLINE.md)).
