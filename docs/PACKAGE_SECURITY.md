# Package Security & Trust (DESIGNED — lands V01.10)

> The requirement this design serves:
>
> **Proprietary packages should be able to protect their implementation
> while still passing the Omnikarai ecosystem's safety/trust
> requirements.**
>
> Status: DESIGNED as requirements; delivery is roadmap V01.10
> ([ROADMAP.md](ROADMAP.md)). Related: [PACKAGE_ECOSYSTEM.md](PACKAGE_ECOSYSTEM.md)
> (distribution forms), [OMNIP.md](OMNIP.md) (enforcement point),
> [OPI.md](OPI.md) (storage), [SECURITY.md](SECURITY.md) (project-level
> security).

## The honest limits first

- **Static analysis cannot mathematically prove arbitrary native code
  harmless.** Compiled packages are opaque machine code; no signature,
  scan or sandbox changes that fact. Anyone who tells you otherwise is
  selling something.
- What a trust system *can* do — and what this design promises — is:
  1. **Provenance**: you can verify *who* published an artifact and that
     *its bytes are exactly what they signed*.
  2. **Compatibility**: you can verify the artifact claims to run on your
     ABI/architecture/platform before loading it.
  3. **Revocation**: a compromised key or malicious package can be
     marked untrusted ecosystem-wide.
  4. **Visibility**: source packages remain fully auditable; compiled
     packages carry advisory metadata (capabilities, permissions).
- The residual risk — "the author signed malicious code" — is reduced by
  publisher reputation, incident response and revocation, and is never
  eliminated. That is the deal, stated plainly.

## Design requirements

### Identity & signing

- **Ed25519 signatures** over package artifacts; publisher keys registered
  with OPI accounts; key rotation documented; signing happens at publish
  (omnip), verification at install (omnip) — the registry stores, it does
  not bless ([OPI.md](OPI.md) rule 3).

### Integrity & dependencies

- Checksums (SHA-256) for every artifact at every hop; the lockfile pins
  exact artifact hashes so a rebuild is byte-verifiable.
- Dependency verification: the transitive closure is checked against the
  lockfile before install; no surprise additions.

### ABI / architecture / platform verification

- Artifacts declare `abi_version`, `arch`, `platform`, `kernel_level`
  ([ABI.md](ABI.md)); omnip refuses to install artifacts that do not match
  the host — a mismatch is an error, not a warning.

### Provenance & metadata

- Provenance record per artifact: publisher id, build declaration (source
  version, build tool version, flags), timestamp. Enables "who built this
  and how" questions after the fact.

### Permissions & capability declarations

- Compiled packages carry **advisory capability declarations** (e.g. raw
  memory access, filesystem, network). V01 treats them as labeling, not
  enforcement — enforced sandboxing is a research problem parked in
  [FUTURE.md](FUTURE.md). Labeling still changes behavior: omnip surfaces
  capabilities at install time so users consent explicitly.

### Malicious-package handling

- Static scanning of *source* packages (patterns, dangerous API mixes) as
  advisory signals in the publishing workflow.
- **Incident response**: a documented path from report → triage →
  revocation list entry → client enforcement; compromise of a publisher
  key leads to key revocation + re-signing guidance.
- Publishing workflow requires authentication; version immutability
  prevents silent replacement attacks (yank, don't overwrite —
  [OPI.md](OPI.md) rule 2).

### Reproducible builds

- Compiled packages SHOULD be reproducible: identical declared inputs
  produce identical bytes; the V01.02 emitter work includes deterministic
  layout groundwork ([ROADMAP.md](ROADMAP.md) V01.02 security note).
  Reproducibility lets independent parties verify a compiled artifact
  corresponds to claimed source — the strongest practical bridge between
  the proprietary and auditable worlds.

### Revocation

- Signed revocation lists distributed via OPI; omnip checks recency and
  honors revocations; clients can operate offline with a
  last-known-good list plus explicit user override (documented as
  riskier).

## What "trust requirements" concretely means at install time

| Check | Failure behavior |
|-------|------------------|
| signature valid for declared publisher | refuse |
| checksum matches lockfile/manifest | refuse |
| ABI/arch/platform match host | refuse |
| no revocation against package or key | refuse (with report) |
| capability declarations surfaced | require explicit consent flag for elevated capabilities |

## Delivery plan

Requirements above are bound to roadmap **V01.10**; the format fields they
need are reserved from **V01.03** (package format v1); enforcement point
is omnip (V01.04 portable client); storage is OPI (V01.05 contract).
Nothing in this file is active before V01.10 — until then the operative
rule is the one in [PACKAGE_ECOSYSTEM.md](PACKAGE_ECOSYSTEM.md): read
source, distrust compiled.
