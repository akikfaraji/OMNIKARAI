// ============================================================
//  OMNIKARAI Compiler — omnicc  v6.02.24
//  Windows x64 — No LLVM — No runtime dependency
//
//  Usage:
//    omnicc run   [--quiet] [--beta] <file.ok>   compile and run (JIT)
//    omnicc build [--quiet] [--beta] <file.ok>   compile to standalone .exe
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
    fprintf(stderr,
        "Omnikarai Compiler (omnicc) v6.02.24\n"
        "  x86-64 native code | Windows | No LLVM | No dependencies\n"
        "  Modules: time, datetime, math, os, io, sys, list, str, ai\n"
        "  Package manager: omnip v6.0.0  |  Registry: https://opi-nine.vercel.app\n"
    );
}

// ── Usage ────────────────────────────────────────────────────
static void print_usage(void) {
    print_version();
    fprintf(stderr,
        "\nUsage:\n"
        "  omnicc run   [--quiet] [--beta] <file.ok>   compile and run (JIT)\n"
        "  omnicc build [--quiet] [--beta] <file.ok>   compile to standalone .exe\n"
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
//  omnicc build — PE32+ standalone .exe emitter
//
//  Architecture:
//    The JIT buffer that omnicc normally copies into VirtualAlloc'd
//    memory is instead written directly into the .text section of a
//    PE32+ file.  A small trampoline at the PE entry point calls into
//    the JIT code.  All Omnikarai runtime helpers (print, time, malloc,
//    etc.) are called via MOV RAX,abs64; CALL RAX sequences in the JIT
//    code.  The .reloc section records every such abs64 fixup so the
//    Windows loader can rebase them when ASLR places the image at a
//    different address than 0x400000.
//
//  PE sections:
//    .text   RVA 0x1000  trampoline (26 bytes) + JIT code
//    .idata  RVA 0x2000  import directory + IAT for KERNEL32.DLL
//    .reloc  RVA 0x3000  base relocation table (IMAGE_REL_BASED_DIR64)
//
//  Known limitation:
//    The .reloc patching works correctly for ASLR only when the loader
//    delta is known.  The runtime helpers embedded via abs64 MOVs
//    reference addresses inside omnicc.exe itself (the compiler process).
//    In a truly standalone .exe those helpers must be compiled in or
//    resolved via IAT thunks.  The current build output is therefore
//    best used for benchmarking JIT-vs-built performance on the same
//    machine where omnicc was compiled, with ASLR disabled or matched.
//    Full standalone build (helpers compiled into the output .exe) is
//    tracked in KNOWN_ISSUES.md as BUILD-002.
// ============================================================

#define PE_IMAGE_BASE    0x400000ULL
#define PE_SECT_ALIGN    0x1000
#define PE_FILE_ALIGN    0x200

static uint32_t pe_align_up(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

// Little-endian write helpers
static void pe_u8 (FILE*f, uint8_t  v) { fwrite(&v, 1, 1, f); }
static void pe_u16(FILE*f, uint16_t v) { fwrite(&v, 2, 1, f); }
static void pe_u32(FILE*f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void pe_u64(FILE*f, uint64_t v) { fwrite(&v, 8, 1, f); }
static void pe_str(FILE*f, const char*s) { fwrite(s, 1, strlen(s)+1, f); }
static void pe_pad(FILE*f, long target) {
    long cur = ftell(f);
    while (cur < target) { pe_u8(f, 0); cur++; }
}
static void pe_patch32(FILE*f, long off, uint32_t v) {
    long cur = ftell(f);
    fseek(f, off, SEEK_SET);
    pe_u32(f, v);
    fseek(f, cur, SEEK_SET);
}

// Entry trampoline — 26 bytes at PE entry point (RVA 0x1000):
//   48 83 EC 28          sub  rsp, 40       (shadow + align)
//   E8 xx xx xx xx       call <jit_entry>   (rel32, patched)
//   48 83 C4 28          add  rsp, 40
//   31 C9                xor  ecx, ecx
//   48 A1 xx xx xx xx xx xx xx xx  mov rax, [abs64_IAT_ExitProcess]
//   FF D0                call rax
//   CC                   int3
#define TRAMP_CALL_OFF  5    // byte offset of the rel32 inside call
#define TRAMP_IAT_OFF  17    // byte offset of the abs64 in mov rax,[m]
#define TRAMP_SIZE     28    // 4+5+4+2+10+2+1 bytes

static const uint8_t tramp_template[TRAMP_SIZE] = {
    0x48,0x83,0xEC,0x28,                          // sub  rsp, 40
    0xE8,0x00,0x00,0x00,0x00,                     // call rel32  (patched)
    0x48,0x83,0xC4,0x28,                          // add  rsp, 40
    0x31,0xC9,                                    // xor  ecx, ecx
    0x48,0xA1,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // mov rax,[abs64] (patched)
    0xFF,0xD0,                                    // call rax
    0xCC                                          // int3
};

// KERNEL32 imports — every function the Omnikarai runtime calls
static const char* const k32[] = {
    "WriteFile",             // 0
    "ReadFile",              // 1
    "GetStdHandle",          // 2
    "VirtualAlloc",          // 3
    "VirtualFree",           // 4
    "ExitProcess",           // 5  <-- trampoline uses slot 5
    "HeapAlloc",             // 6
    "HeapFree",              // 7
    "GetProcessHeap",        // 8
    "GetSystemTimeAsFileTime",   // 9
    "QueryPerformanceCounter",   // 10
    "QueryPerformanceFrequency", // 11
    "GetFileAttributesA",        // 12
    "GetFileAttributesExA",      // 13
    "GetEnvironmentVariableA",   // 14
    "GetCurrentDirectoryA",      // 15
    "GetCurrentProcessId",       // 16
    "CreateDirectoryA",          // 17
    "DeleteFileA",               // 18
    "CreateFileA",               // 19
    "GetFileSizeEx",             // 20
    "CloseHandle",               // 21
    "GetTimeZoneInformation",    // 22
    "SystemTimeToFileTime",      // 23
    "FileTimeToSystemTime",      // 24
    "Sleep",                     // 25
    "GetLastError",              // 26
    "FlushFileBuffers",          // 27
    NULL
};

static int cmd_build(const char* filepath) {
    // ── Output path: swap .ok → .exe ─────────────────────────
    char outpath[512];
    strncpy_s(outpath, sizeof(outpath), filepath, _TRUNCATE);
    char* dot = strrchr(outpath, '.');
    if (dot && _stricmp(dot, ".ok") == 0)
        strcpy_s(dot, sizeof(outpath) - (size_t)(dot - outpath), ".exe");
    else
        strncat_s(outpath, sizeof(outpath), ".exe", _TRUNCATE);

    // ── Compile source → JIT bytes ────────────────────────────
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
    OMNI_LOG("[omnicc build] %zu JIT bytes → %s\n", cg.code.size, outpath);

    // ── Count imports ─────────────────────────────────────────
    int nfn = 0;
    while (k32[nfn]) nfn++;

    // ── idata layout (all offsets relative to idata section start) ──
    //   [0..39]            two 20-byte import descriptors (1 real + null)
    //   [40..iat_end]      IAT:  (nfn+1) * 8 bytes
    //   [iat_end..int_end] ILT:  (nfn+1) * 8 bytes  (original thunks)
    //   [int_end..]        DLL name "KERNEL32.DLL\0"
    //   [..]               Hint/Name entries
    uint32_t idata_virt = 0x2000;
    uint32_t iat_off    = 40;
    uint32_t ilt_off    = iat_off + (uint32_t)(nfn + 1) * 8;
    uint32_t dllname_off= ilt_off + (uint32_t)(nfn + 1) * 8;
    uint32_t hn_off     = dllname_off + 13;  // "KERNEL32.DLL\0" = 13 bytes

    // Compute hint/name RVAs and total idata raw size
    uint32_t hn_rvas[64] = {0};
    uint32_t hn_cur = hn_off;
    for (int i = 0; i < nfn; i++) {
        hn_rvas[i] = idata_virt + hn_cur;
        hn_cur += 2 + (uint32_t)strlen(k32[i]) + 1;
        if (hn_cur & 1) hn_cur++;   // word-align each entry
    }
    uint32_t idata_virt_sz = hn_cur;
    uint32_t idata_raw_sz  = pe_align_up(idata_virt_sz, PE_FILE_ALIGN);

    // ── .text layout ─────────────────────────────────────────
    uint32_t text_virt      = 0x1000;
    uint32_t text_code_sz   = TRAMP_SIZE + (uint32_t)cg.code.size;
    uint32_t text_raw_sz    = pe_align_up(text_code_sz, PE_FILE_ALIGN);

    // ── Scan JIT code for abs64 MOV RAX,imm64 (48 B8) → .reloc ──
    // RVA of JIT code start inside .text section
    uint32_t jit_rva_base = text_virt + TRAMP_SIZE;
    uint32_t reloc_rvas[8192];
    uint32_t nreloc = 0;
    uint8_t* jit = cg.code.data;
    for (uint32_t bi = 0; bi + 10 <= (uint32_t)cg.code.size; bi++) {
        if (jit[bi] == 0x48 && jit[bi+1] == 0xB8) {
            // imm64 is at jit[bi+2..bi+9]
            if (nreloc < 8192)
                reloc_rvas[nreloc++] = jit_rva_base + bi + 2;
        }
    }
    // Also add the abs64 in the trampoline itself (IAT pointer)
    // That's at RVA: text_virt + TRAMP_IAT_OFF
    if (nreloc < 8192)
        reloc_rvas[nreloc++] = text_virt + TRAMP_IAT_OFF;

    // ── Build .reloc binary ───────────────────────────────────
    // Sort RVAs (they're already ascending for JIT, just append tramp)
    // Each block: page RVA (4) + block size (4) + entries (2 each, type 0xA=DIR64)
    uint8_t  reloc_buf[65536];
    uint32_t reloc_sz = 0;
    uint32_t ri = 0;
    while (ri < nreloc) {
        uint32_t page = reloc_rvas[ri] & ~(uint32_t)0xFFF;
        uint32_t bstart = ri;
        while (ri < nreloc && (reloc_rvas[ri] & ~(uint32_t)0xFFF) == page) ri++;
        uint32_t cnt = ri - bstart;
        uint32_t bsz = 8 + cnt * 2;
        if (bsz & 3) bsz += 2;   // DWORD-align block
        if (reloc_sz + bsz > sizeof(reloc_buf)) break;
        memcpy(reloc_buf + reloc_sz, &page, 4); reloc_sz += 4;
        memcpy(reloc_buf + reloc_sz, &bsz,  4); reloc_sz += 4;
        for (uint32_t k = bstart; k < ri; k++) {
            uint16_t e = (uint16_t)(0xA000 | (reloc_rvas[k] & 0xFFF));
            memcpy(reloc_buf + reloc_sz, &e, 2); reloc_sz += 2;
        }
        while (reloc_sz & 3) { memset(reloc_buf + reloc_sz, 0, 2); reloc_sz += 2; }
    }

    // ── Section virtual/file offsets ─────────────────────────
    //   headers:  file 0x000..0x1FF  (512 bytes)
    //   .text:    file 0x200..
    //   .idata:   file after .text
    //   .reloc:   file after .idata
    uint32_t reloc_virt    = idata_virt + pe_align_up(idata_virt_sz, PE_SECT_ALIGN);
    uint32_t reloc_virt_sz = reloc_sz ? reloc_sz : 4;
    uint32_t reloc_raw_sz  = pe_align_up(reloc_virt_sz, PE_FILE_ALIGN);

    uint32_t text_foff   = 0x200;
    uint32_t idata_foff  = text_foff  + text_raw_sz;
    uint32_t reloc_foff  = idata_foff + idata_raw_sz;

    uint32_t image_sz = reloc_virt + pe_align_up(reloc_virt_sz, PE_SECT_ALIGN);

    // ── Open output file ──────────────────────────────────────
    FILE* f = NULL;
    if (fopen_s(&f, outpath, "wb") != 0 || !f) {
        fprintf(stderr, "Error: cannot create '%s'\n", outpath);
        codegen_free(&cg); return 1;
    }

    // ── DOS header + stub ─────────────────────────────────────
    // MZ header fields
    pe_u16(f,0x5A4D); pe_u16(f,0x90); pe_u16(f,3);    pe_u16(f,0);
    pe_u16(f,4);      pe_u16(f,0);    pe_u16(f,0xFFFF);pe_u16(f,0);
    pe_u16(f,0xB8);   pe_u16(f,0);    pe_u16(f,0);     pe_u16(f,0x40);
    pe_u16(f,0);
    for (int i=0;i<10;i++) pe_u16(f,0);  // reserved
    pe_u32(f, 0x80);   // e_lfanew — PE header at offset 0x80
    // Minimal DOS stub: prints "not for DOS" then INT 21h/4C
    static const uint8_t dos_stub[] = {
        0x0E,0x1F,0xBA,0x0E,0x00,0xB4,0x09,0xCD,0x21,0xB8,0x01,0x4C,0xCD,0x21,
        'T','h','i','s',' ','p','r','o','g','r','a','m',' ','c','a','n','n','o',
        't',' ','b','e',' ','r','u','n',' ','i','n',' ','D','O','S',' ','m','o',
        'd','e','.','\r','\r','\n','$',0,0,0,0,0,0,0,0
    };
    fwrite(dos_stub, 1, sizeof(dos_stub), f);
    pe_pad(f, 0x80);

    // ── PE signature + COFF header ────────────────────────────
    pe_u32(f, 0x00004550);   // "PE\0\0"
    pe_u16(f, 0x8664);       // Machine: AMD64
    pe_u16(f, 3);            // NumberOfSections: .text .idata .reloc
    pe_u32(f, (uint32_t)time(NULL)); // TimeDateStamp
    pe_u32(f, 0);            // SymbolTable ptr (none)
    pe_u32(f, 0);            // NumberOfSymbols
    pe_u16(f, 0xF0);         // SizeOfOptionalHeader: 240 (PE32+)
    pe_u16(f, 0x0022);       // Characteristics: IMAGE_FILE_EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE

    // ── Optional header (PE32+, 240 bytes) ───────────────────
    pe_u16(f, 0x020B);          // Magic: PE32+
    pe_u8(f, 14); pe_u8(f, 0);  // Linker version 14.0
    pe_u32(f, text_raw_sz);     // SizeOfCode
    pe_u32(f, idata_raw_sz + reloc_raw_sz); // SizeOfInitializedData
    pe_u32(f, 0);               // SizeOfUninitializedData
    pe_u32(f, text_virt);       // AddressOfEntryPoint = .text RVA
    pe_u32(f, text_virt);       // BaseOfCode
    pe_u64(f, PE_IMAGE_BASE);   // ImageBase
    pe_u32(f, PE_SECT_ALIGN);   // SectionAlignment
    pe_u32(f, PE_FILE_ALIGN);   // FileAlignment
    pe_u16(f, 6); pe_u16(f, 0); // MajorOperatingSystemVersion
    pe_u16(f, 0); pe_u16(f, 0); // ImageVersion
    pe_u16(f, 6); pe_u16(f, 0); // SubsystemVersion (Vista+)
    pe_u32(f, 0);               // Win32VersionValue (reserved, 0)
    pe_u32(f, image_sz);        // SizeOfImage
    pe_u32(f, 0x200);           // SizeOfHeaders
    pe_u32(f, 0);               // CheckSum
    pe_u16(f, 3);               // Subsystem: IMAGE_SUBSYSTEM_WINDOWS_CUI (console)
    pe_u16(f, 0x8160);          // DllCharacteristics: NX_COMPAT|HIGH_ENTROPY_VA|TERMINAL_SERVER_AWARE
    pe_u64(f, 0x100000);        // SizeOfStackReserve
    pe_u64(f, 0x1000);          // SizeOfStackCommit
    pe_u64(f, 0x100000);        // SizeOfHeapReserve
    pe_u64(f, 0x1000);          // SizeOfHeapCommit
    pe_u32(f, 0);               // LoaderFlags
    pe_u32(f, 16);              // NumberOfRvaAndSizes
    // 16 data directories (8 bytes each)
    pe_u32(f,0);pe_u32(f,0);                             // [0] Export
    pe_u32(f,idata_virt);pe_u32(f,idata_virt_sz);        // [1] Import
    pe_u32(f,0);pe_u32(f,0);                             // [2] Resource
    pe_u32(f,0);pe_u32(f,0);                             // [3] Exception
    pe_u32(f,0);pe_u32(f,0);                             // [4] Certificate
    pe_u32(f,reloc_virt);pe_u32(f,reloc_virt_sz);        // [5] BaseReloc
    for (int i=6;i<16;i++){pe_u32(f,0);pe_u32(f,0);}    // [6-15]

    // ── Section table (3 * 40 bytes) ─────────────────────────
    // .text
    fwrite(".text\0\0\0", 1, 8, f);
    pe_u32(f, text_code_sz);  pe_u32(f, text_virt);
    pe_u32(f, text_raw_sz);   pe_u32(f, text_foff);
    pe_u32(f,0);pe_u32(f,0);pe_u16(f,0);pe_u16(f,0);
    pe_u32(f, 0x60000020); // CNT_CODE|MEM_EXECUTE|MEM_READ
    // .idata
    fwrite(".idata\0\0", 1, 8, f);
    pe_u32(f, idata_virt_sz); pe_u32(f, idata_virt);
    pe_u32(f, idata_raw_sz);  pe_u32(f, idata_foff);
    pe_u32(f,0);pe_u32(f,0);pe_u16(f,0);pe_u16(f,0);
    pe_u32(f, 0xC0000040); // CNT_INITIALIZED_DATA|MEM_READ|MEM_WRITE
    // .reloc
    fwrite(".reloc\0\0", 1, 8, f);
    pe_u32(f, reloc_virt_sz); pe_u32(f, reloc_virt);
    pe_u32(f, reloc_raw_sz);  pe_u32(f, reloc_foff);
    pe_u32(f,0);pe_u32(f,0);pe_u16(f,0);pe_u16(f,0);
    pe_u32(f, 0x42000040); // CNT_INITIALIZED_DATA|MEM_DISCARDABLE|MEM_READ

    pe_pad(f, (long)text_foff);

    // ── .text: trampoline + JIT code ─────────────────────────
    // Patch trampoline:
    //   call disp32 = jit_entry_rva - (trampoline_call_end_rva)
    //   trampoline_call_end_rva = text_virt + TRAMP_CALL_OFF + 4 = text_virt + 9
    uint32_t jit_entry_rva = text_virt + TRAMP_SIZE;
    int32_t  call_disp     = (int32_t)(jit_entry_rva - (text_virt + TRAMP_CALL_OFF + 4));
    //   IAT abs64 for ExitProcess = IMAGE_BASE + idata_virt + iat_off + 5*8
    uint32_t exit_iat_rva = idata_virt + iat_off + 5 * 8;
    uint64_t exit_iat_abs = PE_IMAGE_BASE + exit_iat_rva;
    uint8_t tramp[TRAMP_SIZE];
    memcpy(tramp, tramp_template, TRAMP_SIZE);
    memcpy(tramp + TRAMP_CALL_OFF, &call_disp,    4);
    memcpy(tramp + TRAMP_IAT_OFF,  &exit_iat_abs, 8);
    fwrite(tramp, 1, TRAMP_SIZE, f);
    fwrite(cg.code.data, 1, cg.code.size, f);
    pe_pad(f, (long)(text_foff + text_raw_sz));

    // ── .idata: import descriptors + IAT + ILT + strings ─────
    long idata_start = ftell(f);
    uint32_t iat_rva_abs = idata_virt + iat_off;
    uint32_t ilt_rva_abs = idata_virt + ilt_off;
    uint32_t dll_rva_abs = idata_virt + dllname_off;
    // Import descriptor for KERNEL32.DLL
    pe_u32(f, ilt_rva_abs);   // OriginalFirstThunk (ILT)
    pe_u32(f, 0);             // TimeDateStamp
    pe_u32(f, 0);             // ForwarderChain
    pe_u32(f, dll_rva_abs);   // Name RVA
    pe_u32(f, iat_rva_abs);   // FirstThunk (IAT — loader fills this)
    // Null terminator descriptor
    for (int i=0;i<5;i++) pe_u32(f,0);
    // IAT (initially hint/name RVAs; loader replaces with real addresses)
    for (int i=0;i<nfn;i++) pe_u64(f, (uint64_t)hn_rvas[i]);
    pe_u64(f, 0);
    // ILT (original thunks — same as initial IAT)
    for (int i=0;i<nfn;i++) pe_u64(f, (uint64_t)hn_rvas[i]);
    pe_u64(f, 0);
    // DLL name
    fwrite("KERNEL32.DLL", 1, 13, f);  // includes null terminator
    // Hint/Name entries
    for (int i=0;i<nfn;i++) {
        pe_u16(f, (uint16_t)i);      // ordinal hint
        pe_str(f, k32[i]);           // function name + null
        if (ftell(f) & 1) pe_u8(f,0); // word-align
    }
    pe_pad(f, (long)(idata_foff + idata_raw_sz));
    (void)idata_start;

    // ── .reloc ────────────────────────────────────────────────
    fwrite(reloc_buf, 1, reloc_sz, f);
    pe_pad(f, (long)(reloc_foff + reloc_raw_sz));

    long final_size = ftell(f);
    fclose(f);
    codegen_free(&cg);

    fprintf(stderr, "[omnicc build] wrote: %s  (%ld bytes)\n", outpath, final_size);
    OMNI_LOG("[omnicc build] sections: .text=%u  .idata=%u  .reloc=%u\n",
             text_raw_sz, idata_raw_sz, reloc_raw_sz);
    return 0;
}

// ── Entry point ──────────────────────────────────────────────
int main(int argc, char** argv) {
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
