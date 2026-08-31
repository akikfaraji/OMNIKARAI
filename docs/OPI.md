# OPI — Registry & Service Infrastructure

> **OPI is the registry / service infrastructure. Omnip is the client**
> ([OMNIP.md](OMNIP.md)). The only coupling between them is the versioned
> HTTP/JSON contract — OPI must never depend on Omnikarai compiler
> internals. Current live API surface and request mechanics:
> [PACKAGES.md](PACKAGES.md).

## Status today (v7.1.0 tree)

- Serverless Node.js application: **Vercel functions + Neon Postgres**;
  source in `opi/`.
- Live public instance: `https://opi-nine.vercel.app`.
- Auth: register (bcrypt) / login (JWT) / `/me`; API tokens per user.
- Packages: list and publish; stats endpoint.
- Security posture: **fails secure** — the historical `opi-dev-secret`
  fallback was removed; a deployment without `JWT_SECRET` (≥ 16 chars)
  refuses to authenticate instead of trusting a public default
  ([SECURITY.md](SECURITY.md)).
- Known gaps (tracked, not hidden): no live E2E test lane
  ([TECHNICAL_DEBT.md](TECHNICAL_DEBT.md) TD-06); no signatures yet; no
  mirrors; single region.

## Target architecture (V01.05 — API v1)

### Service responsibilities

| Area | Contents |
|------|----------|
| **Package registry** | versions, artifacts (source now, compiled schema later), integrity checksums |
| **Accounts** | publisher registration, auth (JWT + API tokens), profile |
| **Package metadata** | name, versions per [VERSIONING.md](VERSIONING.md), description, license, repository links, deprecation flags |
| **Versions & artifacts** | per-version artifact sets: source artifact; compiled artifacts keyed by `abi_version` / `arch` / `platform` / `kernel_level` ([ABI.md](ABI.md)) |
| **Signatures** | signature + provenance fields stored and served (verification happens client-side in omnip; V01.10) |
| **Dependency metadata** | stored constraint graphs; resolved *by the client*, not the server |
| **Security information** | advisory flags, known-vulnerability notices, revocation list distribution |
| **Download statistics** | per-version counters (privacy-preserving; no per-IP tracking) |
| **Ownership** | publisher(s) per package, ownership transfer, name reservations |
| **Publishing workflow** | authenticated publish, metadata validation, immutability of published versions |
| **API** | versioned (`/v1/...`), documented schemas, contract tests |
| **Mirrors** | read-only mirror specification for resilience |
| **Private / enterprise registries** | same API, self-hostable deployment profile |

### Architectural rules

1. **Contract, not internals.** The API is documented JSON schemas; OPI
   may be re-implemented (self-hosted, enterprise) against the same
   contract. No compiler data structures cross the wire.
2. **Immutability.** A published version is immutable; corrections ship as
   new versions (yanking is a metadata flag, never a deletion —
   reproducibility beats tidiness).
3. **Client-side trust decisions.** The registry stores signatures and
   provenance; it does not "bless" packages. Verification logic lives in
   omnip ([PACKAGE_SECURITY.md](PACKAGE_SECURITY.md)).
4. **Fails secure.** Missing/weak configuration disables the affected
   feature rather than degrading to an insecure default (established with
   the JWT secret removal).
5. **Stats without surveillance.** Aggregate counts only.

## Deployment evolution

| Stage | Shape | Roadmap |
|-------|-------|---------|
| Today | single Vercel project + Neon | — |
| V01.05 | API v1 + contract tests + E2E lane + backup/restore runbook | [ROADMAP.md](ROADMAP.md) |
| Later | mirror support; self-hosted profile for private/enterprise use | [PACKAGE_ECOSYSTEM.md](PACKAGE_ECOSYSTEM.md) |

Operational honesty: single-region serverless is acceptable for the V01
workload; it is listed as a known limitation, not described as
production-hardened.
