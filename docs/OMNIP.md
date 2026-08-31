# Omnip — the Omnikarai Package Manager (client)

> Division of labor: **Omnip is the client; OPI is the registry/service
> infrastructure** ([OPI.md](OPI.md)). They speak versioned HTTP/JSON and
> Omnip never depends on compiler internals. Current practical mechanics:
> [PACKAGES.md](PACKAGES.md).

## Status today (v7.1.0 tree)

- **omnip v6.0.0** — a single-file C client, `omnip/src/omnip.c` (853
  lines).
- **Windows-only**: `windows.h`, `winhttp.h` (HTTPS to the registry),
  `%LOCALAPPDATA%` paths. The POSIX port is roadmap **V01.04**
  ([ROADMAP.md](ROADMAP.md)) and the top ecosystem debt
  ([TECHNICAL_DEBT.md](TECHNICAL_DEBT.md) TD-02).
- Design borrowed from pip's wheel/RECORD model:
  - packages install as **directory trees** (not flat blobs)
  - a **RECORD** file tracks every installed file → clean uninstall
  - packages publish as a JSON file-map `{"rel/path": "content"}`
  - publish collects recursively; install restores recursively
- Install roots: `%LOCALAPPDATA%\Programs\omnikarai\site-packages\<name>\`;
  state under `...\omnip\` (`installed.json`, auth token file).
- Talks to the public registry `opi-nine.vercel.app` over HTTPS.

## Future responsibilities (target state)

The checklist below is the definition of "Omnip is done" for the V01
generation; each line lands in V01.03/V01.04 or is explicitly deferred:

- [x] package installation *(Windows today)*
- [x] package removal (RECORD-driven clean uninstall) *(Windows today)*
- [ ] **POSIX build** — same commands on Linux/Termux *(V01.04)*
- [ ] dependency resolution against format-v1 constraints *(V01.03/V01.04)*
- [ ] version resolution honoring [VERSIONING.md](VERSIONING.md) ordering
- [ ] lockfiles (hash-pinned, reproducible installs) *(V01.03)*
- [x] package publishing (source file-map) *(Windows today)*
- [ ] package metadata validation before publish
- [ ] source packages *(format v1, V01.03)*
- [ ] compiled packages *(EXPERIMENTAL until V01.10 trust)*
- [ ] architecture/platform artifact selection *(V01.02+ tags)*
- [ ] build configuration hooks (`omnikarai.toml` build section)
- [ ] local cache of downloaded artifacts
- [ ] reproducible-build options for compiled packages *(V01.10)*
- [ ] package signing (sign on publish, verify on install) *(V01.10)*
- [ ] security verification pipeline *(V01.10)*
- [ ] local package development loop (editable installs)
- [ ] package search
- [ ] package updates
- [ ] rollback (previous-version restore)

## Omnip vs OPI boundary (stable rule)

| Belongs in Omnip | Belongs in OPI |
|------------------|----------------|
| anything touching the local machine | anything touching shared state |
| install/remove/update/search UX | accounts, publishing API, storage |
| dependency + lockfile computation | dependency metadata *storage* |
| signature verification at install | signature fields at rest |
| caches, PATH/site-packages management | download statistics, ownership |
| local dev workflow | mirrors, private registries |

If a feature needs both, the HTTP/JSON contract is designed first
(V01.05) and both sides implement it — never a compiler-internal shortcut.

## Security notes

- Auth token stored under the user profile; V01.04 tightens file
  permissions (0600) and documents the threat model.
- Downloads verify checksums from the registry (format v1); TLS is
  already mandatory (WinHTTP HTTPS; the POSIX port keeps TLS mandatory).
- omnip is the **enforcement point** for package trust at install time:
  unsigned/tampered compiled packages are refused once V01.10 ships
  ([PACKAGE_SECURITY.md](PACKAGE_SECURITY.md)).
