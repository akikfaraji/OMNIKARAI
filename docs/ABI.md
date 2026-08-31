# ABI — Application Binary Interfaces

> What binary contracts Omnikarai has **today**, and what the versioned
> package ABI will add. Related: [COMPILER.md](COMPILER.md) (how code is
> generated), [PACKAGE_ECOSYSTEM.md](PACKAGE_ECOSYSTEM.md) (distribution),
> [PLATFORM_SUPPORT.md](PLATFORM_SUPPORT.md) (architectures).

## Current ABIs (TODAY)

Omnikarai generated code implements **two** calling conventions, selected
at omnicc *build time* (the same compiler source produces a compiler that
emits the native convention of its host platform):

| ABI | Argument registers | Shadow space | Pinned variable registers | Used on |
|-----|--------------------|--------------|---------------------------|---------|
| **Win64** | RCX, RDX, R8, R9 | 32 bytes required | up to 5 | Windows x64 |
| **SysV AMD64** | RDI, RSI, RDX, RCX, R8, R9 | none | up to 3 | Linux x86-64 |

Source of truth: `include/abi.h`. Callee-saved registers (RSI/RDI on Win64)
and the pinned-register save/restore discipline around loops are documented
in [COMPILER.md](COMPILER.md) — a real bug class was found and fixed there
during the Linux port (pinned-register save executed inside the loop).

### Runtime-helper calling

Runtime helpers (printing, strings, lists, AI kernels) are C functions
compiled into omnicc; generated code calls them with the host convention
via `mov rax, <abs64>; call rax` with address patching. This is why the
current standalone format embeds the engine: helper addresses must resolve
inside the omnicc process. Removing that constraint is the V01.02 native
emitter milestone.

### Standalone payload format (TODAY, legacy — replaced in V01.02)

```
[ omnicc image ][ program source ][ magic "OMNISRC1" ][ uint64 LE length ]
```

Defined in `src/main.c`. This is a *distribution* format, not a binary
interface to other programs; it changes only with a version note.

### Package symbol convention (TODAY)

Installed packages expose functions with `<pkg>__fn` name prefixing, loaded
from the site-packages directory by `use <pkg>`. This convention is
preserved going forward and formalized in package format v1 (V01.03).

## Package ABI (PLANNED — V01.02/V01.03)

Compiled packages require a versioned contract. Planned fields (defined
fully with package format v1):

- `abi_version` — bumped on any change to calling convention, buffer
  layouts, or runtime-helper table
- `arch` — `x86-64`, `aarch64`, … ([PLATFORM_SUPPORT.md](PLATFORM_SUPPORT.md))
- `platform` — OS classification
- `kernel_level` — SIMD requirement tier (`scalar`, `avx2`, `neon`)
- verification hooks — checksum/signature fields reserved now, consumed
  by the trust system in V01.10 ([PACKAGE_SECURITY.md](PACKAGE_SECURITY.md))

## Stability promise (TODAY: NONE — stated honestly)

- **There is no stable ABI today.** Anything may change between any two
  versions before V01.02.
- The first ABI stability promise is made with the versioned package ABI
  (V01.02/V01.03) and governed by [COMPATIBILITY.md](COMPATIBILITY.md).
- Git history and tags are permanent; the ABI *history* is reconstructible
  from them.
