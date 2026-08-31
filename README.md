# Omnikarai

**A small compiled language that produces native x86-64 machine code directly.**
No LLVM. No bytecode VM. One C99 compiler. Runs on Linux and Windows.

```
print("Hello, World!")
```

```
$ omnicc run hello.ok
Hello, World!
```

**Current version:** legacy `v7.1.0-rc` (last release under the old 6.x/7.x
numbering) · **Next version:** `V01.00.000-beta-01`, the first release of
the **Omnikarai V01 generation** — the foundation-and-ecosystem generation
([ROADMAP.md](docs/ROADMAP.md) · [VERSIONING.md](docs/VERSIONING.md)).

---

## What it is

Omnikarai is an imperative language with Python-like syntax that compiles to
native x86-64 machine code. The compiler (`omnicc`) handles lexing, parsing,
code generation, and execution in one ~7,500-line C99 codebase, with no
external dependencies beyond libc/libm:

- `src/lexer.c` — hand-written scanner
- `src/parser.c` — recursive-descent parser with an AST
- `src/codegen.c` — x86-64 code generator (Win64 + SysV ABIs, AVX2 kernels)
- `src/main.c` — CLI: `run`, `build`, `dump`, `check`

Generated code executes at native speed: function calls follow the platform
calling convention, and the AI/math kernels emit SIMD instructions
(`VFMADD231PS`, `VPMADDUBSW`, `VMAXPS`) directly, with scalar fallbacks for
CPUs without AVX2.

The compiler is a real compiler, not a wrapper: it emits x86-64 instruction
encodings byte by byte — REX prefixes, ModRM/SIB, displacement patching,
forward-jump resolution, relocation of helper addresses.

## The V01 vision (summary)

Omnikarai is being built as a **native systems/AI language and ecosystem**:

- **Systems capability** — C/C++-class *performance target* (not yet
  achieved — no optimizer today; the claim is UNPROVEN by design, see
  [docs/BENCHMARKS.md](docs/BENCHMARKS.md)), direct memory control
  ([docs/MEMORY_MODEL.md](docs/MEMORY_MODEL.md)), SIMD, native compilation.
- **High-level usability** — Python-shaped syntax, high-level libraries and
  easy package importing via the Omnip/OPI ecosystem
  ([docs/PACKAGE_ECOSYSTEM.md](docs/PACKAGE_ECOSYSTEM.md)).
- **AI-native** — the language and tooling are designed so AI agents can
  understand, generate, test, debug and optimize Omnikarai programs
  ([docs/AI_NATIVE_DESIGN.md](docs/AI_NATIVE_DESIGN.md)), on top of a
  native numerical/AI stack (Namurai + the AI ecosystem,
  [docs/NAMURAI.md](docs/NAMURAI.md) / [docs/AI_ECOSYSTEM.md](docs/AI_ECOSYSTEM.md)).

It is **not** a Python host: Python, C, C++, Rust, NumPy and PyTorch are
comparison/benchmark targets. Full vision: [docs/VISION.md](docs/VISION.md).

## Roadmap at a glance

| Milestone | Theme |
|-----------|-------|
| V01.00 | Foundation: versioning adoption + structured diagnostics |
| V01.01 | Dual memory model |
| V01.02 | Native executable emitters (ELF64 / PE32+) |
| V01.03–05 | Package system → Omnip (cross-platform) → OPI API v1 |
| V01.06 | AArch64 (Linux / Termux) |
| V01.07–09 | Namurai → AI ecosystem → MCP & AI tooling |
| V01.10–11 | Package security & trust → performance & optimization |

Sequencing is dependency-driven and explained in
[docs/ROADMAP.md](docs/ROADMAP.md); progress tracking:
[docs/VERSION_MATRIX.md](docs/VERSION_MATRIX.md).

## Platforms

| Platform | Status | Notes |
|----------|--------|-------|
| Linux x86-64 | **supported** (primary CI) | gcc, SysV ABI |
| Windows x64 | supported (CI) | MinGW-w64 gcc, Win64 ABI |

Both platforms run the same 30-test suite (21 unit + 9 stress) in CI under
their respective calling conventions. AArch64 + Termux: planned
([docs/AARCH64.md](docs/AARCH64.md)).

## Quick start

```
make                                  # build bin/omnicc
python3 tests/run_tests.py --stress   # verify the toolchain (30 tests)

./bin/omnicc run hello.ok             # JIT: compile and execute in-process
./bin/omnicc build hello.ok           # standalone executable (embeds runtime)
./bin/omnicc check hello.ok           # parse + check only
./bin/omnicc dump hello.ok            # dump generated machine code
```

`omnicc build` produces a standalone executable — a copy of the omnicc
engine with your program embedded, which recompiles it in-process at
startup. The result runs on any same-platform machine with nothing
installed. (True freestanding binaries land in V01.02.)

## The language

Python-shaped, C-shaped at runtime:

```python
use math

fn is_prime(n):
    if n < 2:
        return 0
    set d = 2
    while d * d <= n:
        if n % d == 0:
            return 0
        set d = d + 1
    return 1

print(is_prime(97))          # 1
print(math.sqrt(144.0))      # 12
print("fib(10) = " + fib(10))
```

Full syntax reference: [docs/LANGUAGE.md](docs/LANGUAGE.md) ·
module reference: [docs/MODULES.md](docs/MODULES.md).

- Indentation-based blocks, `fn`/`set`/`while`/`for`/`match`, classes
- int64 / 64-bit float / bool / string / list / nil
- Signed division truncates toward zero (C semantics) — verified by tests
  against negative operands
- User functions are statically type-inferred for printing and string ops

## The AI kernels

The `ai` module emits SIMD machine code directly:

```
use ai

set a = ai.alloc(1024)     # 64-byte aligned, zeroed
set b = ai.alloc(1024)
# ... ai.set(a, i, bits) / ai.set_u8(a, i, byte) ...
ai.dot(a, b, 1024)         # FP32 dot product — VFMADD231PS, 8 floats/cycle
ai.dot_i8(a, b, 1024)      # INT8 dot product — VPMADDUBSW, 32 ops/cycle
ai.matmul(A, x, y, 256, 256)   # cache-tiled matrix × vector
ai.relu(x, 1024)           # VMAXPS
ai.softmax(x, 1024)        # fused AVX2 exp
```

INT8 buffers must be populated with `ai.set_u8` / `ai.set_i8` (added in
v7.1.0); `ai.set` writes FP32 bit patterns and is not byte-compatible.

## Package ecosystem

- **opi** — the registry service (Node.js / Vercel / Neon), live at
  `https://opi-nine.vercel.app`, fail-secure JWT auth.
- **omnip** — the package manager client (pip-inspired RECORD model).
  **Windows-only at this release**; the POSIX port is roadmap V01.04.

Current mechanics: [docs/PACKAGES.md](docs/PACKAGES.md) · design:
[docs/PACKAGE_ECOSYSTEM.md](docs/PACKAGE_ECOSYSTEM.md),
[docs/OMNIP.md](docs/OMNIP.md), [docs/OPI.md](docs/OPI.md).

## Benchmarks

Multi-language benchmark programs live in `benchmarks/` (Omnikarai vs C,
C++, Go, Java, JavaScript, Python) for fib, loops, primes, matmul and dot
products. Run them yourself:

```
python3 benchmarks/run_benchmarks.py
```

Results are machine- and compiler-flag-dependent; the runner prints
everything it measures so you can reproduce it on your hardware. We do not
publish claimed speedups without the runner output next to them. Philosophy
and the target metric lab: [docs/BENCHMARKS.md](docs/BENCHMARKS.md).

## Tests

```
python3 tests/run_tests.py            # 21 unit tests
python3 tests/run_tests.py --stress   # + 9 stress suites
```

CI (`.github/workflows/`) runs the full suite on Linux gcc, Linux with
ASan+UBSan, Linux scalar (no AVX2), and Windows MinGW.

## Documentation

The complete, cross-linked documentation set lives in
[`docs/`](docs/README.md) — start with
[docs/README.md](docs/README.md) (index with reading paths):
vision, evidence-based current state, full roadmap, versioning, memory
model, ABI, platforms, package ecosystem, security, benchmarks,
governance and contribution guides.

## Repository layout

```
src/           compiler sources (lexer, parser, codegen, CLI)
include/       headers: abi.h (calling conventions), omni_platform.h, ...
tests/         21 unit tests + 9 stress tests + portable runner
benchmarks/    cross-language benchmark programs + reproducible runner
docs/          full V01 documentation set (index: docs/README.md)
opi/           package registry service (Node.js / Vercel / Neon)
omnip/         package manager client (Windows-only at this release)
```

## Known limitations (read before evaluating)

Stated plainly, because this project has previously overstated itself:

1. **No optimizer.** There is no scheduling, no SSA, no register allocation
   beyond loop-scoped pinning of hot variables. Generated code is
   straightforward, correct, and not competitive with -O2 C on
   register-heavy code (performance parity is an explicitly UNPROVEN
   target, [docs/BENCHMARKS.md](docs/BENCHMARKS.md)).
2. **`omnip` is Windows-only.** The compiler is cross-platform; the package
   manager client is not yet (WinHTTP dependency).
   [docs/PACKAGES.md](docs/PACKAGES.md).
3. **Standalone builds embed the engine.** `omnicc build` produces a
   ~1 MB executable because it contains the runtime; it is not a
   minimal-size native binary (native emitters: V01.02).
4. **No debugger, no LSP, no incremental compilation.**
5. **Single-module programs.** `use <pkg>` loads installed packages; there
   is no user-level module resolution within one program beyond functions
   and classes.

## Contributing

Read [docs/CONTRIBUTING.md](docs/CONTRIBUTING.md) and
[docs/PROJECT_DISCIPLINE.md](docs/PROJECT_DISCIPLINE.md) — the short
version: correctness before optimization, no feature without tests,
documentation matches implementation, and new ideas go through the
proposal pipeline rather than straight into the codebase.

## License

MIT — see [LICENSE](LICENSE). Third-party notices:
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
