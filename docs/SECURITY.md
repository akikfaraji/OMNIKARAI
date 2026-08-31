# Security — Project-Level

> This document covers the security posture of the Omnikarai project
> itself (repository, compiler, registry service). Package-distribution
> trust is a separate design: [PACKAGE_SECURITY.md](PACKAGE_SECURITY.md).

## Current posture (verified)

| Area | State | Evidence |
|------|-------|----------|
| Registry auth | **fails secure**: deployments without `JWT_SECRET` (≥16 chars) refuse to authenticate; the historical `opi-dev-secret` fallback was removed | `opi/api/_auth.js`; audit finding #10 closed |
| Passwords | bcrypt hashing for registry accounts | `opi/api/auth/` |
| JIT memory | W^X discipline: allocate RW → flip RX → execute → unmap | `include/omni_platform.h`; [COMPILER.md](COMPILER.md) |
| Secrets in repo | none; CI preflight secret-scan planned (V01.00) | repo hygiene checks in this milestone; [ROADMAP.md](ROADMAP.md) V01.00 |
| Memory safety of the compiler itself | ASan+UBSan CI lane; one real OOB read found and fixed by it | `.github/workflows/linux.yml`; commit `3c88ece` |
| Integer semantics | signed division truncates toward zero (C semantics), verified against negative operands — no silent-wrongness | tests t21 (`337dd9e`) |
| Token handling (client) | omnip stores tokens under the user profile; permission hardening (0600) planned | [OMNIP.md](OMNIP.md) |
| Standalone payload | `OMNISRC1` footer is a convenience format with **no integrity check yet** — checksum/signature lands with format v1 (V01.03) and trust work (V01.10) | [ABI.md](ABI.md), [PACKAGE_SECURITY.md](PACKAGE_SECURITY.md) |

## Security principles (project-wide)

1. **Fail secure.** Missing configuration disables the feature; it never
   falls back to an insecure default. (Applied once already — the JWT
   secret — and it is the standing rule for every future auth/verification
   decision.)
2. **No trust in package authors.** The registry stores; clients verify.
   Compiled packages are untrusted input until the V01.10 machinery says
   otherwise ([PACKAGE_ECOSYSTEM.md](PACKAGE_ECOSYSTEM.md)).
3. **Honesty is a security control.** Docs that overstate capabilities
   cause unsafe deployments. Status labels are mandatory
   ([PROJECT_DISCIPLINE.md](PROJECT_DISCIPLINE.md)).
4. **W^X everywhere** executable memory is involved, on every platform.
5. **Sanitizers in CI** for the compiler and runtime code; findings are
   release blockers, not backlog.
6. **Smallest change wins.** Fewer moving parts, fewer CVEs
   (the no-LLVM, no-dependency stance is also a supply-chain decision).

## Vulnerability reporting

- **Planned process** (formalized with V01.05's publishing workflow):
  security reports go to the maintainer via the contact route documented
  in [GOVERNANCE.md](GOVERNANCE.md); acknowledgment within a best-effort
  window; coordinated disclosure; a `SECURITY` advisory note per fix in
  release notes.
- Until a dedicated reporting channel exists: open a restricted
  (non-public) issue contact request on the GitHub repository — do not
  post exploit details publicly.
- Registry incidents (compromised accounts/keys) follow the revocation
  design in [PACKAGE_SECURITY.md](PACKAGE_SECURITY.md) once it ships.

## Scope of current guarantees — read before relying on anything

- The compiler is a small C99 program with sanitizer coverage and a real
  test suite — that is *evidence of care*, not a security certification.
- The opi service runs on third-party serverless infrastructure (Vercel /
  Neon) whose operational security is inherited, not audited here.
- Standalone binaries embed source with no integrity check yet (above).
- No sandboxing exists anywhere in the stack today; sandboxing ideas are
  parked in [FUTURE.md](FUTURE.md).
