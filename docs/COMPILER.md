# Compiler Architecture

`omnicc` is ~7,500 lines of C99 across four source files. It compiles the
Omnikarai language to native x86-64 machine code — no LLVM, no intermediate
bytecode VM, no external dependencies beyond libc/libm.

```
source.ok ──lexer.c──▶ tokens ──parser.c──▶ AST ──codegen.c──▶ x86-64 bytes
                                                            │
main.c ─────────────────────────────────────────────────────┴─▶ JIT run
                                                                or standalone exe
```

## lexer.c (~540 lines)

Hand-written scanner. Produces the usual token set plus the language's
structural tokens (`:`, indentation-based blocks are reconstructed by the
parser from newline/indent tokens). Keywords: `fn set if elif else while for
in match case default return break continue use const class self true false
nil and or not`.

## parser.c (~1,300 lines)

Recursive descent. Builds an AST for: function definitions, `set`, `if /
elif / else`, `while`, `for … in range/list`, `match/case/default`, `class`
definitions with methods, `use`, `const`, and full expression grammar with
correct precedence. The parser also desugars compound assignment (`x += 1`
→ `x = x + 1`), `obj[i] = v` (→ `list.set`), and string repetition.

## codegen.c (~5,000 lines)

One-pass code generator that emits x86-64 bytes into an in-memory buffer.

Key subsystems:

- **Calling conventions** (`include/abi.h`): generated code targets either
  Win64 (`RCX RDX R8 R9`, shadow space, RSI/RDI callee-saved, up to 5 pinned
  variable registers) or SysV AMD64 (`RDI RSI RDX RCX R8 R9`, no shadow
  space, up to 3 pinned registers). The selection is made at omnicc compile
  time so the same compiler source produces correct native code on both
  platforms.
- **Register pinning**: a hot-variable analyzer counts reads per variable
  and pins the hottest ones into callee-saved registers inside loops. The
  loop codegen saves/restores caller values around the pin; getting this
  wrong corrupts the JIT host (a real bug class found and fixed during the
  Linux port — see FINDINGS.md N6).
- **Ephemeral pass**: store→load pair elimination for stack slots,
  including MOV-on-load when the value is still in RAX.
- **AVX2 AI kernels**: `math`/`ai` operations emit VFMADD231PS, VPMADDUBSW
  (INT8 dot products), VMAXPS (ReLU) and fused softmax/layernorm sequences
  directly as machine code, with scalar fallbacks compiled in for
  non-AVX2 hosts (`make portable`).
- **Inliner**: small leaf functions (≤ 8 statements, no calls, no loops) are
  expanded at call sites. Inlined `return`s become `eval → jmp end`, never
  physical `ret` instructions (see FINDINGS.md #13).
- **Runtime helpers**: printing, string ops, list ops, time, and the AI
  kernels are `__attribute__((noinline))` C functions compiled into omnicc;
  generated code calls them with the host ABI. Extern calls go through
  `mov rax, <abs64>; call rax` with address patching.
- **W^X discipline**: JIT memory is allocated RW, flipped to RX before
  execution (mmap/mprotect on POSIX, VirtualAlloc/VirtualProtect on
  Windows), and unmapped after the run.

## main.c (~600 lines)

CLI driver with five commands: `run` (JIT), `build` (standalone executable),
`dump` (hex dump of generated code), `check` (parse + check), `version`.

### Standalone builds

`omnicc build prog.ok` produces a standalone executable by copying the
omnicc binary itself and appending a payload footer:

```
[ omnicc image ][ program source ][ magic "OMNISRC1" ][ uint64 LE length ]
```

At startup, `main()` checks its own image for the footer. If present, the
copy compiles and runs the embedded source in-process — every helper address
and string literal is resolved naturally by the same codegen that produced
the original compile. This replaces an earlier from-scratch PE32+ emitter
whose helper references could not resolve outside the omnicc process
(audit finding #15); that emitter is preserved in git history.

## Runtime startup sequence

1. `omni_runtime_init()` — caches stdout/stdin handles and the monotonic
   clock frequency.
2. Main-body prologue — `push rbp`, frame setup, save r14/r15.
3. Program statements; `main` body returns the process exit code.

## What is NOT in the compiler (by design or honest limitation)

- No instruction scheduler / register allocator beyond the pinning
  described above (stack slots are used directly for most variables).
- No optimizer pass over the emitted byte stream.
- No incremental compilation, no LSP, no debugger.
- `omnip` (the package manager client) is Windows-only at v7.1.0 — it uses
  WinHTTP; a POSIX port is tracked as future work.
