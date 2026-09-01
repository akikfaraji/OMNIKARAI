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
  including MOV-on-load when the value is still in RAX. Every raw
  RAX-writing emitter must invalidate the tracker; a missing
  invalidation in the constant-fold path once made `2 + 3 * 4`
  compile to `24` (fixed and regression-pinned in V01.00, TD-16).
- **Float arithmetic (SSE2)**: when either operand is statically
  float, `+ - * /` compile to ADDSD/SUBSD/MULSD/DIVSD over
  stack-staged doubles, and comparisons to ordered UCOMISD+setcc
  sequences; `infer_type` propagates FLOAT so print/args/returns use
  the double conventions. `%`/`**` on floats are loud TypeErrors.
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

## Diagnostics (`include/omni_diag.h`, `src/diag.c`)

Since V01.00, parse and codegen errors are structured diagnostics with
stable codes (`OMNI-E2NNN` syntax, `OMNI-E3NNN` semantic), file/line/
column from the current token, optional hints, and two renderers: human
text with a caret and the `omnikarai.diag.v0` JSON document
(`omnicc check --json`; schema and code registry:
[DIAGNOSTICS.md](DIAGNOSTICS.md)). Codegen error paths terminate via
`exit(1)`; in JSON mode the CLI registers an atexit flush so exactly one
JSON document is printed on every termination path. The parser keeps a
legacy plain-string error array in sync for compatibility.

## Memory funnel (`include/omni_mem.h`, `src/omni_mem.c`)

All RUNTIME allocations (lists, AI/tensor buffers, class instances)
flow through `omni_mem_*` since V01.00 — with live counters and an
`OMNI_MEM_DEBUG=1` poison-on-free mode — deliberately separate from
compiler-internal allocations (AST, scopes, code buffers, string pool),
which stay on plain malloc. Allocator hooks (arena/pool) land in V01.01
behind these functions. See [MEMORY_MODEL.md](MEMORY_MODEL.md).

## main.c (~600 lines)

CLI driver with five commands: `run` (JIT), `build` (standalone executable),
`dump` (hex dump of generated code), `check` (parse + check), `version`.
V01.00 additions: `check --json`, `version --machine`, `--help`, and the
deterministic exit-code table (0 ok / 1 diagnostics / 2 usage-IO).
See [DIAGNOSTICS.md](DIAGNOSTICS.md).

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
