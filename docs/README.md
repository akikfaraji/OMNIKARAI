# Omnikarai — Documentation

Omnikarai is a small imperative programming language with an x86-64 native
compiler (`omnicc`) written in C99. It has no LLVM dependency and no runtime
install requirement: `omnicc` compiles `.ok` source files to native x86-64
machine code and executes them in-process (JIT) or builds standalone
executables.

| Document | Contents |
|----------|----------|
| [LANGUAGE.md](LANGUAGE.md) | Syntax reference: variables, functions, control flow, types, operators |
| [MODULES.md](MODULES.md) | Built-in modules: `time`, `datetime`, `math`, `os`, `io`, `sys`, `list`, `str`, `ai` |
| [ARCHITECTURE.md](ARCHITECTURE.md) | How the compiler works: lexer → parser → codegen → JIT / standalone build |
| [BUILDING.md](BUILDING.md) | Building from source, platforms, CI, sanitizer and portable builds |
| [PACKAGES.md](PACKAGES.md) | Packages: the `omnip` client and the `opi` registry service |

## Status summary (v7.1.0)

- **Platforms:** Linux x86-64 (primary CI) and Windows x64 (MinGW CI).
- **Tests:** 21 unit tests + 9 stress tests, all passing on Linux; the same
  suite runs on Windows CI under the Win64 calling convention.
- **AI kernels:** AVX2 FP32 and INT8 quantized primitives, emitted as native
  machine code with scalar fallbacks (`make portable`).
- **Known limitations** are listed in the README — the project does not
  claim features it does not have.
