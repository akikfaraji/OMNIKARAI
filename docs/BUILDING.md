# Building Omnikarai

## Requirements

- Any C99 compiler (gcc, clang, MinGW-w64 gcc) — no external libraries.
  The compiler itself does **not** require AVX2; only the default build of
  the *language runtime* kernels does (see Portable builds below).
- Python 3 (test suite only).

## POSIX (Linux / macOS)

```
make              # build bin/omnicc (default: AVX2+FMA runtime kernels)
make test         # build + run the unit test suite
make portable     # build without AVX2/FMA (scalar fallback kernels)
make asan         # AddressSanitizer + UBSan build (debug)
make windows      # cross-compile omnicc.exe with x86_64-w64-mingw32-gcc
make clean
```

Run the full suite (unit + stress):

```
python3 tests/run_tests.py --stress
```

Build and run a program:

```
./bin/omnicc run hello.ok      # JIT
./bin/omnicc build hello.ok    # standalone executable
./hello.exe                    # runs anywhere on the same platform
```

## Windows

Build with MinGW-w64 (MSYS2):

```
gcc -Iinclude -O2 -std=c99 -Wall -Wextra -o bin/omnicc.exe \
    src/main.c src/lexer.c src/parser.c src/codegen.c -lm
python tests/run_tests.py --stress
```

GitHub Actions runs the same suite on `windows-latest` with the Win64
calling convention (see `.github/workflows/windows.yml`).

## CI

- **linux.yml**: gcc build + full suite; ASan+UBSan build + suite;
  scalar (`make portable`) build + suite.
- **windows.yml**: MinGW-w64 build (Win64 ABI) + full suite.

The test suite is the source of truth for platform support: a platform is
supported if and only if all 30 tests pass on it.

## Sanitizer notes

`make asan` builds the compiler with `-fsanitize=address,undefined`. The
generated JIT code itself is not instrumented, but the sanitizer catches
host-side memory errors, stack overflows in codegen, and UB in the C
sources. `ASAN_OPTIONS=detect_leaks=0` is expected — the compiler
intentionally does not free the program AST (functions may be inlined by
pointer into the JIT buffer for the lifetime of the run).
