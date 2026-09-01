// ============================================================
//  OMNIKARAI Compiler — omnicc
//  Windows x64 — No LLVM — No runtime dependency
//  Version: single-sourced in include/omni_version.h (docs/VERSIONING.md)
//
//  Usage:
//    omnicc run   [--quiet] [--beta] <file.ok>   compile and run (JIT)
//    omnicc build [--quiet] [--beta] <file.ok>   compile to a standalone exe
//    omnicc dump  [--quiet] [--beta] <file.ok>   dump x86-64 machine code bytes
//    omnicc check [--quiet] [--beta] <file.ok>   parse + check only (no run)
//
//  Flags:
//    --quiet  suppress [omnicc] diagnostic lines
//    --beta   enable verbose codegen + runtime debug traces
//    --ut     extension/unit-test mode: no [omnicc] stderr, clean stdout only
//
//  omnicc build emits a PE32+ .exe with:
//    - .text  section  : small entry trampoline + JIT-compiled code
//    - .idata section  : import table (KERNEL32.DLL only)
//    - .reloc section  : base relocation table for ASLR compatibility
//  The produced .exe runs standalone — omnicc not required at runtime.
//  All runtime helpers (print, time, malloc, etc.) are inlined into
//  the .exe via the embedded Omnikarai runtime in codegen.c.
// ============================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>          /* BUG-006 fix: include before windows.h */

#ifndef _WIN32
#include <sys/utsname.h>   /* OMNI-E0005: host-arch detection (backend refusal) */
#endif

#include "omni_platform.h"   // platform layer (Win32 native / POSIX shims)

#include "omni_version.h"    // single-sourced version (V01 convention)
#include "omni_diag.h"       // structured diagnostics (V01.00)
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "codegen.h"

// ── Global flags ─────────────────────────────────────────
static int g_quiet = 0;
static int g_ut    = 0;
extern int g_beta;

/* JSON diagnostics mode (`omnicc check --json`): stdout carries a
   omnikarai.diag.v0 document; stderr stays silent; exit codes:
   0 clean · 1 diagnostics reported · 2 usage/IO error.
   See docs/DIAGNOSTICS.md for the schema contract. */
static int           g_json_mode    = 0;
static int           g_json_printed = 0;
static OmniDiagList  g_json_diags;

#define OMNI_LOG(...) do { if (!g_quiet && !g_ut) fprintf(stderr, __VA_ARGS__); } while(0)

/* atexit flush: codegen errors terminate via exit(1) inside
   codegen.c; in JSON mode the capture list is flushed here so the
   document is printed exactly once, on every termination path. */
static void omni_json_atexit_flush(void) {
    if (!g_json_mode || g_json_printed) return;
    g_json_printed = 1;
    omni_diag_print_json(omni_diag_capture_list(), 0, stdout);
    omni_diag_capture_end();
}

static void emit_json(OmniDiagList* list, int ok) {
    g_json_printed = 1;
    omni_diag_print_json(list, ok, stdout);
    if (list == &g_json_diags) omni_diag_init(&g_json_diags);
}

/* Split source into lines for caret rendering in text diagnostics. */
static char** split_source_lines(const char* source, int* out_count) {
    if (!source) { *out_count = 0; return NULL; }
    int count = 1;
    for (const char* p = source; *p; p++) if (*p == '\n') count++;
    char** lines = (char**)calloc((size_t)count, sizeof(char*));
    if (!lines) { *out_count = 0; return NULL; }
    const char* start = source; int idx = 0;
    for (const char* p = source; ; p++) {
        if (*p == '\n' || *p == '\0') {
            size_t len = (size_t)(p - start);
            lines[idx] = (char*)malloc(len + 1);
            if (lines[idx]) {
                memcpy(lines[idx], start, len);
                lines[idx][len] = '\0';
            }
            idx++;
            if (*p == '\0') break;
            start = p + 1;
        }
    }
    *out_count = idx;
    return lines;
}

static void free_source_lines(char** lines, int count) {
    if (!lines) return;
    for (int i = 0; i < count; i++) free(lines[i]);
    free(lines);
}

/* Print parser diagnostics in text mode (with --beta internal detail). */
static void print_text_diags(const OmniDiagList* diags, const char* source) {
    int nlines = 0;
    char** lines = split_source_lines(source, &nlines);
    omni_diag_print_text(diags, lines, nlines, stderr);
    free_source_lines(lines, nlines);
    if (g_beta) {
        for (const OmniDiag* d = diags->head; d; d = d->next)
            if (d->detail[0])
                fprintf(stderr, "[BETA-DIAG] %s %s\n", d->code, d->detail);
    }
}

// ── Version banner ───────────────────────────────────────────
static void print_version(void) {
#if defined(_WIN32)
    const char* plat = "Windows";
#else
    const char* plat = "Linux/macOS (POSIX)";
#endif
    fprintf(stderr,
        "Omnikarai Compiler (omnicc) " OMNI_VERSION "\n"
        "  x86-64 native code | %s | No LLVM | No dependencies\n"
        "  Modules: time, datetime, math, os, io, sys, list, str, ai\n"
        "  Registry: https://opi-nine.vercel.app\n",
        plat
    );
}

/* Machine-parsable version — one key=value per line, stdout.
   Schema revision = OMNI_VERSION_MACHINE_SCHEMA (omni_version.h).
   Contract (docs/DIAGNOSTICS.md): never reorder existing keys;
   new keys are appended; removal/renames bump the schema. */
static void print_version_machine(void) {
#if defined(_WIN32)
    const char* plat = "windows-x64";
    const char* abi  = "win64";
#else
    const char* plat = "linux-x64";
    const char* abi  = "sysv";
#endif
    printf("schema=%d\n", OMNI_VERSION_MACHINE_SCHEMA);
    printf("version=%s\n", OMNI_VERSION);
    printf("platform=%s\n", plat);
    printf("arch=x86-64\n");
    printf("abi=%s\n", abi);
    printf("modules=time,datetime,math,os,io,sys,list,str,ai\n");
}

// ── Usage ────────────────────────────────────────────────────
/* Usage/help text. Goes to stdout for explicit --help (exit 0) and
   to stderr for usage errors. */
static void print_usage_to(FILE* out) {
    fprintf(out,
        "Omnikarai Compiler (omnicc) " OMNI_VERSION "\n"
        "  x86-64 native code | No LLVM | No dependencies\n"
        "\n"
        "Usage:\n"
        "  omnicc run     [--quiet] [--beta] <file.ok>   compile and run (JIT)\n"
        "  omnicc build   [--quiet] [--beta] <file.ok>   compile to standalone executable\n"
        "                                                 (embeds runtime + source)\n"
        "  omnicc dump    [--quiet] [--beta] <file.ok>   dump x86-64 machine code bytes\n"
        "  omnicc check   [--quiet] [--beta] [--json] <file.ok>\n"
        "                                                parse + check only (no run)\n"
        "  omnicc version [--machine]                     show version info\n"
        "\n"
        "Flags:\n"
        "  --quiet   suppress [omnicc] diagnostic output\n"
        "  --beta    enable verbose beta debug traces (codegen + runtime)\n"
        "  --json    check only: emit omnikarai.diag.v0 JSON to stdout\n"
        "\n"
        "Exit codes:\n"
        "  0  success (or the program's own exit code for run)\n"
        "  1  diagnostics reported (parse/compile errors)\n"
        "  2  usage or IO error (unknown command/flag, cannot open file)\n"
        "\n"
        "Docs: docs/DIAGNOSTICS.md (JSON schema), docs/LANGUAGE.md (language)\n"
    );
}

static void print_usage(void) {
    print_usage_to(stderr);
}

// ── File reader ──────────────────────────────────────────────
static char* read_file(const char* path) {
    FILE* f = NULL;
    if (fopen_s(&f, path, "rb") != 0 || !f) {
        fprintf(stderr, "Error: cannot open '%s'\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0) { fclose(f); return NULL; }
    char* buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, (size_t)len, f);
    buf[len] = '\0';
    fclose(f);
    return buf;
}

// ── Compile pipeline: source → AST ──────────────────────────
/* Parses `source`. On parse errors returns NULL; diagnostics are
   moved into *out_diags when the caller wants them (JSON mode),
   otherwise they are printed to stderr in text form. */
static AST_Program* compile_source(const char* source, const char* filename,
                                   OmniDiagList* out_diags) {
    Lexer l;
    lexer_init(&l, source);
    Parser* p = new_parser(&l);
    p->diag_file = filename;
    AST_Program* program = parse_program(p);
    if (p->error_count > 0) {
        if (out_diags) {
            *out_diags = p->diags;          /* move ownership */
            omni_diag_init(&p->diags);
        } else {
            fprintf(stderr, "\n  %s: %d error(s) found\n", filename, p->error_count);
            print_text_diags(&p->diags, source);
            fprintf(stderr, "\n");
        }
        free_parser(p);
        return NULL;
    }
    omni_diag_free(&p->diags);
    OMNI_LOG("[omnicc] parsed %d statement(s) from '%s'\n",
             program->statement_count, filename);
    free_parser(p);
    return program;
}

// ── omnicc run ───────────────────────────────────────────────
/* TEMP DIAGNOSTIC: SIGSEGV handler that reports the faulting PC and address
   so JIT faults can be mapped back to a JIT buffer offset. Enabled when
   OMNI_JIT_DEBUG=1. Remove once the SysV port is proven stable.
   x86-64-only: reads REG_RIP from the mcontext; on other architectures
   the JIT cannot run anyway, so the handler is compiled out. */
#if !defined(_WIN32) && defined(__x86_64__)
#define _GNU_SOURCE /* for REG_RIP in ucontext */
#include <signal.h>
static volatile void* g_jit_mem = NULL;
static volatile size_t g_jit_size = 0;
static void omni_jit_sigaction(int sig, siginfo_t* si, void* uctx) {
    ucontext_t* uc = (ucontext_t*)uctx;
    uintptr_t pc = (uintptr_t)uc->uc_mcontext.gregs[16]; /* 16 == REG_RIP on x86-64 glibc */
    fprintf(stderr, "\n[SIGSEGV] sig=%d fault_addr=%p pc=%p", sig, si->si_addr, (void*)pc);
    if (g_jit_mem && (uintptr_t)pc >= (uintptr_t)g_jit_mem &&
        (uintptr_t)pc <  (uintptr_t)g_jit_mem + g_jit_size) {
        fprintf(stderr, " → JIT offset 0x%zx\n", (size_t)(pc - (uintptr_t)g_jit_mem));
    } else {
        /* Resolve pc against the main executable's load base via /proc/self/maps */
        FILE* mf = fopen("/proc/self/maps", "r");
        uintptr_t base = 0; char line[512];
        if (mf) {
            while (fgets(line, sizeof(line), mf)) {
                if (strstr(line, "omnicc")) {
                    base = (uintptr_t)strtoul(line, NULL, 16);
                    break;
                }
            }
            fclose(mf);
        }
        if (base && pc >= base)
            fprintf(stderr, " → host offset 0x%zx (use addr2line)\n", (size_t)(pc - base));
        else
            fprintf(stderr, " (unknown region)\n");
    }
    _exit(139);
}

void omni_host_set_jit_region(void* mem, size_t sz) {
    g_jit_mem = mem; g_jit_size = sz;
}
#else  /* !_WIN32 && __x86_64__ — other hosts: region hook is a no-op */
void omni_host_set_jit_region(void* mem, size_t sz) {
    (void)mem; (void)sz;
}
#endif /* !(!_WIN32 && __x86_64__) */

/* ── OMNI-E0005 — honest backend/host refusal ────────────────
   The one-pass backend emits native x86-64 machine code (JIT for
   `run`, engine-embedded standalone for `build`). On any other host
   the emitted bytes would fault (SIGILL) — a silent, dishonest
   failure. Instead we refuse cleanly at the command boundary and
   point at what DOES work here: the diagnostics tier (check /
   check --json / dump / version), which is architecture-independent.
   Native AArch64 execution is planned for V01.06 (docs/AARCH64.md);
   this guard is lifted by that release, not before. */
static int omni_backend_refuse_if_unsupported(const char* cmd) {
#if defined(_WIN32)
    (void)cmd;
    return 0;   /* x86-64 Windows is the legacy main target; Windows-on-ARM
                   is not a claimed platform (docs/PLATFORM_SUPPORT.md). */
#else
    struct utsname u;
    if (uname(&u) != 0) return 0;   /* cannot tell — best effort */
    if (strcmp(u.machine, "x86_64") == 0 || strcmp(u.machine, "amd64") == 0)
        return 0;
    fprintf(stderr,
        "OMNI-E0005 error: '%s' needs the x86-64 code-generation backend, "
        "but this host is '%s'.\n"
        "  The backend emits native x86-64 machine code; running it here "
        "would fault (SIGILL).\n"
        "  Available on this host: omnicc check, check --json, dump, version "
        "(diagnostics tier).\n"
        "  Native execution on this architecture is planned: docs/AARCH64.md "
        "(V01.06).\n", cmd, u.machine);
    return 2;
#endif
}

static int cmd_run(const char* filepath) {
    char* source = read_file(filepath);
    if (!source) return 2;
    AST_Program* program = compile_source(source, filepath, NULL);
    if (!program) { free(source); return 1; }
    CodeGen cg;
    codegen_set_source(filepath, source);
    free(source);
    codegen_init(&cg);
    if (!codegen_compile(&cg, program)) {
        fprintf(stderr, "Compilation failed.\n");
        codegen_free(&cg);
        return 1;
    }
    if (getenv("OMNI_JIT_DEBUG")) {
#if !defined(_WIN32) && defined(__x86_64__)
        struct sigaction sa; memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = omni_jit_sigaction;
        sa.sa_flags = SA_SIGINFO;
        sigaction(SIGSEGV, &sa, NULL);
        sigaction(SIGILL,  &sa, NULL);
        sigaction(SIGBUS,  &sa, NULL);
        extern void codegen_set_jit_region(void* mem, size_t sz);
        /* region pointer is set inside codegen_run via the weak hook below */
#endif
    }
    OMNI_LOG("[omnicc] running %zu bytes of native x86-64 code...\n", cg.code.size);
    LARGE_INTEGER t0, t1, freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    int64_t exit_code = codegen_run(&cg);
    QueryPerformanceCounter(&t1);
    double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;
    OMNI_LOG("[omnicc] done in %.3f ms | exit_code=%lld\n", ms, (long long)exit_code);
    codegen_free(&cg);
    return (int)exit_code;
}

// ── omnicc dump ──────────────────────────────────────────────
static int cmd_dump(const char* filepath) {
    char* source = read_file(filepath);
    if (!source) return 2;
    AST_Program* program = compile_source(source, filepath, NULL);
    if (!program) { free(source); return 1; }
    CodeGen cg;
    codegen_set_source(filepath, source);
    free(source);
    codegen_init(&cg);
    if (!codegen_compile(&cg, program)) {
        fprintf(stderr, "Compilation failed.\n");
        codegen_free(&cg);
        return 1;
    }
    codegen_dump(&cg);
    codegen_free(&cg);
    return 0;
}

// ── omnicc check ─────────────────────────────────────────────
/* Parse + full compile validation without executing.
   Text mode: human-readable diagnostics on stderr.
   JSON mode (--json): omnikarai.diag.v0 document on stdout —
   stable enough for editors, CI and AI coding agents
   (schema: docs/DIAGNOSTICS.md). */
static int cmd_check(const char* filepath, int json) {
    char* source = read_file(filepath);
    if (!source) {
        if (json) {
            OmniDiagList list;
            omni_diag_init(&list);
            omni_diag_add(&list, OMNI_DIAG_ERROR, "OMNI-E0004",
                          filepath, 0, 0, 0, "cannot open source file");
            emit_json(&list, 0);
        }
        return 2;
    }
    if (json) {
        AST_Program* program = compile_source(source, filepath, &g_json_diags);
        if (!program) {
            emit_json(&g_json_diags, 0);
            free(source);
            return 1;
        }
        /* parse OK → codegen; a codegen error exits(1) inside codegen.c
           and the atexit handler flushes the JSON document. */
        CodeGen cg;
        codegen_set_source(filepath, source);
        free(source);
        codegen_init(&cg);
        int ok = codegen_compile(&cg, program);
        if (ok) {
            emit_json(&g_json_diags, 1);
            codegen_free(&cg);
            return 0;
        }
        /* non-exit failure path (should not normally happen) */
        if (!g_json_printed)
            emit_json(&g_json_diags, 0);
        codegen_free(&cg);
        return 1;
    }
    AST_Program* program = compile_source(source, filepath, NULL);
    if (!program) { free(source); return 1; }
    CodeGen cg;
    codegen_set_source(filepath, source);
    free(source);
    codegen_init(&cg);
    int ok = codegen_compile(&cg, program);
    if (ok)
        OMNI_LOG("[omnicc] check OK — %zu bytes generated, no errors\n", cg.code.size);
    else
        fprintf(stderr, "[omnicc] check FAILED\n");
    codegen_free(&cg);
    return ok ? 0 : 1;
}

// ============================================================
//  omnicc build — standalone executable emitter
//
//  Architecture (fix for audit finding #15 / BUILD-002):
//  The previous from-scratch PE32+ emitter copied JIT bytes containing
//  absolute 64-bit addresses of the Omnikarai runtime helpers and of
//  compile-time string literals — both live only inside the omnicc
//  process, so the emitted .exe could never run standalone (its helper
//  calls pointed into a process image that does not exist at runtime).
//  A correct fix would require a full linker embedding the compiled
//  runtime; that is out of scope and was NOT faked.
//
//  The working design: a built artifact is a full copy of this omnicc
//  executable with the program SOURCE appended in a tagged payload.
//  At startup the copy checks its own image for the payload; if found,
//  it compiles and runs the embedded source in-process — every helper
//  address and string literal is resolved naturally by the same codegen
//  that ran in the original compile. The result is a genuinely standalone
//  executable (~200 KB) that runs on any machine of the same platform
//  with no omnicc installed.
//
//  Payload layout appended to the copy (footer at a FIXED offset from the
//  end of the file so the copy can find it without knowing its own size):
//    [0..]    ... executable image bytes ...
//    [N..N+L] source bytes (L = source length)
//    [N+L..N+L+7]   magic  "OMNISRC1"
//    [N+L+7..N+L+15] uint64 LE source length
// ============================================================
static const char OMNI_PAYLOAD_MAGIC[8] = { 'O','M','N','I','S','R','C','1' };

static const char* g_self_exe = NULL;  /* set from argv[0] in main() */

/* Locate an appended payload in our own executable image.
   Returns malloc'd NUL-terminated source, or NULL. */
static char* omni_read_own_payload(uint64_t* out_len) {
    const char* self = g_self_exe;
    if (!self || !*self) return NULL;
#if defined(__linux__)
    FILE* f = fopen("/proc/self/exe", "rb");
    if (!f) f = fopen(self, "rb");
#else
    FILE* f = fopen(self, "rb");
#endif
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long fsz = ftell(f);
    if (fsz < 16 + (long)sizeof(OMNI_PAYLOAD_MAGIC)) { fclose(f); return NULL; }
    if (fseek(f, fsz - 16, SEEK_SET) != 0) { fclose(f); return NULL; }
    char magic[8];
    uint8_t lenbuf[8];
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, OMNI_PAYLOAD_MAGIC, 8) != 0) {
        fclose(f); return NULL;
    }
    if (fread(lenbuf, 1, 8, f) != 8) { fclose(f); return NULL; }
    uint64_t slen = 0;
    for (int i = 0; i < 8; i++) slen |= (uint64_t)lenbuf[i] << (8 * i);
    if (slen == 0 || slen > (uint64_t)fsz) { fclose(f); return NULL; }
    char* src = (char*)malloc((size_t)slen + 1);
    if (!src) { fclose(f); return NULL; }
    if (fseek(f, fsz - 16 - (long)slen, SEEK_SET) != 0 ||
        fread(src, 1, (size_t)slen, f) != (size_t)slen) {
        free(src); fclose(f); return NULL;
    }
    fclose(f);
    src[slen] = '\0';
    if (out_len) *out_len = slen;
    return src;
}

/* Compile + run an in-memory source (shared by cmd_run and the embedded
   payload path). Takes ownership of nothing; mirrors cmd_run's body. */
static int omni_run_source(const char* path, char* source) {
    AST_Program* program = compile_source(source, path, NULL);
    if (!program) { free(source); return 1; }
    CodeGen cg;
    codegen_set_source(path, source);
    free(source);
    codegen_init(&cg);
    if (!codegen_compile(&cg, program)) {
        fprintf(stderr, "Compilation failed.\n");
        codegen_free(&cg);
        return 1;
    }
    OMNI_LOG("[omnicc] running %zu bytes of native x86-64 code...\n", cg.code.size);
    LARGE_INTEGER t0, t1, freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    int64_t exit_code = codegen_run(&cg);
    QueryPerformanceCounter(&t1);
    double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;
    OMNI_LOG("[omnicc] done in %.3f ms | exit_code=%lld\n", ms, (long long)exit_code);
    codegen_free(&cg);
    return (int)exit_code;
}

/* Returns the embedded program's exit code, or -1 when this executable
   carries no payload (i.e., it is the plain omnicc tool). */
static int cmd_run_embedded(void) {
    char* src = omni_read_own_payload(NULL);
    if (!src) return -1;
    return omni_run_source("<embedded>", src);
}

static int cmd_build(const char* filepath) {
    // ── Output path: swap .ok → .exe ─────────────────────────
    char outpath[512];
    strncpy_s(outpath, sizeof(outpath), filepath, _TRUNCATE);
    char* dot = strrchr(outpath, '.');
    if (dot && _stricmp(dot, ".ok") == 0)
        strcpy_s(dot, sizeof(outpath) - (size_t)(dot - outpath), ".exe");
    else
        strncat_s(outpath, sizeof(outpath), ".exe", _TRUNCATE);

    // ── Read + validate the source BEFORE copying (fail early) ──
    char* source = read_file(filepath);
    if (!source) return 2;
    AST_Program* program = compile_source(source, filepath, NULL);
    if (!program) { free(source); return 1; }
    free(source);   /* validated; the payload appends the original file bytes */
    /* NOTE: compile_source parsed successfully — the program is valid.
       We append the ORIGINAL file bytes; the built artifact re-parses
       them at startup. */

    // ── Copy our own executable + append the payload ─────────
    const char* self = g_self_exe;
    if (!self || !*self) {
        fprintf(stderr, "Error: cannot determine own executable path\n");
        return 1;
    }
#if defined(__linux__)
    FILE* in = fopen("/proc/self/exe", "rb");
    if (!in) in = fopen(self, "rb");
#else
    FILE* in = fopen(self, "rb");
#endif
    if (!in) {
        fprintf(stderr, "Error: cannot open own executable '%s'\n", self);
        return 1;
    }
    char* file_bytes = read_file(filepath);   /* re-read for the payload */
    if (!file_bytes) { fclose(in); return 1; }
    size_t src_len = strlen(file_bytes);

    FILE* out = NULL;
    if (fopen_s(&out, outpath, "wb") != 0 || !out) {
        fprintf(stderr, "Error: cannot create '%s'\n", outpath);
        free(file_bytes); fclose(in); return 1;
    }
    uint8_t buf[65536];
    size_t n; uint64_t total = 0;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { fprintf(stderr, "Error: write failed\n"); fclose(in); fclose(out); free(file_bytes); return 1; }
        total += n;
    }
    fclose(in);
    uint8_t lenbuf[8];
    for (int i = 0; i < 8; i++) lenbuf[i] = (uint8_t)((src_len >> (8 * i)) & 0xFF);
    fwrite(file_bytes, 1, src_len, out);
    fwrite(OMNI_PAYLOAD_MAGIC, 1, 8, out);
    fwrite(lenbuf, 1, 8, out);
    if (fclose(out) != 0) {
        fprintf(stderr, "Error: flush failed\n");
        free(file_bytes); return 1;
    }
#ifndef _WIN32
    chmod(outpath, 0755);   /* keep the copy executable */
#endif
    free(file_bytes);

    fprintf(stderr, "[omnicc build] wrote: %s  (%llu bytes + %zu source bytes)\n",
            outpath, (unsigned long long)total, src_len);
    fprintf(stderr, "[omnicc build] standalone executable — embeds the Omnikarai runtime\n");
    return 0;
}


// ── Entry point ──────────────────────────────────────────────
int main(int argc, char** argv) {
    g_self_exe = argc > 0 ? argv[0] : NULL;

    /* Standalone payload check: a program built by `omnicc build` is a copy
       of this executable with the source appended. If the payload is present
       we are the built program — compile and run it, ignoring CLI args. */
    {
        int rc = cmd_run_embedded();
        if (rc >= 0) return rc;
    }

    if (argc < 2) { print_usage(); return 2; }
    const char* cmd = argv[1];

    /* Explicit help: stdout, exit 0 (docs/DIAGNOSTICS.md exit-code table) */
    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0 || strcmp(cmd, "help") == 0) {
        print_usage_to(stdout);
        return 0;
    }

    if (strcmp(cmd, "version") == 0 || strcmp(cmd, "--version") == 0) {
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--machine") == 0) { print_version_machine(); return 0; }
        }
        print_version(); return 0;
    }

    const char* filepath = NULL;
    int check_json = 0;
    for (int i = 2; i < argc; i++) {
        if      (strcmp(argv[i], "--quiet") == 0) { g_quiet = 1; }
        else if (strcmp(argv[i], "--beta")  == 0) { g_beta  = 1; fprintf(stderr,"[omnicc] --beta mode enabled\n"); }
        else if (strcmp(argv[i], "--ut")    == 0) { g_ut    = 1; g_quiet = 1; }
        else if (strcmp(argv[i], "--json")  == 0) { check_json = 1; }
        else if (argv[i][0] == '-' && argv[i][1] == '-') {
            fprintf(stderr, "Error: unknown flag '%s' (see omnicc --help)\n", argv[i]);
            return 2;
        }
        else { filepath = argv[i]; }
    }

    if (!filepath &&
        (strcmp(cmd,"run")==0||strcmp(cmd,"dump")==0||
         strcmp(cmd,"check")==0||strcmp(cmd,"build")==0)) {
        fprintf(stderr, "Error: no source file specified (see omnicc --help)\n");
        return 2;
    }

    if (strcmp(cmd, "run")   == 0) {
        int refused = omni_backend_refuse_if_unsupported(cmd);
        if (refused) return refused;
        return cmd_run(filepath);
    }
    if (strcmp(cmd, "dump")  == 0) return cmd_dump(filepath);
    if (strcmp(cmd, "check") == 0) {
        if (check_json) {
            g_json_mode = 1;
            g_quiet = 1;   /* stdout JSON must not mix with [omnicc] logs */
            atexit(omni_json_atexit_flush);
            omni_diag_capture_begin(filepath);
            omni_diag_init(&g_json_diags);
        }
        return cmd_check(filepath, check_json);
    }
    if (strcmp(cmd, "build") == 0) {
        int refused = omni_backend_refuse_if_unsupported(cmd);
        if (refused) return refused;
        return cmd_build(filepath);
    }

    fprintf(stderr, "Error: unknown command '%s' (see omnicc --help)\n", cmd);
    return 2;
}
