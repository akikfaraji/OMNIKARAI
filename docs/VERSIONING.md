# Omnikarai Versioning

This document defines the **official** versioning convention for Omnikarai.
It replaces — and maps from — the legacy `6.x` / `7.x` numbering. There is
no competing semantic-versioning scheme in this project.

## The convention

```
VXX.YY.ZZZ-beta-NN
```

| Field   | Meaning                        | Rules |
|---------|--------------------------------|-------|
| `VXX`   | Major platform/language **generation** | `V01` is the current generation ("V01" = the foundation-and-ecosystem generation). A new generation implies breaking changes at the language/ABI level and a fresh roadmap. |
| `YY`    | **Feature generation**         | Bumped when a roadmap feature release ships (e.g. the memory model, the package system). Feature releases are planned in [ROADMAP.md](ROADMAP.md). |
| `ZZZ`   | **Bug-fix / patch generation** | Bumped for fixes and small improvements that add no new roadmap features and break nothing. Three digits, zero-padded. |
| `beta-NN` | **Prerelease iteration**     | Two-digit zero-padded counter. Bumped on every prerelease cut of the same `VXX.YY.ZZZ`. `-beta` is dropped only when [RELEASE_PROCESS.md](RELEASE_PROCESS.md) gates (Definition of Done) pass. |

## Examples

| Version                | Reading                                                        |
|------------------------|----------------------------------------------------------------|
| `V01.00.000-beta-01`   | First prerelease of the V01 foundation release                 |
| `V01.00.001-beta-01`   | Bug-fix on top of V01.00.000, still prerelease                 |
| `V01.01.000-beta-01`   | First prerelease of the memory-model feature release           |
| `V01.01.001-beta-02`   | Second prerelease iteration of a patch on the memory model     |
| `V01.01.000`           | Memory-model release promoted to **stable** (gates passed)     |

## Rules

1. **Precedence.** `VXX` > `YY` > `ZZZ` > `NN`. A higher field resets all
   lower fields: shipping `V01.02.000` resets patches to `000`.
2. **Prerelease iteration.** `beta-NN` only ever increments within the same
   `VXX.YY.ZZZ`. Changing `YY` or `ZZZ` restarts the counter at `01`.
3. **Stable promotion.** A version drops `-beta-NN` only when every gate in
   [RELEASE_PROCESS.md](RELEASE_PROCESS.md) passes. Dropping the suffix is
   the promotion event; it is never done for schedule reasons.
4. **No silent renumbering.** Once a version string is published (tag, docs,
   binary banner), that exact version is permanent and describes a fixed
   tree. Regressions get a new `ZZZ`, never a rewritten tag.
5. **Single source of truth.** The version must be bumped **atomically** in
   all the places listed below, in the same commit.

## Where the version lives (currently)

| Location | What it is today (v7.1.0) |
|----------|---------------------------|
| `src/codegen.c` — `omni_sys_omni_ver()` | returns `"7.1.0"` to programs via the `sys` module |
| `src/codegen.c` — platform banner | `"Omnikarai v7.1.0 (x86-64 Linux)"` / `"...(x86-64 Windows)"` |
| `src/main.c` — CLI banner | `omnicc v7.1.0` |
| `omnikarai.toml` | project metadata (`version = "7.1.0"`) |
| `tests/run_tests.py` | asserts the `sys.version` output of compiled tests |

Because `tests/run_tests.py` asserts version output, bumping the version
without updating the runner fails CI — this coupling is deliberate and
stays until a version self-check replaces it.

## Mapping from legacy versions

| Legacy | Meaning |
|--------|---------|
| `6.x`, `7.x` (e.g. `v7.1.0-rc`) | **Pre-V01** history. Tags are preserved in git and are never rewritten or deleted. |
| `v7.1.0-rc` (tag `da13c15`) | Last legacy release: the stabilized Linux/Windows x86-64 compiler. Equivalent in content to what would have been `V01.00.000-beta-00`. |
| `V01.00.000-beta-01` | **Next planned release.** First release under the new convention; switches in-code version strings, tags and metadata to the new scheme. |

## Git tags

- Prerelease tag: `vV01.00.000-beta-01` (lowercase `v` prefix, matching the
  existing `v7.1.0-rc` convention).
- Stable tag: `vV01.01.000`.
- Tags are annotated and point at the release commit.

## Cross-links

- Feature releases and their content: [ROADMAP.md](ROADMAP.md)
- Release gates: [RELEASE_PROCESS.md](RELEASE_PROCESS.md)
- Version/capability tracking: [VERSION_MATRIX.md](VERSION_MATRIX.md)
