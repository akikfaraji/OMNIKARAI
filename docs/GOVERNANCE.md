# Governance

> Who decides what, and how decisions are recorded. Short today, because
> the project currently has one maintainer — stated plainly rather than
> dressed up as a committee.

## Current model (TODAY)

- **Maintainer**: Akik (Fraziym Tech & AI) — per project metadata
  (`omnikarai.toml`).
- The maintainer makes final decisions on roadmap, architecture and
  releases, bound by the same written rules as everyone else:
  [PROJECT_DISCIPLINE.md](PROJECT_DISCIPLINE.md) (pipeline + hard rules),
  [RELEASE_PROCESS.md](RELEASE_PROCESS.md) (gates),
  [COMPATIBILITY.md](COMPATIBILITY.md) (promises).
- Contributors propose via the pipeline; technical review happens in the
  open (issues/PRs on the GitHub repository); decisions get recorded in
  the roadmap (version assignment) or the parking lot
  ([FUTURE.md](FUTURE.md)) — silence is not a decision.

## Decision types and where they're recorded

| Decision | Recorded in |
|----------|-------------|
| idea rejected/parked | [FUTURE.md](FUTURE.md) |
| idea accepted | [ROADMAP.md](ROADMAP.md) + [VERSION_MATRIX.md](VERSION_MATRIX.md) |
| architecture direction | [ARCHITECTURE.md](ARCHITECTURE.md) + relevant design doc |
| release cut | release notes + tag ([RELEASE_PROCESS.md](RELEASE_PROCESS.md)) |
| policy change (these docs) | the doc itself, in a `docs` commit with rationale in the message |

## Growing beyond one maintainer

When the project gains recurring contributors, the intended model is:

1. **Consensus-first, maintainer-override**: technical consensus in the
   open; if blocked, the maintainer decides and records the rationale.
2. **Ownership areas**: subsystems (compiler, omnip, opi, docs, benchmarks)
   get named owners with review authority in their area.
3. **No governance by stealth**: any change to this document follows the
   same public pipeline as code.

## Legal & commercial notes (flagged, not decided)

- **Flagged for professional legal review** (no final legal structure
  exists and none is pretended):
  - trademark/name protection for "Omnikarai", "Namurai", "omnicc",
    "Omnip", "OPI";
  - the licensing models for the open-source / source-available /
    proprietary-compiled triad ([PACKAGE_ECOSYSTEM.md](PACKAGE_ECOSYSTEM.md));
  - any future entity structure for commercial tiers.
- **Commercial ecosystem** (concept only, per the V01 specification):
  possible future tiers — Free, Personal, Pro, Team, Business, Enterprise,
  Ultra — differing in compute, AI capabilities, package access, private
  packages, enterprise registry, collaboration, support, optimization
  services, cloud features, deployment and security features.
- **Pricing rule**: pricing will only be determined after real usage and
  cost data exist. No billing is built now; nothing in the repository
  depends on any tier existing.

## Code of conduct

Short version, enforced in all project spaces: be precise, be honest, no
harassment. Behavioral violations are handled by the maintainer (and by
area owners, once they exist). The honesty rules of
[PROJECT_DISCIPLINE.md](PROJECT_DISCIPLINE.md) apply to communication as
much as to code.
