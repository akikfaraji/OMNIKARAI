# Package Ecosystem (DESIGNED — format v1 lands V01.03)

> What packages *are* in Omnikarai, the two distribution forms, and the
> trust consequences of each. Today's practical mechanics (site-packages
> layout, `use <pkg>`, the live registry endpoints) are in
> [PACKAGES.md](PACKAGES.md). The client is documented in
> [OMNIP.md](OMNIP.md), the registry service in [OPI.md](OPI.md), and the
> security machinery in [PACKAGE_SECURITY.md](PACKAGE_SECURITY.md).

## Principle

**Packages are primarily written in Omnikarai.** The ecosystem exists to
share Omnikarai source and (later) native Omnikarai artifacts — not to wrap
foreign runtimes ([VISION.md](VISION.md)).

## Two distribution forms

### Source package — [TODAY in local form / format v1 lands V01.03]

Contains readable Omnikarai `.ok` source plus its `omnikarai.toml`.

| Benefit | Why it matters |
|---------|----------------|
| Open source | the default culture of the ecosystem |
| Auditing | reviewers read exactly what runs |
| Education | packages double as learning material |
| Modification | fork-and-fix is always possible |
| Research | reproducible experiments ship as source |

Today this form already works locally: `use <pkg>` loads `.ok` files from
the site-packages directory with `<pkg>__fn` prefixing. Format v1 (V01.03)
adds the manifest schema, dependency constraints, lockfiles and integrity
checksums.

### Compiled package — [PLANNED: format V01.03, trust V01.10, enabled by native emitters V01.02]

Contains native Omnikarai artifacts while keeping implementation source
private.

| Benefit | Why it matters |
|---------|----------------|
| Proprietary algorithms | authors can commercialize without disclosing source |
| Commercial libraries | a viable business model for ecosystem contributors |
| IP protection | implementation details stay private |
| Optimized distributions | artifacts can be pre-specialized per arch/kernel tier |

**Honest precondition:** meaningful compiled packages require the native
ELF64/PE32+ emitters (V01.02). Until then, "compiled" would just mean the
OMNISRC1 engine-embedded payload, which *contains the source* and protects
nothing. The compiled form is therefore EXPERIMENTAL until V01.02 exists
and is not trustworthy until V01.10 signatures exist.

## The three publishing postures

| Posture | What is visible | Typical use |
|---------|-----------------|-------------|
| **Open source** | everything, under a license | community libraries |
| **Source-available** | readable source, but a license restricts reuse/redistribution | free-to-inspect, not free-to-fork |
| **Proprietary compiled** | only the native artifact + API surface | commercial/proprietary libraries |

These are licensing postures, not technical formats: source-available is a
source package with a restrictive license; proprietary is a compiled
package. Licensing choices belong to authors; the ecosystem's job is to
make all three mechanically possible and honestly labeled. Flagged:
final legal structures need professional legal review
([GOVERNANCE.md](GOVERNANCE.md)).

## Trust boundary — read this before installing anything compiled

**Compiled code is never automatically trustworthy.** A compiled package
is opaque bytes; "it was published on a registry" is not a security
property. The ecosystem therefore pairs compiled distribution with a
verification system — signatures, provenance, dependency/ABI/arch checks,
revocation — designed in [PACKAGE_SECURITY.md](PACKAGE_SECURITY.md) and
delivered in V01.10. Until that ships, the honest rule is:

- install source packages you can read;
- treat compiled artifacts as untrusted input.

## What belongs where (division of labor)

| Concern | Owner |
|---------|-------|
| install/remove/update/search/rollback, lockfiles, caches | **Omnip** (client) — [OMNIP.md](OMNIP.md) |
| registry storage, accounts, metadata, stats, signatures-at-rest | **OPI** (service) — [OPI.md](OPI.md) |
| package format, ABI/arch tags, symbol prefixing | **this document + [ABI.md](ABI.md)** |
| trust, signatures, verification, revocation | [PACKAGE_SECURITY.md](PACKAGE_SECURITY.md) |
