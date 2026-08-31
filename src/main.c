// ============================================================
//  OMNIKARAI Compiler — omnicc  v7.1.0
//  Windows x64 — No LLVM — No runtime dependency
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

#include "omni_platform.h"   // platform layer (Win32 native / POSIX shims)

#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "codegen.h"

// ── Global flags ─────────────────────────────────────────────
static int g_quiet = 0;
static int g_ut    = 0;
extern int g_beta;

#define OMNI_LOG(...) do { if (!g_quiet && !g_ut) fprintf(stderr, __VA_ARGS__); } while(0)

// ── Version banner ───────────────────────────────────────────
static void print_version(void) {
#if defined(_WIN32)
    const char* plat = "Windows";
#else
    const char* plat = "Linux/macOS (POSIX)";
#endif
    fprintf(stderr,
        "Omnikarai Compiler (omnicc) v7.1.0\n"
        "  x86-64 native code | %s | No LLVM | No dependencies\n"
        "  Modules: time, datetime, math, os, io, sys, list, str, ai\n"
        "  Registry: https://opi-nine.vercel.app\n",
        plat
    );
}

// ── Usage ────────────────────────────────────────────────────
static void print_usage(void) {
    print_version();
    fprintf(stderr,
        "\nUsage:\n"
        "  omnicc run   [--quiet] [--beta] <file.ok>   compile and run (JIT)\n"
        "  omnicc build [--quiet] [--beta] <file.ok>   compile to standalone executable\n"
        "                                              (embeds runtime + source)\n"
        "  omnicc dump  [--quiet] [--beta] <file.ok>   dump x86-64 machine code bytes\n"
        "  omnicc check [--quiet] [--beta] <file.ok>   parse + check only (no run)\n"
        "  omnicc version                               show version info\n"
        "\nFlags:\n"
        "  --quiet   suppress [omnicc] diagnostic output\n"
        "  --beta    enable verbose beta debug traces (codegen + runtime)\n"
    );
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
static AST_Program* compile_source(const char* source, const char* filename) {
    Lexer l;
    lexer_init(&l, source);
    Parser* p = new_parser(&l);
    AST_Program* program = parse_program(p);
    if (p->error_count > 0) {
        fprintf(stderr, "\n  File \"%s\"\n", filename);
        fprintf(stderr, "ParseError: %d error(s) found\n\n", p->error_count);
        for (int i = 0; i < p->error_count; i++)
            fprintf(stderr, "  [%d] %s\n", i + 1, p->errors[i]);
        fprintf(stderr, "\n");
        free_parser(p);
        return NULL;
    }
    OMNI_LOG("[omnicc] parsed %d statement(s) from '%s'\n",
             program->statement_count, filename);
    free_parser(p);
    return program;
}

// ── omnicc run ───────────────────────────────────────────────
/* TEMP DIAGNOSTIC: SIGSEGV handler that reports the faulting PC and address
   so JIT faults can be mapped back to a JIT buffer offset. Enabled when
   OMNI_JIT_DEBUG=1. Remove once the SysV port is proven stable. */
#ifndef _WIN32
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
#endif /* !_WIN32 */

static int cmd_run(const char* filepath) {
    char* source = read_file(filepath);
    if (!source) return 1;
    AST_Program* program = compile_source(source, filepath);
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
        struct sigaction sa; memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = omni_jit_sigaction;
        sa.sa_flags = SA_SIGINFO;
        sigaction(SIGSEGV, &sa, NULL);
        sigaction(SIGILL,  &sa, NULL);
        sigaction(SIGBUS,  &sa, NULL);
        extern void codegen_set_jit_region(void* mem, size_t sz);
        /* region pointer is set inside codegen_run via the weak hook below */
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
    if (!source) return 1;
    AST_Program* program = compile_source(source, filepath);
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
static int cmd_check(const char* filepath) {
    char* source = read_file(filepath);
    if (!source) return 1;
    AST_Program* program = compile_source(source, filepath);
    if (!program) { free(source); return 1; }
    CodeGen cg;
    codegen_set_source(filepath, source);
    free(source);
    codegen_init(&cg);
    int ok = codegen_compile(&cg, program);
    if (ok)
        fprintf(stderr, "[omnicc] check OK — %zu bytes generated, no errors\n", cg.code.size);
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
    AST_Program* program = compile_source(source, path);
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
    if (!source) return 1;
    AST_Program* program = compile_source(source, filepath);
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

    if (argc < 2) { print_usage(); return 1; }
    const char* cmd = argv[1];

    if (strcmp(cmd, "version") == 0 || strcmp(cmd, "--version") == 0) {
        print_version(); return 0;
    }

    const char* filepath = NULL;
    for (int i = 2; i < argc; i++) {
        if      (strcmp(argv[i], "--quiet") == 0) { g_quiet = 1; }
        else if (strcmp(argv[i], "--beta")  == 0) { g_beta  = 1; fprintf(stderr,"[omnicc] --beta mode enabled\n"); }
        else if (strcmp(argv[i], "--ut")    == 0) { g_ut    = 1; g_quiet = 1; }
        else { filepath = argv[i]; }
    }

    if (!filepath &&
        (strcmp(cmd,"run")==0||strcmp(cmd,"dump")==0||
         strcmp(cmd,"check")==0||strcmp(cmd,"build")==0)) {
        fprintf(stderr, "Error: no source file specified\n");
        print_usage(); return 1;
    }

    if (strcmp(cmd, "run")   == 0) return cmd_run(filepath);
    if (strcmp(cmd, "dump")  == 0) return cmd_dump(filepath);
    if (strcmp(cmd, "check") == 0) return cmd_check(filepath);
    if (strcmp(cmd, "build") == 0) return cmd_build(filepath);

    fprintf(stderr, "Unknown command '%s'\n\n", cmd);
    print_usage();
    return 1;
}
