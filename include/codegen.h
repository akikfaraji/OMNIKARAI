#ifndef OMNI_CODEGEN_H
#define OMNI_CODEGEN_H

#include "ast.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

// ============================================================
//  OMNIKARAI x86-64 Native Code Generator  v4.0
//  Target: Windows x64 (Microsoft ABI)
//  No LLVM. No dependencies. Pure machine code emission.
// ============================================================

// --- Register IDs ---
typedef enum {
    REG_RAX=0, REG_RCX=1, REG_RDX=2, REG_RBX=3,
    REG_RSP=4, REG_RBP=5, REG_RSI=6, REG_RDI=7,
    REG_R8=8,  REG_R9=9,  REG_R10=10, REG_R11=11,
} Register;

// --- Symbol types ---
#define SYM_TABLE_SIZE 256

typedef enum {
    OMNI_TYPE_INT,
    OMNI_TYPE_FLOAT,
    OMNI_TYPE_BOOL,
    OMNI_TYPE_STR,
    OMNI_TYPE_LIST,   // Phase 4
    OMNI_TYPE_DICT,   // Phase 4
    OMNI_TYPE_UNKNOWN
} OmniType;

typedef struct Symbol {
    char     name[64];
    OmniType type;
    int      stack_offset;
    struct Symbol* next;
} Symbol;

typedef struct SymbolTable {
    Symbol*            buckets[SYM_TABLE_SIZE];
    struct SymbolTable* parent;
    int                next_offset;
} SymbolTable;

// --- Code Buffer ---
typedef struct {
    uint8_t* data;
    size_t   size;
    size_t   capacity;
} CodeBuf;

// --- Jump patch (forward jumps) ---
typedef struct {
    size_t patch_offset;
    size_t target_label;
} Patch;

// --- Label ---
typedef struct {
    size_t offset;
} Label;

// --- Class table entry ---
#define MAX_CLASSES    64
#define MAX_FIELDS     32
#define MAX_METHODS    32

typedef struct {
    char class_name[64];
    char field_names[MAX_FIELDS][64];
    int  field_count;
    char method_names[MAX_METHODS][64];  // just method name (without class prefix)
    int  method_count;
} ClassEntry;

// --- Function table entry ---
#define MAX_FUNCTIONS  256
#define MAX_CALL_PATCHES 512
#define MAX_LOOP_PATCHES 128

typedef struct {
    char   name[64];
    size_t code_offset;  // byte offset in CodeBuf where this fn starts
    int    param_count;
    int    resolved;     // 1 if code_offset is valid
    // v4.0: leaf inlining
    int    is_inline;    // 1 = single-expression body, inlined at call sites
    void*  inline_ast;   // pointer to AST_Statement_FnDef (for inline emit)
    OmniType ret_type;   // statically inferred return type (INT if unknown)
} FnEntry;

// --- Call patch (for resolving CALL rel32 after all fns are emitted) ---
typedef struct {
    size_t patch_offset; // offset of the 4-byte rel32 in CodeBuf
    char   fn_name[64];  // name of function to call
} CallPatch;

// --- CodeGen State ---
#define MAX_LABELS  1024
#define MAX_PATCHES 1024

typedef struct {
    CodeBuf      code;
    SymbolTable* scope;

    // Labels and forward-jump patches (for if/while)
    Label        labels[MAX_LABELS];
    int          label_count;
    Patch        patches[MAX_PATCHES];
    int          patch_count;

    // Stack / frame tracking
    int          stack_size;
    int          returned;

    // String literal pool (stable heap addresses)
    char**       string_pool;
    int          string_pool_count;

    // Function registry
    FnEntry      fn_table[MAX_FUNCTIONS];
    int          fn_count;

    // Class registry
    ClassEntry   class_table[MAX_CLASSES];
    int          class_count;
    // When inside a method, tracks the current class name and self pointer slot
    char         current_class[64];  // empty string if not in a method
    int          self_slot;          // stack offset of 'self' pointer (ptr to field array)

    // Call patches — resolved after all functions are emitted
    CallPatch    call_patches[MAX_CALL_PATCHES];
    int          call_patch_count;

    // Loop break/continue patch stacks
    size_t       break_patches[MAX_LOOP_PATCHES];
    int          break_patch_count;
    size_t       continue_patches[MAX_LOOP_PATCHES];
    int          continue_patch_count;

    // v4.0+: Register-pinned variables
    // Slots 0-1: r14, r15 (for-loop counters)
    // Slots 2-4: rbx, r12, r13 (while/for hot vars)
    // Slots 5-6: rsi, rdi  (extra callee-saved slots)
    int          reg_var_depth;
    char         reg_var_names[7][64]; // names of register-pinned variables
    int          reg_var_saved[7];
    int          in_main_body;         // 1 when emitting main body (not inside a fn)

    // ── Ephemeral pass: store+load elimination ──────────────────────────────
    // Tracks the last stack slot written by emit_store_rax so that a
    // subsequent emit_load_rax/rcx/rdx of the same slot can be replaced
    // by a register-register MOV (or eliminated entirely when dest==RAX).
    // Reset to -1 whenever a CALL, branch, or store to a different slot occurs.
    int          eph_last_store_off;   // stack offset of last emit_store_rax (-1 = none)
    int          eph_last_store_reg;   // 0=RAX, 1=RCX, 2=RDX (which reg held the value)
    size_t       eph_store_code_pos;   // code position of the store instruction

    // ── Module alias table ───────────────────────────────────────────────────
    // Populated by `use X as Y` statements.  When a call `Y.method()` is
    // encountered, the namespace Y is resolved to its canonical module name X
    // before dispatch.  Supports up to 32 simultaneous aliases.
    // e.g. "use numrai as np"  → alias_from[i]="np", alias_to[i]="numrai"
    // e.g. "use math as m"     → alias_from[i]="m",  alias_to[i]="math"
#define MAX_MODULE_ALIASES 32
    char         alias_from[MAX_MODULE_ALIASES][64];
    char         alias_to  [MAX_MODULE_ALIASES][64];
    int          alias_count;
} CodeGen;

// ============================================================
//  Public API
// ============================================================

// Error reporter: call before codegen_init to set source for diagnostics
void    codegen_set_source(const char* filename, const char* source);

void    codegen_init(CodeGen* cg);
void    codegen_free(CodeGen* cg);
int     codegen_compile(CodeGen* cg, AST_Program* program);
int64_t codegen_run(CodeGen* cg);
void    codegen_dump(CodeGen* cg);

#endif // OMNI_CODEGEN_H
