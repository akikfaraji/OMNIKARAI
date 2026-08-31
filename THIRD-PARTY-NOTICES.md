# Third-Party Notices

Omnikarai itself (the `omnicc` compiler, the language runtime, the tests and
the benchmarks) contains **no vendored third-party source code** — everything
under `src/`, `include/`, `tests/` and `benchmarks/` is original to this
project and is licensed under the MIT license in `LICENSE`.

## Package registry (`opi/`)

The registry is a serverless Node.js application. It is *deployed* with the
following npm dependencies (not vendored in this repository — they are
installed from the npm registry at deploy time):

| Package     | License | Role                              |
|-------------|---------|-----------------------------------|
| `jose`      | MIT     | JWT signing / verification        |
| `bcryptjs`  | MIT     | Password hashing                  |
| `@neondatabase/serverless` | MIT | Postgres driver (Neon) |

Deployment hosting (Vercel) and the managed Postgres instance (Neon) are
external services, not code in this repository.

## Benchmarks

The benchmark programs in `benchmarks/` are original implementations written
for this project in each respective language (C, C++, Go, Java, JavaScript,
Python, and Omnikarai). They compile/run with the standard toolchain of each
language and pull in no third-party libraries.
