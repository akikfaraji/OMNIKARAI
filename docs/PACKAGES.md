# Packages: omnip and opi

The Omnikarai package ecosystem has two parts:

- **opi** — a package registry service (`opi/` in this repo). It is a
  serverless Node.js application: Vercel functions + a Neon Postgres
  database. Users authenticate (JWT), packages are published over HTTP and
  stored in Postgres.
- **omnip** — the command-line package manager client (`omnip/src/omnip.c`),
  written in C.

## opi registry

Public instance: https://opi-nine.vercel.app

API surface (all under `/api`):

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/api/auth/register` | POST | create account (bcrypt password) |
| `/api/auth/login` | POST | login, returns JWT |
| `/api/auth/me` | GET | current user |
| `/api/user/tokens` | GET/POST | API tokens |
| `/api/user/profile` | GET | profile |
| `/api/packages` | GET/POST | list / publish packages |
| `/api/stats` | GET | registry stats |

Security posture (v7.1.0): the JWT layer **fails secure** — the previous
hardcoded `opi-dev-secret` fallback was removed (audit finding #10). A
deployment without `JWT_SECRET` (≥ 16 chars) refuses to authenticate
instead of silently trusting a public default.

## omnip client

Status at v7.1.0: **Windows-only**. omnip uses WinHTTP + kernel32 for
networking and file operations; there is no POSIX build of it in this
release.

This is the main remaining platform gap of the ecosystem (the compiler
itself is fully cross-platform). A POSIX port of omnip is tracked as
future work; until then, `omnip` is excluded from the default `make`
targets and the compiler's package loader works against whatever is
already installed in the site-packages directory:

```
Windows: %LOCALAPPDATA%\Programs\omnikarai\site-packages\<name>\
POSIX:   $XDG_DATA_HOME/omnikarai/site-packages/<name>/
         (default: ~/.local/share/omnikarai/site-packages/)
```

`use <package>` in a program loads the package's `.ok` files from that
directory and prefixes its functions (`<pkg>__fn`) to avoid collisions.
