# Project Discipline

> **New ideas do not automatically become active development.**
>
> This document is the anti-scope-explosion charter of the Omnikarai
> project. It exists because the most likely way for a small, focused
> compiler project to die is to become an uncontrolled feature list.

## The pipeline

Every idea — from a one-line suggestion to a full subsystem — travels the
same path. No stage may be skipped, and nothing before *ROADMAP* touches
the source tree:

```
IDEA
  ↓        raw thought, one paragraph, filed in FUTURE.md or an issue
PROPOSAL
  ↓        problem statement, alternatives, cost estimate, risks
TECHNICAL REVIEW
  ↓        does it fit the vision? is it technically sound? what breaks?
ROADMAP
  ↓        accepted → placed in ROADMAP.md with a priority (P0–P4)
VERSION ASSIGNMENT
  ↓        mapped to a feature release (V01.xx) or explicitly deferred
IMPLEMENTATION
  ↓        smallest change that satisfies the proposal; tests included
TESTING
  ↓        test suite + relevant gates of RELEASE_PROCESS.md
RELEASE
           version bump per VERSIONING.md, release notes, docs updated
```

### Stage rules

| Stage | Entry | Exit |
|-------|-------|------|
| IDEA | anyone, anywhere | written down in [FUTURE.md](FUTURE.md) or the issue tracker |
| PROPOSAL | a written problem statement | reviewer can say "this is worth building" or "no" |
| TECHNICAL REVIEW | proposal exists | decision recorded: accept / reject / needs-rework, with reasons |
| ROADMAP | accepted proposal | entry in [ROADMAP.md](ROADMAP.md) with priority and status |
| VERSION ASSIGNMENT | roadmap entry | milestone assigned, **or** parked in FUTURE.md |
| IMPLEMENTATION | assigned version, design agreed | code + tests + docs in one reviewable change |
| TESTING | implementation done | suite green, no new sanitizer findings, no weakened tests |
| RELEASE | gates pass | version stamped, tagged, notes written |

## Hard rules

1. **Ideas are cheap; versions are expensive.** Anything that does not
   belong to the *current* release goes to [FUTURE.md](FUTURE.md) —
   mandatory, no exceptions, including the maintainer's own ideas.
2. **No feature without tests.** A change that cannot show a test cannot
   merge. Tests are never weakened to make a pass.
3. **Documentation matches implementation.** Public docs describe what
   *is*, plans are labelled PLANNED/DESIGNED/EXPLORATORY. No aspirational
   README.
4. **No implementation during specification phases.** Phases whose purpose
   is research/architecture/documentation do not ship features. (This
   document set was produced under exactly such a phase: the only source
   change accompanying it is a stale-metadata fix in `omnikarai.toml`.)
5. **Do not rewrite working architecture for aesthetics.** Refactors need a
   defect or a measured cost, not taste.
6. **One reviewable change.** A commit or PR does one logical thing;
   unrelated fixes ride in their own commits.
7. **Preserve history.** No force-push, no history rewrites on shared
   branches.

## Priority and status vocabulary

Every roadmap entry carries:

- **PRIORITY**: `P0` blocking · `P1` critical · `P2` important · `P3` useful
  · `P4` exploratory
- **STATUS**: `DONE` · `IN PROGRESS` · `PLANNED` · `DESIGNED` ·
  `EXPERIMENTAL` · `BLOCKED` · `DEFERRED`

A feature without a priority and status is not on the roadmap — it is an
idea, and belongs in FUTURE.md.

## Proposal sketch (what a PROPOSAL must contain)

- Problem: what cannot be done, or is done badly, today (with evidence —
  file, test, benchmark or audit reference)
- Proposal: the smallest change that solves it
- Alternatives considered, including "do nothing"
- Cost: rough implementation/test/docs effort; what it touches
- Risks: compatibility, security, performance, scope
- Verification: how we will prove it works (tests, benchmarks)
