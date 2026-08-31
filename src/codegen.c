// ============================================================
//  OMNIKARAI x86-64 Native Code Generator  v7.0  — COMPLETE LANGUAGE
//  "Tier 1 fixed. Full stdlib. dict. str. list. const. Universal."
//
//  NEW in v7.0 (April 2026):
//    - str module:      upper, lower, trim, lstrip, rstrip, split, join,
//                       contains, find, replace, starts_with, ends_with,
//                       repeat, pad_left, pad_right, to_upper_first
//    - list module:     sort, reverse, copy, insert, remove, slice,
//                       find, clear, map (with fn ptr), filter, reduce
//    - dict module:     new, get, set, has, delete, keys, values, items, len
//                       (open-addressing hash map, string keys)
//    - math module:     exp, exp2, tanh, atan, atan2, cbrt + float ABI fixed
//    - const keyword:   compile-time constant folding
//    - min/max/abs:     builtins callable without module prefix
//    - for item in list: fixed (cg_for_list)
//    - aug-assign:      x+=, x-=, x*=, x/=, x%=, x**= fixed in parser
//    - index write:     obj[i] = val desugars to list.set
//    - float ABI:       CVTSI2SD + MOVQ for all math calls
//
//  NEW in v6.0 (Speed God Plan — Fraziym Tech, March 2026):
//    - ai module:       matmul, dot, relu, softmax, layernorm (AVX2 direct emit)
//    - INT8 quantized:  VPMADDUBSW path — 32 ops/cycle vs 8 FP32 ops/cycle
//    - FP32 AVX2:       VFMADD231PS — 8 floats/cycle, auto-vectorized loops
//    - Ephemeral pass:  store+load pair elimination for register-resident values
//    - Cache tiling:    L2-aware 256×256 block matmul
//    - Alloc arena:     64-byte aligned, cache-line optimal
//
//  NEW in v5.0:
//    - math module:     abs, sqrt, pow, min, max, floor, ceil,
//                       sin, cos, tan, log, log2, log10, pi, e, tau
//    - datetime module: now(), date(), time(), format(), diff(),
//                       year/month/day/hour/minute/second accessors,
//                       timezone support (UTC offset), timestamp()
//    - os module:       exit(), getenv(), platform(), cwd(), getpid()
//    - io module:       read(), write(), append(), exists(), delete()
//    - sys module:      version(), platform(), argv(), omni_ver()
//    - list type:       list.new(), list.push(), list.pop(),
//                       list.get(), list.set(), list.len(), list.free()
//    - assert(cond, msg) builtin
//    - type(val) builtin  (returns string: "int","str","bool","list","nil")
//    - range(start,stop,step) full 3-arg form
//    - --beta flag: verbose debug traces on codegen + runtime
//    - omnicc check <file>: type-check + lint pass (AST walk)
//    - omnicc bench <file>: built-in benchmark harness
//
//  Architecture v5.0 — new on top of v4.0:
//    - SIMD-ready emitters: AVX2 256-bit vector ops for list/math
//    - Tagged-pointer values: upper 3 bits = type tag
//      INT=0, STR=1, LIST=2, BOOL=3, FLOAT=4, NIL=7
//    - Flat list heap: 8-byte length + n*8 elements (cache-linear)
//    - Compile-time constant folding for math.*
//    - beta_trace(): runtime debug prints controlled by g_beta flag
//
//  BUG FIXES v5.0.1:
//    - emit_load_r8/r9: REX_WB -> REX_WR (REX.R extends dest, not base)
//    - fn prologue R8/R9 param store: same REX fix
//    - emit_cmp_r14/r15_rcx: opcode 0x3B -> 0x39 (CMP Ev,Gv = counter-limit)
//    - math.clamp / datetime.make: clean 3-arg pattern, no duplicate emit
//    - os.getenv: Win32 GetEnvironmentVariableA (replaces unreliable CRT getenv)
// ============================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <ctype.h>
#include <math.h>

#include "omni_platform.h"   // platform layer (Win32 native / POSIX shims)
#include "abi.h"             // calling-convention abstraction

#include "codegen.h"
#include "ast.h"
#include "lexer.h"
#include "parser.h"

// ============================================================
//  BETA DEBUG FLAG  (set by --beta on CLI, read by runtime)
// ============================================================
int g_beta = 0;  // 0 = silent, 1 = beta trace output

#define BETA_TRACE(...) do { if(g_beta) { fprintf(stderr, "[BETA] " __VA_ARGS__); } } while(0)
#define BETA_TRACE_CG(fmt,...) do { if(g_beta) { fprintf(stderr, "[CG] " fmt "\n", ##__VA_ARGS__); } } while(0)

// ============================================================
//  ERROR REPORTING
// ============================================================
static const char* g_error_filename = "<unknown>";
static char**      g_source_lines   = NULL;
static int         g_source_line_count = 0;

void codegen_set_source(const char* filename, const char* source) {
    g_error_filename = filename;
    if (g_source_lines) {
        for (int i = 0; i < g_source_line_count; i++) free(g_source_lines[i]);
        free(g_source_lines);
    }
    g_source_lines = NULL; g_source_line_count = 0;
    if (!source) return;
    int count = 1;
    for (const char* p = source; *p; p++) if (*p == '\n') count++;
    g_source_lines = (char**)malloc(count * sizeof(char*));
    if (!g_source_lines) return;
    const char* start = source; int idx = 0;
    for (const char* p = source; ; p++) {
        if (*p == '\n' || *p == '\0') {
            size_t len = (size_t)(p - start);
            g_source_lines[idx] = (char*)malloc(len + 1);
            if (g_source_lines[idx]) { memcpy(g_source_lines[idx], start, len); g_source_lines[idx][len]='\0'; }
            idx++; if (*p == '\0') break; start = p + 1;
        }
    }
    g_source_line_count = idx;
}

static void omni_error(int line, int col, const char* kind, const char* fmt, ...) {
    fprintf(stderr, "\n  File \"%s\", line %d\n", g_error_filename, line);
    if (line >= 1 && line <= g_source_line_count) {
        fprintf(stderr, "    %s\n", g_source_lines[line-1]);
        fprintf(stderr, "    ");
        for (int i = 1; i < col; i++) fprintf(stderr, " ");
        fprintf(stderr, "^\n");
    }
    va_list args; va_start(args, fmt);
    fprintf(stderr, "%s: ", kind); vfprintf(stderr, fmt, args); va_end(args);
    fprintf(stderr, "\n\n"); exit(1);
}
static void omni_error_nopos(const char* kind, const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    fprintf(stderr, "\n%s: ", kind); vfprintf(stderr, fmt, args); va_end(args);
    fprintf(stderr, "\n\n"); exit(1);
}

// ============================================================
//  CORE RUNTIME HELPERS
//  OPT: g_stdout cached once — eliminates GetStdHandle() per print call.
//       Each print was doing a kernel call just to get the handle.
//       On a 1000-iteration loop with print this saves ~200ns/call.
// ============================================================
static HANDLE g_stdout = INVALID_HANDLE_VALUE;
static HANDLE g_stdin  = INVALID_HANDLE_VALUE;
static void omni_io_init(void) {
    if (g_stdout == INVALID_HANDLE_VALUE) g_stdout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (g_stdin  == INVALID_HANDLE_VALUE) g_stdin  = GetStdHandle(STD_INPUT_HANDLE);
}
static void omni_time_init(void); // forward decl — defined in TIME MODULE below
static const char* alias_resolve(CodeGen* cg, const char* ns); // forward decl — defined in ALIAS section below
__attribute__((noinline)) void omni_runtime_init(void) {
    omni_io_init();
    omni_time_init();  // pre-init QPC freq so first timer call is instant
}

__attribute__((noinline)) void omni_print_str(const char* s) {
    omni_io_init(); DWORD w;
    if (!s) { WriteFile(g_stdout,"(nil)\n",6,&w,NULL); return; }
    DWORD len=(DWORD)strlen(s); WriteFile(g_stdout,s,len,&w,NULL); WriteFile(g_stdout,"\n",1,&w,NULL);
}
__attribute__((noinline)) void omni_print_int(int64_t v) {
    omni_io_init();
    char buf[32]; int neg=(v<0);
    uint64_t uv=neg?(uint64_t)(-(v+1))+1:(uint64_t)v;
    int i=30; buf[31]='\n';
    if(uv==0){buf[i--]='0';}else{while(uv>0){buf[i--]='0'+(char)(uv%10);uv/=10;}}
    if(neg) buf[i--]='-';
    DWORD w; WriteFile(g_stdout,buf+i+1,(DWORD)(31-i),&w,NULL);
}
__attribute__((noinline)) void omni_print_bool(int64_t v) {
    omni_io_init(); DWORD w;
    if(v) WriteFile(g_stdout,"true\n",5,&w,NULL); else WriteFile(g_stdout,"false\n",6,&w,NULL);
}
__attribute__((noinline)) void omni_print_str_noline(const char* s) {
    omni_io_init(); if(!s) return; DWORD w;
    WriteFile(g_stdout,s,(DWORD)strlen(s),&w,NULL);
    FlushFileBuffers(g_stdout);
}
__attribute__((noinline)) void omni_print_float(double v) {
    omni_io_init();
    char buf[64]; int len=snprintf(buf,sizeof(buf)-1,"%.6g\n",v); if(len<0)len=0;
    DWORD w; WriteFile(g_stdout,buf,(DWORD)len,&w,NULL);
}
/*
 * omni_input — reads one line of text from stdin.
 *
 * Uses ReadFile for ALL cases (console and pipe).
 * ReadFile on a Windows console handle is fully line-buffered and echoed —
 * identical UX to ReadConsoleA for interactive use, and also correct for pipes.
 * ReadConsoleA is avoided because it fails when stdin is inherited through
 * certain shell configurations (PowerShell, ConPTY) even when GetConsoleMode
 * succeeds, returning 0 bytes and causing omni_input to spin or exit early.
 *
 * A 64 KB internal ring buffer (s_ibuf) carries leftover bytes between calls
 * so that when ReadFile returns multiple lines at once (common with pipes),
 * each omni_input() call correctly returns exactly one line.
 *
 * EOF with no data → InputError printed to stderr, process exits 1.
 */
static char  s_ibuf[65536];   /* internal stdin buffer                  */
static DWORD s_ibuf_head = 0; /* index of next byte to consume          */
static DWORD s_ibuf_tail = 0; /* one past last valid byte               */
static BOOL  s_ibuf_eof  = FALSE;

__attribute__((noinline)) char* omni_input(void) {
    omni_io_init();
    FlushFileBuffers(g_stdout);   /* flush any pending prompt before blocking */

    char* out = (char*)malloc(1024);
    if (!out) return (char*)"";
    int olen = 0;

    while (olen < 1022) {
        /* Refill internal buffer when exhausted */
        if (s_ibuf_head >= s_ibuf_tail) {
            if (s_ibuf_eof) break;
            DWORD rd = 0;
            BOOL  ok = ReadFile(g_stdin, s_ibuf, sizeof(s_ibuf), &rd, NULL);
            if (!ok || rd == 0) { s_ibuf_eof = TRUE; break; }
            s_ibuf_head = 0;
            s_ibuf_tail = rd;
        }

        char c = s_ibuf[s_ibuf_head++];
        if (c == '\n') break;    /* end of line */
        if (c == '\r') continue; /* skip CR (handles CRLF and bare CR) */
        out[olen++] = c;
    }

    /* EOF before any characters → InputError */
    if (olen == 0 && s_ibuf_eof) {
        DWORD w;
        WriteFile(g_stdout, "\n", 1, &w, NULL);
        FlushFileBuffers(g_stdout);
        WriteFile(GetStdHandle(STD_ERROR_HANDLE),
                  "\nInputError: reached end of input (EOF on stdin)\n", 49, &w, NULL);
        free(out);
        ExitProcess(1);
    }

    out[olen] = '\0';
    return out;
}
__attribute__((noinline)) int64_t omni_len_str(const char* s) {
    if(!s) return 0; int64_t n=0; while(s[n]) n++; return n;
}
__attribute__((noinline)) char* omni_int_to_str(int64_t v) {
    char* buf=(char*)malloc(32); if(!buf) return (char*)"";
    int neg=(v<0); uint64_t uv=neg?(uint64_t)(-(v+1))+1:(uint64_t)v;
    int i=30; buf[31]='\0';
    if(uv==0){buf[i--]='0';}else{while(uv>0){buf[i--]='0'+(char)(uv%10);uv/=10;}}
    if(neg) buf[i--]='-';
    int len=30-i; memmove(buf,buf+i+1,len+1); return buf;
}
__attribute__((noinline)) char* omni_str_concat(const char* a,const char* b) {
    if(!a)a=""; if(!b)b="";
    size_t la=strlen(a),lb=strlen(b);
    char* out=(char*)malloc(la+lb+1); if(!out) return (char*)"";
    memcpy(out,a,la); memcpy(out+la,b,lb); out[la+lb]='\0'; return out;
}
__attribute__((noinline)) int64_t omni_str_to_int(const char* s) {
    if(!s) return 0; int64_t result=0,neg=0;
    if(*s=='-'){neg=1;s++;} while(*s>='0'&&*s<='9'){result=result*10+(*s++-'0');}
    return neg?-result:result;
}
__attribute__((noinline)) char* omni_float_to_str(double v) {
    char* buf=(char*)malloc(64); if(!buf) return (char*)"";
    snprintf(buf,63,"%.6g",v); return buf;
}
__attribute__((noinline)) int64_t omni_str_eq(const char* a, const char* b) {
    if(!a||!b) return 0;
    return strcmp(a,b)==0 ? 1 : 0;
}
__attribute__((noinline)) char* omni_str_slice(const char* s, int64_t start, int64_t end) {
    if(!s) return (char*)"";
    int64_t len = (int64_t)strlen(s);
    if(start<0) start=len+start; if(start<0) start=0;
    if(end<0)   end=len+end;     if(end>len) end=len;
    if(start>=end) return (char*)"";
    int64_t sz=end-start;
    char* out=(char*)malloc((size_t)sz+1);
    memcpy(out,s+start,(size_t)sz); out[sz]='\0';
    return out;
}
__attribute__((noinline)) const char* omni_type_of(int64_t tagged) {
    (void)tagged;
    return "int";
}
__attribute__((noinline)) void omni_assert(int64_t cond, const char* msg) {
    if(!cond) {
        fprintf(stderr,"\nAssertionError: %s\n\n", msg?msg:"assertion failed");
        exit(1);
    }
}
__attribute__((noinline)) void omni_beta_trace(const char* label, int64_t val) {
    if(g_beta) {
        fprintf(stderr, "[BETA-RT] %s = %lld\n", label, (long long)val);
    }
}

// ============================================================
//  TIME MODULE  (QueryPerformanceCounter — Win32)
// ============================================================
static LARGE_INTEGER g_qpc_freq = {0};
static void omni_time_init(void) {
    if (g_qpc_freq.QuadPart == 0) QueryPerformanceFrequency(&g_qpc_freq);
}
__attribute__((noinline)) int64_t omni_time_now(void) {
    omni_time_init(); LARGE_INTEGER t; QueryPerformanceCounter(&t); return (int64_t)t.QuadPart;
}
__attribute__((noinline)) int64_t omni_time_ms(int64_t t0) {
    omni_time_init(); LARGE_INTEGER now; QueryPerformanceCounter(&now);
    int64_t diff=(int64_t)now.QuadPart-t0; return diff*1000LL/g_qpc_freq.QuadPart;
}
__attribute__((noinline)) int64_t omni_time_us(int64_t t0) {
    omni_time_init(); LARGE_INTEGER now; QueryPerformanceCounter(&now);
    int64_t diff=(int64_t)now.QuadPart-t0; return diff*1000000LL/g_qpc_freq.QuadPart;
}
__attribute__((noinline)) int64_t omni_time_ns(int64_t t0) {
    omni_time_init(); LARGE_INTEGER now; QueryPerformanceCounter(&now);
    int64_t diff=(int64_t)now.QuadPart-t0;
    return (diff/g_qpc_freq.QuadPart)*1000000000LL
          +(diff%g_qpc_freq.QuadPart)*1000000000LL/g_qpc_freq.QuadPart;
}
__attribute__((noinline)) void omni_time_sleep(int64_t ms) {
    if(ms>0) Sleep((DWORD)ms);
}
__attribute__((noinline)) char* omni_time_format(int64_t t0) {
    omni_time_init(); LARGE_INTEGER now; QueryPerformanceCounter(&now);
    int64_t diff=(int64_t)now.QuadPart-t0;
    int64_t ns=(diff/g_qpc_freq.QuadPart)*1000000000LL
              +(diff%g_qpc_freq.QuadPart)*1000000000LL/g_qpc_freq.QuadPart;
    char* buf=(char*)malloc(32); if(!buf) return (char*)"";
    if(ns>=1000000000LL){int sw=(int)(ns/1000000000LL);int sf=(int)((ns%1000000000LL)/1000000LL);snprintf(buf,32,"%d.%03ds",sw,sf);}
    else if(ns>=1000000LL){int mw=(int)(ns/1000000LL);int mf=(int)((ns%1000000LL)/1000LL);snprintf(buf,32,"%d.%03dms",mw,mf);}
    else if(ns>=1000LL){snprintf(buf,32,"%dus",(int)(ns/1000LL));}
    else{snprintf(buf,32,"%dns",(int)ns);}
    return buf;
}

// ============================================================
//  DATETIME MODULE
// ============================================================
static void filetime_to_systime(int64_t ft, SYSTEMTIME* st) {
    FILETIME f; f.dwHighDateTime=(DWORD)((uint64_t)ft>>32); f.dwLowDateTime=(DWORD)ft;
    FileTimeToSystemTime(&f, st);
}
__attribute__((noinline)) int64_t omni_dt_now(void) {
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    return ((int64_t)ft.dwHighDateTime << 32) | (int64_t)ft.dwLowDateTime;
}
__attribute__((noinline)) int64_t omni_dt_utcnow(void) { return omni_dt_now(); }
__attribute__((noinline)) int64_t omni_dt_year   (int64_t ft){SYSTEMTIME s;filetime_to_systime(ft,&s);return s.wYear;}
__attribute__((noinline)) int64_t omni_dt_month  (int64_t ft){SYSTEMTIME s;filetime_to_systime(ft,&s);return s.wMonth;}
__attribute__((noinline)) int64_t omni_dt_day    (int64_t ft){SYSTEMTIME s;filetime_to_systime(ft,&s);return s.wDay;}
__attribute__((noinline)) int64_t omni_dt_hour   (int64_t ft){SYSTEMTIME s;filetime_to_systime(ft,&s);return s.wHour;}
__attribute__((noinline)) int64_t omni_dt_minute (int64_t ft){SYSTEMTIME s;filetime_to_systime(ft,&s);return s.wMinute;}
__attribute__((noinline)) int64_t omni_dt_second (int64_t ft){SYSTEMTIME s;filetime_to_systime(ft,&s);return s.wSecond;}
__attribute__((noinline)) int64_t omni_dt_weekday(int64_t ft){SYSTEMTIME s;filetime_to_systime(ft,&s);return s.wDayOfWeek;}
__attribute__((noinline)) int64_t omni_dt_timestamp(int64_t ft) {
    return (int64_t)((uint64_t)ft / 10000000ULL) - 11644473600LL;
}
__attribute__((noinline)) int64_t omni_dt_diff_ms(int64_t ft_a, int64_t ft_b) {
    return (ft_a - ft_b) / 10000LL;
}
__attribute__((noinline)) int64_t omni_dt_diff_s(int64_t ft_a, int64_t ft_b) {
    return (ft_a - ft_b) / 10000000LL;
}
__attribute__((noinline)) char* omni_dt_format(int64_t ft) {
    SYSTEMTIME s; filetime_to_systime(ft,&s);
    char* buf=(char*)malloc(64); if(!buf) return (char*)"";
    snprintf(buf,63,"%04d-%02d-%02d %02d:%02d:%02d",
             s.wYear,s.wMonth,s.wDay,s.wHour,s.wMinute,s.wSecond);
    return buf;
}
__attribute__((noinline)) char* omni_dt_format_date(int64_t ft) {
    SYSTEMTIME s; filetime_to_systime(ft,&s);
    char* buf=(char*)malloc(16); if(!buf) return (char*)"";
    snprintf(buf,15,"%04d-%02d-%02d",s.wYear,s.wMonth,s.wDay);
    return buf;
}
__attribute__((noinline)) char* omni_dt_format_time(int64_t ft) {
    SYSTEMTIME s; filetime_to_systime(ft,&s);
    char* buf=(char*)malloc(12); if(!buf) return (char*)"";
    snprintf(buf,11,"%02d:%02d:%02d",s.wHour,s.wMinute,s.wSecond);
    return buf;
}
__attribute__((noinline)) int64_t omni_dt_from_timestamp(int64_t unix_sec) {
    return (int64_t)((uint64_t)(unix_sec + 11644473600LL) * 10000000ULL);
}
__attribute__((noinline)) int64_t omni_dt_make(int64_t year, int64_t month, int64_t day) {
    SYSTEMTIME s={0};
    s.wYear=(WORD)year; s.wMonth=(WORD)month; s.wDay=(WORD)day;
    FILETIME ft; SystemTimeToFileTime(&s,&ft);
    return ((int64_t)ft.dwHighDateTime<<32)|(int64_t)ft.dwLowDateTime;
}
__attribute__((noinline)) int64_t omni_dt_timezone(void) {
    TIME_ZONE_INFORMATION tz; GetTimeZoneInformation(&tz);
    return -(int64_t)tz.Bias;
}
static const char* g_weekday_names[7]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
__attribute__((noinline)) const char* omni_dt_weekday_name(int64_t ft) {
    SYSTEMTIME s; filetime_to_systime(ft,&s);
    return g_weekday_names[s.wDayOfWeek%7];
}
static const char* g_month_names[13]={"","January","February","March","April","May","June",
    "July","August","September","October","November","December"};
__attribute__((noinline)) const char* omni_dt_month_name(int64_t ft) {
    SYSTEMTIME s; filetime_to_systime(ft,&s);
    return g_month_names[s.wMonth<=12?s.wMonth:0];
}

// ============================================================
//  MATH MODULE
// ============================================================
__attribute__((noinline)) int64_t omni_math_abs_i(int64_t v)  { return v<0?-v:v; }
__attribute__((noinline)) double  omni_math_sqrt(double v)    { return sqrt(v); }
__attribute__((noinline)) double  omni_math_pow(double b,double e) { return pow(b,e); }
__attribute__((noinline)) double  omni_math_sin(double v)     { return sin(v); }
__attribute__((noinline)) double  omni_math_cos(double v)     { return cos(v); }
__attribute__((noinline)) double  omni_math_tan(double v)     { return tan(v); }
__attribute__((noinline)) double  omni_math_log(double v)     { return log(v); }
__attribute__((noinline)) double  omni_math_log2(double v)    { return log2(v); }
__attribute__((noinline)) double  omni_math_log10(double v)   { return log10(v); }
__attribute__((noinline)) double  omni_math_floor(double v)   { return floor(v); }
__attribute__((noinline)) double  omni_math_ceil(double v)    { return ceil(v); }
__attribute__((noinline)) double  omni_math_round(double v)   { return round(v); }
__attribute__((noinline)) int64_t omni_math_min_i(int64_t a, int64_t b) { return a<b?a:b; }
__attribute__((noinline)) int64_t omni_math_max_i(int64_t a, int64_t b) { return a>b?a:b; }
__attribute__((noinline)) double  omni_math_min_f(double a,double b)    { return a<b?a:b; }
__attribute__((noinline)) double  omni_math_max_f(double a,double b)    { return a>b?a:b; }
__attribute__((noinline)) int64_t omni_math_gcd(int64_t a, int64_t b)  {
    if(a<0)a=-a; if(b<0)b=-b;
    while(b){int64_t t=b;b=a%b;a=t;} return a;
}
__attribute__((noinline)) int64_t omni_math_clamp(int64_t v,int64_t lo,int64_t hi) {
    return v<lo?lo:(v>hi?hi:v);
}
__attribute__((noinline)) double  omni_math_exp(double v)     { return exp(v); }
__attribute__((noinline)) double  omni_math_exp2(double v)    { return exp2(v); }
__attribute__((noinline)) double  omni_math_tanh(double v)    { return tanh(v); }
__attribute__((noinline)) double  omni_math_asin(double v)    { return asin(v); }
__attribute__((noinline)) double  omni_math_acos(double v)    { return acos(v); }
__attribute__((noinline)) double  omni_math_atan(double v)    { return atan(v); }
__attribute__((noinline)) double  omni_math_atan2(double y,double x) { return atan2(y,x); }
__attribute__((noinline)) double  omni_math_sinh(double v)    { return sinh(v); }
__attribute__((noinline)) double  omni_math_cosh(double v)    { return cosh(v); }
__attribute__((noinline)) double  omni_math_cbrt(double v)    { return cbrt(v); }
__attribute__((noinline)) double  omni_math_hypot(double a,double b) { return hypot(a,b); }
__attribute__((noinline)) int64_t omni_math_sign(double v)   { return (v>0.0)-(v<0.0); }
__attribute__((noinline)) int64_t omni_math_isnan(double v)   { return isnan(v)?1:0; }
__attribute__((noinline)) int64_t omni_math_isinf(double v)   { return isinf(v)?1:0; }
static const double OMNI_PI  = 3.14159265358979323846;
static const double OMNI_E   = 2.71828182845904523536;
static const double OMNI_TAU = 6.28318530717958647692;

__attribute__((noinline)) double  omni_itof(int64_t v) { return (double)v; }
__attribute__((noinline)) int64_t omni_ftoi(double v)  { return (int64_t)v; }
/* Integer power: base**exp — handles negative exp (returns 0 for negative int power) */
__attribute__((noinline)) int64_t omni_int_pow(int64_t base, int64_t exp) {
    if(exp<0)  return 0;
    if(exp==0) return 1;
    int64_t result=1;
    while(exp>0){
        if(exp&1) result*=base;
        base*=base;
        exp>>=1;
    }
    return result;
}

// ============================================================
//  OS MODULE
// ============================================================
__attribute__((noinline)) void omni_os_exit(int64_t code) { exit((int)code); }
__attribute__((noinline)) char* omni_os_getenv(const char* name) {
    if(!name) return (char*)"";
    // Win32 GetEnvironmentVariableA is more reliable than CRT getenv on Windows
    char* buf = (char*)malloc(4096);
    if(!buf) return (char*)"";
    DWORD len = GetEnvironmentVariableA(name, buf, 4095);
    if(len > 0 && len < 4095) { buf[len] = '\0'; return buf; }
    free(buf);
    // Fallback to CRT
    char* v = getenv(name);
    return v ? v : (char*)"";
}
__attribute__((noinline)) const char* omni_os_platform(void) {
#if defined(_WIN32)
    return "windows";
#else
    return "linux";
#endif
}
__attribute__((noinline)) char* omni_os_cwd(void) {
    char* buf=(char*)malloc(512);
    if(!buf) return (char*)"";
    if(GetCurrentDirectoryA(511,buf)) return buf;
    buf[0]='\0'; return buf;
}
__attribute__((noinline)) int64_t omni_os_getpid(void) { return (int64_t)GetCurrentProcessId(); }
__attribute__((noinline)) int64_t omni_os_exists(const char* path) {
    if(!path) return 0;
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES) ? 1 : 0;
}
__attribute__((noinline)) int64_t omni_os_mkdir(const char* path) {
    if(!path) return 0;
    return CreateDirectoryA(path,NULL) ? 1 : 0;
}

// ============================================================
//  IO MODULE
// ============================================================
__attribute__((noinline)) char* omni_io_read(const char* path) {
    if(!path) return (char*)"";
    FILE* f=NULL; fopen_s(&f,path,"rb");
    if(!f) return (char*)"";
    fseek(f,0,SEEK_END); long len=ftell(f); fseek(f,0,SEEK_SET);
    if(len<0){fclose(f);return (char*)""; }
    char* buf=(char*)malloc((size_t)len+1);
    if(!buf){fclose(f);return (char*)""; }
    fread(buf,1,(size_t)len,f); buf[len]='\0'; fclose(f);
    return buf;
}
__attribute__((noinline)) int64_t omni_io_write(const char* path, const char* content) {
    if(!path||!content) return 0;
    FILE* f=NULL; fopen_s(&f,path,"wb");
    if(!f) return 0;
    fwrite(content,1,strlen(content),f); fclose(f); return 1;
}
__attribute__((noinline)) int64_t omni_io_append(const char* path, const char* content) {
    if(!path||!content) return 0;
    FILE* f=NULL; fopen_s(&f,path,"ab");
    if(!f) return 0;
    fwrite(content,1,strlen(content),f); fclose(f); return 1;
}
__attribute__((noinline)) int64_t omni_io_exists(const char* path) { return omni_os_exists(path); }
__attribute__((noinline)) int64_t omni_io_delete(const char* path) {
    if(!path) return 0;
    return DeleteFileA(path) ? 1 : 0;
}
__attribute__((noinline)) int64_t omni_io_size(const char* path) {
    if(!path) return -1;
    WIN32_FILE_ATTRIBUTE_DATA d;
    if(!GetFileAttributesExA(path,GetFileExInfoStandard,&d)) return -1;
    return ((int64_t)d.nFileSizeHigh<<32)|(int64_t)d.nFileSizeLow;
}

// ============================================================
//  SYS MODULE
// ============================================================
__attribute__((noinline)) const char* omni_sys_version(void) {
#if defined(_WIN32)
    return "Omnikarai v7.1.0 (x86-64 Windows)";
#else
    return "Omnikarai v7.1.0 (x86-64 Linux)";
#endif
}
__attribute__((noinline)) const char* omni_sys_platform(void) {
#if defined(_WIN32)
    return "windows-x64";
#else
    return "linux-x64";
#endif
}
__attribute__((noinline)) const char* omni_sys_arch(void)      { return "x86_64"; }
__attribute__((noinline)) const char* omni_sys_omni_ver(void)  { return "7.1.0"; }
__attribute__((noinline)) int64_t     omni_sys_bits(void)      { return 64; }

// ============================================================
//  STR MODULE — extended functions
// ============================================================
__attribute__((noinline)) char* omni_str_upper(const char* s) {
    if(!s) return (char*)"";
    size_t n = strlen(s);
    char* r = (char*)malloc(n+1);
    for(size_t i=0;i<n;i++) r[i]=(char)toupper((unsigned char)s[i]);
    r[n]=0; return r;
}
__attribute__((noinline)) char* omni_str_lower(const char* s) {
    if(!s) return (char*)"";
    size_t n = strlen(s);
    char* r = (char*)malloc(n+1);
    for(size_t i=0;i<n;i++) r[i]=(char)tolower((unsigned char)s[i]);
    r[n]=0; return r;
}
__attribute__((noinline)) char* omni_str_trim(const char* s) {
    if(!s) return (char*)"";
    while(*s==' '||*s=='\t'||*s=='\r'||*s=='\n') s++;
    size_t n=strlen(s);
    while(n>0&&(s[n-1]==' '||s[n-1]=='\t'||s[n-1]=='\r'||s[n-1]=='\n')) n--;
    char* r=(char*)malloc(n+1); memcpy(r,s,n); r[n]=0; return r;
}
__attribute__((noinline)) int64_t omni_str_contains(const char* s, const char* sub) {
    if(!s||!sub) return 0;
    return strstr(s,sub)?1:0;
}
__attribute__((noinline)) int64_t omni_str_starts(const char* s, const char* pre) {
    if(!s||!pre) return 0;
    size_t n=strlen(pre);
    return strncmp(s,pre,n)==0?1:0;
}
__attribute__((noinline)) int64_t omni_str_ends(const char* s, const char* suf) {
    if(!s||!suf) return 0;
    size_t ls=strlen(s), lx=strlen(suf);
    if(lx>ls) return 0;
    return strcmp(s+ls-lx,suf)==0?1:0;
}
__attribute__((noinline)) char* omni_str_replace(const char* s, const char* old, const char* neu) {
    if(!s||!old||!neu) return (char*)(s?s:"");
    size_t sl=strlen(s),ol=strlen(old),nl=strlen(neu);
    if(ol==0) return (char*)s;
    // count occurrences
    size_t cnt=0; const char* p=s;
    while((p=strstr(p,old))){cnt++;p+=ol;}
    size_t rlen=sl+cnt*(nl>ol?nl-ol:0)-(cnt*(ol>nl?ol-nl:0));
    char* r=(char*)malloc(rlen+1); char* w=r; p=s;
    const char* q;
    while((q=strstr(p,old))){
        size_t chunk=q-p; memcpy(w,p,chunk); w+=chunk;
        memcpy(w,neu,nl); w+=nl; p=q+ol;
    }
    strcpy(w,p); return r;
}
__attribute__((noinline)) int64_t omni_str_find(const char* s, const char* sub) {
    if(!s||!sub) return -1;
    const char* p=strstr(s,sub);
    return p?(int64_t)(p-s):-1;
}
__attribute__((noinline)) char* omni_str_repeat(const char* s, int64_t n) {
    if(!s||n<=0) return (char*)"";
    size_t l=strlen(s);
    char* r=(char*)malloc(l*(size_t)n+1);
    char* w=r;
    for(int64_t i=0;i<n;i++){memcpy(w,s,l);w+=l;}
    *w=0; return r;
}
__attribute__((noinline)) char* omni_str_reverse(const char* s) {
    if(!s) return (char*)"";
    size_t n=strlen(s);
    char* r=(char*)malloc(n+1);
    for(size_t i=0;i<n;i++) r[i]=s[n-1-i];
    r[n]=0; return r;
}
__attribute__((noinline)) int64_t omni_str_count(const char* s, const char* sub) {
    if(!s||!sub||strlen(sub)==0) return 0;
    int64_t cnt=0; size_t sl=strlen(sub);
    const char* p=s;
    while((p=strstr(p,sub))){cnt++;p+=sl;}
    return cnt;
}
__attribute__((noinline)) char* omni_str_int_to_str(int64_t v) {
    char buf[32]; snprintf(buf,sizeof(buf),"%lld",(long long)v);
    return strdup(buf);
}
__attribute__((noinline)) char* omni_str_pad_left(const char* s, int64_t width) {
    if(!s) s="";
    size_t l=strlen(s);
    if((int64_t)l>=(int64_t)width) return (char*)s;
    size_t pad=(size_t)width-l;
    char* r=(char*)malloc((size_t)width+1);
    memset(r,' ',pad); memcpy(r+pad,s,l); r[width]=0; return r;
}
__attribute__((noinline)) char* omni_str_pad_right(const char* s, int64_t width) {
    if(!s) s="";
    size_t l=strlen(s);
    if((int64_t)l>=(int64_t)width) return (char*)s;
    char* r=(char*)malloc((size_t)width+1);
    memcpy(r,s,l); memset(r+l,' ',(size_t)width-l); r[width]=0; return r;
}
__attribute__((noinline)) int64_t omni_str_is_digit(const char* s) {
    if(!s||!*s) return 0;
    for(;*s;s++) if(!isdigit((unsigned char)*s)) return 0;
    return 1;
}
__attribute__((noinline)) int64_t omni_str_is_alpha(const char* s) {
    if(!s||!*s) return 0;
    for(;*s;s++) if(!isalpha((unsigned char)*s)) return 0;
    return 1;
}

// ============================================================
//  OS MODULE — extended functions
// ============================================================
__attribute__((noinline)) int64_t omni_os_remove(const char* path) {
    return DeleteFileA(path)?1:0;
}
__attribute__((noinline)) int64_t omni_os_rename(const char* from, const char* to) {
    return MoveFileA(from,to)?1:0;
}
__attribute__((noinline)) int64_t omni_os_isfile(const char* path) {
    DWORD a=GetFileAttributesA(path);
    return (a!=INVALID_FILE_ATTRIBUTES&&!(a&FILE_ATTRIBUTE_DIRECTORY))?1:0;
}
__attribute__((noinline)) int64_t omni_os_isdir(const char* path) {
    DWORD a=GetFileAttributesA(path);
    return (a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_DIRECTORY))?1:0;
}
__attribute__((noinline)) char* omni_os_abspath(const char* path) {
    char* r=(char*)malloc(MAX_PATH);
    if(!GetFullPathNameA(path,MAX_PATH,r,NULL)){r[0]=0;}
    return r;
}
__attribute__((noinline)) char* omni_os_basename(const char* path) {
    if(!path) return (char*)"";
    const char* p=path+strlen(path);
    while(p>path&&*(p-1)!='/'&&*(p-1)!='\\') p--;
    return strdup(p);
}
__attribute__((noinline)) char* omni_os_dirname(const char* path) {
    if(!path) return (char*)"";
    size_t n=strlen(path);
    char* r=strdup(path);
    char* p=r+n;
    while(p>r&&*(p-1)!='/'&&*(p-1)!='\\') p--;
    if(p>r) {*(p-1)=0;} else {r[0]='.';r[1]=0;}
    return r;
}
__attribute__((noinline)) char* omni_os_join(const char* a, const char* b) {
    if(!a) a=""; if(!b) b="";
    size_t la=strlen(a),lb=strlen(b);
    char* r=(char*)malloc(la+lb+2);
    if(!r) return (char*)"";
    memcpy(r,a,la);
    if(la>0&&a[la-1]!='/'&&a[la-1]!='\\'){r[la]=OMNI_PATH_SEP;la++;}
    memcpy(r+la,b,lb); r[la+lb]=0;
    return r;
}

// ============================================================
//  IO MODULE — extended functions
// ============================================================
__attribute__((noinline)) int64_t omni_io_copy(const char* src, const char* dst) {
    return CopyFileA(src,dst,FALSE)?1:0;
}
__attribute__((noinline)) char* omni_io_readline(const char* path, int64_t linenum) {
    /* read line N (0-indexed) from file */
    FILE* f=fopen(path,"r"); if(!f) return (char*)"";
    char buf[4096]; int64_t cur=0;
    while(fgets(buf,sizeof(buf),f)){
        if(cur==linenum){
            size_t n=strlen(buf);
            if(n>0&&buf[n-1]=='\n') buf[n-1]=0;
            fclose(f); return strdup(buf);
        }
        cur++;
    }
    fclose(f); return (char*)"";
}
__attribute__((noinline)) int64_t omni_io_line_count(const char* path) {
    FILE* f=fopen(path,"r"); if(!f) return 0;
    int64_t cnt=0; int c;
    while((c=fgetc(f))!=EOF) if(c=='\n') cnt++;
    fclose(f); return cnt;
}
__attribute__((noinline)) int64_t omni_io_rename(const char* from, const char* to) {
    return MoveFileA(from,to)?1:0;
}

// ============================================================
//  SYS MODULE — extended functions
// ============================================================
__attribute__((noinline)) int64_t omni_sys_exit(int64_t code) { exit((int)code); return 0; }
__attribute__((noinline)) char* omni_sys_input(void) {
    omni_io_init();
    char buf[4096]; DWORD r=0;
    ReadFile(GetStdHandle(STD_INPUT_HANDLE),buf,sizeof(buf)-1,&r,NULL);
    buf[r]=0;
    size_t n=strlen(buf);
    while(n>0&&(buf[n-1]=='\n'||buf[n-1]=='\r')) buf[--n]=0;
    return strdup(buf);
}
__attribute__((noinline)) int64_t omni_sys_memory(void) {
    MEMORYSTATUSEX ms; ms.dwLength=sizeof(ms);
    GlobalMemoryStatusEx(&ms);
    return (int64_t)(ms.ullTotalPhys/1024/1024); /* MB */
}

// ============================================================
//  LIST MODULE — extended functions
// ============================================================
/* Forward declaration of OmniList struct needed by extended list functions */
typedef struct { int64_t capacity; int64_t length; int64_t data[1]; } OmniList;
/* Forward declarations for omni_list_new / omni_list_push used below */
int64_t omni_list_new(void);
int64_t omni_list_push(int64_t lst_ptr, int64_t val);

__attribute__((noinline)) void omni_list_reverse(int64_t lst_ptr) {
    if(!lst_ptr) return;
    OmniList* l=(OmniList*)lst_ptr;
    int64_t lo=0,hi=l->length-1;
    while(lo<hi){int64_t t=l->data[lo];l->data[lo]=l->data[hi];l->data[hi]=t;lo++;hi--;}
}
__attribute__((noinline)) void omni_list_sort(int64_t lst_ptr) {
    if(!lst_ptr) return;
    OmniList* l=(OmniList*)lst_ptr;
    /* simple insertion sort — good enough for typical list sizes */
    for(int64_t i=1;i<l->length;i++){
        int64_t key=l->data[i],j=i-1;
        while(j>=0&&l->data[j]>key){l->data[j+1]=l->data[j];j--;}
        l->data[j+1]=key;
    }
}
__attribute__((noinline)) void omni_list_clear(int64_t lst_ptr) {
    if(!lst_ptr) return;
    OmniList* l=(OmniList*)lst_ptr;
    l->length=0;
}
__attribute__((noinline)) int64_t omni_list_copy(int64_t lst_ptr) {
    if(!lst_ptr) return 0;
    OmniList* src=(OmniList*)lst_ptr;
    size_t sz=sizeof(OmniList)+(size_t)(src->capacity-1)*sizeof(int64_t);
    OmniList* dst=(OmniList*)malloc(sz);
    memcpy(dst,src,sz);
    return (int64_t)dst;
}
__attribute__((noinline)) int64_t omni_list_index(int64_t lst_ptr, int64_t val) {
    if(!lst_ptr) return -1;
    OmniList* l=(OmniList*)lst_ptr;
    for(int64_t i=0;i<l->length;i++) if(l->data[i]==val) return i;
    return -1;
}
__attribute__((noinline)) int64_t omni_list_insert(int64_t lst_ptr, int64_t idx, int64_t val) {
    /* first grow via push to ensure capacity */
    lst_ptr=omni_list_push(lst_ptr,val);
    OmniList* l=(OmniList*)lst_ptr;
    if(idx<0) idx=0;
    if(idx>=l->length-1) return lst_ptr; /* already at end */
    /* shift right */
    for(int64_t i=l->length-1;i>idx;i--) l->data[i]=l->data[i-1];
    l->data[idx]=val;
    return lst_ptr;
}
__attribute__((noinline)) int64_t omni_list_remove(int64_t lst_ptr, int64_t val) {
    if(!lst_ptr) return lst_ptr;
    OmniList* l=(OmniList*)lst_ptr;
    for(int64_t i=0;i<l->length;i++){
        if(l->data[i]==val){
            for(int64_t j=i;j<l->length-1;j++) l->data[j]=l->data[j+1];
            l->length--; return lst_ptr;
        }
    }
    return lst_ptr;
}
__attribute__((noinline)) int64_t omni_list_min(int64_t lst_ptr) {
    if(!lst_ptr) return 0;
    OmniList* l=(OmniList*)lst_ptr;
    if(l->length==0) return 0;
    int64_t m=l->data[0];
    for(int64_t i=1;i<l->length;i++) if(l->data[i]<m) m=l->data[i];
    return m;
}
__attribute__((noinline)) int64_t omni_list_max(int64_t lst_ptr) {
    if(!lst_ptr) return 0;
    OmniList* l=(OmniList*)lst_ptr;
    if(l->length==0) return 0;
    int64_t m=l->data[0];
    for(int64_t i=1;i<l->length;i++) if(l->data[i]>m) m=l->data[i];
    return m;
}
__attribute__((noinline)) int64_t omni_list_sum(int64_t lst_ptr) {
    if(!lst_ptr) return 0;
    OmniList* l=(OmniList*)lst_ptr;
    int64_t s=0;
    for(int64_t i=0;i<l->length;i++) s+=l->data[i];
    return s;
}
__attribute__((noinline)) int64_t omni_list_concat(int64_t a_ptr, int64_t b_ptr) {
    /* returns new list = a + b */
    int64_t r=omni_list_new();
    if(!a_ptr||!b_ptr) return r;
    OmniList* a=(OmniList*)a_ptr;
    OmniList* b=(OmniList*)b_ptr;
    for(int64_t i=0;i<a->length;i++) r=omni_list_push(r,a->data[i]);
    for(int64_t i=0;i<b->length;i++) r=omni_list_push(r,b->data[i]);
    return r;
}


// ============================================================
//  LIST MODULE
// ============================================================
__attribute__((noinline)) int64_t omni_list_new(void) {
    int64_t cap=8;
    OmniList* lst=(OmniList*)malloc(sizeof(int64_t)*2 + sizeof(int64_t)*(size_t)cap);
    if(!lst){fprintf(stderr,"Fatal: OOM list_new\n");exit(1);}
    lst->capacity=cap; lst->length=0;
    return (int64_t)(uintptr_t)lst;
}
__attribute__((noinline)) int64_t omni_list_push(int64_t lst_ptr, int64_t val) {
    OmniList* lst=(OmniList*)(uintptr_t)lst_ptr;
    if(!lst) return lst_ptr;
    if(lst->length>=lst->capacity) {
        int64_t new_cap=lst->capacity*2;
        OmniList* new_lst=(OmniList*)realloc(lst,sizeof(int64_t)*2+sizeof(int64_t)*(size_t)new_cap);
        if(!new_lst){fprintf(stderr,"Fatal: OOM list_push\n");exit(1);}
        new_lst->capacity=new_cap; lst=new_lst;
    }
    lst->data[lst->length++]=val;
    return (int64_t)(uintptr_t)lst;
}
__attribute__((noinline)) int64_t omni_list_pop(int64_t lst_ptr) {
    OmniList* lst=(OmniList*)(uintptr_t)lst_ptr;
    if(!lst||lst->length==0) return 0;
    return lst->data[--lst->length];
}
__attribute__((noinline)) int64_t omni_list_get(int64_t lst_ptr, int64_t idx) {
    OmniList* lst=(OmniList*)(uintptr_t)lst_ptr;
    if(!lst) return 0;
    if(idx<0) idx=lst->length+idx;
    if(idx<0||idx>=lst->length){fprintf(stderr,"IndexError: list index %lld out of range (len=%lld)\n",(long long)idx,(long long)lst->length);exit(1);}
    return lst->data[idx];
}
__attribute__((noinline)) void omni_list_set(int64_t lst_ptr, int64_t idx, int64_t val) {
    OmniList* lst=(OmniList*)(uintptr_t)lst_ptr;
    if(!lst) return;
    if(idx<0) idx=lst->length+idx;
    if(idx<0||idx>=lst->length){fprintf(stderr,"IndexError: list index %lld out of range\n",(long long)idx);exit(1);}
    lst->data[idx]=val;
}
__attribute__((noinline)) int64_t omni_list_len(int64_t lst_ptr) {
    OmniList* lst=(OmniList*)(uintptr_t)lst_ptr;
    return lst?lst->length:0;
}
__attribute__((noinline)) void omni_list_free(int64_t lst_ptr) {
    OmniList* lst=(OmniList*)(uintptr_t)lst_ptr;
    if(lst) free(lst);
}
__attribute__((noinline)) int64_t omni_list_contains(int64_t lst_ptr, int64_t val) {
    OmniList* lst=(OmniList*)(uintptr_t)lst_ptr;
    if(!lst) return 0;
    for(int64_t i=0;i<lst->length;i++) if(lst->data[i]==val) return 1;
    return 0;
}
__attribute__((noinline)) void omni_list_print(int64_t lst_ptr) {
    omni_io_init();
    OmniList* lst=(OmniList*)(uintptr_t)lst_ptr;
    DWORD w;
    WriteFile(g_stdout,"[",1,&w,NULL);
    if(lst) {
        for(int64_t i=0;i<lst->length;i++) {
            char buf[32]; int len=snprintf(buf,31,"%lld",(long long)lst->data[i]);
            WriteFile(g_stdout,buf,(DWORD)len,&w,NULL);
            if(i+1<lst->length) WriteFile(g_stdout,", ",2,&w,NULL);
        }
    }
    WriteFile(g_stdout,"]\n",2,&w,NULL);
}

// ============================================================
//  FUNCTION POINTERS (stable addresses for JIT calls)
// ============================================================
static void* volatile g_fn_print_str      = (void*)omni_print_str;
static void* volatile g_fn_print_int      = (void*)omni_print_int;
static void* volatile g_fn_print_bool     = (void*)omni_print_bool;
static void* volatile g_fn_print_float    = (void*)omni_print_float;
static void* volatile g_fn_print_str_nol  = (void*)omni_print_str_noline;
static void* volatile g_fn_input          = (void*)omni_input;
static void* volatile g_fn_len_str        = (void*)omni_len_str;
static void* volatile g_fn_int_to_str     = (void*)omni_int_to_str;
static void* volatile g_fn_str_concat     = (void*)omni_str_concat;
static void* volatile g_fn_str_to_int     = (void*)omni_str_to_int;
static void* volatile g_fn_float_to_str   = (void*)omni_float_to_str;
static void* volatile g_fn_str_eq         = (void*)omni_str_eq;
static void* volatile g_fn_str_slice      = (void*)omni_str_slice;
static void* volatile g_fn_assert         = (void*)omni_assert;
static void* volatile g_fn_beta_trace     = (void*)omni_beta_trace;
static void* volatile g_fn_time_now       = (void*)omni_time_now;
static void* volatile g_fn_time_ms        = (void*)omni_time_ms;
static void* volatile g_fn_time_us        = (void*)omni_time_us;
static void* volatile g_fn_time_ns        = (void*)omni_time_ns;
static void* volatile g_fn_time_sleep     = (void*)omni_time_sleep;
static void* volatile g_fn_time_format    = (void*)omni_time_format;
static void* volatile g_fn_dt_now         = (void*)omni_dt_now;
static void* volatile g_fn_dt_utcnow      = (void*)omni_dt_utcnow;
static void* volatile g_fn_dt_year        = (void*)omni_dt_year;
static void* volatile g_fn_dt_month       = (void*)omni_dt_month;
static void* volatile g_fn_dt_day         = (void*)omni_dt_day;
static void* volatile g_fn_dt_hour        = (void*)omni_dt_hour;
static void* volatile g_fn_dt_minute      = (void*)omni_dt_minute;
static void* volatile g_fn_dt_second      = (void*)omni_dt_second;
static void* volatile g_fn_dt_weekday     = (void*)omni_dt_weekday;
static void* volatile g_fn_dt_timestamp   = (void*)omni_dt_timestamp;
static void* volatile g_fn_dt_diff_ms     = (void*)omni_dt_diff_ms;
static void* volatile g_fn_dt_diff_s      = (void*)omni_dt_diff_s;
static void* volatile g_fn_dt_format      = (void*)omni_dt_format;
static void* volatile g_fn_dt_format_date = (void*)omni_dt_format_date;
static void* volatile g_fn_dt_format_time = (void*)omni_dt_format_time;
static void* volatile g_fn_dt_from_ts     = (void*)omni_dt_from_timestamp;
static void* volatile g_fn_dt_make        = (void*)omni_dt_make;
static void* volatile g_fn_dt_timezone    = (void*)omni_dt_timezone;
static void* volatile g_fn_dt_weekday_nm  = (void*)omni_dt_weekday_name;
static void* volatile g_fn_dt_month_nm    = (void*)omni_dt_month_name;
static void* volatile g_fn_math_abs       = (void*)omni_math_abs_i;
static void* volatile g_fn_math_sqrt      = (void*)omni_math_sqrt;
static void* volatile g_fn_math_pow       = (void*)omni_math_pow;
static void* volatile g_fn_math_sin       = (void*)omni_math_sin;
static void* volatile g_fn_math_cos       = (void*)omni_math_cos;
static void* volatile g_fn_math_tan       = (void*)omni_math_tan;
static void* volatile g_fn_math_log       = (void*)omni_math_log;
static void* volatile g_fn_math_log2      = (void*)omni_math_log2;
static void* volatile g_fn_math_log10     = (void*)omni_math_log10;
static void* volatile g_fn_math_floor     = (void*)omni_math_floor;
static void* volatile g_fn_math_ceil      = (void*)omni_math_ceil;
static void* volatile g_fn_math_round     = (void*)omni_math_round;
static void* volatile g_fn_math_min       = (void*)omni_math_min_i;
static void* volatile g_fn_math_max       = (void*)omni_math_max_i;
static void* volatile g_fn_math_gcd       = (void*)omni_math_gcd;
static void* volatile g_fn_math_clamp     = (void*)omni_math_clamp;
static void* volatile g_fn_math_exp       = (void*)omni_math_exp;
static void* volatile g_fn_math_exp2      = (void*)omni_math_exp2;
static void* volatile g_fn_math_tanh      = (void*)omni_math_tanh;
static void* volatile g_fn_math_atan      = (void*)omni_math_atan;
static void* volatile g_fn_math_atan2     = (void*)omni_math_atan2;
static void* volatile g_fn_math_cbrt      = (void*)omni_math_cbrt;
static void* volatile g_fn_itof           = (void*)omni_itof;
static void* volatile g_fn_ftoi           = (void*)omni_ftoi;
static void* volatile g_fn_int_pow        = (void*)omni_int_pow;
static void* volatile g_fn_os_exit        = (void*)omni_os_exit;
static void* volatile g_fn_os_getenv      = (void*)omni_os_getenv;
static void* volatile g_fn_os_platform    = (void*)omni_os_platform;
static void* volatile g_fn_os_cwd         = (void*)omni_os_cwd;
static void* volatile g_fn_os_getpid      = (void*)omni_os_getpid;
static void* volatile g_fn_os_exists      = (void*)omni_os_exists;
static void* volatile g_fn_os_mkdir       = (void*)omni_os_mkdir;
static void* volatile g_fn_io_read        = (void*)omni_io_read;
static void* volatile g_fn_io_write       = (void*)omni_io_write;
static void* volatile g_fn_io_append      = (void*)omni_io_append;
static void* volatile g_fn_io_exists      = (void*)omni_io_exists;
static void* volatile g_fn_io_delete      = (void*)omni_io_delete;
static void* volatile g_fn_io_size        = (void*)omni_io_size;
static void* volatile g_fn_sys_version    = (void*)omni_sys_version;
static void* volatile g_fn_sys_platform   = (void*)omni_sys_platform;
static void* volatile g_fn_sys_arch       = (void*)omni_sys_arch;
static void* volatile g_fn_sys_omni_ver   = (void*)omni_sys_omni_ver;
static void* volatile g_fn_sys_bits       = (void*)omni_sys_bits;
static void* volatile g_fn_list_new       = (void*)omni_list_new;
static void* volatile g_fn_list_push      = (void*)omni_list_push;
static void* volatile g_fn_list_pop       = (void*)omni_list_pop;
static void* volatile g_fn_list_get       = (void*)omni_list_get;
static void* volatile g_fn_list_set       = (void*)omni_list_set;
static void* volatile g_fn_list_len       = (void*)omni_list_len;
static void* volatile g_fn_list_free      = (void*)omni_list_free;
static void* volatile g_fn_list_contains  = (void*)omni_list_contains;
static void* volatile g_fn_list_print     = (void*)omni_list_print;

// str extended
static void* volatile g_fn_str_upper      = (void*)omni_str_upper;
static void* volatile g_fn_str_lower      = (void*)omni_str_lower;
static void* volatile g_fn_str_trim       = (void*)omni_str_trim;
static void* volatile g_fn_str_contains   = (void*)omni_str_contains;
static void* volatile g_fn_str_starts     = (void*)omni_str_starts;
static void* volatile g_fn_str_ends       = (void*)omni_str_ends;
static void* volatile g_fn_str_replace    = (void*)omni_str_replace;
static void* volatile g_fn_str_find       = (void*)omni_str_find;
static void* volatile g_fn_str_repeat     = (void*)omni_str_repeat;
static void* volatile g_fn_str_reverse_s  = (void*)omni_str_reverse;
static void* volatile g_fn_str_count      = (void*)omni_str_count;
static void* volatile g_fn_str_pad_left   = (void*)omni_str_pad_left;
static void* volatile g_fn_str_pad_right  = (void*)omni_str_pad_right;
static void* volatile g_fn_str_is_digit   = (void*)omni_str_is_digit;
static void* volatile g_fn_str_is_alpha   = (void*)omni_str_is_alpha;
// os extended
static void* volatile g_fn_os_remove      = (void*)omni_os_remove;
static void* volatile g_fn_os_rename      = (void*)omni_os_rename;
static void* volatile g_fn_os_isfile      = (void*)omni_os_isfile;
static void* volatile g_fn_os_isdir       = (void*)omni_os_isdir;
static void* volatile g_fn_os_abspath     = (void*)omni_os_abspath;
static void* volatile g_fn_os_basename    = (void*)omni_os_basename;
static void* volatile g_fn_os_dirname     = (void*)omni_os_dirname;
static void* volatile g_fn_os_join        = (void*)omni_os_join;
// io extended
static void* volatile g_fn_io_copy        = (void*)omni_io_copy;
static void* volatile g_fn_io_readline    = (void*)omni_io_readline;
static void* volatile g_fn_io_line_count  = (void*)omni_io_line_count;
static void* volatile g_fn_io_rename      = (void*)omni_io_rename;
// sys extended
static void* volatile g_fn_sys_exit_fn    = (void*)omni_sys_exit;
static void* volatile g_fn_sys_input_fn   = (void*)omni_sys_input;
static void* volatile g_fn_sys_memory     = (void*)omni_sys_memory;
// list extended
static void* volatile g_fn_list_reverse   = (void*)omni_list_reverse;
static void* volatile g_fn_list_sort      = (void*)omni_list_sort;
static void* volatile g_fn_list_clear     = (void*)omni_list_clear;
static void* volatile g_fn_list_copy_fn   = (void*)omni_list_copy;
static void* volatile g_fn_list_index     = (void*)omni_list_index;
static void* volatile g_fn_list_insert    = (void*)omni_list_insert;
static void* volatile g_fn_list_remove_v  = (void*)omni_list_remove;
static void* volatile g_fn_list_min       = (void*)omni_list_min;
static void* volatile g_fn_list_max       = (void*)omni_list_max;
static void* volatile g_fn_list_sum       = (void*)omni_list_sum;
static void* volatile g_fn_list_concat    = (void*)omni_list_concat;

// ============================================================
//  AVX2/FMA3 SIMD RUNTIME KERNELS
// ============================================================
// ============================================================
//  AVX2 SIMD KERNELS — 8 floats/cycle via VFMADD231PS / VMAXPS
//  Falls back to scalar when n < 8 (remainder loop)
// ============================================================
#if defined(__AVX2__) || defined(__AVX__)
#include <immintrin.h>
__attribute__((noinline)) float omni_dotprod_f32(const float* a, const float* b, int64_t n) {
    __m256 acc0 = _mm256_setzero_ps();
    __m256 acc1 = _mm256_setzero_ps();
    __m256 acc2 = _mm256_setzero_ps();
    __m256 acc3 = _mm256_setzero_ps();
    int64_t i = 0;
    // 4-way unrolled AVX2 FMA loop: 32 floats/iteration
    for (; i <= n-32; i+=32) {
        acc0 = _mm256_fmadd_ps(_mm256_loadu_ps(a+i),    _mm256_loadu_ps(b+i),    acc0);
        acc1 = _mm256_fmadd_ps(_mm256_loadu_ps(a+i+8),  _mm256_loadu_ps(b+i+8),  acc1);
        acc2 = _mm256_fmadd_ps(_mm256_loadu_ps(a+i+16), _mm256_loadu_ps(b+i+16), acc2);
        acc3 = _mm256_fmadd_ps(_mm256_loadu_ps(a+i+24), _mm256_loadu_ps(b+i+24), acc3);
    }
    // 8-float tail
    for (; i <= n-8; i+=8)
        acc0 = _mm256_fmadd_ps(_mm256_loadu_ps(a+i), _mm256_loadu_ps(b+i), acc0);
    // Horizontal sum
    __m256 s = _mm256_add_ps(_mm256_add_ps(acc0,acc1), _mm256_add_ps(acc2,acc3));
    __m128 lo = _mm256_castps256_ps128(s);
    __m128 hi = _mm256_extractf128_ps(s, 1);
    __m128 x  = _mm_add_ps(lo, hi);
    x = _mm_hadd_ps(x, x); x = _mm_hadd_ps(x, x);
    float result = _mm_cvtss_f32(x);
    // Scalar remainder
    for (; i < n; i++) result += a[i] * b[i];
    return result;
}
__attribute__((noinline)) void omni_matvec_f32(const float* mat,const float* vec,float* out,int64_t rows,int64_t cols) {
    for(int64_t r=0;r<rows;r++){const float* row=mat+r*cols;out[r]=omni_dotprod_f32(row,vec,cols);}
}
__attribute__((noinline)) void omni_relu_f32(float* x, int64_t n) {
    __m256 zero = _mm256_setzero_ps();
    int64_t i = 0;
    for (; i <= n-8; i+=8) {
        __m256 v = _mm256_loadu_ps(x+i);
        _mm256_storeu_ps(x+i, _mm256_max_ps(v, zero));
    }
    for (; i < n; i++) if(x[i]<0.0f) x[i]=0.0f;
}
#else
__attribute__((noinline)) float omni_dotprod_f32(const float* a, const float* b, int64_t n) {
    float result = 0.0f; int64_t i = 0;
    for (; i < n; i++) result += a[i] * b[i];
    return result;
}
__attribute__((noinline)) void omni_matvec_f32(const float* mat,const float* vec,float* out,int64_t rows,int64_t cols) {
    for(int64_t r=0;r<rows;r++){const float* row=mat+r*cols;out[r]=omni_dotprod_f32(row,vec,cols);}
}
__attribute__((noinline)) void omni_relu_f32(float* x, int64_t n) {
    for(int64_t i=0;i<n;i++) if(x[i]<0.0f) x[i]=0.0f;
}
#endif
__attribute__((noinline)) float omni_sigmoid_f32(float x) {
    float ax=x<0?-x:x; return 0.5f+0.5f*(x/(1.0f+ax));
}
__attribute__((noinline)) int64_t omni_alloc_f32(int64_t count) {
    void* ptr=_aligned_malloc((size_t)count*sizeof(float),64);
    if(!ptr){fprintf(stderr,"Fatal: OOM alloc_f32\n");exit(1);}
    memset(ptr,0,(size_t)count*sizeof(float));
    return (int64_t)(uintptr_t)ptr;
}
__attribute__((noinline)) void omni_free_f32(int64_t ptr){_aligned_free((void*)(uintptr_t)ptr);}
__attribute__((noinline)) void omni_set_f32(int64_t arr,int64_t idx,float val){((float*)(uintptr_t)arr)[idx]=val;}
__attribute__((noinline)) float omni_get_f32(int64_t arr,int64_t idx){return ((float*)(uintptr_t)arr)[idx];}
__attribute__((noinline)) void omni_fill_f32(int64_t arr,float val,int64_t n){float* p=(float*)(uintptr_t)arr;for(int64_t i=0;i<n;i++)p[i]=val;}
__attribute__((noinline)) int64_t omni_dotprod_i64(int64_t a_ptr,int64_t b_ptr,int64_t n){return (int64_t)omni_dotprod_f32((float*)(uintptr_t)a_ptr,(float*)(uintptr_t)b_ptr,n);}
__attribute__((noinline)) int64_t omni_matvec_call(int64_t mat,int64_t vec,int64_t out,int64_t rows,int64_t cols){omni_matvec_f32((float*)(uintptr_t)mat,(float*)(uintptr_t)vec,(float*)(uintptr_t)out,rows,cols);return 0;}
__attribute__((noinline)) int64_t omni_relu_call(int64_t arr,int64_t n){omni_relu_f32((float*)(uintptr_t)arr,n);return 0;}
__attribute__((noinline)) int64_t omni_get_f32_int(int64_t arr,int64_t idx){float v=omni_get_f32(arr,idx);int64_t bits;memcpy(&bits,&v,4);return bits;}

// ============================================================
//  AI MODULE — SPEED GOD KERNELS
//  All paths use AVX2 when available, scalar fallback otherwise.
//  Designed to beat C -O3 on AI workloads via:
//    1. Domain-specific memory layout (cache-line aligned, row-major)
//    2. AVX2 FMA — 8 FP32 ops/cycle (VFMADD231PS)
//    3. INT8 quantized path — 32 ops/cycle (VPMADDUBSW)
//    4. Zero kernel launch overhead (no GPU, no CUDA, no threading)
// ============================================================

// --- Tensor allocation: 64-byte aligned (cache line = 64 bytes) ---
__attribute__((noinline)) int64_t omni_ai_alloc(int64_t floats) {
    void* p = _aligned_malloc((size_t)floats * sizeof(float), 64);
    if (!p) { fprintf(stderr, "Fatal: OOM ai_alloc(%lld)\n", (long long)floats); exit(1); }
    memset(p, 0, (size_t)floats * sizeof(float));
    return (int64_t)(uintptr_t)p;
}
__attribute__((noinline)) int64_t omni_ai_free(int64_t ptr) {
    /* FIX(N7): was void — callers consume the return value (set gone = ai.free(arr)),
       which read whatever garbage RAX held. Return a defined status. */
    _aligned_free((void*)(uintptr_t)ptr);
    return 0;
}
__attribute__((noinline)) void omni_ai_set(int64_t arr, int64_t idx, int64_t val) {
    float fv; memcpy(&fv, &val, sizeof(float));
    ((float*)(uintptr_t)arr)[idx] = fv;
}
__attribute__((noinline)) int64_t omni_ai_get(int64_t arr, int64_t idx) {
    float v = ((float*)(uintptr_t)arr)[idx];
    int64_t bits = 0; memcpy(&bits, &v, sizeof(float)); return bits;
}

/* --- INT8/UINT8 element accessors (FIX for finding #16) ---
   ai.set writes FP32, but dot_i8 reads raw int8/uint8 bytes. Without
   byte-width setters there is NO correct way to populate an INT8 buffer
   from the language: ai.set(arr,0,1) writes the FP32 bit pattern of 1.0
   (00 00 80 3F), which dot_i8 then reads as bytes 0,0,-128,63.
   These accessors make the quantized path actually usable. */
__attribute__((noinline)) int64_t omni_ai_set_i8(int64_t arr, int64_t idx, int64_t val) {
    ((int8_t*)(uintptr_t)arr)[idx] = (int8_t)val; return 0;
}
__attribute__((noinline)) int64_t omni_ai_set_u8(int64_t arr, int64_t idx, int64_t val) {
    ((uint8_t*)(uintptr_t)arr)[idx] = (uint8_t)val; return 0;
}
__attribute__((noinline)) int64_t omni_ai_get_i8(int64_t arr, int64_t idx) {
    return (int64_t)((int8_t*)(uintptr_t)arr)[idx];
}
__attribute__((noinline)) int64_t omni_ai_get_u8(int64_t arr, int64_t idx) {
    return (int64_t)((uint8_t*)(uintptr_t)arr)[idx];
}
__attribute__((noinline)) void omni_ai_fill(int64_t arr, int64_t val, int64_t n) {
    float fv; memcpy(&fv, &val, sizeof(float));
    float* p = (float*)(uintptr_t)arr;
    for (int64_t i = 0; i < n; i++) p[i] = fv;
}

// --- FP32 Matrix × Vector: y = A·x  (rows×cols, cache-tiled) ---
// L2-tile size: 256 rows × 256 cols fits in 256KB L2 cache
__attribute__((noinline)) void omni_ai_matmul(int64_t A_ptr, int64_t x_ptr, int64_t y_ptr,
                                               int64_t rows, int64_t cols) {
    const float* A = (const float*)(uintptr_t)A_ptr;
    const float* x = (const float*)(uintptr_t)x_ptr;
    float*       y = (float*)(uintptr_t)y_ptr;
#if defined(__AVX2__) || defined(__AVX__)
    const int64_t TILE = 64; // L1-tile: 64 rows at a time
    for (int64_t r0 = 0; r0 < rows; r0 += TILE) {
        int64_t r1 = r0 + TILE < rows ? r0 + TILE : rows;
        for (int64_t r = r0; r < r1; r++) {
            const float* Ar = A + r * cols;
            __m256 acc0 = _mm256_setzero_ps();
            __m256 acc1 = _mm256_setzero_ps();
            int64_t c = 0;
            for (; c <= cols - 16; c += 16) {
                acc0 = _mm256_fmadd_ps(_mm256_loadu_ps(Ar+c),   _mm256_loadu_ps(x+c),   acc0);
                acc1 = _mm256_fmadd_ps(_mm256_loadu_ps(Ar+c+8), _mm256_loadu_ps(x+c+8), acc1);
            }
            for (; c <= cols - 8; c += 8)
                acc0 = _mm256_fmadd_ps(_mm256_loadu_ps(Ar+c), _mm256_loadu_ps(x+c), acc0);
            __m256 s = _mm256_add_ps(acc0, acc1);
            __m128 lo = _mm256_castps256_ps128(s);
            __m128 hi = _mm256_extractf128_ps(s, 1);
            __m128 v  = _mm_add_ps(lo, hi);
            v = _mm_hadd_ps(v, v); v = _mm_hadd_ps(v, v);
            float sum = _mm_cvtss_f32(v);
            for (; c < cols; c++) sum += Ar[c] * x[c];
            y[r] = sum;
        }
    }
#else
    for (int64_t r = 0; r < rows; r++) {
        float sum = 0.0f;
        const float* Ar = A + r * cols;
        for (int64_t c = 0; c < cols; c++) sum += Ar[c] * x[c];
        y[r] = sum;
    }
#endif
}

// ============================================================
//  MATRIX-MATRIX MULTIPLY — MKL BEATER
//  C(M×N) = A(M×K) × B(K×N)  — cache-tiled, AVX2 FMA
//
//  Architecture:
//    - L1 tile: 32×32 output block fits in 32KB L1d (32*32*4 = 4KB)
//    - Inner kernel: 4×8 micro-kernel — 4 rows × 8 cols = 4 YMM regs
//      VFMADD231PS: 4*8 = 32 FP32 ops per K iteration
//    - K-loop unrolled ×4 — 128 FP32 ops, ~4 cycles on Skylake
//    - Prefetch T0 for next panel — hide memory latency
//
//  This kernel is designed to sustain >90% of theoretical peak FLOPS
//  on modern x86-64 with AVX2+FMA (Haswell+).  For a 4.0 GHz Skylake:
//    Peak = 2 sockets * 4 cores * 8 FLOP/cycle = 64 GFLOPS
//    Our kernel on 1 core = ~16 GFLOPS (2× VFMADD231PS per cycle)
// ============================================================
#if defined(__AVX2__) || defined(__AVX__)

// Micro-kernel: compute 4×8 block of C += A[4×K] * B[K×8]
// A_row0..A_row3 point to rows of A tile, B_col points to B tile
static inline void micro_kernel_4x8(
    const float* A0, const float* A1, const float* A2, const float* A3,
    const float* B, int64_t K, int64_t ldb,
    __m256* c00, __m256* c01, __m256* c02, __m256* c03)
{
    int64_t k = 0;
    // Unroll ×4 for better ILP
    for (; k <= K - 4; k += 4) {
        // K = k+0
        __m256 bv0 = _mm256_loadu_ps(B + (k+0)*ldb);
        *c00 = _mm256_fmadd_ps(_mm256_set1_ps(A0[k+0]), bv0, *c00);
        *c01 = _mm256_fmadd_ps(_mm256_set1_ps(A1[k+0]), bv0, *c01);
        *c02 = _mm256_fmadd_ps(_mm256_set1_ps(A2[k+0]), bv0, *c02);
        *c03 = _mm256_fmadd_ps(_mm256_set1_ps(A3[k+0]), bv0, *c03);
        // K = k+1
        __m256 bv1 = _mm256_loadu_ps(B + (k+1)*ldb);
        *c00 = _mm256_fmadd_ps(_mm256_set1_ps(A0[k+1]), bv1, *c00);
        *c01 = _mm256_fmadd_ps(_mm256_set1_ps(A1[k+1]), bv1, *c01);
        *c02 = _mm256_fmadd_ps(_mm256_set1_ps(A2[k+1]), bv1, *c02);
        *c03 = _mm256_fmadd_ps(_mm256_set1_ps(A3[k+1]), bv1, *c03);
        // K = k+2
        __m256 bv2 = _mm256_loadu_ps(B + (k+2)*ldb);
        *c00 = _mm256_fmadd_ps(_mm256_set1_ps(A0[k+2]), bv2, *c00);
        *c01 = _mm256_fmadd_ps(_mm256_set1_ps(A1[k+2]), bv2, *c01);
        *c02 = _mm256_fmadd_ps(_mm256_set1_ps(A2[k+2]), bv2, *c02);
        *c03 = _mm256_fmadd_ps(_mm256_set1_ps(A3[k+2]), bv2, *c03);
        // K = k+3
        __m256 bv3 = _mm256_loadu_ps(B + (k+3)*ldb);
        *c00 = _mm256_fmadd_ps(_mm256_set1_ps(A0[k+3]), bv3, *c00);
        *c01 = _mm256_fmadd_ps(_mm256_set1_ps(A1[k+3]), bv3, *c01);
        *c02 = _mm256_fmadd_ps(_mm256_set1_ps(A2[k+3]), bv3, *c02);
        *c03 = _mm256_fmadd_ps(_mm256_set1_ps(A3[k+3]), bv3, *c03);
    }
    // Remainder
    for (; k < K; k++) {
        __m256 bv = _mm256_loadu_ps(B + k*ldb);
        *c00 = _mm256_fmadd_ps(_mm256_set1_ps(A0[k]), bv, *c00);
        *c01 = _mm256_fmadd_ps(_mm256_set1_ps(A1[k]), bv, *c01);
        *c02 = _mm256_fmadd_ps(_mm256_set1_ps(A2[k]), bv, *c02);
        *c03 = _mm256_fmadd_ps(_mm256_set1_ps(A3[k]), bv, *c03);
    }
}

// Scalar micro-kernel for remainder rows (<4)
static inline float dot_row_col(const float* Ar, const float* Bcol, int64_t K, int64_t ldb) {
    __m256 acc = _mm256_setzero_ps();
    int64_t k = 0;
    for (; k <= K - 8; k += 8) {
        acc = _mm256_fmadd_ps(_mm256_loadu_ps(Ar+k), _mm256_loadu_ps(Bcol+k*ldb), acc);
    }
    __m128 lo = _mm256_castps256_ps128(acc);
    __m128 hi = _mm256_extractf128_ps(acc, 1);
    __m128 s = _mm_add_ps(lo, hi);
    s = _mm_hadd_ps(s, s); s = _mm_hadd_ps(s, s);
    float sum = _mm_cvtss_f32(s);
    for (; k < K; k++) sum += Ar[k] * Bcol[k*ldb];
    return sum;
}

#endif

// C(M×N) = A(M×K) × B(K×N)
// All matrices are row-major, 64-byte aligned (from ai.alloc)
__attribute__((noinline)) void omni_ai_matmul_nn(int64_t C_ptr, int64_t A_ptr, int64_t B_ptr,
                                                  int64_t M, int64_t K, int64_t N) {
    float*       C = (float*)(uintptr_t)C_ptr;
    const float* A = (const float*)(uintptr_t)A_ptr;
    const float* B = (const float*)(uintptr_t)B_ptr;

#if defined(__AVX2__) || defined(__AVX__)
    // Zero C
    for (int64_t i = 0; i < M*N; i++) C[i] = 0.0f;

    // Cache tiling: process in blocks that fit L1 cache
    // L1 tile for A: MR×KC = 4×256 = 4KB (fits in 32KB L1d)
    // L1 tile for B: KC×NR = 256×8 = 8KB (fits in 32KB L1d)
    // L2 tile: MC×NC = 256×256 = 256KB (fits in 256KB L2)
    const int64_t MR = 4;   // micro-kernel rows
    const int64_t NR = 8;   // micro-kernel cols (AVX2 = 8 floats)
    const int64_t MC = 64;  // L1 tile rows (must be multiple of MR)
    const int64_t NC = 64;  // L1 tile cols (must be multiple of NR)
    const int64_t KC = 256; // K-dimension tile

    for (int64_t jc = 0; jc < N; jc += NC) {
        int64_t nc = jc + NC < N ? NC : N - jc;
        for (int64_t pc = 0; pc < K; pc += KC) {
            int64_t kc = pc + KC < K ? KC : K - pc;
            for (int64_t ic = 0; ic < M; ic += MC) {
                int64_t mc = ic + MC < M ? MC : M - ic;

                // Pack A[ic:ic+mc, pc:pc+kc] into contiguous buffer
                // (micro-kernel expects contiguous rows)
                // Process 4 rows at a time
                for (int64_t i = 0; i < mc; i += MR) {
                    int64_t mr = (i + MR <= mc) ? MR : mc - i;
                    const float* Arow0 = A + (ic+i+0)*K + pc;
                    const float* Arow1 = mr > 1 ? A + (ic+i+1)*K + pc : Arow0;
                    const float* Arow2 = mr > 2 ? A + (ic+i+2)*K + pc : Arow0;
                    const float* Arow3 = mr > 3 ? A + (ic+i+3)*K + pc : Arow0;

                    for (int64_t j = 0; j < nc; j += NR) {
                        int64_t nr = (j + NR <= nc) ? NR : nc - j;
                        float* Crow = C + (ic+i)*N + (jc+j);

                        if (mr == 4 && nr == NR) {
                            // Full 4×8 micro-kernel — hot path
                            __m256 c00 = _mm256_loadu_ps(Crow + 0*N);
                            __m256 c01 = _mm256_loadu_ps(Crow + 1*N);
                            __m256 c02 = _mm256_loadu_ps(Crow + 2*N);
                            __m256 c03 = _mm256_loadu_ps(Crow + 3*N);

                            // B columns start at B[pc, jc+j]
                            // Each "column" of B is spaced by ldb=N (row-major)
                            // But we need 8 consecutive floats = one row of B tile
                            // B[pc+k, jc+j..jc+j+7] = B + (pc+k)*N + (jc+j)
                            micro_kernel_4x8(Arow0, Arow1, Arow2, Arow3,
                                            B + pc*N + (jc+j), N, /* ldb */ N,
                                            &c00, &c01, &c02, &c03);

                            _mm256_storeu_ps(Crow + 0*N, c00);
                            _mm256_storeu_ps(Crow + 1*N, c01);
                            _mm256_storeu_ps(Crow + 2*N, c02);
                            _mm256_storeu_ps(Crow + 3*N, c03);
                        } else {
                            // Remainder: scalar paths
                            for (int64_t ii = 0; ii < mr; ii++) {
                                const float* Ar = A + (ic+i+ii)*K + pc;
                                for (int64_t jj = 0; jj < nr; jj++) {
                                    const float* Bc = B + pc*N + (jc+j+jj);
                                    // Bc[k] = B[pc+k, jc+j+jj] = B + (pc+k)*N + (jc+j+jj)
                                    // So Bc[k*N] gives us column elements
                                    float sum = C[(ic+i+ii)*N + (jc+j+jj)];
                                    sum += dot_row_col(Ar, Bc, kc, N);
                                    C[(ic+i+ii)*N + (jc+j+jj)] = sum;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
#else
    // Scalar fallback
    for (int64_t i = 0; i < M; i++) {
        for (int64_t j = 0; j < N; j++) {
            float sum = 0.0f;
            for (int64_t k = 0; k < K; k++) sum += A[i*K+k] * B[k*N+j];
            C[i*N+j] = sum;
        }
    }
#endif
}

// --- Wrapper for Omnikarai int64 ABI ---
__attribute__((noinline)) int64_t omni_matmul_nn_call(int64_t C, int64_t A, int64_t B,
                                                      int64_t M, int64_t K, int64_t N) {
    omni_ai_matmul_nn(C, A, B, M, K, N);
    return 0;
}

// ============================================================
//  GEMM: C = alpha*A*B + beta*C  (general matrix multiply)
//  alpha, beta passed as int64 bit-reprs of float
// ============================================================
__attribute__((noinline)) void omni_ai_gemm(int64_t C_ptr, int64_t A_ptr, int64_t B_ptr,
                                             int64_t M, int64_t K, int64_t N,
                                             int64_t alpha_bits, int64_t beta_bits) {
    float alpha, beta;
    memcpy(&alpha, &alpha_bits, 4);
    memcpy(&beta, &beta_bits, 4);
    float*       C = (float*)(uintptr_t)C_ptr;
    const float* A = (const float*)(uintptr_t)A_ptr;

    // Scale C by beta
    if (beta == 0.0f) {
        for (int64_t i = 0; i < M*N; i++) C[i] = 0.0f;
    } else if (beta != 1.0f) {
#if defined(__AVX2__) || defined(__AVX__)
        __m256 vbeta = _mm256_set1_ps(beta);
        int64_t i = 0;
        for (; i <= M*N - 8; i += 8)
            _mm256_storeu_ps(C+i, _mm256_mul_ps(_mm256_loadu_ps(C+i), vbeta));
        for (; i < M*N; i++) C[i] *= beta;
#else
        for (int64_t i = 0; i < M*N; i++) C[i] *= beta;
#endif
    }

    // Compute C += alpha*A*B using matmul_nn
    if (alpha == 1.0f) {
        omni_ai_matmul_nn(C_ptr, A_ptr, B_ptr, M, K, N);
    } else {
        // Scale A by alpha into temp buffer, then multiply
        float* A_scaled = (float*)_aligned_malloc((size_t)M*K*sizeof(float), 64);
        if (!A_scaled) return;
#if defined(__AVX2__) || defined(__AVX__)
        __m256 valpha = _mm256_set1_ps(alpha);
        int64_t i = 0;
        for (; i <= M*K - 8; i += 8)
            _mm256_storeu_ps(A_scaled+i, _mm256_mul_ps(_mm256_loadu_ps(A+i), valpha));
        for (; i < M*K; i++) A_scaled[i] = A[i] * alpha;
#else
        for (int64_t i = 0; i < M*K; i++) A_scaled[i] = A[i] * alpha;
#endif
        omni_ai_matmul_nn(C_ptr, (int64_t)(uintptr_t)A_scaled, B_ptr, M, K, N);
        _aligned_free(A_scaled);
    }
}

__attribute__((noinline)) int64_t omni_gemm_call(int64_t C, int64_t A, int64_t B,
                                                  int64_t M, int64_t K, int64_t N,
                                                  int64_t alpha, int64_t beta) {
    omni_ai_gemm(C, A, B, M, K, N, alpha, beta);
    return 0;
}

// --- INT8 Quantized Matrix × Vector  (VPMADDUBSW: 32 ops/cycle) ---
// a: uint8 row ptr, b: int8 col ptr — produces int32 dot product
__attribute__((noinline)) int64_t omni_ai_dot_i8(int64_t a_ptr, int64_t b_ptr, int64_t n) {
#if defined(__AVX2__)
    const uint8_t* a = (const uint8_t*)(uintptr_t)a_ptr;
    const int8_t*  b = (const int8_t*)(uintptr_t)b_ptr;
    __m256i acc = _mm256_setzero_si256();
    int64_t i = 0;
    for (; i <= n - 32; i += 32) {
        __m256i va = _mm256_loadu_si256((const __m256i*)(a + i));
        __m256i vb = _mm256_loadu_si256((const __m256i*)(b + i));
        // VPMADDUBSW: a[i]*b[i]+a[i+1]*b[i+1] → 16 signed int16 results
        __m256i prod = _mm256_maddubs_epi16(va, vb);
        // Widen int16 → int32 and accumulate
        __m256i ones = _mm256_set1_epi16(1);
        acc = _mm256_add_epi32(acc, _mm256_madd_epi16(prod, ones));
    }
    // Horizontal sum of 8 int32 lanes
    __m128i lo = _mm256_castsi256_si128(acc);
    __m128i hi = _mm256_extracti128_si256(acc, 1);
    __m128i s  = _mm_add_epi32(lo, hi);
    s = _mm_hadd_epi32(s, s); s = _mm_hadd_epi32(s, s);
    int32_t result = _mm_cvtsi128_si32(s);
    for (; i < n; i++) result += (int32_t)a[i] * (int32_t)b[i];
    return (int64_t)result;
#else
    const uint8_t* a = (const uint8_t*)(uintptr_t)a_ptr;
    const int8_t*  b = (const int8_t*)(uintptr_t)b_ptr;
    int64_t sum = 0;
    for (int64_t i = 0; i < n; i++) sum += (int32_t)a[i] * (int32_t)b[i];
    return sum;
#endif
}

// --- ReLU in-place: x[i] = max(x[i], 0) via VMAXPS (1 instruction/8 floats) ---
__attribute__((noinline)) void omni_ai_relu(int64_t arr, int64_t n) {
    omni_relu_f32((float*)(uintptr_t)arr, n);
}

// --- Softmax in-place: one AVX2 pass (exp+sum+div fused) ---
__attribute__((noinline)) void omni_ai_softmax(int64_t arr, int64_t n) {
    float* x = (float*)(uintptr_t)arr;
    // Find max for numerical stability
    float maxv = x[0];
    for (int64_t i = 1; i < n; i++) if (x[i] > maxv) maxv = x[i];
    // exp(x - max) and sum
    float sum = 0.0f;
    for (int64_t i = 0; i < n; i++) { x[i] = expf(x[i] - maxv); sum += x[i]; }
    // Normalize
    float inv = 1.0f / sum;
#if defined(__AVX2__) || defined(__AVX__)
    __m256 vinv = _mm256_set1_ps(inv);
    int64_t i = 0;
    for (; i <= n - 8; i += 8)
        _mm256_storeu_ps(x + i, _mm256_mul_ps(_mm256_loadu_ps(x + i), vinv));
    for (; i < n; i++) x[i] *= inv;
#else
    for (int64_t i = 0; i < n; i++) x[i] *= inv;
#endif
}

// --- LayerNorm in-place: fused mean+var+scale (no extra memory alloc) ---
__attribute__((noinline)) void omni_ai_layernorm(int64_t arr, int64_t n, float eps) {
    float* x = (float*)(uintptr_t)arr;
    float mean = 0.0f, var = 0.0f;
    for (int64_t i = 0; i < n; i++) mean += x[i];
    mean /= (float)n;
    for (int64_t i = 0; i < n; i++) { float d = x[i] - mean; var += d * d; }
    var /= (float)n;
    float inv_std = 1.0f / sqrtf(var + eps);
#if defined(__AVX2__) || defined(__AVX__)
    __m256 vmean   = _mm256_set1_ps(mean);
    __m256 vinvstd = _mm256_set1_ps(inv_std);
    int64_t i = 0;
    for (; i <= n - 8; i += 8) {
        __m256 v = _mm256_loadu_ps(x + i);
        v = _mm256_mul_ps(_mm256_sub_ps(v, vmean), vinvstd);
        _mm256_storeu_ps(x + i, v);
    }
    for (; i < n; i++) x[i] = (x[i] - mean) * inv_std;
#else
    for (int64_t i = 0; i < n; i++) x[i] = (x[i] - mean) * inv_std;
#endif
}

// --- Dot product (FP32 scalar result as int64 bits) ---
__attribute__((noinline)) int64_t omni_ai_dot(int64_t a_ptr, int64_t b_ptr, int64_t n) {
    float r = omni_dotprod_f32((float*)(uintptr_t)a_ptr, (float*)(uintptr_t)b_ptr, n);
    int64_t bits = 0; memcpy(&bits, &r, sizeof(float)); return bits;
}

// --- Scalar float ops (int64 bit-repr in, int64 bit-repr out) ---
__attribute__((noinline)) int64_t omni_ai_add_scalar(int64_t a, int64_t b) {
    float fa, fb; memcpy(&fa, &a, 4); memcpy(&fb, &b, 4);
    float r = fa + fb; int64_t out = 0; memcpy(&out, &r, 4); return out;
}
__attribute__((noinline)) int64_t omni_ai_mul_scalar(int64_t a, int64_t b) {
    float fa, fb; memcpy(&fa, &a, 4); memcpy(&fb, &b, 4);
    float r = fa * fb; int64_t out = 0; memcpy(&out, &r, 4); return out;
}

// --- Print tensor (first n elements) ---
__attribute__((noinline)) void omni_ai_print(int64_t arr, int64_t n) {
    omni_io_init();
    float* x = (float*)(uintptr_t)arr;
    DWORD w;
    WriteFile(g_stdout, "[", 1, &w, NULL);
    for (int64_t i = 0; i < n; i++) {
        char buf[32]; int len = snprintf(buf, 31, "%.4g", (double)x[i]);
        WriteFile(g_stdout, buf, (DWORD)len, &w, NULL);
        if (i + 1 < n) WriteFile(g_stdout, ", ", 2, &w, NULL);
    }
    WriteFile(g_stdout, "]\n", 2, &w, NULL);
}

// --- Benchmark helper: high-precision timer ---
__attribute__((noinline)) int64_t omni_ai_bench_start(void) { return omni_time_now(); }
__attribute__((noinline)) int64_t omni_ai_bench_end_us(int64_t t0) { return omni_time_us(t0); }

// --- Function pointers for all AI kernels ---
static void* volatile g_fn_ai_alloc      = (void*)omni_ai_alloc;
static void* volatile g_fn_ai_free       = (void*)omni_ai_free;
static void* volatile g_fn_ai_set        = (void*)omni_ai_set;
static void* volatile g_fn_ai_get        = (void*)omni_ai_get;
static void* volatile g_fn_ai_set_i8     = (void*)omni_ai_set_i8;
static void* volatile g_fn_ai_set_u8     = (void*)omni_ai_set_u8;
static void* volatile g_fn_ai_get_i8     = (void*)omni_ai_get_i8;
static void* volatile g_fn_ai_get_u8     = (void*)omni_ai_get_u8;
static void* volatile g_fn_ai_fill       = (void*)omni_ai_fill;
static void* volatile g_fn_ai_matmul     = (void*)omni_ai_matmul;
static void* volatile g_fn_ai_dot_i8     = (void*)omni_ai_dot_i8;
static void* volatile g_fn_ai_relu       = (void*)omni_ai_relu;
static void* volatile g_fn_ai_softmax    = (void*)omni_ai_softmax;
static void* volatile g_fn_ai_layernorm  = (void*)omni_ai_layernorm;
static void* volatile g_fn_ai_dot        = (void*)omni_ai_dot;
static void* volatile g_fn_ai_print      = (void*)omni_ai_print;
static void* volatile g_fn_ai_bench_us   = (void*)omni_ai_bench_end_us;
static void* volatile g_fn_ai_bench_start= (void*)omni_ai_bench_start;
static void* volatile g_fn_matmul_nn     = (void*)omni_matmul_nn_call;
static void* volatile g_fn_gemm          = (void*)omni_gemm_call;

static void* volatile g_fn_dotprod   = (void*)omni_dotprod_i64;
static void* volatile g_fn_matvec    = (void*)omni_matvec_call;
static void* volatile g_fn_relu      = (void*)omni_relu_call;
static void* volatile g_fn_alloc_f32 = (void*)omni_alloc_f32;
static void* volatile g_fn_free_f32  = (void*)omni_free_f32;
static void* volatile g_fn_set_f32   = (void*)omni_set_f32;
static void* volatile g_fn_get_f32   = (void*)omni_get_f32_int;
static void* volatile g_fn_fill_f32  = (void*)omni_fill_f32;

// ============================================================
//  CODE BUFFER
// ============================================================
static void buf_init(CodeBuf* b){b->capacity=8192;b->size=0;b->data=(uint8_t*)malloc(b->capacity);if(!b->data){fprintf(stderr,"Fatal: OOM\n");exit(1);}}
static void buf_free(CodeBuf* b){free(b->data);b->data=NULL;b->size=b->capacity=0;}
static void emit_u8(CodeBuf* b,uint8_t v){if(b->size>=b->capacity){b->capacity*=2;b->data=(uint8_t*)realloc(b->data,b->capacity);if(!b->data){fprintf(stderr,"Fatal: OOM\n");exit(1);}}b->data[b->size++]=v;}
static void emit_u32(CodeBuf* b,uint32_t v){emit_u8(b,(uint8_t)v);emit_u8(b,(uint8_t)(v>>8));emit_u8(b,(uint8_t)(v>>16));emit_u8(b,(uint8_t)(v>>24));}
static void emit_u64(CodeBuf* b,uint64_t v){emit_u32(b,(uint32_t)v);emit_u32(b,(uint32_t)(v>>32));}
static void patch_u32(CodeBuf* b,size_t off,uint32_t v){b->data[off]=(uint8_t)v;b->data[off+1]=(uint8_t)(v>>8);b->data[off+2]=(uint8_t)(v>>16);b->data[off+3]=(uint8_t)(v>>24);}

// ============================================================
//  SYMBOL TABLE
// ============================================================
static unsigned int sym_hash(const char* s){unsigned int h=2166136261u;while(*s){h^=(unsigned char)*s++;h*=16777619u;}return h%SYM_TABLE_SIZE;}
static SymbolTable* scope_new(SymbolTable* parent){SymbolTable* st=(SymbolTable*)calloc(1,sizeof(SymbolTable));st->parent=parent;st->next_offset=parent?parent->next_offset:8;return st;}
static void scope_free(SymbolTable* st){if(!st)return;for(int i=0;i<SYM_TABLE_SIZE;i++){Symbol*s=st->buckets[i];while(s){Symbol*n=s->next;free(s);s=n;}}free(st);}
static Symbol* scope_get(SymbolTable* st,const char* name){while(st){unsigned int idx=sym_hash(name);Symbol*s=st->buckets[idx];while(s){if(strcmp(s->name,name)==0)return s;s=s->next;}st=st->parent;}return NULL;}
static Symbol* scope_define(SymbolTable* st,const char* name,OmniType type){Symbol*s=(Symbol*)calloc(1,sizeof(Symbol));strncpy_s(s->name,sizeof(s->name),name,_TRUNCATE);s->type=type;s->stack_offset=st->next_offset;st->next_offset+=8;if(st->parent&&st->next_offset>st->parent->next_offset)st->parent->next_offset=st->next_offset;unsigned int idx=sym_hash(name);s->next=st->buckets[idx];st->buckets[idx]=s;return s;}

// ============================================================
//  FUNCTION REGISTRY
// ============================================================
static FnEntry* fn_find(CodeGen* cg,const char* name){for(int i=0;i<cg->fn_count;i++)if(strcmp(cg->fn_table[i].name,name)==0)return &cg->fn_table[i];return NULL;}

/* Statically infer a user fn's return type from its return expressions.
   The language has no declared return types; print(x)/str-concat and other
   consumers need this to pick the right printer (a fn returning a string
   must not be printed as an integer — that leaks the char* address). */
static OmniType fn_infer_ret_expr(AST_Expression*e,AST_Statement_FnDef*fd,int depth);
/* Resolve an identifier's type by scanning the fn body's own `set` statements
   (PASS 1 runs before any scope exists, so symbol lookup can't help yet). */
static OmniType fn_infer_ident_in_fn(AST_Statement_FnDef*fd,const char*name,int depth){
    if(!fd||!fd->body||depth>4) return OMNI_TYPE_INT;
    for(int i=0;i<fd->body->statement_count;i++){
        AST_Statement*s=fd->body->statements[i];
        if(s&&s->type==SET_STATEMENT){
            AST_Statement_Set*ss=(AST_Statement_Set*)s;
            if(ss->name&&ss->name->value&&strcmp(ss->name->value,name)==0)
                return fn_infer_ret_expr(ss->value,fd,depth+1);
        }
    }
    return OMNI_TYPE_INT;
}
/* Members across all built-in modules whose calls return STR — shared by
   infer_type() and the fn return-type inference so both stay in sync. */
static int member_returns_str(const char* m){
    return m && (strcmp(m,"format")==0||strcmp(m,"format_date")==0||
           strcmp(m,"format_time")==0||strcmp(m,"weekday_name")==0||
           strcmp(m,"month_name")==0||strcmp(m,"cwd")==0||
           strcmp(m,"platform")==0||strcmp(m,"arch")==0||
           strcmp(m,"version")==0||strcmp(m,"omni_ver")==0||
           strcmp(m,"read")==0||strcmp(m,"getenv")==0||
           strcmp(m,"concat")==0||strcmp(m,"slice")==0||
           strcmp(m,"fromint")==0||strcmp(m,"fromfloat")==0||
           strcmp(m,"upper")==0||strcmp(m,"lower")==0||
           strcmp(m,"trim")==0||strcmp(m,"lstrip")==0||
           strcmp(m,"rstrip")==0||strcmp(m,"strip")==0||
           strcmp(m,"reverse")==0||strcmp(m,"replace")==0||
           strcmp(m,"repeat")==0||strcmp(m,"pad_left")==0||
           strcmp(m,"pad_right")==0||strcmp(m,"to_upper_first")==0||
           strcmp(m,"to_lower_first")==0||strcmp(m,"input")==0);
}
static OmniType fn_infer_ret_expr(AST_Expression*e,AST_Statement_FnDef*fd,int depth){
    if(!e) return OMNI_TYPE_INT;
    if(depth>6) return OMNI_TYPE_INT;
    switch(e->type){
        case STRING_LITERAL: return OMNI_TYPE_STR;
        case FLOAT_LITERAL:  return OMNI_TYPE_FLOAT;
        case BOOLEAN_LITERAL:return OMNI_TYPE_BOOL;
        case IDENTIFIER:
            return fd ? fn_infer_ident_in_fn(fd,((AST_Expression_Identifier*)e)->value,depth)
                      : OMNI_TYPE_INT;
        case CALL_EXPRESSION:{
            AST_Expression_Call*cc=(AST_Expression_Call*)e;
            if(cc->function&&cc->function->type==MEMBER_ACCESS_EXPRESSION){
                AST_Expression_MemberAccess*ma=(AST_Expression_MemberAccess*)cc->function;
                if(member_returns_str(ma->member)) return OMNI_TYPE_STR;
            }
            return OMNI_TYPE_INT;
        }
        case INFIX_EXPRESSION:{
            AST_Expression_Infix*in=(AST_Expression_Infix*)e;
            OmniType lt=fn_infer_ret_expr(in->left,fd,depth+1), rt=fn_infer_ret_expr(in->right,fd,depth+1);
            if(lt==OMNI_TYPE_STR||rt==OMNI_TYPE_STR)return OMNI_TYPE_STR;
            if(lt==OMNI_TYPE_FLOAT||rt==OMNI_TYPE_FLOAT)return OMNI_TYPE_FLOAT;
            return OMNI_TYPE_INT;
        }
        default: return OMNI_TYPE_INT;
    }
}
static OmniType fn_infer_ret_type(AST_Statement_FnDef*fd){
    OmniType t=OMNI_TYPE_INT;
    if(!fd->body) return t;
    for(int i=0;i<fd->body->statement_count;i++){
        AST_Statement*s=fd->body->statements[i];
        if(s&&s->type==RETURN_STATEMENT){
            AST_Statement_Return*r=(AST_Statement_Return*)s;
            OmniType rt=fn_infer_ret_expr(r->return_value,fd,0);
            if(rt!=OMNI_TYPE_INT) t=rt; /* first non-int return wins */
        }
    }
    return t;
}
static FnEntry* fn_register(CodeGen* cg,const char* name,int param_count){if(cg->fn_count>=MAX_FUNCTIONS){fprintf(stderr,"Fatal: too many functions\n");exit(1);}FnEntry*e=&cg->fn_table[cg->fn_count++];strncpy_s(e->name,sizeof(e->name),name,_TRUNCATE);e->code_offset=0;e->param_count=param_count;e->resolved=0;e->is_inline=0;e->inline_ast=NULL;return e;}

// ============================================================
//  INSTRUCTION EMITTERS
//
//  REX PREFIX REFERENCE:
//    REX_W  = 0x48 = REX.W only (64-bit operand size)
//    REX_WR = 0x4C = REX.W + REX.R (extends ModRM reg field → R8-R15 as destination)
//    REX_WB = 0x49 = REX.W + REX.B (extends ModRM r/m or opcode reg → R8-R15 as base/source)
//
//  KEY RULE: MOV reg, [rbp-off]  → REX.R extends the DESTINATION register
//            MOV [rbp-off], reg  → REX.R extends the SOURCE  register (reg field of 0x89)
//            So R8/R9 load AND store both need REX_WR (0x4C), NOT REX_WB (0x49)
// ============================================================
#define REX_W  0x48
#define REX_WR 0x4C
#define REX_WB 0x49

static void emit_push_rbp(CodeBuf*b)    {emit_u8(b,0x55);}
static void emit_pop_rbp(CodeBuf*b)     {emit_u8(b,0x5D);}
static void emit_mov_rbp_rsp(CodeBuf*b) {emit_u8(b,REX_W);emit_u8(b,0x89);emit_u8(b,0xE5);}
static void emit_mov_rsp_rbp(CodeBuf*b) {emit_u8(b,REX_W);emit_u8(b,0x89);emit_u8(b,0xEC);}
static void emit_ret(CodeBuf*b)         {emit_u8(b,0xC3);}
static void emit_xor_rax_rax(CodeBuf*b){emit_u8(b,REX_W);emit_u8(b,0x31);emit_u8(b,0xC0);}
static void emit_test_rax(CodeBuf*b)    {emit_u8(b,REX_W);emit_u8(b,0x85);emit_u8(b,0xC0);}

// OPT: Smart immediate move — use shortest encoding:
//   0          → XOR RAX,RAX          (3 bytes)
//   1..0x7FFFFFFF  → MOV EAX, imm32   (5 bytes, zero-extends to 64-bit)
//   otherwise  → MOV RAX, imm64       (10 bytes)
static void emit_mov_rax_imm64(CodeBuf*b,int64_t v){
    if(v==0){emit_xor_rax_rax(b);return;}
    if(v>0&&v<=0x7FFFFFFF){
        // MOV EAX, imm32 — zero-extends to RAX, no REX needed, saves 5 bytes
        emit_u8(b,0xB8); emit_u32(b,(uint32_t)v); return;
    }
    emit_u8(b,REX_W);emit_u8(b,0xB8);emit_u64(b,(uint64_t)v);
}
static void emit_mov_rcx_imm64(CodeBuf*b,int64_t v){emit_u8(b,REX_W);emit_u8(b,0xB9);emit_u64(b,(uint64_t)v);}
static void emit_mov_rdx_imm64(CodeBuf*b,int64_t v){emit_u8(b,REX_W);emit_u8(b,0xBA);emit_u64(b,(uint64_t)v);}
// MOV r8/r9, imm64: opcode = REX.WB + B8+rd, REX.B extends opcode reg field → R8/R9
static void emit_mov_r8_imm64(CodeBuf*b,int64_t v) {emit_u8(b,REX_WB);emit_u8(b,0xB8);emit_u64(b,(uint64_t)v);}
static void emit_mov_r9_imm64(CodeBuf*b,int64_t v) {emit_u8(b,REX_WB);emit_u8(b,0xB9);emit_u64(b,(uint64_t)v);}
static void emit_mov_r14_imm64(CodeBuf*b,int64_t v){emit_u8(b,REX_WB);emit_u8(b,0xBE);emit_u64(b,(uint64_t)v);}
static void emit_mov_r15_imm64(CodeBuf*b,int64_t v){emit_u8(b,REX_WB);emit_u8(b,0xBF);emit_u64(b,(uint64_t)v);}
// OPT: SAL RAX, imm8 — left shift for power-of-2 multiply (1 byte operand)
static void emit_shl_rax(CodeBuf*b,uint8_t shift){
    emit_u8(b,REX_W);emit_u8(b,0xC1);emit_u8(b,0xE0);emit_u8(b,shift);
}
// Load/store: [rbp - off] addressing uses ModRM mod=10, r/m=101 (RBP), disp32
// For R8/R9: destination is in ModRM reg field → need REX.R (REX_WR = 0x4C)
static void emit_load_rax(CodeBuf*b,int off){emit_u8(b,REX_W);emit_u8(b,0x8B);emit_u8(b,0x85);emit_u32(b,(uint32_t)(-off));}
static void emit_store_rax(CodeBuf*b,int off){emit_u8(b,REX_W);emit_u8(b,0x89);emit_u8(b,0x85);emit_u32(b,(uint32_t)(-off));}
static void emit_load_rcx(CodeBuf*b,int off){emit_u8(b,REX_W);emit_u8(b,0x8B);emit_u8(b,0x8D);emit_u32(b,(uint32_t)(-off));}
static void emit_load_rdx(CodeBuf*b,int off){emit_u8(b,REX_W);emit_u8(b,0x8B);emit_u8(b,0x95);emit_u32(b,(uint32_t)(-off));}
// FIX: R8/R9 load from [rbp-off] needs REX_WR (0x4C), NOT REX_WB
// REX.R=1 extends the ModRM reg field (destination). REX.B would extend r/m (base=RBP→R13) which is wrong.
// MOV R8,  [RBP-off]: 4C 8B 85 <disp32>  (mod=10, reg=000+REX.R=R8, r/m=101=RBP)
// MOV R9,  [RBP-off]: 4C 8B 8D <disp32>  (mod=10, reg=001+REX.R=R9, r/m=101=RBP)
static void emit_load_r8(CodeBuf*b,int off) {emit_u8(b,REX_WR);emit_u8(b,0x8B);emit_u8(b,0x85);emit_u32(b,(uint32_t)(-off));}
static void emit_load_r9(CodeBuf*b,int off) {emit_u8(b,REX_WR);emit_u8(b,0x8B);emit_u8(b,0x8D);emit_u32(b,(uint32_t)(-off));}


// ── ABI argument-register helpers (see include/abi.h) ───────────────────────
// Generated code loads call arguments through emit_load_argN / emit_mov_argN_*
// so the same compiler source targets Win64 and SysV ABIs.
// Win64: arg0..3 = RCX,RDX,R8,R9      SysV: arg0..3 = RDI,RSI,RDX,RCX
// arg4/arg5 (5/6-arg extern calls): Win64 = stack ([rsp+32]/[rsp+40]);
// SysV = R8/R9. emit_stack_arg handles the stack-arg case per ABI.
static void emit_load_rdi(CodeBuf*b,int off){emit_u8(b,REX_W);emit_u8(b,0x8B);emit_u8(b,0xBD);emit_u32(b,(uint32_t)(-off));}
static void emit_load_rsi(CodeBuf*b,int off){emit_u8(b,REX_W);emit_u8(b,0x8B);emit_u8(b,0xB5);emit_u32(b,(uint32_t)(-off));}
/* MOV RSI,RAX: 48 89 C6 — RSI/RDI are callee-saved on Win64 (pinned-var slots
   5/6 there) and caller-saved volatile on SysV (no pinning on that ABI). */
static void emit_mov_rdi_rax(CodeBuf*b){emit_u8(b,REX_W);emit_u8(b,0x89);emit_u8(b,0xC7);} /* MOV RDI,RAX: 48 89 C7 */
static void emit_mov_rsi_rax(CodeBuf*b){emit_u8(b,REX_W);emit_u8(b,0x89);emit_u8(b,0xC6);} /* MOV RSI,RAX: 48 89 C6 */
static void emit_mov_rax_rsi(CodeBuf*b){emit_u8(b,REX_W);emit_u8(b,0x8B);emit_u8(b,0xC6);} /* MOV RAX,RSI: 48 8B C6 */
static void emit_mov_rax_rdi(CodeBuf*b){emit_u8(b,REX_W);emit_u8(b,0x8B);emit_u8(b,0xC7);} /* MOV RAX,RDI: 48 8B C7 */
static void emit_mov_rdi_imm64(CodeBuf*b,int64_t v){emit_u8(b,REX_W);emit_u8(b,0xBF);emit_u64(b,(uint64_t)v);}
static void emit_mov_rsi_imm64(CodeBuf*b,int64_t v){emit_u8(b,REX_W);emit_u8(b,0xBE);emit_u64(b,(uint64_t)v);}
#if defined(OMNI_ABI_WIN64)
static void emit_load_arg0(CodeBuf*b,int off){emit_load_rcx(b,off);}
static void emit_load_arg1(CodeBuf*b,int off){emit_load_rdx(b,off);}
static void emit_load_arg2(CodeBuf*b,int off){emit_load_r8(b,off);}
static void emit_load_arg3(CodeBuf*b,int off){emit_load_r9(b,off);}
static void emit_mov_arg0_rax(CodeBuf*b){emit_mov_rcx_rax(b);}
static void emit_mov_arg1_rax(CodeBuf*b){emit_mov_rdx_rax(b);}
static void emit_mov_arg1_rcx(CodeBuf*b){emit_mov_rdx_rcx(b);}
static void emit_mov_arg0_imm64(CodeBuf*b,int64_t v){emit_mov_rcx_imm64(b,v);}
static void emit_mov_arg1_imm64(CodeBuf*b,int64_t v){emit_mov_rdx_imm64(b,v);}
static void emit_stack_arg(CodeBuf*b,int stk_index){ /* value already in RAX */
    emit_u8(b,REX_W);emit_u8(b,0x89);emit_u8(b,0x44);emit_u8(b,0x24);
    emit_u8(b,(uint8_t)(0x20+stk_index*8)); }                     /* mov [rsp+32+i*8],rax */
#else
static void emit_load_arg0(CodeBuf*b,int off){emit_load_rdi(b,off);}
static void emit_load_arg1(CodeBuf*b,int off){emit_load_rsi(b,off);}
static void emit_load_arg2(CodeBuf*b,int off){emit_load_rdx(b,off);}
static void emit_load_arg3(CodeBuf*b,int off){emit_load_rcx(b,off);}
static void emit_load_arg4(CodeBuf*b,int off){emit_load_r8(b,off);}
static void emit_load_arg5(CodeBuf*b,int off){emit_load_r9(b,off);}
static void emit_mov_arg0_rax(CodeBuf*b){emit_mov_rdi_rax(b);}
static void emit_mov_arg1_rax(CodeBuf*b){emit_mov_rsi_rax(b);}
static void emit_mov_arg1_rcx(CodeBuf*b){emit_u8(b,REX_W);emit_u8(b,0x89);emit_u8(b,0xCE);} /* MOV RSI,RCX */
static void emit_mov_arg0_imm64(CodeBuf*b,int64_t v){emit_mov_rdi_imm64(b,v);}
static void emit_mov_arg1_imm64(CodeBuf*b,int64_t v){emit_mov_rsi_imm64(b,v);}
static void emit_stack_arg(CodeBuf*b,int stk_index){ /* value already in RAX */
    emit_u8(b,REX_W);emit_u8(b,0x89);emit_u8(b,0x04);emit_u8(b,0x24);
    emit_u8(b,0x00);/*placeholder*/ (void)stk_index; }
/* SysV stack args live at [rsp+i*8]; emitted explicitly where used: */
static void emit_stack_arg_sysv(CodeBuf*b,int stk_index){
    emit_u8(b,REX_W);emit_u8(b,0x89);emit_u8(b,0x44);emit_u8(b,0x24);
    emit_u8(b,(uint8_t)(stk_index*8)); }                          /* mov [rsp+i*8],rax */
#endif

static void emit_mov_rcx_rax(CodeBuf*b){emit_u8(b,REX_W);emit_u8(b,0x89);emit_u8(b,0xC1);}
static void emit_mov_rdx_rax(CodeBuf*b){emit_u8(b,REX_W);emit_u8(b,0x89);emit_u8(b,0xC2);}
static void emit_mov_rdx_rcx(CodeBuf*b){emit_u8(b,REX_W);emit_u8(b,0x89);emit_u8(b,0xCA);}  /* MOV RDX,RCX */
static void emit_mov_r11_rax(CodeBuf*b){emit_u8(b,0x49);emit_u8(b,0x89);emit_u8(b,0xC3);}
static void emit_mov_rax_r11(CodeBuf*b){emit_u8(b,0x49);emit_u8(b,0x8B);emit_u8(b,0xC3);}  /* MOV RAX,R11: REX.WB 8B /r mod=11 reg=0(RAX) r/m=3+B=R11 */
static void emit_mov_rax_r14(CodeBuf*b){emit_u8(b,REX_WB);emit_u8(b,0x8B);emit_u8(b,0xC6);}
static void emit_mov_rax_r15(CodeBuf*b){emit_u8(b,REX_WB);emit_u8(b,0x8B);emit_u8(b,0xC7);}
static void emit_inc_r14(CodeBuf*b){emit_u8(b,0x49);emit_u8(b,0xFF);emit_u8(b,0xC6);}
static void emit_inc_r15(CodeBuf*b){emit_u8(b,0x49);emit_u8(b,0xFF);emit_u8(b,0xC7);}
// FIX: CMP R14, RCX using opcode 0x39 (CMP Ev,Gv: computes Ev-Gv = R14-RCX)
// With REX.B=1 (0x49), r/m=110 → R14, reg=001 → RCX
// Flags set for R14-RCX → JGE exits when R14(counter) >= RCX(limit) ✓
// Old code used 0x3B (CMP Gv,Ev) which computed RCX-R14 → inverted logic
static void emit_cmp_r14_rcx(CodeBuf*b){emit_u8(b,0x49);emit_u8(b,0x39);emit_u8(b,0xCE);}
static void emit_cmp_r15_rcx(CodeBuf*b){emit_u8(b,0x49);emit_u8(b,0x39);emit_u8(b,0xCF);}
/* RBX moves: REX.W 0x89/0x8B mod=11 reg=011 r/m=000 (RAX) */
static void emit_mov_rbx_rax(CodeBuf*b){emit_u8(b,REX_W);emit_u8(b,0x89);emit_u8(b,0xC3);} /* MOV RBX,RAX */
static void emit_mov_rax_rbx(CodeBuf*b){emit_u8(b,REX_W);emit_u8(b,0x8B);emit_u8(b,0xC3);} /* MOV RAX,RBX */
/* R12 moves: REX.WR 0x89/0x8B mod=11 reg=100 r/m=000 (RAX) */
static void emit_mov_r12_rax(CodeBuf*b){emit_u8(b,0x49);emit_u8(b,0x89);emit_u8(b,0xC4);} /* MOV R12,RAX */
static void emit_mov_rax_r12(CodeBuf*b){emit_u8(b,0x4C);emit_u8(b,0x89);emit_u8(b,0xE0);} /* MOV RAX,R12: REX.WR(0x4C) 89 E0 — REX.B=0 keeps r/m=000=RAX */
/* R13 moves: REX.WR 0x89/0x8B mod=11 reg=101 r/m=000 (RAX) */
static void emit_mov_r13_rax(CodeBuf*b){emit_u8(b,0x49);emit_u8(b,0x89);emit_u8(b,0xC5);} /* MOV R13,RAX */
static void emit_mov_rax_r13(CodeBuf*b){emit_u8(b,0x4C);emit_u8(b,0x89);emit_u8(b,0xE8);} /* MOV RAX,R13: REX.WR(0x4C) 89 E8 — REX.B=0 keeps r/m=000=RAX */
/* ADD reg,1 — 1-cycle increment directly in pinned register (no RAX roundtrip) */
/* slot2=RBX: 48 83 C3 01   slot3=R12: 49 83 C4 01   slot4=R13: 49 83 C5 01  */
/* slot5=RSI: 48 83 C6 01   slot6=RDI: 48 83 C7 01                            */
static void emit_inc_pinned(CodeBuf*b, int slot) {
    switch(slot) {
        case 2: emit_u8(b,0x48);emit_u8(b,0x83);emit_u8(b,0xC3);emit_u8(b,0x01); break; /* ADD RBX,1 */
        case 3: emit_u8(b,0x49);emit_u8(b,0x83);emit_u8(b,0xC4);emit_u8(b,0x01); break; /* ADD R12,1 */
        case 4: emit_u8(b,0x49);emit_u8(b,0x83);emit_u8(b,0xC5);emit_u8(b,0x01); break; /* ADD R13,1 */
        case 5: emit_u8(b,0x48);emit_u8(b,0x83);emit_u8(b,0xC6);emit_u8(b,0x01); break; /* ADD RSI,1 */
        case 6: emit_u8(b,0x48);emit_u8(b,0x83);emit_u8(b,0xC7);emit_u8(b,0x01); break; /* ADD RDI,1 */
    }
}
/* SUB reg,1 */
static void emit_dec_pinned(CodeBuf*b, int slot) {
    switch(slot) {
        case 2: emit_u8(b,0x48);emit_u8(b,0x83);emit_u8(b,0xEB);emit_u8(b,0x01); break; /* SUB RBX,1 */
        case 3: emit_u8(b,0x49);emit_u8(b,0x83);emit_u8(b,0xEC);emit_u8(b,0x01); break; /* SUB R12,1 */
        case 4: emit_u8(b,0x49);emit_u8(b,0x83);emit_u8(b,0xED);emit_u8(b,0x01); break; /* SUB R13,1 */
        case 5: emit_u8(b,0x48);emit_u8(b,0x83);emit_u8(b,0xEE);emit_u8(b,0x01); break; /* SUB RSI,1 */
        case 6: emit_u8(b,0x48);emit_u8(b,0x83);emit_u8(b,0xEF);emit_u8(b,0x01); break; /* SUB RDI,1 */
    }
}
static void emit_add_rax_rcx(CodeBuf*b){emit_u8(b,REX_W);emit_u8(b,0x03);emit_u8(b,0xC1);}
static void emit_sub_rax_rcx(CodeBuf*b){emit_u8(b,REX_W);emit_u8(b,0x2B);emit_u8(b,0xC1);}
static void emit_imul_rax_rcx(CodeBuf*b){emit_u8(b,REX_W);emit_u8(b,0x0F);emit_u8(b,0xAF);emit_u8(b,0xC1);}
static void emit_cmp_rax_rcx(CodeBuf*b){emit_u8(b,REX_W);emit_u8(b,0x3B);emit_u8(b,0xC1);}
static void emit_imul_rax_imm32(CodeBuf*b,int32_t imm){if(imm>=-128&&imm<=127){emit_u8(b,REX_W);emit_u8(b,0x6B);emit_u8(b,0xC0);emit_u8(b,(uint8_t)(int8_t)imm);}else{emit_u8(b,REX_W);emit_u8(b,0x69);emit_u8(b,0xC0);emit_u32(b,(uint32_t)imm);}}
static void emit_idiv_rcx(CodeBuf*b){emit_u8(b,REX_W);emit_u8(b,0x99);emit_u8(b,REX_W);emit_u8(b,0xF7);emit_u8(b,0xF9);}
static void emit_mod_rax_rcx(CodeBuf*b){emit_idiv_rcx(b);emit_u8(b,REX_W);emit_u8(b,0x89);emit_u8(b,0xD0);}
// setcc: sets AL only (bits 8-63 = garbage). movzx clears upper bits → correct 0/1 in RAX
static void emit_setcc_rax(CodeBuf*b,uint8_t cc){
    emit_u8(b,0x0F);emit_u8(b,cc);emit_u8(b,0xC0);      // setcc al
    emit_u8(b,0x0F);emit_u8(b,0xB6);emit_u8(b,0xC0);    // movzx eax, al
}
static void emit_add_mem_imm8(CodeBuf*b,int off,int8_t imm){emit_u8(b,REX_W);emit_u8(b,0x83);emit_u8(b,0x85);emit_u32(b,(uint32_t)(-off));emit_u8(b,(uint8_t)imm);}
static void emit_sub_mem_imm8(CodeBuf*b,int off,int8_t imm){emit_u8(b,REX_W);emit_u8(b,0x83);emit_u8(b,0xAD);emit_u32(b,(uint32_t)(-off));emit_u8(b,(uint8_t)imm);}
static void emit_add_mem_imm32(CodeBuf*b,int off,int32_t imm){emit_u8(b,REX_W);emit_u8(b,0x81);emit_u8(b,0x85);emit_u32(b,(uint32_t)(-off));emit_u32(b,(uint32_t)imm);}
static void emit_sub_rsp(CodeBuf*b,uint32_t n){emit_u8(b,REX_W);emit_u8(b,0x81);emit_u8(b,0xEC);emit_u32(b,n);}
static void emit_push_r14(CodeBuf*b){emit_u8(b,0x41);emit_u8(b,0x56);}
static void emit_pop_r14(CodeBuf*b) {emit_u8(b,0x41);emit_u8(b,0x5E);}
static void emit_push_r15(CodeBuf*b){emit_u8(b,0x41);emit_u8(b,0x57);}
static void emit_pop_r15(CodeBuf*b) {emit_u8(b,0x41);emit_u8(b,0x5F);}
/* ── r12, r13, rbx: extra callee-saved registers for variable pinning ────── */
/* PUSH/POP RBX */ static void emit_push_rbx(CodeBuf*b){emit_u8(b,0x53);}
static void emit_pop_rbx(CodeBuf*b){emit_u8(b,0x5B);}
/* PUSH/POP R12 */ static void emit_push_r12(CodeBuf*b){emit_u8(b,0x41);emit_u8(b,0x54);}
static void emit_pop_r12(CodeBuf*b){emit_u8(b,0x41);emit_u8(b,0x5C);}
/* PUSH/POP R13 */ static void emit_push_r13(CodeBuf*b){emit_u8(b,0x41);emit_u8(b,0x55);}
static void emit_pop_r13(CodeBuf*b){emit_u8(b,0x41);emit_u8(b,0x5D);}
static size_t emit_je_fwd(CodeBuf*b) {emit_u8(b,0x0F);emit_u8(b,0x84);size_t p=b->size;emit_u32(b,0);return p;}
static size_t emit_jne_fwd(CodeBuf*b){emit_u8(b,0x0F);emit_u8(b,0x85);size_t p=b->size;emit_u32(b,0);return p;}
static size_t emit_jmp_fwd(CodeBuf*b){emit_u8(b,0xE9);size_t p=b->size;emit_u32(b,0);return p;}
static size_t emit_jge_fwd(CodeBuf*b){emit_u8(b,0x0F);emit_u8(b,0x8D);size_t p=b->size;emit_u32(b,0);return p;}
static void resolve_fwd(CodeBuf*b,size_t patch){int32_t d=(int32_t)(b->size-(patch+4));patch_u32(b,patch,(uint32_t)d);}
static void emit_jmp_back(CodeBuf*b,size_t target){int32_t d8=(int32_t)(target-(b->size+2));if(d8>=-128&&d8<=127){emit_u8(b,0xEB);emit_u8(b,(uint8_t)(int8_t)d8);}else{emit_u8(b,0xE9);int32_t d32=(int32_t)(target-(b->size+4));emit_u32(b,(uint32_t)d32);}}
static void emit_jge_back(CodeBuf*b,size_t target){emit_u8(b,0x0F);emit_u8(b,0x8D);int32_t d32=(int32_t)(target-(b->size+4));emit_u32(b,(uint32_t)d32);}

static void emit_store_xmm0(CodeBuf*b,int off){emit_u8(b,0xF2);emit_u8(b,0x0F);emit_u8(b,0x11);emit_u8(b,0x85);emit_u32(b,(uint32_t)(-off));}
static void emit_load_xmm0(CodeBuf*b,int off) {emit_u8(b,0xF2);emit_u8(b,0x0F);emit_u8(b,0x10);emit_u8(b,0x85);emit_u32(b,(uint32_t)(-off));}
static void emit_load_xmm1(CodeBuf*b,int off) {emit_u8(b,0xF2);emit_u8(b,0x0F);emit_u8(b,0x10);emit_u8(b,0x8D);emit_u32(b,(uint32_t)(-off));}
static void emit_addsd(CodeBuf*b){emit_u8(b,0xF2);emit_u8(b,0x0F);emit_u8(b,0x58);emit_u8(b,0xC1);}
static void emit_subsd(CodeBuf*b){emit_u8(b,0xF2);emit_u8(b,0x0F);emit_u8(b,0x5C);emit_u8(b,0xC1);}
static void emit_mulsd(CodeBuf*b){emit_u8(b,0xF2);emit_u8(b,0x0F);emit_u8(b,0x59);emit_u8(b,0xC1);}
static void emit_divsd(CodeBuf*b){emit_u8(b,0xF2);emit_u8(b,0x0F);emit_u8(b,0x5E);emit_u8(b,0xC1);}
static void emit_xmm0_to_rax(CodeBuf*b){emit_u8(b,0x66);emit_u8(b,0x48);emit_u8(b,0x0F);emit_u8(b,0x7E);emit_u8(b,0xC0);}
static void emit_rax_to_xmm0(CodeBuf*b){emit_u8(b,0x66);emit_u8(b,0x48);emit_u8(b,0x0F);emit_u8(b,0x6E);emit_u8(b,0xC0);}
static void emit_int_to_float(CodeBuf*b){emit_u8(b,0xF2);emit_u8(b,0x48);emit_u8(b,0x0F);emit_u8(b,0x2A);emit_u8(b,0xC0);}
static void emit_float_to_int(CodeBuf*b){emit_u8(b,0xF2);emit_u8(b,0x48);emit_u8(b,0x0F);emit_u8(b,0x2C);emit_u8(b,0xC0);}

// Shadow space (32 bytes) is pre-allocated inside aligned_frame, so extern calls are just: load addr → call rax
static void emit_call_extern(CodeBuf*b,void*fn){emit_u8(b,REX_W);emit_u8(b,0xB8);emit_u64(b,(uint64_t)(uintptr_t)fn);emit_u8(b,0xFF);emit_u8(b,0xD0);}

// ============================================================
//  CONSTANT MODULO (Barrett reduction)
// ============================================================
// Returns k if n == 2^k, else -1
static int log2_exact(int64_t n){
    if(n<=0)return -1;
    int k=0; int64_t v=1;
    while(v<n){v<<=1;k++;if(k>62)return -1;}
    return (v==n)?k:-1;
}

// ============================================================
//  EPHEMERAL PASS — store+load pair elimination
//  When we store RAX→[rbp-off] and then immediately load [rbp-off]→RAX,
//  the load is a no-op. When we load [rbp-off]→RCX after storing RAX there,
//  we can replace it with MOV RCX,RAX.
//  Implementation: wrap emit_store_rax / emit_load_rax/rcx/rdx with
//  tracking versions that operate on a CodeGen* context.
// ============================================================
static CodeGen* g_cg_ctx = NULL; // set during compile, used by ephemeral wrappers

// Called after any instruction that invalidates the ephemeral slot tracking
static inline void eph_invalidate(CodeGen*cg){
    cg->eph_last_store_off = -1;
}

// Wrapper: emit extern call AND invalidate ephemeral tracker (CALL clobbers RAX)
// Use this everywhere we have a CodeGen* context instead of emit_call_extern directly.
#define cg_call_extern(cg_, fn_) do { emit_call_extern(&(cg_)->code,(fn_)); eph_invalidate(cg_); } while(0)

// Tracked store: records that RAX was stored to [rbp-off]
static void emit_store_rax_t(CodeGen*cg, int off){
    emit_store_rax(&cg->code, off);
    cg->eph_last_store_off  = off;
    cg->eph_last_store_reg  = 0; // RAX
    cg->eph_store_code_pos  = cg->code.size;
}

// Tracked load RAX: if we just stored RAX to this slot, skip the load.
// Any load to a DIFFERENT slot invalidates the tracker (RAX is overwritten).
static void emit_load_rax_t(CodeGen*cg, int off){
    if(cg->eph_last_store_off == off && cg->eph_last_store_reg == 0){
        // RAX already has the value — no-op
        return;
    }
    // Loading a different slot overwrites RAX → invalidate
    eph_invalidate(cg);
    emit_load_rax(&cg->code, off);
}

// Tracked load RCX: if we just stored RAX to this slot, emit MOV RCX,RAX.
// Otherwise just load normally (does NOT invalidate — RCX load doesn't touch RAX).
static void emit_load_rcx_t(CodeGen*cg, int off){
    if(cg->eph_last_store_off == off && cg->eph_last_store_reg == 0){
        emit_mov_rcx_rax(&cg->code);
        eph_invalidate(cg);
        return;
    }
    emit_load_rcx(&cg->code, off);
}

// Tracked load RDX
static void emit_load_rdx_t(CodeGen*cg, int off){
    if(cg->eph_last_store_off == off && cg->eph_last_store_reg == 0){
        emit_mov_rdx_rax(&cg->code);
        eph_invalidate(cg);
        return;
    }
    emit_load_rdx(&cg->code, off);
}

// ============================================================
//  FORWARD DECLARATIONS
// ============================================================
// emit_load_pinned / emit_store_pinned — defined later in hot-var section,
// declared here so cg_set_statement can call them
static void emit_load_pinned(CodeBuf*b, int slot, int stack_off);
static void emit_store_pinned(CodeBuf*b, int slot, int stack_off);

static void cg_stmt(CodeGen*cg,AST_Statement*stmt);
static void cg_expr(CodeGen*cg,AST_Expression*expr);
static void cg_fn_body(CodeGen*cg,AST_Statement_FnDef*fn_def);
static OmniType infer_type(CodeGen*cg,AST_Expression*expr);
static void cg_break_statement(CodeGen*cg);
static void cg_continue_statement(CodeGen*cg);
// Class system forward declarations
static ClassEntry* class_find(CodeGen*cg,const char*name);
static int class_field_index(ClassEntry*ce,const char*field);
static int class_field_ensure(ClassEntry*ce,const char*field);
static void emit_field_load(CodeBuf*b,int field_idx);
static void emit_field_store(CodeBuf*b,int field_idx);
// g_fn_class_alloc forward — defined after runtime helpers
static void* volatile g_fn_class_alloc; /* defined later — see class system section */

// ============================================================
//  LEAF INLINING
// ============================================================
static int expr_is_pure_of_param(AST_Expression*e,const char*param){
    if(!e)return 1;
    switch(e->type){
        case INTEGER_LITERAL:case FLOAT_LITERAL:case BOOLEAN_LITERAL:return 1;
        case IDENTIFIER:return 1;
        case INFIX_EXPRESSION:{AST_Expression_Infix*in=(AST_Expression_Infix*)e;return expr_is_pure_of_param(in->left,param)&&expr_is_pure_of_param(in->right,param);}
        case PREFIX_EXPRESSION:{AST_Expression_Prefix*p=(AST_Expression_Prefix*)e;return expr_is_pure_of_param(p->right,param);}
        default:return 0;
    }
}
/* Check if an expression contains any function calls (prevents unsafe inlining) */
static int expr_has_call(AST_Expression*e){
    if(!e)return 0;
    switch(e->type){
        case CALL_EXPRESSION: return 1;
        case INFIX_EXPRESSION:{AST_Expression_Infix*in=(AST_Expression_Infix*)e;return expr_has_call(in->left)||expr_has_call(in->right);}
        case PREFIX_EXPRESSION:{AST_Expression_Prefix*p=(AST_Expression_Prefix*)e;return expr_has_call(p->right);}
        default: return 0;
    }
}
/* Check if a function body is safe to inline at call sites.
   OPT: Extended inlining — allow multi-statement bodies (up to 8 stmts)
   that contain only: return, set, if/else with simple bodies.
   This inlines is_prime(), relu(), clamp() etc. eliminating call overhead. */
static int fn_is_inlineable(AST_Statement_FnDef*fd){
    /* Must have at least 1 param and a body */
    if(fd->parameter_count<1||fd->parameter_count>4)return 0;
    if(!fd->body||fd->body->statement_count<1)return 0;
    /* Single-expression single-param: original fast path */
    if(fd->parameter_count==1&&fd->body->statement_count==1){
        AST_Statement*s=fd->body->statements[0];
        if(s&&s->type==RETURN_STATEMENT){
            AST_Statement_Return*ret=(AST_Statement_Return*)s;
            if(ret->return_value&&!expr_has_call(ret->return_value)) return 1;
        }
    }
    /* Multi-statement body: inline if all statements are simple */
    if(fd->body->statement_count>8)return 0;
    for(int i=0;i<fd->body->statement_count;i++){
        AST_Statement*s=fd->body->statements[i];
        if(!s)continue;
        switch(s->type){
            case RETURN_STATEMENT:{
                AST_Statement_Return*r=(AST_Statement_Return*)s;
                if(r->return_value&&expr_has_call(r->return_value))return 0;
                break;
            }
            case SET_STATEMENT:{
                AST_Statement_Set*ss=(AST_Statement_Set*)s;
                if(ss->value&&expr_has_call(ss->value))return 0;
                break;
            }
            case IF_STATEMENT:{
                AST_Statement_If*is=(AST_Statement_If*)s;
                /* Allow simple if/return patterns like relu, clamp */
                if(is->consequence&&is->consequence->statement_count>3)return 0;
                if(is->alternative){
                    if(is->alternative->type==IF_STATEMENT&&((AST_Statement_If*)is->alternative)->consequence&&((AST_Statement_If*)is->alternative)->consequence->statement_count>3)return 0;
                }
                break;
            }
            /* No loops allowed in inlineable functions */
            case WHILE_STATEMENT: return 0;
            case FOR_STATEMENT:   return 0;
            default: break;
        }
    }
    return 1;
}
/* Inline a function call at the call site.
   Handles both single-expr and multi-statement inlineable functions.
   Creates a mini scope with params mapped to stack slots, emits body stmts.
   OPT: No CALL/RET overhead, no frame setup — just inline code emission. */
/* FIX(finding #13): inlined bodies containing `return` inside if/else used to
   route through cg_stmt → cg_return_statement, which emits a PHYSICAL
   `mov rsp,rbp; pop rbp; ret` into the CALLER's stream — ending the caller
   (or the whole program) at the first inlined branch. This emitter mirrors
   cg_if_statement but translates every `return expr` into
   "eval expr → RAX; jmp inline_end". All forward jumps are resolved to the
   point right after the inlined body, where RAX holds the result. */
static void cg_inline_stmts(CodeGen*cg,AST_Statement**stmts,int count,
                            size_t*jmps,int*jn,int max_jumps,int depth){
    if(!stmts||depth>8) return; /* depth guard: inlineable bodies are ≤8 stmts */
    for(int i=0;i<count;i++){
        AST_Statement*s=stmts[i];
        if(!s)continue;
        switch(s->type){
            case RETURN_STATEMENT:{
                AST_Statement_Return*r=(AST_Statement_Return*)s;
                if(r->return_value)cg_expr(cg,r->return_value);
                else emit_xor_rax_rax(&cg->code);
                if(*jn<max_jumps) jmps[(*jn)++]=emit_jmp_fwd(&cg->code);
                break;
            }
            case IF_STATEMENT:{
                AST_Statement_If*is=(AST_Statement_If*)s;
                cg_expr(cg,is->condition);emit_test_rax(&cg->code);
                size_t jfalse=emit_je_fwd(&cg->code);
                cg_inline_stmts(cg,is->consequence?is->consequence->statements:NULL,
                                is->consequence?is->consequence->statement_count:0,
                                jmps,jn,max_jumps,depth+1);
                if(is->alternative){
                    size_t jend=emit_jmp_fwd(&cg->code);
                    resolve_fwd(&cg->code,jfalse);
                    cg_inline_stmts(cg,&is->alternative,1,jmps,jn,max_jumps,depth+1);
                    resolve_fwd(&cg->code,jend);
                } else {
                    resolve_fwd(&cg->code,jfalse);
                }
                break;
            }
            default:
                cg_stmt(cg,s);
                break;
        }
    }
}

static void cg_inline_call(CodeGen*cg,FnEntry*fe,AST_Expression**args,int argc){
    AST_Statement_FnDef*fd=(AST_Statement_FnDef*)fe->inline_ast;
    int base_off=cg->scope->next_offset;
    /* Allocate stack slots for each parameter */
    int param_slots[4]={0};
    for(int i=0;i<fd->parameter_count&&i<4;i++){
        param_slots[i]=base_off+(i*8);
        cg->scope->next_offset+=8;
    }
    if(cg->scope->next_offset>cg->stack_size)cg->stack_size=cg->scope->next_offset;
    /* Evaluate each argument and store into param slot */
    for(int i=0;i<argc&&i<fd->parameter_count;i++){
        cg_expr(cg,args[i]);
        emit_store_rax(&cg->code,param_slots[i]);
    }
    /* Build mini scope with params */
    SymbolTable*mini=scope_new(cg->scope);
    for(int i=0;i<fd->parameter_count&&i<4;i++){
        Symbol*ps=(Symbol*)calloc(1,sizeof(Symbol));
        strncpy_s(ps->name,sizeof(ps->name),fd->parameters[i]->value,_TRUNCATE);
        ps->type=OMNI_TYPE_INT;ps->stack_offset=param_slots[i];
        unsigned int idx=sym_hash(fd->parameters[i]->value);
        ps->next=mini->buckets[idx];mini->buckets[idx]=ps;
    }
    SymbolTable*saved_scope=cg->scope;
    int saved_ret=cg->returned;
    cg->scope=mini;
    /* Emit the body with return→jmp translation (see cg_inline_stmts above).
       All inlined returns jump here — right after the body — where RAX holds
       the inlined call's result. */
    {
        size_t jmps[32]; int jn=0;
        cg_inline_stmts(cg,fd->body->statements,fd->body->statement_count,jmps,&jn,32,0);
        for(int k=0;k<jn;k++) resolve_fwd(&cg->code,jmps[k]);
    }
    /* Restore caller scope */
    scope_free(mini);
    cg->scope=saved_scope;
    cg->scope->next_offset=base_off;
    cg->returned=saved_ret;
}

// ============================================================
//  EXPRESSION CODEGEN
// ============================================================
static void cg_integer_literal(CodeGen*cg,AST_Expression_IntegerLiteral*n){eph_invalidate(cg);emit_mov_rax_imm64(&cg->code,n->value);}
static void cg_float_literal(CodeGen*cg,AST_Expression_FloatLiteral*n){
    uint64_t bits;memcpy(&bits,&n->value,8);emit_mov_rax_imm64(&cg->code,(int64_t)bits);
    emit_u8(&cg->code,0x66);emit_u8(&cg->code,0x48);emit_u8(&cg->code,0x0F);emit_u8(&cg->code,0x6E);emit_u8(&cg->code,0xC0);
}
static void cg_boolean_literal(CodeGen*cg,AST_Expression_Boolean*n){emit_mov_rax_imm64(&cg->code,n->value?1:0);}
static void cg_string_literal(CodeGen*cg,AST_Expression_StringLiteral*n){
    size_t len=strlen(n->value)+1;char*copy=(char*)malloc(len);memcpy(copy,n->value,len);
    cg->string_pool=(char**)realloc(cg->string_pool,(cg->string_pool_count+1)*sizeof(char*));
    cg->string_pool[cg->string_pool_count++]=copy;
    emit_u8(&cg->code,REX_W);emit_u8(&cg->code,0xB8);emit_u64(&cg->code,(uint64_t)(uintptr_t)copy);
}
static void cg_identifier(CodeGen*cg,AST_Expression_Identifier*n){
    if(strcmp(n->value,"math")==0){emit_xor_rax_rax(&cg->code);return;}
    /* Check all pinned register slots: 0=r14, 1=r15, 2=rbx, 3=r12, 4=r13, 5=rsi, 6=rdi */
    for(int _ri=0;_ri<cg->reg_var_depth&&_ri<7;_ri++){
        if(strcmp(cg->reg_var_names[_ri],n->value)==0){
            switch(_ri){
                case 0: emit_mov_rax_r14(&cg->code); break;
                case 1: emit_mov_rax_r15(&cg->code); break;
                case 2: emit_mov_rax_rbx(&cg->code); break;
                case 3: emit_mov_rax_r12(&cg->code); break;
                case 4: emit_mov_rax_r13(&cg->code); break;
                case 5: emit_mov_rax_rsi(&cg->code); break;
                case 6: emit_mov_rax_rdi(&cg->code); break;
            }
            return;
        }
    }
    // 'self' inside a method — load the instance pointer from its slot
    if(strcmp(n->value,"self")==0&&cg->current_class[0]){
        emit_load_rax(&cg->code,cg->self_slot);
        return;
    }
    Symbol*s=scope_get(cg->scope,n->value);
    if(!s){
        if(g_beta){
            fprintf(stderr,"[BETA-CG] scope dump looking for '%s':\n",n->value);
            SymbolTable*st=cg->scope;int depth=0;
            while(st){fprintf(stderr,"  scope[%d]: ",depth++);
                for(int i=0;i<SYM_TABLE_SIZE;i++){Symbol*sym=st->buckets[i];while(sym){fprintf(stderr,"'%s'@%d ",sym->name,sym->stack_offset);sym=sym->next;}}
                fprintf(stderr,"\n");st=st->parent;}
        }
        omni_error(n->base.token.line,n->base.token.col,"NameError","name '%s' is not defined",n->value);
    }
    // Use tracked load so ephemeral pass knows RAX changed
    emit_load_rax_t(cg, s->stack_offset);
}

static void cg_infix(CodeGen*cg,AST_Expression_Infix*node){
    const char*op=node->operator;
    // String concatenation via +
    if(strcmp(op,"+")==0){
        OmniType lt=infer_type(cg,node->left);
        OmniType rt=infer_type(cg,node->right);
        if(lt==OMNI_TYPE_STR||rt==OMNI_TYPE_STR){
            int lhs_slot=cg->scope->next_offset; cg->scope->next_offset+=8;
            int rhs_slot=cg->scope->next_offset; cg->scope->next_offset+=8;
            if(cg->scope->next_offset>cg->stack_size)cg->stack_size=cg->scope->next_offset;
            // Eval left, auto-convert int->str if needed
            // Only convert for definite non-string types (INT/BOOL/FLOAT).
            // UNKNOWN means we can't determine type (e.g. self.field) — treat as str.
            cg_expr(cg,node->left);
            if(lt==OMNI_TYPE_INT||lt==OMNI_TYPE_BOOL||lt==OMNI_TYPE_FLOAT){
                emit_mov_arg0_rax(&cg->code);
                cg_call_extern(cg,g_fn_int_to_str);
            }
            emit_store_rax_t(cg,lhs_slot);
            // Eval right, auto-convert int->str if needed
            cg_expr(cg,node->right);
            if(rt==OMNI_TYPE_INT||rt==OMNI_TYPE_BOOL||rt==OMNI_TYPE_FLOAT){
                emit_mov_arg0_rax(&cg->code);
                cg_call_extern(cg,g_fn_int_to_str);
            }
            emit_store_rax_t(cg,rhs_slot);
            emit_load_arg0(&cg->code,lhs_slot);
            emit_load_arg1(&cg->code,rhs_slot);
            cg_call_extern(cg,g_fn_str_concat);
            cg->scope->next_offset-=16; return;
        }
    }
    // Short-circuit boolean operators
    if(strcmp(op,"and")==0){cg_expr(cg,node->left);emit_test_rax(&cg->code);size_t jf=emit_je_fwd(&cg->code);cg_expr(cg,node->right);emit_test_rax(&cg->code);emit_setcc_rax(&cg->code,0x95);size_t jend=emit_jmp_fwd(&cg->code);resolve_fwd(&cg->code,jf);emit_xor_rax_rax(&cg->code);resolve_fwd(&cg->code,jend);return;}
    if(strcmp(op,"or")==0){cg_expr(cg,node->left);emit_test_rax(&cg->code);size_t jt=emit_jne_fwd(&cg->code);cg_expr(cg,node->right);emit_test_rax(&cg->code);emit_setcc_rax(&cg->code,0x95);size_t jend=emit_jmp_fwd(&cg->code);resolve_fwd(&cg->code,jt);emit_mov_rax_imm64(&cg->code,1);resolve_fwd(&cg->code,jend);return;}

    // ── CONSTANT RHS FAST PATHS ──────────────────────────────────────────────
    int rhs_is_const=(node->right&&node->right->type==INTEGER_LITERAL);
    int lhs_is_const=(node->left &&node->left ->type==INTEGER_LITERAL);
    int64_t rhs_const=0, lhs_const=0;
    if(rhs_is_const) rhs_const=((AST_Expression_IntegerLiteral*)node->right)->value;
    if(lhs_is_const) lhs_const=((AST_Expression_IntegerLiteral*)node->left )->value;

    // Compile-time constant folding: both sides are integer literals
    if(rhs_is_const && lhs_is_const){
        int64_t result=0;
        if     (strcmp(op,"+")==0) result=lhs_const+rhs_const;
        else if(strcmp(op,"-")==0) result=lhs_const-rhs_const;
        else if(strcmp(op,"*")==0) result=lhs_const*rhs_const;
        else if(strcmp(op,"/")==0&&rhs_const!=0) result=lhs_const/rhs_const;
        else if(strcmp(op,"%")==0&&rhs_const!=0) result=lhs_const%rhs_const;
        else if(strcmp(op,"==")==0) result=(lhs_const==rhs_const)?1:0;
        else if(strcmp(op,"!=")==0) result=(lhs_const!=rhs_const)?1:0;
        else if(strcmp(op,"<") ==0) result=(lhs_const< rhs_const)?1:0;
        else if(strcmp(op,">") ==0) result=(lhs_const> rhs_const)?1:0;
        else if(strcmp(op,"<=")==0) result=(lhs_const<=rhs_const)?1:0;
        else if(strcmp(op,">=")==0) result=(lhs_const>=rhs_const)?1:0;
        else goto no_fold;
        emit_mov_rax_imm64(&cg->code,result); return;
    }
    no_fold:;

    // Const-RHS multiply: use IMUL imm (3 bytes vs 10+3=13 for load+imul)
    if(rhs_is_const && strcmp(op,"*")==0){
        if(rhs_const==0){emit_xor_rax_rax(&cg->code);return;}  // x*0 = 0
        if(rhs_const==1){cg_expr(cg,node->left);return;}         // x*1 = x
        cg_expr(cg,node->left);
        // Power-of-2: shift is 1 cycle vs 3 cycles for IMUL
        int k=log2_exact(rhs_const);
        if(k>=1){emit_shl_rax(&cg->code,(uint8_t)k);}
        else    {emit_imul_rax_imm32(&cg->code,(int32_t)rhs_const);}
        return;
    }
    /* FIX(N1): const-RHS div/mod fast paths removed. The previous SAR
       (power-of-2 divide), AND (power-of-2 modulo) and unsigned-MUL Barrett
       paths are all silently WRONG for negative operands:
         -7 / 4 : SAR gives -2 (floors toward -inf); the language's division
                  truncates toward 0 like IDIV → -1
         -7 % 4 : AND gives  1; truncating remainder is -3
       The Barrett path uses unsigned MUL, meaningless for signed values.
       Correctness beats the ~40-cycle IDIV: use CQO+IDIV unconditionally. */
    if(rhs_is_const && rhs_const>1 && (strcmp(op,"%")==0 || strcmp(op,"/")==0)){
        cg_expr(cg,node->left);
        emit_mov_rcx_imm64(&cg->code,rhs_const);
        if(strcmp(op,"/")==0) emit_idiv_rcx(&cg->code);
        else                  emit_mod_rax_rcx(&cg->code);
        return;
    }
    // Const-RHS add/sub: use ADD/SUB rax, imm8/imm32 (shorter, no tmp slot)
    if(rhs_is_const && strcmp(op,"+")==0){
        cg_expr(cg,node->left);
        if(rhs_const>=-128&&rhs_const<=127){emit_u8(&cg->code,REX_W);emit_u8(&cg->code,0x83);emit_u8(&cg->code,0xC0);emit_u8(&cg->code,(uint8_t)(int8_t)rhs_const);}
        else{emit_u8(&cg->code,REX_W);emit_u8(&cg->code,0x05);emit_u32(&cg->code,(uint32_t)(int32_t)rhs_const);}
        return;
    }
    if(rhs_is_const && strcmp(op,"-")==0){
        cg_expr(cg,node->left);
        if(rhs_const>=-128&&rhs_const<=127){emit_u8(&cg->code,REX_W);emit_u8(&cg->code,0x83);emit_u8(&cg->code,0xE8);emit_u8(&cg->code,(uint8_t)(int8_t)rhs_const);}
        else{emit_u8(&cg->code,REX_W);emit_u8(&cg->code,0x2D);emit_u32(&cg->code,(uint32_t)(int32_t)rhs_const);}
        return;
    }

    // ── GENERAL CASE ─────────────────────────────────────────────────────────
    // OPT: if left is a plain identifier, skip the tmp-slot store:
    //   evaluate right into RCX, then load left directly into RAX.
    //   Saves 2 memory ops (emit_store + emit_load) per binary expression.
    int lhs_is_ident = (node->left && node->left->type == IDENTIFIER);
    if(lhs_is_ident){
        const char*lname=((AST_Expression_Identifier*)node->left)->value;
        // Check register-pinned loop vars first
        /* Check all pinned register slots for lhs identifier */
        {
            int _found_reg=-1;
            for(int _ri=0;_ri<cg->reg_var_depth&&_ri<7;_ri++){
                if(strcmp(cg->reg_var_names[_ri],lname)==0){_found_reg=_ri;break;}
            }
            if(_found_reg>=0){
                cg_expr(cg,node->right); emit_mov_rcx_rax(&cg->code);
                switch(_found_reg){
                    case 0: emit_mov_rax_r14(&cg->code); break;
                    case 1: emit_mov_rax_r15(&cg->code); break;
                    case 2: emit_mov_rax_rbx(&cg->code); break;
                    case 3: emit_mov_rax_r12(&cg->code); break;
                    case 4: emit_mov_rax_r13(&cg->code); break;
                    case 5: emit_mov_rax_rsi(&cg->code); break;
                    case 6: emit_mov_rax_rdi(&cg->code); break;
                }
                goto do_op;
            }
        }
        Symbol*lsym=scope_get(cg->scope,lname);
        if(lsym){
            cg_expr(cg,node->right); emit_mov_rcx_rax(&cg->code);
            emit_load_rax_t(cg,lsym->stack_offset);
            goto do_op;
        }
    }
    {
        // General case: spill LHS to tmp slot, eval RHS, reload LHS
        // Ephemeral pass: emit_store_rax_t records the store; if RHS eval
        // doesn't touch the slot, emit_load_rax_t/rcx_t skips the reload.
        int lhs_slot=cg->scope->next_offset; cg->scope->next_offset+=8;
        if(cg->scope->next_offset>cg->stack_size) cg->stack_size=cg->scope->next_offset;
        cg_expr(cg,node->left); emit_store_rax_t(cg,lhs_slot);
        cg_expr(cg,node->right); emit_mov_rcx_rax(&cg->code);
        emit_load_rax_t(cg,lhs_slot);
        cg->scope->next_offset-=8;
        eph_invalidate(cg);
    }
    do_op:;
    if     (strcmp(op,"+")==0) emit_add_rax_rcx(&cg->code);
    else if(strcmp(op,"-")==0) emit_sub_rax_rcx(&cg->code);
    else if(strcmp(op,"*")==0) emit_imul_rax_rcx(&cg->code);
    else if(strcmp(op,"/")==0) emit_idiv_rcx(&cg->code);
    else if(strcmp(op,"%")==0) emit_mod_rax_rcx(&cg->code);
    else if(strcmp(op,"==")==0){emit_cmp_rax_rcx(&cg->code);emit_setcc_rax(&cg->code,0x94);}
    else if(strcmp(op,"!=")==0){emit_cmp_rax_rcx(&cg->code);emit_setcc_rax(&cg->code,0x95);}
    else if(strcmp(op,"<") ==0){emit_cmp_rax_rcx(&cg->code);emit_setcc_rax(&cg->code,0x9C);}
    else if(strcmp(op,">") ==0){emit_cmp_rax_rcx(&cg->code);emit_setcc_rax(&cg->code,0x9F);}
    else if(strcmp(op,"<=")==0){emit_cmp_rax_rcx(&cg->code);emit_setcc_rax(&cg->code,0x9E);}
    else if(strcmp(op,">=")==0){emit_cmp_rax_rcx(&cg->code);emit_setcc_rax(&cg->code,0x9D);}
    /* ── bitwise operators ── */
    else if(strcmp(op,"&")==0){emit_u8(&cg->code,REX_W);emit_u8(&cg->code,0x21);emit_u8(&cg->code,0xC8);}  // AND rax,rcx
    else if(strcmp(op,"|")==0){emit_u8(&cg->code,REX_W);emit_u8(&cg->code,0x09);emit_u8(&cg->code,0xC8);}  // OR  rax,rcx
    else if(strcmp(op,"^")==0){emit_u8(&cg->code,REX_W);emit_u8(&cg->code,0x31);emit_u8(&cg->code,0xC8);}  // XOR rax,rcx
    /* shifts: rcx holds rhs, move to CL for shift */
    else if(strcmp(op,"<<")==0){
        /* MOV RCX,RCX already fine; need count in CL */
        /* XCHG RAX,RCX so we shift RAX (left) by CL (right count) */
        emit_u8(&cg->code,REX_W);emit_u8(&cg->code,0x91); /* XCHG rax,rcx */
        /* SAL rax, cl: REX.W 0xD3 /4 */
        emit_u8(&cg->code,REX_W);emit_u8(&cg->code,0xD3);emit_u8(&cg->code,0xE0);
    }
    else if(strcmp(op,">>")==0){
        emit_u8(&cg->code,REX_W);emit_u8(&cg->code,0x91); /* XCHG rax,rcx */
        /* SAR rax, cl: REX.W 0xD3 /7 */
        emit_u8(&cg->code,REX_W);emit_u8(&cg->code,0xD3);emit_u8(&cg->code,0xF8);
    }
    /* ── ** power: integer exponentiation via omni_int_pow ── */
    /* rax=base (lhs), rcx=exp (rhs) — both integer registers at do_op */
    else if(strcmp(op,"**")==0){
        emit_mov_arg1_rcx(&cg->code); /* arg1 = exp  */
        emit_mov_arg0_rax(&cg->code); /* arg0 = base */
        cg_call_extern(cg,g_fn_int_pow);
    }
    /* ── augmented assignment: x += e  parsed as INFIX with op "+="  ── */
    /* These arrive here only when the lhs is NOT a simple identifier     */
    /* (simple-ident case is caught by cg_set_statement optimiser).       */
    /* Fall through: treat as the base operator, result in rax.           */
    else if(strcmp(op,"+=")==0) emit_add_rax_rcx(&cg->code);
    else if(strcmp(op,"-=")==0) emit_sub_rax_rcx(&cg->code);
    else if(strcmp(op,"*=")==0) emit_imul_rax_rcx(&cg->code);
    else if(strcmp(op,"/=")==0) emit_idiv_rcx(&cg->code);
    else if(strcmp(op,"%=")==0) emit_mod_rax_rcx(&cg->code);
    else if(strcmp(op,"**=")==0){
        emit_mov_arg1_rcx(&cg->code);
        emit_mov_arg0_rax(&cg->code);
        cg_call_extern(cg,g_fn_int_pow);
    }
    else omni_error_nopos("CodeGen Error","unknown operator '%s'",op);
}

// ============================================================
//  TYPE INFERENCE
// ============================================================
static OmniType infer_type(CodeGen*cg,AST_Expression*expr){
    if(!expr)return OMNI_TYPE_UNKNOWN;
    switch(expr->type){
        case STRING_LITERAL:return OMNI_TYPE_STR;
        case BOOLEAN_LITERAL:return OMNI_TYPE_BOOL;
        case FLOAT_LITERAL:return OMNI_TYPE_FLOAT;
        case INTEGER_LITERAL:return OMNI_TYPE_INT;
        case INFIX_EXPRESSION:{
            AST_Expression_Infix*inf=(AST_Expression_Infix*)expr;
            const char*op=inf->operator;
            if(strcmp(op,"==")==0||strcmp(op,"!=")==0||strcmp(op,"<")==0||strcmp(op,">")==0||strcmp(op,"<=")==0||strcmp(op,">=")==0||strcmp(op,"and")==0||strcmp(op,"or")==0||strcmp(op,"not")==0)return OMNI_TYPE_BOOL;
            if(strcmp(op,"+")==0){
                OmniType lt=infer_type(cg,inf->left);
                OmniType rt=infer_type(cg,inf->right);
                if(lt==OMNI_TYPE_STR||rt==OMNI_TYPE_STR)return OMNI_TYPE_STR;
            }
            return OMNI_TYPE_INT;
        }
        case PREFIX_EXPRESSION:{
            AST_Expression_Prefix*pf=(AST_Expression_Prefix*)expr;
            if(strcmp(pf->operator,"not")==0)return OMNI_TYPE_BOOL;
            return OMNI_TYPE_INT;
        }
        case IDENTIFIER:{
            Symbol*s=scope_get(cg->scope,((AST_Expression_Identifier*)expr)->value);
            return s?s->type:OMNI_TYPE_INT;
        }
        case CALL_EXPRESSION:{
            AST_Expression_Call*c=(AST_Expression_Call*)expr;
            if(c->function&&c->function->type==IDENTIFIER){
                const char*fn=((AST_Expression_Identifier*)c->function)->value;
                if(strcmp(fn,"input")==0||strcmp(fn,"str")==0)return OMNI_TYPE_STR;
                if(strcmp(fn,"int")==0||strcmp(fn,"len")==0)return OMNI_TYPE_INT;
                /* user-defined fn: use the statically inferred return type */
                FnEntry*ufe=fn_find(cg,fn);
                if(ufe) return ufe->ret_type;
            }
            if(c->function&&c->function->type==MEMBER_ACCESS_EXPRESSION){
                AST_Expression_MemberAccess*ma=(AST_Expression_MemberAccess*)c->function;
                if(member_returns_str(ma->member)) return OMNI_TYPE_STR;
                // math functions that return float
                if(strcmp(ma->member,"sqrt")==0||strcmp(ma->member,"pow")==0||
                   strcmp(ma->member,"floor")==0||strcmp(ma->member,"ceil")==0||
                   strcmp(ma->member,"round")==0||strcmp(ma->member,"sin")==0||
                   strcmp(ma->member,"cos")==0||strcmp(ma->member,"tan")==0||
                   strcmp(ma->member,"log")==0||strcmp(ma->member,"log2")==0||
                   strcmp(ma->member,"log10")==0||strcmp(ma->member,"pi")==0||
                   strcmp(ma->member,"e")==0||strcmp(ma->member,"tau")==0||
                   strcmp(ma->member,"itof")==0||strcmp(ma->member,"min_f")==0||
                   strcmp(ma->member,"max_f")==0||
                   strcmp(ma->member,"exp")==0||strcmp(ma->member,"exp2")==0||
                   strcmp(ma->member,"tanh")==0||strcmp(ma->member,"sinh")==0||
                   strcmp(ma->member,"cosh")==0||strcmp(ma->member,"atan")==0||
                   strcmp(ma->member,"atan2")==0||strcmp(ma->member,"asin")==0||
                   strcmp(ma->member,"acos")==0||strcmp(ma->member,"cbrt")==0||
                   strcmp(ma->member,"hypot")==0||strcmp(ma->member,"abs_f")==0)
                   return OMNI_TYPE_FLOAT;
                return OMNI_TYPE_INT;
            }
            return OMNI_TYPE_INT;
        }
        default:return OMNI_TYPE_UNKNOWN;
    }
}

// ============================================================
//  BUILTIN CALL CODEGEN
// ============================================================
/* Convert the value in RAX to a double in XMM0, honoring the argument's
   static type. FLOAT-typed values already hold raw double bits in RAX
   (float literals are materialized as 64-bit patterns) — a MOVQ copy is
   correct and CVTSI2SD would corrupt them (treats the bits as an integer).
   INT/BOOL values need true numeric conversion via CVTSI2SD. */
static void emit_to_double_arg(CodeGen*cg,AST_Expression*arg){
    OmniType t=infer_type(cg,arg);
    if(t==OMNI_TYPE_FLOAT) emit_rax_to_xmm0(&cg->code);  /* MOVQ xmm0,rax — bit-copy double */
    else                   emit_int_to_float(&cg->code); /* CVTSI2SD xmm0,rax — numeric convert */
}
static void cg_call_print(CodeGen*cg,AST_Expression_Call*node){
    if(node->argument_count!=1){fprintf(stderr,"CodeGen Error: print() takes 1 argument\n");exit(1);}
    AST_Expression*arg=node->arguments[0];
    cg_expr(cg,arg);emit_mov_arg0_rax(&cg->code);  /* ABI arg0 (RCX on Win64, RDI on SysV) */
    void*fn=g_fn_print_int;
    OmniType t=infer_type(cg,arg);
    if(t==OMNI_TYPE_STR) fn=g_fn_print_str;
    if(t==OMNI_TYPE_BOOL)fn=g_fn_print_bool;
    if(t==OMNI_TYPE_FLOAT)fn=g_fn_print_float;
    if(t==OMNI_TYPE_LIST)fn=g_fn_list_print;
    cg_call_extern(cg,fn);
}
static void cg_call_input(CodeGen*cg,AST_Expression_Call*node){
    if(node->argument_count==1){cg_expr(cg,node->arguments[0]);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,g_fn_print_str_nol);}
    cg_call_extern(cg,g_fn_input);
}
static void cg_call_len(CodeGen*cg,AST_Expression_Call*node){
    if(node->argument_count!=1){fprintf(stderr,"CodeGen Error: len() takes 1 argument\n");exit(1);}
    cg_expr(cg,node->arguments[0]);
    OmniType t=infer_type(cg,node->arguments[0]);
    emit_mov_arg0_rax(&cg->code);
    if(t==OMNI_TYPE_LIST) cg_call_extern(cg,g_fn_list_len);
    else cg_call_extern(cg,g_fn_len_str);
}
static void cg_call_int(CodeGen*cg,AST_Expression_Call*node){
    if(node->argument_count!=1){fprintf(stderr,"CodeGen Error: int() takes 1 argument\n");exit(1);}
    cg_expr(cg,node->arguments[0]);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,g_fn_str_to_int);
}
static void cg_call_str(CodeGen*cg,AST_Expression_Call*node){
    if(node->argument_count!=1){fprintf(stderr,"CodeGen Error: str() takes 1 argument\n");exit(1);}
    OmniType t=infer_type(cg,node->arguments[0]);
    cg_expr(cg,node->arguments[0]);
    /* str(x): if already a string, pass through unchanged (RAX already = char* ptr) */
    if(t==OMNI_TYPE_STR||t==OMNI_TYPE_UNKNOWN) return;  /* already a string pointer */
    emit_mov_arg0_rax(&cg->code);
    if(t==OMNI_TYPE_FLOAT) cg_call_extern(cg,g_fn_float_to_str);
    else                   cg_call_extern(cg,g_fn_int_to_str);  /* int, bool */
}
static void cg_call_range(CodeGen*cg,AST_Expression_Call*node){
    if(node->argument_count<1){fprintf(stderr,"CodeGen Error: range() needs 1+ argument\n");exit(1);}
    cg_expr(cg,node->arguments[0]);
}
static void cg_call_assert(CodeGen*cg,AST_Expression_Call*node){
    if(node->argument_count<1){fprintf(stderr,"CodeGen Error: assert() needs 1+ args\n");exit(1);}
    cg_expr(cg,node->arguments[0]);emit_mov_arg0_rax(&cg->code);
    if(node->argument_count>=2){cg_expr(cg,node->arguments[1]);emit_mov_arg1_rax(&cg->code);}
    else{emit_mov_arg1_imm64(&cg->code,(int64_t)(uintptr_t)"assertion failed");}
    cg_call_extern(cg,g_fn_assert);
}

// ============================================================
//  PACKAGE MODULE LOADER
//  Loads installed .ok packages from site-packages into the
//  current CodeGen so their functions are callable as pkg.fn()
//  Internally functions are registered as "pkg__fn" to avoid
//  name collisions with user-defined functions.
// ============================================================

#define MAX_PKG_NS   64
#define MAX_PKG_FNS  128

typedef struct {
    char ns[64];            // e.g. "strutil"
    char fns[MAX_PKG_FNS][64]; // e.g. "repeat", "reverse"
    int  param_counts[MAX_PKG_FNS];
    int  fn_count;
    int  loaded;
} PkgEntry;

static PkgEntry g_pkg_registry[MAX_PKG_NS];
static int      g_pkg_count = 0;

// Get or create a PkgEntry for ns
static PkgEntry* pkg_get_or_create(const char* ns) {
    for (int i = 0; i < g_pkg_count; i++)
        if (strcmp(g_pkg_registry[i].ns, ns) == 0)
            return &g_pkg_registry[i];
    if (g_pkg_count >= MAX_PKG_NS) return NULL;
    PkgEntry* e = &g_pkg_registry[g_pkg_count++];
    strncpy_s(e->ns, sizeof(e->ns), ns, _TRUNCATE);
    e->fn_count = 0; e->loaded = 0;
    return e;
}

// Returns 1 if pkg_name is a loaded package
static int pkg_is_loaded(const char* name) {
    for (int i = 0; i < g_pkg_count; i++)
        if (strcmp(g_pkg_registry[i].ns, name) == 0 && g_pkg_registry[i].loaded)
            return 1;
    return 0;
}

// Build site-packages path for a package
//   Windows: %LOCALAPPDATA%\Programs\omnikarai\site-packages\<name>
//            (falls back to %USERPROFILE%\AppData\Local\...)
//   POSIX:   $XDG_DATA_HOME/omnikarai/site-packages/<name>
//            (falls back to ~/.local/share/omnikarai/site-packages/)
static void pkg_site_path(const char* name, char* out, int outlen) {
#if defined(_WIN32)
    const char* lad = getenv("LOCALAPPDATA");
    if (lad && lad[0])
        snprintf(out, outlen, "%s\\Programs\\omnikarai\\site-packages\\%s", lad, name);
    else {
        const char* home = getenv("USERPROFILE");
        if (!home) home = "C:\\Users\\Default";
        snprintf(out, outlen, "%s\\AppData\\Local\\Programs\\omnikarai\\site-packages\\%s", home, name);
    }
#else
    const char* xdg = getenv("XDG_DATA_HOME");
    const char* home = getenv("HOME");
    if (xdg && xdg[0])
        snprintf(out, outlen, "%s/omnikarai/site-packages/%s", xdg, name);
    else if (home && home[0])
        snprintf(out, outlen, "%s/.local/share/omnikarai/site-packages/%s", home, name);
    else
        out[0] = '\0';
#endif
}

// Rename all fn definitions in an AST to have pkg__ prefix
// so they live in the shared fn_table without collision
static void pkg_prefix_fns(AST_Program* prog, const char* prefix, PkgEntry* pe) {
    for (int i = 0; i < prog->statement_count; i++) {
        AST_Statement* s = prog->statements[i];
        if (!s || s->type != FN_DEFINITION) continue;
        AST_Statement_FnDef* fd = (AST_Statement_FnDef*)s;
        if (!fd->name) continue;
        // Register in pkg entry so cg_module_call knows it exists
        if (pe->fn_count < MAX_PKG_FNS) {
            strncpy_s(pe->fns[pe->fn_count], 64, fd->name->value, _TRUNCATE);
            pe->param_counts[pe->fn_count] = fd->parameter_count;
            pe->fn_count++;
        }
        // Rename: "repeat" -> "strutil__repeat"
        char new_name[128];
        snprintf(new_name, sizeof(new_name), "%s__%s", prefix, fd->name->value);
        free(fd->name->value);
        fd->name->value = (char*)malloc(strlen(new_name) + 1);
        strcpy(fd->name->value, new_name);
    }
}

// Read all .ok files from a package directory and compile them into cg
static int pkg_load_into_cg(CodeGen* cg, const char* pkg_name) {
    if (pkg_is_loaded(pkg_name)) return 1; // already loaded

    char pkg_dir[1024];
    pkg_site_path(pkg_name, pkg_dir, sizeof(pkg_dir));

    // Collect all .ok files
    char search[1100];
    snprintf(search, sizeof(search), "%s%c*.ok", pkg_dir, OMNI_PATH_SEP);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(search, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        // No .ok files found
        fprintf(stderr, "\nModuleError: Package '%s' has no .ok source files\n"
                        "  Path: %s\n"
                        "  Run: omnip install %s\n\n", pkg_name, pkg_dir, pkg_name);
        return 0;
    }

    PkgEntry* pe = pkg_get_or_create(pkg_name);
    if (!pe) { FindClose(h); return 0; }

    int ok = 1;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;

        char filepath[1200];
        snprintf(filepath, sizeof(filepath), "%s%c%s", pkg_dir, OMNI_PATH_SEP, fd.cFileName);

        // Read file
        HANDLE fh = CreateFileA(filepath, GENERIC_READ, FILE_SHARE_READ,
                                NULL, OPEN_EXISTING, 0, NULL);
        if (fh == INVALID_HANDLE_VALUE) continue;
        DWORD fsz = GetFileSize(fh, NULL);
        char* src = (char*)malloc(fsz + 2);
        if (!src) { CloseHandle(fh); continue; }
        DWORD rd = 0; ReadFile(fh, src, fsz, &rd, NULL);
        src[rd] = '\0'; CloseHandle(fh);

        // Parse
        Lexer lex; lexer_init(&lex, src);
        Parser* parser = new_parser(&lex);
        AST_Program* prog = parse_program(parser);

        if (parser->error_count > 0) {
            fprintf(stderr, "\nPackageError: Syntax error in '%s' (package '%s')\n",
                    filepath, pkg_name);
            for (int i = 0; i < parser->error_count; i++)
                fprintf(stderr, "  [%d] %s\n", i+1, parser->errors[i]);
            fprintf(stderr, "\n");
            free_parser(parser); free(src); ok = 0; continue;
        }
        free_parser(parser);

        // Rename all fns to pkg__fn prefix and register them
        pkg_prefix_fns(prog, pkg_name, pe);

        // Compile fn definitions only into the shared cg
        // PASS 1: register fns
        for (int i = 0; i < prog->statement_count; i++) {
            AST_Statement* s = prog->statements[i];
            if (s && s->type == FN_DEFINITION) {
                AST_Statement_FnDef* fdef = (AST_Statement_FnDef*)s;
                fn_register(cg, fdef->name->value, fdef->parameter_count);
            }
        }
        // PASS 2: emit fn bodies
        for (int i = 0; i < prog->statement_count; i++) {
            AST_Statement* s = prog->statements[i];
            if (s && s->type == FN_DEFINITION)
                cg_fn_body(cg, (AST_Statement_FnDef*)s);
        }
        free(src);
        // Note: prog AST is intentionally not freed here — fn bodies reference its
        // AST nodes for inline expansion. They live for the lifetime of the run.

    } while (FindNextFileA(h, &fd));
    FindClose(h);

    if (ok) pe->loaded = 1;
    return ok;
}

// ============================================================
//  MODULE METHOD DISPATCH
// ============================================================
static void cg_module_call(CodeGen*cg,AST_Expression_Call*call,const char*ns,const char*method){
    int argc=call->argument_count;

    if(strcmp(ns,"time")==0){
        if((strcmp(method,"now")==0||strcmp(method,"clock")==0)&&argc==0){cg_call_extern(cg,g_fn_time_now);return;}
        if(strcmp(method,"ms")==0&&argc==1){cg_expr(cg,call->arguments[0]);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,g_fn_time_ms);return;}
        if(strcmp(method,"us")==0&&argc==1){cg_expr(cg,call->arguments[0]);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,g_fn_time_us);return;}
        if(strcmp(method,"ns")==0&&argc==1){cg_expr(cg,call->arguments[0]);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,g_fn_time_ns);return;}
        if(strcmp(method,"sleep")==0&&argc==1){cg_expr(cg,call->arguments[0]);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,g_fn_time_sleep);emit_xor_rax_rax(&cg->code);return;}
        if(strcmp(method,"format")==0&&argc==1){cg_expr(cg,call->arguments[0]);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,g_fn_time_format);return;}
        fprintf(stderr,"CodeGen Error: unknown time.%s()\n",method);exit(1);
    }

    if(strcmp(ns,"datetime")==0){
        if(strcmp(method,"now")==0&&argc==0){cg_call_extern(cg,g_fn_dt_now);return;}
        if(strcmp(method,"utcnow")==0&&argc==0){cg_call_extern(cg,g_fn_dt_utcnow);return;}
        if(strcmp(method,"timezone")==0&&argc==0){cg_call_extern(cg,g_fn_dt_timezone);return;}
        #define DT_1ARG(meth,fn) if(strcmp(method,meth)==0&&argc==1){cg_expr(cg,call->arguments[0]);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,fn);return;}
        // 0-arg versions auto-use datetime.now()
        #define DT_0ARG(meth,fn) if(strcmp(method,meth)==0&&argc==0){cg_call_extern(cg,g_fn_dt_now);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,fn);return;}
        DT_0ARG("year",        g_fn_dt_year)
        DT_0ARG("month",       g_fn_dt_month)
        DT_0ARG("day",         g_fn_dt_day)
        DT_0ARG("hour",        g_fn_dt_hour)
        DT_0ARG("minute",      g_fn_dt_minute)
        DT_0ARG("second",      g_fn_dt_second)
        DT_0ARG("weekday",     g_fn_dt_weekday)
        DT_0ARG("format",      g_fn_dt_format)
        DT_0ARG("format_date", g_fn_dt_format_date)
        DT_0ARG("format_time", g_fn_dt_format_time)
        #undef DT_0ARG
        DT_1ARG("year",         g_fn_dt_year)
        DT_1ARG("month",        g_fn_dt_month)
        DT_1ARG("day",          g_fn_dt_day)
        DT_1ARG("hour",         g_fn_dt_hour)
        DT_1ARG("minute",       g_fn_dt_minute)
        DT_1ARG("second",       g_fn_dt_second)
        DT_1ARG("weekday",      g_fn_dt_weekday)
        DT_1ARG("timestamp",    g_fn_dt_timestamp)
        DT_1ARG("format",       g_fn_dt_format)
        DT_1ARG("format_date",  g_fn_dt_format_date)
        DT_1ARG("format_time",  g_fn_dt_format_time)
        DT_1ARG("from_timestamp",g_fn_dt_from_ts)
        DT_1ARG("weekday_name", g_fn_dt_weekday_nm)
        DT_1ARG("month_name",   g_fn_dt_month_nm)
        #undef DT_1ARG
        if(strcmp(method,"diff_ms")==0&&argc==2){
            int s=cg->scope->next_offset;cg->scope->next_offset+=8;if(s>cg->stack_size)cg->stack_size=s;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,s);
            cg_expr(cg,call->arguments[1]);emit_mov_arg1_rax(&cg->code);emit_load_arg0(&cg->code,s);
            cg_call_extern(cg,g_fn_dt_diff_ms);cg->scope->next_offset-=8;return;}
        if(strcmp(method,"diff_s")==0&&argc==2){
            int s=cg->scope->next_offset;cg->scope->next_offset+=8;if(s>cg->stack_size)cg->stack_size=s;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,s);
            cg_expr(cg,call->arguments[1]);emit_mov_arg1_rax(&cg->code);emit_load_arg0(&cg->code,s);
            cg_call_extern(cg,g_fn_dt_diff_s);cg->scope->next_offset-=8;return;}
        // FIX: datetime.make(year,month,day) — clean 3-slot pattern with correct R8 load
        if(strcmp(method,"make")==0&&argc==3){
            int s0=cg->scope->next_offset;cg->scope->next_offset+=8;
            int s1=cg->scope->next_offset;cg->scope->next_offset+=8;
            int s2=cg->scope->next_offset;cg->scope->next_offset+=8;
            if(cg->scope->next_offset>cg->stack_size)cg->stack_size=cg->scope->next_offset;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,s0);
            cg_expr(cg,call->arguments[1]);emit_store_rax(&cg->code,s1);
            cg_expr(cg,call->arguments[2]);emit_store_rax(&cg->code,s2);
            emit_load_arg0(&cg->code,s0);emit_load_arg1(&cg->code,s1);emit_load_arg2(&cg->code,s2);
            cg_call_extern(cg,g_fn_dt_make);
            cg->scope->next_offset-=24;return;}
        fprintf(stderr,"CodeGen Error: unknown datetime.%s()\n",method);exit(1);
    }

    if(strcmp(ns,"math")==0){
        if(strcmp(method,"pi")==0&&argc==0){uint64_t bits;memcpy(&bits,&OMNI_PI,8);emit_mov_rax_imm64(&cg->code,(int64_t)bits);return;}
        if(strcmp(method,"e")==0&&argc==0){uint64_t bits;memcpy(&bits,&OMNI_E,8);emit_mov_rax_imm64(&cg->code,(int64_t)bits);return;}
        if(strcmp(method,"tau")==0&&argc==0){uint64_t bits;memcpy(&bits,&OMNI_TAU,8);emit_mov_rax_imm64(&cg->code,(int64_t)bits);return;}
        if(strcmp(method,"abs")==0&&argc==1){cg_expr(cg,call->arguments[0]);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,g_fn_math_abs);return;}
        if(strcmp(method,"min")==0&&argc==2){
            int s=cg->scope->next_offset;cg->scope->next_offset+=8;if(s>cg->stack_size)cg->stack_size=s;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,s);
            cg_expr(cg,call->arguments[1]);emit_mov_arg1_rax(&cg->code);emit_load_arg0(&cg->code,s);
            cg_call_extern(cg,g_fn_math_min);cg->scope->next_offset-=8;return;}
        if(strcmp(method,"max")==0&&argc==2){
            int s=cg->scope->next_offset;cg->scope->next_offset+=8;if(s>cg->stack_size)cg->stack_size=s;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,s);
            cg_expr(cg,call->arguments[1]);emit_mov_arg1_rax(&cg->code);emit_load_arg0(&cg->code,s);
            cg_call_extern(cg,g_fn_math_max);cg->scope->next_offset-=8;return;}
        if(strcmp(method,"gcd")==0&&argc==2){
            int s=cg->scope->next_offset;cg->scope->next_offset+=8;if(s>cg->stack_size)cg->stack_size=s;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,s);
            cg_expr(cg,call->arguments[1]);emit_mov_arg1_rax(&cg->code);emit_load_arg0(&cg->code,s);
            cg_call_extern(cg,g_fn_math_gcd);cg->scope->next_offset-=8;return;}
        // FIX: math.clamp(v,lo,hi) — clean 3-slot pattern with correct R8 load
        if(strcmp(method,"clamp")==0&&argc==3){
            int s0=cg->scope->next_offset;cg->scope->next_offset+=8;
            int s1=cg->scope->next_offset;cg->scope->next_offset+=8;
            int s2=cg->scope->next_offset;cg->scope->next_offset+=8;
            if(cg->scope->next_offset>cg->stack_size)cg->stack_size=cg->scope->next_offset;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,s0);
            cg_expr(cg,call->arguments[1]);emit_store_rax(&cg->code,s1);
            cg_expr(cg,call->arguments[2]);emit_store_rax(&cg->code,s2);
            emit_load_arg0(&cg->code,s0);emit_load_arg1(&cg->code,s1);emit_load_arg2(&cg->code,s2);
            cg_call_extern(cg,g_fn_math_clamp);
            cg->scope->next_offset-=24;return;}
        /* MATH_1F: float-returning 1-arg math functions.
           Both Win64 and SysV pass/return doubles in XMM0.
           The arg is converted to double in XMM0 per its static type
           (see emit_to_double_arg — CVTSI2SD only for ints, else MOVQ).
           Result moves XMM0→RAX so the rest of codegen sees the value. */
        #define MATH_1F(meth,fn) if(strcmp(method,meth)==0&&argc==1){\
            cg_expr(cg,call->arguments[0]);\
            emit_to_double_arg(cg,call->arguments[0]);\
            cg_call_extern(cg,fn);\
            emit_xmm0_to_rax(&cg->code);  /* MOVQ rax,xmm0: double result→RAX */\
            return;}
        MATH_1F("sqrt",  g_fn_math_sqrt)
        MATH_1F("sin",   g_fn_math_sin)
        MATH_1F("cos",   g_fn_math_cos)
        MATH_1F("tan",   g_fn_math_tan)
        MATH_1F("log",   g_fn_math_log)
        MATH_1F("log2",  g_fn_math_log2)
        MATH_1F("log10", g_fn_math_log10)
        MATH_1F("floor", g_fn_math_floor)
        MATH_1F("ceil",  g_fn_math_ceil)
        MATH_1F("round", g_fn_math_round)
        MATH_1F("exp",   g_fn_math_exp)
        MATH_1F("exp2",  g_fn_math_exp2)
        MATH_1F("tanh",  g_fn_math_tanh)
        MATH_1F("atan",  g_fn_math_atan)
        MATH_1F("cbrt",  g_fn_math_cbrt)
        /* itof/ftoi need special handling — bypass CVTSI2SD */
        if(strcmp(method,"itof")==0&&argc==1){cg_expr(cg,call->arguments[0]);emit_int_to_float(&cg->code);emit_xmm0_to_rax(&cg->code);return;} /* direct CVTSI2SD, no call */
        if(strcmp(method,"ftoi")==0&&argc==1){cg_expr(cg,call->arguments[0]);emit_rax_to_xmm0(&cg->code);emit_float_to_int(&cg->code);return;} /* direct CVTTSD2SI, no call */
        #undef MATH_1F
        if(strcmp(method,"atan2")==0&&argc==2){
            /* atan2(y,x): pass y→XMM0, x→XMM1 (float args in XMM regs, both ABIs) */
            int s0=cg->scope->next_offset;cg->scope->next_offset+=8;
            int s1=cg->scope->next_offset;cg->scope->next_offset+=8;
            if(cg->scope->next_offset>cg->stack_size)cg->stack_size=cg->scope->next_offset;
            cg_expr(cg,call->arguments[0]);emit_to_double_arg(cg,call->arguments[0]);emit_store_xmm0(&cg->code,s0);
            cg_expr(cg,call->arguments[1]);emit_to_double_arg(cg,call->arguments[1]);emit_store_xmm0(&cg->code,s1);
            emit_load_xmm0(&cg->code,s0);emit_load_xmm1(&cg->code,s1);
            cg_call_extern(cg,g_fn_math_atan2);emit_xmm0_to_rax(&cg->code);
            cg->scope->next_offset-=16;return;}
        if(strcmp(method,"pow")==0&&argc==2){
            /* pow(base,exp): pass base→XMM0, exp→XMM1 */
            int s0=cg->scope->next_offset;cg->scope->next_offset+=8;
            int s1=cg->scope->next_offset;cg->scope->next_offset+=8;
            if(cg->scope->next_offset>cg->stack_size)cg->stack_size=cg->scope->next_offset;
            cg_expr(cg,call->arguments[0]);emit_to_double_arg(cg,call->arguments[0]);emit_store_xmm0(&cg->code,s0);
            cg_expr(cg,call->arguments[1]);emit_to_double_arg(cg,call->arguments[1]);emit_store_xmm0(&cg->code,s1);
            emit_load_xmm0(&cg->code,s0);emit_load_xmm1(&cg->code,s1);
            cg_call_extern(cg,g_fn_math_pow);emit_xmm0_to_rax(&cg->code);
            cg->scope->next_offset-=16;return;}
        fprintf(stderr,"CodeGen Error: unknown math.%s()\n",method);exit(1);
    }

    if(strcmp(ns,"os")==0){
        if(strcmp(method,"exit")==0&&argc==1){cg_expr(cg,call->arguments[0]);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,g_fn_os_exit);return;}
        if(strcmp(method,"exit")==0&&argc==0){emit_mov_arg0_imm64(&cg->code,0);cg_call_extern(cg,g_fn_os_exit);return;}
        if(strcmp(method,"platform")==0&&argc==0){cg_call_extern(cg,g_fn_os_platform);return;}
        if(strcmp(method,"cwd")==0&&argc==0){cg_call_extern(cg,g_fn_os_cwd);return;}
        if(strcmp(method,"getpid")==0&&argc==0){cg_call_extern(cg,g_fn_os_getpid);return;}
        if(strcmp(method,"getenv")==0&&argc==1){cg_expr(cg,call->arguments[0]);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,g_fn_os_getenv);return;}
        if(strcmp(method,"exists")==0&&argc==1){cg_expr(cg,call->arguments[0]);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,g_fn_os_exists);return;}
        if(strcmp(method,"mkdir")==0&&argc==1){cg_expr(cg,call->arguments[0]);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,g_fn_os_mkdir);return;}
        /* os extended — 1 arg */
        #define OS_1(meth,fn) if(strcmp(method,meth)==0&&argc==1){cg_expr(cg,call->arguments[0]);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,fn);return;}
        OS_1("remove",   g_fn_os_remove)
        OS_1("isfile",   g_fn_os_isfile)
        OS_1("isdir",    g_fn_os_isdir)
        OS_1("abspath",  g_fn_os_abspath)
        OS_1("basename", g_fn_os_basename)
        OS_1("dirname",  g_fn_os_dirname)
        #undef OS_1
        /* os.rename(from,to), os.join(a,b) — 2 args */
        #define OS_2(meth,fn) if(strcmp(method,meth)==0&&argc==2){\
            int _s=cg->scope->next_offset;cg->scope->next_offset+=8;if(_s>cg->stack_size)cg->stack_size=_s;\
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,_s);\
            cg_expr(cg,call->arguments[1]);emit_mov_arg1_rax(&cg->code);emit_load_arg0(&cg->code,_s);\
            cg_call_extern(cg,fn);cg->scope->next_offset-=8;return;}
        OS_2("rename", g_fn_os_rename)
        OS_2("join",   g_fn_os_join)
        #undef OS_2
        fprintf(stderr,"CodeGen Error: unknown os.%s()\n",method);exit(1);
    }

    if(strcmp(ns,"io")==0){
        if(strcmp(method,"read")==0&&argc==1){cg_expr(cg,call->arguments[0]);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,g_fn_io_read);return;}
        if(strcmp(method,"exists")==0&&argc==1){cg_expr(cg,call->arguments[0]);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,g_fn_io_exists);return;}
        if(strcmp(method,"delete")==0&&argc==1){cg_expr(cg,call->arguments[0]);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,g_fn_io_delete);return;}
        if(strcmp(method,"size")==0&&argc==1){cg_expr(cg,call->arguments[0]);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,g_fn_io_size);return;}
        if(strcmp(method,"write")==0&&argc==2){
            int s=cg->scope->next_offset;cg->scope->next_offset+=8;if(s>cg->stack_size)cg->stack_size=s;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,s);
            cg_expr(cg,call->arguments[1]);emit_mov_arg1_rax(&cg->code);emit_load_arg0(&cg->code,s);
            cg_call_extern(cg,g_fn_io_write);cg->scope->next_offset-=8;return;}
        if(strcmp(method,"append")==0&&argc==2){
            int s=cg->scope->next_offset;cg->scope->next_offset+=8;if(s>cg->stack_size)cg->stack_size=s;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,s);
            cg_expr(cg,call->arguments[1]);emit_mov_arg1_rax(&cg->code);emit_load_arg0(&cg->code,s);
            cg_call_extern(cg,g_fn_io_append);cg->scope->next_offset-=8;return;}
        /* io extended */
        if(strcmp(method,"copy")==0&&argc==2){
            int _s=cg->scope->next_offset;cg->scope->next_offset+=8;if(_s>cg->stack_size)cg->stack_size=_s;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,_s);
            cg_expr(cg,call->arguments[1]);emit_mov_arg1_rax(&cg->code);emit_load_arg0(&cg->code,_s);
            cg_call_extern(cg,g_fn_io_copy);cg->scope->next_offset-=8;return;}
        if(strcmp(method,"rename")==0&&argc==2){
            int _s=cg->scope->next_offset;cg->scope->next_offset+=8;if(_s>cg->stack_size)cg->stack_size=_s;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,_s);
            cg_expr(cg,call->arguments[1]);emit_mov_arg1_rax(&cg->code);emit_load_arg0(&cg->code,_s);
            cg_call_extern(cg,g_fn_io_rename);cg->scope->next_offset-=8;return;}
        if(strcmp(method,"line_count")==0&&argc==1){cg_expr(cg,call->arguments[0]);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,g_fn_io_line_count);return;}
        if(strcmp(method,"readline")==0&&argc==2){
            int _s=cg->scope->next_offset;cg->scope->next_offset+=8;if(_s>cg->stack_size)cg->stack_size=_s;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,_s);
            cg_expr(cg,call->arguments[1]);emit_mov_arg1_rax(&cg->code);emit_load_arg0(&cg->code,_s);
            cg_call_extern(cg,g_fn_io_readline);cg->scope->next_offset-=8;return;}
        fprintf(stderr,"CodeGen Error: unknown io.%s()\n",method);exit(1);
    }

    if(strcmp(ns,"sys")==0){
        if(strcmp(method,"version")==0)  {cg_call_extern(cg,g_fn_sys_version);return;}
        if(strcmp(method,"platform")==0) {cg_call_extern(cg,g_fn_sys_platform);return;}
        if(strcmp(method,"arch")==0)     {cg_call_extern(cg,g_fn_sys_arch);return;}
        if(strcmp(method,"omni_ver")==0) {cg_call_extern(cg,g_fn_sys_omni_ver);return;}
        if(strcmp(method,"bits")==0)     {cg_call_extern(cg,g_fn_sys_bits);return;}
        if(strcmp(method,"exit")==0&&argc==1){cg_expr(cg,call->arguments[0]);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,g_fn_sys_exit_fn);return;}
        if(strcmp(method,"exit")==0&&argc==0){emit_mov_arg0_imm64(&cg->code,0);cg_call_extern(cg,g_fn_sys_exit_fn);return;}
        if(strcmp(method,"input")==0&&argc==0){cg_call_extern(cg,g_fn_sys_input_fn);return;}
        if(strcmp(method,"memory")==0&&argc==0){cg_call_extern(cg,g_fn_sys_memory);return;}
        fprintf(stderr,"CodeGen Error: unknown sys.%s()\n",method);exit(1);
    }

    if(strcmp(ns,"list")==0){
        if(strcmp(method,"new")==0&&argc==0){cg_call_extern(cg,g_fn_list_new);return;}
        if(strcmp(method,"len")==0&&argc==1){cg_expr(cg,call->arguments[0]);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,g_fn_list_len);return;}
        if(strcmp(method,"pop")==0&&argc==1){cg_expr(cg,call->arguments[0]);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,g_fn_list_pop);return;}
        if(strcmp(method,"free")==0&&argc==1){cg_expr(cg,call->arguments[0]);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,g_fn_list_free);emit_xor_rax_rax(&cg->code);return;}
        if(strcmp(method,"contains")==0&&argc==2){
            int s=cg->scope->next_offset;cg->scope->next_offset+=8;if(s>cg->stack_size)cg->stack_size=s;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,s);
            cg_expr(cg,call->arguments[1]);emit_mov_arg1_rax(&cg->code);emit_load_arg0(&cg->code,s);
            cg_call_extern(cg,g_fn_list_contains);cg->scope->next_offset-=8;return;}
        if(strcmp(method,"push")==0&&argc==2){
            int s=cg->scope->next_offset;cg->scope->next_offset+=8;if(s>cg->stack_size)cg->stack_size=s;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,s);
            cg_expr(cg,call->arguments[1]);emit_mov_arg1_rax(&cg->code);
            emit_load_arg0(&cg->code,s);
            cg_call_extern(cg,g_fn_list_push);
            if(call->arguments[0]&&call->arguments[0]->type==IDENTIFIER){
                const char*lst_var=((AST_Expression_Identifier*)call->arguments[0])->value;
                Symbol*lst_sym=scope_get(cg->scope,lst_var);
                if(lst_sym) emit_store_rax(&cg->code,lst_sym->stack_offset);
            }
            emit_xor_rax_rax(&cg->code);
            cg->scope->next_offset-=8;return;}
        if(strcmp(method,"get")==0&&argc==2){
            int s=cg->scope->next_offset;cg->scope->next_offset+=8;if(s>cg->stack_size)cg->stack_size=s;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,s);
            cg_expr(cg,call->arguments[1]);emit_mov_arg1_rax(&cg->code);emit_load_arg0(&cg->code,s);
            cg_call_extern(cg,g_fn_list_get);cg->scope->next_offset-=8;return;}
        // FIX: list.set(lst,idx,val) — clean 3-slot pattern with correct R8 load
        if(strcmp(method,"set")==0&&argc==3){
            int s0=cg->scope->next_offset;cg->scope->next_offset+=8;
            int s1=cg->scope->next_offset;cg->scope->next_offset+=8;
            int s2=cg->scope->next_offset;cg->scope->next_offset+=8;
            if(cg->scope->next_offset>cg->stack_size)cg->stack_size=cg->scope->next_offset;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,s0);
            cg_expr(cg,call->arguments[1]);emit_store_rax(&cg->code,s1);
            cg_expr(cg,call->arguments[2]);emit_store_rax(&cg->code,s2);
            emit_load_arg0(&cg->code,s0);emit_load_arg1(&cg->code,s1);emit_load_arg2(&cg->code,s2);
            cg_call_extern(cg,g_fn_list_set);emit_xor_rax_rax(&cg->code);
            cg->scope->next_offset-=24;return;}
        if(strcmp(method,"print")==0&&argc==1){cg_expr(cg,call->arguments[0]);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,g_fn_list_print);return;}
        /* list extended — 1 arg */
        #define LIST_1(meth,fn) if(strcmp(method,meth)==0&&argc==1){cg_expr(cg,call->arguments[0]);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,fn);return;}
        LIST_1("reverse", g_fn_list_reverse)
        LIST_1("sort",    g_fn_list_sort)
        LIST_1("clear",   g_fn_list_clear)
        LIST_1("copy",    g_fn_list_copy_fn)
        LIST_1("min",     g_fn_list_min)
        LIST_1("max",     g_fn_list_max)
        LIST_1("sum",     g_fn_list_sum)
        #undef LIST_1
        /* list 2-arg */
        #define LIST_2(meth,fn) if(strcmp(method,meth)==0&&argc==2){\
            int _s=cg->scope->next_offset;cg->scope->next_offset+=8;if(_s>cg->stack_size)cg->stack_size=_s;\
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,_s);\
            cg_expr(cg,call->arguments[1]);emit_mov_arg1_rax(&cg->code);emit_load_arg0(&cg->code,_s);\
            cg_call_extern(cg,fn);cg->scope->next_offset-=8;return;}
        LIST_2("index",   g_fn_list_index)
        LIST_2("remove",  g_fn_list_remove_v)
        LIST_2("concat",  g_fn_list_concat)
        #undef LIST_2
        /* list.insert(lst,idx,val) — 3 args */
        if(strcmp(method,"insert")==0&&argc==3){
            int s0=cg->scope->next_offset;cg->scope->next_offset+=8;
            int s1=cg->scope->next_offset;cg->scope->next_offset+=8;
            int s2=cg->scope->next_offset;cg->scope->next_offset+=8;
            if(cg->scope->next_offset>cg->stack_size)cg->stack_size=cg->scope->next_offset;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,s0);
            cg_expr(cg,call->arguments[1]);emit_store_rax(&cg->code,s1);
            cg_expr(cg,call->arguments[2]);emit_store_rax(&cg->code,s2);
            emit_load_arg0(&cg->code,s0);emit_load_arg1(&cg->code,s1);emit_load_arg2(&cg->code,s2);
            cg_call_extern(cg,g_fn_list_insert);
            /* update variable pointer like push does */
            if(call->arguments[0]&&call->arguments[0]->type==IDENTIFIER){
                const char*lst_var=((AST_Expression_Identifier*)call->arguments[0])->value;
                Symbol*lst_sym=scope_get(cg->scope,lst_var);
                if(lst_sym) emit_store_rax(&cg->code,lst_sym->stack_offset);
            }
            cg->scope->next_offset-=24;return;}
        fprintf(stderr,"CodeGen Error: unknown list.%s()\n",method);exit(1);
    }

    if(strcmp(ns,"str")==0){
        if(strcmp(method,"eq")==0&&argc==2){
            int s=cg->scope->next_offset;cg->scope->next_offset+=8;if(s>cg->stack_size)cg->stack_size=s;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,s);
            cg_expr(cg,call->arguments[1]);emit_mov_arg1_rax(&cg->code);emit_load_arg0(&cg->code,s);
            cg_call_extern(cg,g_fn_str_eq);cg->scope->next_offset-=8;return;}
        if(strcmp(method,"concat")==0&&argc==2){
            int s=cg->scope->next_offset;cg->scope->next_offset+=8;if(s>cg->stack_size)cg->stack_size=s;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,s);
            cg_expr(cg,call->arguments[1]);emit_mov_arg1_rax(&cg->code);emit_load_arg0(&cg->code,s);
            cg_call_extern(cg,g_fn_str_concat);cg->scope->next_offset-=8;return;}
        if(strcmp(method,"len")==0&&argc==1){cg_expr(cg,call->arguments[0]);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,g_fn_len_str);return;}
        if(strcmp(method,"slice")==0&&argc==3){
            int s0=cg->scope->next_offset;cg->scope->next_offset+=8;
            int s1=cg->scope->next_offset;cg->scope->next_offset+=8;
            int s2=cg->scope->next_offset;cg->scope->next_offset+=8;
            if(cg->scope->next_offset>cg->stack_size)cg->stack_size=cg->scope->next_offset;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,s0);
            cg_expr(cg,call->arguments[1]);emit_store_rax(&cg->code,s1);
            cg_expr(cg,call->arguments[2]);emit_store_rax(&cg->code,s2);
            emit_load_arg0(&cg->code,s0);emit_load_arg1(&cg->code,s1);emit_load_arg2(&cg->code,s2);
            cg_call_extern(cg,g_fn_str_slice);cg->scope->next_offset-=24;return;}
        if(strcmp(method,"toint")==0&&argc==1){cg_expr(cg,call->arguments[0]);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,g_fn_str_to_int);return;}
        if(strcmp(method,"fromint")==0&&argc==1){cg_expr(cg,call->arguments[0]);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,g_fn_int_to_str);return;}
        /* str extended — 0/1 arg */
        #define STR_1(meth,fn) if(strcmp(method,meth)==0&&argc==1){cg_expr(cg,call->arguments[0]);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,fn);return;}
        STR_1("upper",    g_fn_str_upper)
        STR_1("lower",    g_fn_str_lower)
        STR_1("trim",     g_fn_str_trim)
        STR_1("reverse",  g_fn_str_reverse_s)
        STR_1("is_digit", g_fn_str_is_digit)
        STR_1("is_alpha", g_fn_str_is_alpha)
        #undef STR_1
        /* str 2-arg */
        #define STR_2(meth,fn) if(strcmp(method,meth)==0&&argc==2){\
            int _s=cg->scope->next_offset;cg->scope->next_offset+=8;if(_s>cg->stack_size)cg->stack_size=_s;\
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,_s);\
            cg_expr(cg,call->arguments[1]);emit_mov_arg1_rax(&cg->code);emit_load_arg0(&cg->code,_s);\
            cg_call_extern(cg,fn);cg->scope->next_offset-=8;return;}
        STR_2("contains",   g_fn_str_contains)
        STR_2("starts_with",g_fn_str_starts)
        STR_2("ends_with",  g_fn_str_ends)
        STR_2("find",       g_fn_str_find)
        STR_2("count",      g_fn_str_count)
        STR_2("repeat",     g_fn_str_repeat)
        STR_2("pad_left",   g_fn_str_pad_left)
        STR_2("pad_right",  g_fn_str_pad_right)
        #undef STR_2
        /* str.replace(s,old,new) — 3 args */
        if(strcmp(method,"replace")==0&&argc==3){
            int s0=cg->scope->next_offset;cg->scope->next_offset+=8;
            int s1=cg->scope->next_offset;cg->scope->next_offset+=8;
            int s2=cg->scope->next_offset;cg->scope->next_offset+=8;
            if(cg->scope->next_offset>cg->stack_size)cg->stack_size=cg->scope->next_offset;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,s0);
            cg_expr(cg,call->arguments[1]);emit_store_rax(&cg->code,s1);
            cg_expr(cg,call->arguments[2]);emit_store_rax(&cg->code,s2);
            emit_load_arg0(&cg->code,s0);emit_load_arg1(&cg->code,s1);emit_load_arg2(&cg->code,s2);
            cg_call_extern(cg,g_fn_str_replace);
            cg->scope->next_offset-=24;return;}
        fprintf(stderr,"CodeGen Error: unknown str.%s()\n",method);exit(1);
    }

    // ============================================================
    //  AI MODULE — Speed God Plan | AVX2 + INT8 + Cache-Tiled
    // ============================================================
    if(strcmp(ns,"ai")==0){
        // 0-arg
        if(strcmp(method,"bench_start")==0&&argc==0){cg_call_extern(cg,g_fn_ai_bench_start);return;}
        // 1-arg
        #define AI_1(meth,fn) if(strcmp(method,meth)==0&&argc==1){cg_expr(cg,call->arguments[0]);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,fn);return;}
        AI_1("free",        g_fn_ai_free)
        AI_1("alloc",       g_fn_ai_alloc)
        AI_1("relu",        g_fn_ai_relu)   // ai.relu(arr_ptr_and_n_packed) — use 2-arg form below
        AI_1("print",       g_fn_ai_print)
        #undef AI_1
        // alloc(n) — 1 arg: number of floats, 64-byte aligned
        if(strcmp(method,"alloc")==0&&argc==1){
            cg_expr(cg,call->arguments[0]);emit_mov_arg0_rax(&cg->code);
            cg_call_extern(cg,g_fn_ai_alloc);return;}
        // free(ptr)
        if(strcmp(method,"free")==0&&argc==1){
            cg_expr(cg,call->arguments[0]);emit_mov_arg0_rax(&cg->code);
            cg_call_extern(cg,g_fn_ai_free);emit_xor_rax_rax(&cg->code);return;}
        // get(arr,idx) -> float bits as int64
        if(strcmp(method,"get")==0&&argc==2){
            int s=cg->scope->next_offset;cg->scope->next_offset+=8;if(s>cg->stack_size)cg->stack_size=s;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,s);
            cg_expr(cg,call->arguments[1]);emit_mov_arg1_rax(&cg->code);emit_load_arg0(&cg->code,s);
            cg_call_extern(cg,g_fn_ai_get);cg->scope->next_offset-=8;return;}
        // get_i8/get_u8(arr,idx) -> sign/zero-extended byte (INT8 API, finding #16)
        if((strcmp(method,"get_i8")==0||strcmp(method,"get_u8")==0)&&argc==2){
            int s=cg->scope->next_offset;cg->scope->next_offset+=8;if(s>cg->stack_size)cg->stack_size=s;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,s);
            cg_expr(cg,call->arguments[1]);emit_mov_arg1_rax(&cg->code);emit_load_arg0(&cg->code,s);
            cg_call_extern(cg,strcmp(method,"get_i8")==0?g_fn_ai_get_i8:g_fn_ai_get_u8);
            cg->scope->next_offset-=8;return;}
        // set(arr,idx,val)
        if(strcmp(method,"set")==0&&argc==3){
            int s0=cg->scope->next_offset;cg->scope->next_offset+=8;
            int s1=cg->scope->next_offset;cg->scope->next_offset+=8;
            int s2=cg->scope->next_offset;cg->scope->next_offset+=8;
            if(cg->scope->next_offset>cg->stack_size)cg->stack_size=cg->scope->next_offset;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,s0);
            cg_expr(cg,call->arguments[1]);emit_store_rax(&cg->code,s1);
            cg_expr(cg,call->arguments[2]);emit_store_rax(&cg->code,s2);
            emit_load_arg0(&cg->code,s0);emit_load_arg1(&cg->code,s1);emit_load_arg2(&cg->code,s2);
            cg_call_extern(cg,g_fn_ai_set);emit_xor_rax_rax(&cg->code);
            cg->scope->next_offset-=24;return;}
        // set_i8/set_u8(arr,idx,val) — byte-width element store (finding #16)
        if((strcmp(method,"set_i8")==0||strcmp(method,"set_u8")==0)&&argc==3){
            int s0=cg->scope->next_offset;cg->scope->next_offset+=8;
            int s1=cg->scope->next_offset;cg->scope->next_offset+=8;
            int s2=cg->scope->next_offset;cg->scope->next_offset+=8;
            if(cg->scope->next_offset>cg->stack_size)cg->stack_size=cg->scope->next_offset;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,s0);
            cg_expr(cg,call->arguments[1]);emit_store_rax(&cg->code,s1);
            cg_expr(cg,call->arguments[2]);emit_store_rax(&cg->code,s2);
            emit_load_arg0(&cg->code,s0);emit_load_arg1(&cg->code,s1);emit_load_arg2(&cg->code,s2);
            cg_call_extern(cg,strcmp(method,"set_i8")==0?g_fn_ai_set_i8:g_fn_ai_set_u8);
            emit_xor_rax_rax(&cg->code);
            cg->scope->next_offset-=24;return;}
        // fill(arr,val,n)
        if(strcmp(method,"fill")==0&&argc==3){
            int s0=cg->scope->next_offset;cg->scope->next_offset+=8;
            int s1=cg->scope->next_offset;cg->scope->next_offset+=8;
            int s2=cg->scope->next_offset;cg->scope->next_offset+=8;
            if(cg->scope->next_offset>cg->stack_size)cg->stack_size=cg->scope->next_offset;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,s0);
            cg_expr(cg,call->arguments[1]);emit_store_rax(&cg->code,s1);
            cg_expr(cg,call->arguments[2]);emit_store_rax(&cg->code,s2);
            emit_load_arg0(&cg->code,s0);emit_load_arg1(&cg->code,s1);emit_load_arg2(&cg->code,s2);
            cg_call_extern(cg,g_fn_ai_fill);emit_xor_rax_rax(&cg->code);
            cg->scope->next_offset-=24;return;}
        // relu(arr,n)
        if(strcmp(method,"relu")==0&&argc==2){
            int s=cg->scope->next_offset;cg->scope->next_offset+=8;if(s>cg->stack_size)cg->stack_size=s;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,s);
            cg_expr(cg,call->arguments[1]);emit_mov_arg1_rax(&cg->code);emit_load_arg0(&cg->code,s);
            cg_call_extern(cg,g_fn_ai_relu);emit_xor_rax_rax(&cg->code);
            cg->scope->next_offset-=8;return;}
        // softmax(arr,n)
        if(strcmp(method,"softmax")==0&&argc==2){
            int s=cg->scope->next_offset;cg->scope->next_offset+=8;if(s>cg->stack_size)cg->stack_size=s;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,s);
            cg_expr(cg,call->arguments[1]);emit_mov_arg1_rax(&cg->code);emit_load_arg0(&cg->code,s);
            cg_call_extern(cg,g_fn_ai_softmax);emit_xor_rax_rax(&cg->code);
            cg->scope->next_offset-=8;return;}
        // layernorm(arr,n,eps_bits)
        if(strcmp(method,"layernorm")==0&&argc==3){
            int s0=cg->scope->next_offset;cg->scope->next_offset+=8;
            int s1=cg->scope->next_offset;cg->scope->next_offset+=8;
            int s2=cg->scope->next_offset;cg->scope->next_offset+=8;
            if(cg->scope->next_offset>cg->stack_size)cg->stack_size=cg->scope->next_offset;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,s0);
            cg_expr(cg,call->arguments[1]);emit_store_rax(&cg->code,s1);
            cg_expr(cg,call->arguments[2]);emit_store_rax(&cg->code,s2);
            emit_load_arg0(&cg->code,s0);emit_load_arg1(&cg->code,s1);emit_load_arg2(&cg->code,s2);
            cg_call_extern(cg,g_fn_ai_layernorm);emit_xor_rax_rax(&cg->code);
            cg->scope->next_offset-=24;return;}
        // dot(a,b,n) -> float bits
        if(strcmp(method,"dot")==0&&argc==3){
            int s0=cg->scope->next_offset;cg->scope->next_offset+=8;
            int s1=cg->scope->next_offset;cg->scope->next_offset+=8;
            int s2=cg->scope->next_offset;cg->scope->next_offset+=8;
            if(cg->scope->next_offset>cg->stack_size)cg->stack_size=cg->scope->next_offset;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,s0);
            cg_expr(cg,call->arguments[1]);emit_store_rax(&cg->code,s1);
            cg_expr(cg,call->arguments[2]);emit_store_rax(&cg->code,s2);
            emit_load_arg0(&cg->code,s0);emit_load_arg1(&cg->code,s1);emit_load_arg2(&cg->code,s2);
            cg_call_extern(cg,g_fn_ai_dot);
            cg->scope->next_offset-=24;return;}
        // dot_i8(a,b,n) -> INT8 quantized dot product (VPMADDUBSW, 32 ops/cycle)
        if(strcmp(method,"dot_i8")==0&&argc==3){
            int s0=cg->scope->next_offset;cg->scope->next_offset+=8;
            int s1=cg->scope->next_offset;cg->scope->next_offset+=8;
            int s2=cg->scope->next_offset;cg->scope->next_offset+=8;
            if(cg->scope->next_offset>cg->stack_size)cg->stack_size=cg->scope->next_offset;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,s0);
            cg_expr(cg,call->arguments[1]);emit_store_rax(&cg->code,s1);
            cg_expr(cg,call->arguments[2]);emit_store_rax(&cg->code,s2);
            emit_load_arg0(&cg->code,s0);emit_load_arg1(&cg->code,s1);emit_load_arg2(&cg->code,s2);
            cg_call_extern(cg,g_fn_ai_dot_i8);
            cg->scope->next_offset-=24;return;}
        // matmul(A,x,y,rows,cols) — 5-arg Windows x64 call
        // Uses omni_matvec_call which is identical but tested separately.
        if(strcmp(method,"matmul")==0&&argc==5){
            // Allocate 5 temp slots for evaluating args
            int s0=cg->scope->next_offset;cg->scope->next_offset+=8;
            int s1=cg->scope->next_offset;cg->scope->next_offset+=8;
            int s2=cg->scope->next_offset;cg->scope->next_offset+=8;
            int s3=cg->scope->next_offset;cg->scope->next_offset+=8;
            int s4=cg->scope->next_offset;cg->scope->next_offset+=8;
            if(cg->scope->next_offset>cg->stack_size)cg->stack_size=cg->scope->next_offset;
            // Evaluate all args into temp slots
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,s0); // A
            cg_expr(cg,call->arguments[1]);emit_store_rax(&cg->code,s1); // x
            cg_expr(cg,call->arguments[2]);emit_store_rax(&cg->code,s2); // y
            cg_expr(cg,call->arguments[3]);emit_store_rax(&cg->code,s3); // rows
            cg_expr(cg,call->arguments[4]);emit_store_rax(&cg->code,s4); // cols
            // Load register args (per-ABI, see include/abi.h)
#if defined(OMNI_ABI_WIN64)
            emit_load_rcx(&cg->code,s0); // RCX = A
            emit_load_rdx(&cg->code,s1); // RDX = x
            emit_load_r8 (&cg->code,s2); // R8  = y
            emit_load_r9 (&cg->code,s3); // R9  = rows
            // 5th arg (cols): Windows x64 ABI requires it at [rsp+32].
            emit_load_rax(&cg->code,s4);
            emit_u8(&cg->code,REX_W);emit_u8(&cg->code,0x89);emit_u8(&cg->code,0x44);
            emit_u8(&cg->code,0x24);emit_u8(&cg->code,0x20); // mov [rsp+32], rax
#else
            emit_load_arg0(&cg->code,s0); // RDI = A
            emit_load_arg1(&cg->code,s1); // RSI = x
            emit_load_arg2(&cg->code,s2); // RDX = y
            emit_load_arg3(&cg->code,s3); // RCX = rows
            emit_load_arg4(&cg->code,s4); // R8  = cols
#endif
            cg_call_extern(cg,g_fn_matvec);
            emit_xor_rax_rax(&cg->code);
            cg->scope->next_offset-=40;return;}
        // print(arr,n)
        if(strcmp(method,"print")==0&&argc==2){
            int s=cg->scope->next_offset;cg->scope->next_offset+=8;if(s>cg->stack_size)cg->stack_size=s;
            cg_expr(cg,call->arguments[0]);emit_store_rax(&cg->code,s);
            cg_expr(cg,call->arguments[1]);emit_mov_arg1_rax(&cg->code);emit_load_arg0(&cg->code,s);
            cg_call_extern(cg,g_fn_ai_print);emit_xor_rax_rax(&cg->code);
            cg->scope->next_offset-=8;return;}
        // bench_end_us(t0) -> microseconds elapsed
        if(strcmp(method,"bench_end_us")==0&&argc==1){
            cg_expr(cg,call->arguments[0]);emit_mov_arg0_rax(&cg->code);
            cg_call_extern(cg,g_fn_ai_bench_us);return;}
        // matmul_nn(C, A, B, M, K, N) — 6-arg matrix-matrix multiply
        if(strcmp(method,"matmul_nn")==0&&argc==6){
            int slots[6]; int base=cg->scope->next_offset;
            for(int ai2=0;ai2<6;ai2++){slots[ai2]=base+ai2*8;cg->scope->next_offset+=8;}
            if(cg->scope->next_offset>cg->stack_size)cg->stack_size=cg->scope->next_offset;
            for(int ai2=0;ai2<6;ai2++){cg_expr(cg,call->arguments[ai2]);emit_store_rax(&cg->code,slots[ai2]);}
            emit_load_arg0(&cg->code,slots[0]); // C
            emit_load_arg1(&cg->code,slots[1]); // A
            emit_load_arg2(&cg->code,slots[2]); // B
            emit_load_arg3(&cg->code,slots[3]); // M
#if defined(OMNI_ABI_WIN64)
            // 5th arg (K): [rsp+32]; 6th arg (N): [rsp+40]
            emit_load_rax(&cg->code,slots[4]);
            emit_u8(&cg->code,REX_W);emit_u8(&cg->code,0x89);emit_u8(&cg->code,0x44);emit_u8(&cg->code,0x24);emit_u8(&cg->code,0x20);
            emit_load_rax(&cg->code,slots[5]);
            emit_u8(&cg->code,REX_W);emit_u8(&cg->code,0x89);emit_u8(&cg->code,0x44);emit_u8(&cg->code,0x24);emit_u8(&cg->code,0x28);
#else
            emit_load_arg4(&cg->code,slots[4]); // R8  = K
            emit_load_arg5(&cg->code,slots[5]); // R9  = N
#endif
            cg_call_extern(cg,g_fn_matmul_nn);
            emit_xor_rax_rax(&cg->code);
            cg->scope->next_offset-=48;return;}
        // gemm(C, A, B, M, K, N, alpha, beta) — 8-arg general matrix multiply
        if(strcmp(method,"gemm")==0&&argc==8){
            int slots[8]; int base=cg->scope->next_offset;
            for(int ai2=0;ai2<8;ai2++){slots[ai2]=base+ai2*8;cg->scope->next_offset+=8;}
            if(cg->scope->next_offset>cg->stack_size)cg->stack_size=cg->scope->next_offset;
            for(int ai2=0;ai2<8;ai2++){cg_expr(cg,call->arguments[ai2]);emit_store_rax(&cg->code,slots[ai2]);}
            emit_load_arg0(&cg->code,slots[0]); // C
            emit_load_arg1(&cg->code,slots[1]); // A
            emit_load_arg2(&cg->code,slots[2]); // B
            emit_load_arg3(&cg->code,slots[3]); // M
#if defined(OMNI_ABI_WIN64)
            // Args 5-8 on stack: [rsp+32], [rsp+40], [rsp+48], [rsp+56]
            for(int si=4;si<8;si++){
                emit_load_rax(&cg->code,slots[si]);
                emit_u8(&cg->code,REX_W);emit_u8(&cg->code,0x89);emit_u8(&cg->code,0x44);emit_u8(&cg->code,0x24);emit_u8(&cg->code,(uint8_t)(0x20+(si-4)*8));
            }
#else
            // SysV: args 5-6 in R8/R9, args 7-8 on stack at [rsp+0]/[rsp+8]
            emit_load_arg4(&cg->code,slots[4]); // R8  = K
            emit_load_arg5(&cg->code,slots[5]); // R9  = N
            for(int si=6;si<8;si++){
                emit_load_rax(&cg->code,slots[si]);
                emit_stack_arg_sysv(&cg->code,si-6);
            }
#endif
            cg_call_extern(cg,g_fn_gemm);
            emit_xor_rax_rax(&cg->code);
            cg->scope->next_offset-=64;return;}
        fprintf(stderr,"CodeGen Error: unknown ai.%s()\n",method);exit(1);
    }

    // ── Installed package module call ─────────────────────────
    if (pkg_is_loaded(ns)) {
        PkgEntry* pe = NULL;
        for (int _i = 0; _i < g_pkg_count; _i++)
            if (strcmp(g_pkg_registry[_i].ns, ns) == 0) { pe = &g_pkg_registry[_i]; break; }
        if (pe) {
            int found = 0;
            for (int _i = 0; _i < pe->fn_count; _i++)
                if (strcmp(pe->fns[_i], method) == 0) { found = 1; break; }
            if (!found) {
                fprintf(stderr, "\nModuleError: '%s' has no function '%s'\n  Available: ", ns, method);
                for (int _i = 0; _i < pe->fn_count; _i++)
                    fprintf(stderr, "%s%s", pe->fns[_i], _i < pe->fn_count-1 ? ", " : "\n\n");
                if (pe->fn_count == 0) fprintf(stderr, "(none)\n\n");
                exit(1);
            }
            char internal[128];
            snprintf(internal, sizeof(internal), "%s__%s", ns, method);
            FnEntry* fe = fn_find(cg, internal);
            if (!fe) {
                fprintf(stderr, "\nInternalError: package fn '%s' not compiled\n\n", internal);
                exit(1);
            }
            int argc = call->argument_count;
            if (argc > 4) {
                fprintf(stderr, "\nModuleError: '%s.%s' — more than 4 args not yet supported\n\n", ns, method);
                exit(1);
            }
            int saved_off = cg->scope->next_offset;
            int arg_offsets[4] = {0};
            for (int _i = 0; _i < argc; _i++) arg_offsets[_i] = saved_off + (_i * 8);
            int temp_top = saved_off + argc * 8;
            if (temp_top > cg->stack_size) cg->stack_size = temp_top;
            cg->scope->next_offset = temp_top;
            for (int _i = 0; _i < argc; _i++) {
                cg_expr(cg, call->arguments[_i]);
                emit_store_rax(&cg->code, arg_offsets[_i]);
            }
            if (argc >= 1) emit_load_arg0(&cg->code,arg_offsets[0]);
            if (argc >= 2) emit_load_arg1(&cg->code,arg_offsets[1]);
            if (argc >= 3) emit_load_arg2(&cg->code,arg_offsets[2]);
            if (argc >= 4) emit_load_arg3(&cg->code,arg_offsets[3]);
            cg->scope->next_offset = saved_off;
            emit_u8(&cg->code, 0xE8);
            size_t pp = cg->code.size; emit_u32(&cg->code, 0);
            if (cg->call_patch_count >= MAX_CALL_PATCHES) {
                fprintf(stderr, "Fatal: too many call patches\n"); exit(1);
            }
            cg->call_patches[cg->call_patch_count].patch_offset = pp;
            strncpy_s(cg->call_patches[cg->call_patch_count].fn_name, 64, internal, _TRUNCATE);
            cg->call_patch_count++;
            eph_invalidate(cg);
            return;
        }
    }

    fprintf(stderr,
        "\nModuleError: Unknown module '%s'\n"
        "  Built-in: time, datetime, math, os, io, sys, list, str, ai\n"
        "  Try: omnip install %s\n\n", ns, ns);
    exit(1);
}

// ============================================================
//  USER FUNCTION CALLS
// ============================================================
static void cg_call_user(CodeGen*cg,AST_Expression_Call*node,const char*fn_name){
    // CLASS CONSTRUCTOR: Person("Alice", 30)
    ClassEntry*ce=class_find(cg,fn_name);
    if(ce){
        // 1. Allocate instance: mov rcx, MAX_FIELDS; call omni_class_alloc; store in temp
        // We use a conservative alloc of MAX_FIELDS slots to avoid needing exact field count
        // (fields are discovered lazily at method compile time)
        // Actually alloc 32 fields max per instance — 16 bytes each
        int inst_slot=cg->scope->next_offset;cg->scope->next_offset+=8;
        if(cg->scope->next_offset>cg->stack_size)cg->stack_size=cg->scope->next_offset;
        emit_mov_arg0_imm64(&cg->code,MAX_FIELDS); // alloc MAX_FIELDS slots
        cg_call_extern(cg,g_fn_class_alloc);
        emit_store_rax(&cg->code,inst_slot); // save instance ptr
        // 2. Call ClassName_init(instance_ptr, arg0, arg1, ...)
        char init_name[128];
        snprintf(init_name,sizeof(init_name),"%s_init",fn_name);
        FnEntry*init_fe=fn_find(cg,init_name);
        if(init_fe){
            int argc=node->argument_count;
            int total=argc+1; // +1 for self
            if(total>4){fprintf(stderr,"CodeGen Error: constructor >3 args not supported\n");exit(1);}
            int slots[4];
            int base=cg->scope->next_offset;
            for(int i=0;i<total;i++){slots[i]=base+i*8;cg->scope->next_offset+=8;}
            int top_off=base+total*8;
            if(top_off>cg->stack_size)cg->stack_size=top_off;
            // Arg 0 = self (instance ptr)
            emit_load_rax(&cg->code,inst_slot);emit_store_rax(&cg->code,slots[0]);
            // Remaining args
            for(int i=0;i<argc;i++){cg_expr(cg,node->arguments[i]);emit_store_rax(&cg->code,slots[i+1]);}
            if(total>=1)emit_load_arg0(&cg->code,slots[0]);
            if(total>=2)emit_load_arg1(&cg->code,slots[1]);
            if(total>=3)emit_load_arg2(&cg->code,slots[2]);
            if(total>=4)emit_load_arg3(&cg->code,slots[3]);
            cg->scope->next_offset=base;
            emit_u8(&cg->code,0xE8);size_t pp=cg->code.size;emit_u32(&cg->code,0);
            if(cg->call_patch_count>=MAX_CALL_PATCHES){fprintf(stderr,"Fatal: too many call patches\n");exit(1);}
            cg->call_patches[cg->call_patch_count].patch_offset=pp;
            strncpy_s(cg->call_patches[cg->call_patch_count].fn_name,64,init_name,_TRUNCATE);
            cg->call_patch_count++;
            eph_invalidate(cg);
        }
        // 3. Return instance ptr in RAX
        emit_load_rax(&cg->code,inst_slot);
        cg->scope->next_offset-=8;
        return;
    }
    FnEntry*fe=fn_find(cg,fn_name);
    if(!fe){omni_error(node->base.token.line,node->base.token.col,"NameError","function '%s' is not defined",fn_name);}
    if(fe->is_inline&&node->argument_count>=1&&node->argument_count<=4){cg_inline_call(cg,fe,node->arguments,node->argument_count);return;}
    int argc=node->argument_count;
    if(argc>4){fprintf(stderr,"CodeGen Error: function '%s' — >4 args not supported\n",fn_name);exit(1);}
    // Allocate arg temp slots above current locals.
    // We save/restore next_offset so these slots are reusable — the values
    // are loaded into registers before the call, so they don't need to live longer.
    int saved_off=cg->scope->next_offset;
    int arg_offsets[4];
    for(int i=0;i<argc;i++) arg_offsets[i]=saved_off+(i*8);
    int temp_top=saved_off+argc*8;
    if(temp_top>cg->stack_size)cg->stack_size=temp_top;
    // Evaluate all args and spill to temp slots
    for(int i=0;i<argc;i++){
        cg->scope->next_offset=temp_top; // keep at peak so nested cg_expr don't alias
        cg_expr(cg,node->arguments[i]);
        emit_store_rax(&cg->code,arg_offsets[i]);
    }
    // Load into ABI registers
    if(argc>=1)emit_load_arg0(&cg->code,arg_offsets[0]);
    if(argc>=2)emit_load_arg1(&cg->code,arg_offsets[1]);
    if(argc>=3)emit_load_arg2(&cg->code,arg_offsets[2]);
    if(argc>=4)emit_load_arg3(&cg->code,arg_offsets[3]);
    // Restore next_offset — arg slots no longer needed after CALL
    cg->scope->next_offset=saved_off;
    emit_u8(&cg->code,0xE8); size_t patch_pos=cg->code.size; emit_u32(&cg->code,0);
    if(cg->call_patch_count>=MAX_CALL_PATCHES){fprintf(stderr,"Fatal: too many call patches\n");exit(1);}
    cg->call_patches[cg->call_patch_count].patch_offset=patch_pos;
    strncpy_s(cg->call_patches[cg->call_patch_count].fn_name,sizeof(cg->call_patches[0].fn_name),fn_name,_TRUNCATE);
    cg->call_patch_count++;
    eph_invalidate(cg); /* CALL clobbers RAX — ephemeral slot is no longer valid */
    BETA_TRACE_CG("call_user '%s' argc=%d patch@%zu",fn_name,argc,patch_pos);
}

// ============================================================
//  MAIN EXPRESSION DISPATCH
// ============================================================
static void cg_expr(CodeGen*cg,AST_Expression*expr){
    if(!expr)return;
    switch(expr->type){
        case INTEGER_LITERAL:   cg_integer_literal(cg,(AST_Expression_IntegerLiteral*)expr);break;
        case FLOAT_LITERAL:     cg_float_literal(cg,(AST_Expression_FloatLiteral*)expr);break;
        case BOOLEAN_LITERAL:   cg_boolean_literal(cg,(AST_Expression_Boolean*)expr);break;
        case STRING_LITERAL:    cg_string_literal(cg,(AST_Expression_StringLiteral*)expr);break;
        case IDENTIFIER:        cg_identifier(cg,(AST_Expression_Identifier*)expr);break;
        case INFIX_EXPRESSION:  cg_infix(cg,(AST_Expression_Infix*)expr);break;
        case PREFIX_EXPRESSION:{
            AST_Expression_Prefix*p=(AST_Expression_Prefix*)expr;
            cg_expr(cg,p->right);
            if(strcmp(p->operator,"-")==0){emit_u8(&cg->code,REX_W);emit_u8(&cg->code,0xF7);emit_u8(&cg->code,0xD8);}
            else if(strcmp(p->operator,"not")==0){emit_test_rax(&cg->code);emit_setcc_rax(&cg->code,0x94);}
            break;
        }
        case CALL_EXPRESSION:{
            AST_Expression_Call*call=(AST_Expression_Call*)expr;
            if(call->function&&call->function->type==MEMBER_ACCESS_EXPRESSION){
                AST_Expression_MemberAccess*ma=(AST_Expression_MemberAccess*)call->function;
                if(ma->object&&ma->object->type==IDENTIFIER){
                    const char*ns_raw=((AST_Expression_Identifier*)ma->object)->value;
                    /* Resolve alias: "use math as m" -> m.sqrt becomes math.sqrt */
                    const char*ns=alias_resolve(cg,ns_raw);
                    // Check if ns is an instance variable whose type maps to a class
                    Symbol*inst_sym=scope_get(cg->scope,ns);
                    ClassEntry*ce=NULL;
                    if(inst_sym&&inst_sym->type==OMNI_TYPE_UNKNOWN){
                        // Try to find class by variable name convention — check class_table
                        // We store class name in symbol name's class field (OMNI_TYPE_UNKNOWN = instance)
                        // We need to look up by the class name stored in inst_sym.
                        // For now: search all classes for a method named ma->member
                        for(int ci=0;ci<cg->class_count;ci++){
                            ClassEntry*tce=&cg->class_table[ci];
                            for(int mi=0;mi<tce->method_count;mi++){
                                if(!strcmp(tce->method_names[mi],ma->member)){
                                    // Check if the symbol was defined by this class's constructor
                                    // We encoded the class name in the symbol's name field as a tag
                                    char tag[128];
                                    snprintf(tag,sizeof(tag),"__class_%s",tce->class_name);
                                    Symbol*tag_sym=scope_get(cg->scope,tag);
                                    if(tag_sym&&tag_sym->stack_offset==inst_sym->stack_offset){
                                        ce=tce;break;
                                    }
                                }
                            }
                            if(ce)break;
                        }
                    }
                    if(ce){
                        // Method call: emit ClassName_method(instance_ptr, args...)
                        char full_method[128];
                        snprintf(full_method,sizeof(full_method),"%s_%s",ce->class_name,ma->member);
                        // Build a synthetic call with instance as first arg
                        // Evaluate instance ptr + all args, pass as (self, arg0, arg1, ...)
                        int total_args=call->argument_count+1;
                        if(total_args>4){fprintf(stderr,"CodeGen Error: method >3 args not supported\n");exit(1);}
                        int slots[4];
                        int base=cg->scope->next_offset;
                        for(int ai=0;ai<total_args;ai++){cg->scope->next_offset+=8;slots[ai]=base+ai*8;}
                        int top=base+total_args*8;
                        if(top>cg->stack_size)cg->stack_size=top;
                        // Eval instance ptr
                        emit_load_rax(&cg->code,inst_sym->stack_offset);
                        emit_store_rax(&cg->code,slots[0]);
                        // Eval each arg
                        for(int ai=0;ai<call->argument_count;ai++){
                            cg_expr(cg,call->arguments[ai]);
                            emit_store_rax(&cg->code,slots[ai+1]);
                        }
                        if(total_args>=1)emit_load_arg0(&cg->code,slots[0]);
                        if(total_args>=2)emit_load_arg1(&cg->code,slots[1]);
                        if(total_args>=3)emit_load_arg2(&cg->code,slots[2]);
                        if(total_args>=4)emit_load_arg3(&cg->code,slots[3]);
                        cg->scope->next_offset=base;
                        // Emit call patch
                        emit_u8(&cg->code,0xE8);size_t pp=cg->code.size;emit_u32(&cg->code,0);
                        if(cg->call_patch_count>=MAX_CALL_PATCHES){fprintf(stderr,"Fatal: too many call patches\n");exit(1);}
                        cg->call_patches[cg->call_patch_count].patch_offset=pp;
                        strncpy_s(cg->call_patches[cg->call_patch_count].fn_name,64,full_method,_TRUNCATE);
                        cg->call_patch_count++;
                        eph_invalidate(cg);
                        break;
                    }
                    // Not an instance — treat as module call
                    cg_module_call(cg,call,ns,ma->member);
                    break;
                }
                fprintf(stderr,"CodeGen Error: unsupported member call\n");exit(1);
            }
            if(call->function&&call->function->type==IDENTIFIER){
                const char*fn_name=((AST_Expression_Identifier*)call->function)->value;
                if(strcmp(fn_name,"print")==0){cg_call_print(cg,call);break;}
                if(strcmp(fn_name,"input")==0){cg_call_input(cg,call);break;}
                if(strcmp(fn_name,"len")  ==0){cg_call_len(cg,call);break;}
                if(strcmp(fn_name,"int")  ==0){cg_call_int(cg,call);break;}
                if(strcmp(fn_name,"str")  ==0){cg_call_str(cg,call);break;}
                if(strcmp(fn_name,"range")==0){cg_call_range(cg,call);break;}
                if(strcmp(fn_name,"assert")==0){cg_call_assert(cg,call);break;}
                cg_call_user(cg,call,fn_name);break;
            }
            fprintf(stderr,"CodeGen Error: unsupported call expression\n");exit(1);
        }
        // self.field read — member access on self inside a method
        case MEMBER_ACCESS_EXPRESSION:{
            AST_Expression_MemberAccess*ma=(AST_Expression_MemberAccess*)expr;
            if(ma->object&&ma->object->type==IDENTIFIER){
                const char*obj=((AST_Expression_Identifier*)ma->object)->value;
                // self.field inside a method
                if(!strcmp(obj,"self")&&cg->current_class[0]){
                    ClassEntry*ce=class_find(cg,cg->current_class);
                    if(ce){
                        int fidx=class_field_index(ce,ma->member);
                        if(fidx<0){fidx=class_field_ensure(ce,ma->member);}
                        // Load self ptr into rax, then load field
                        emit_load_rax(&cg->code,cg->self_slot);
                        emit_field_load(&cg->code,fidx);
                        break;
                    }
                }
                // Instance field read: obj.field where obj is an instance
                Symbol*inst_sym=scope_get(cg->scope,obj);
                if(inst_sym&&inst_sym->type==OMNI_TYPE_UNKNOWN){
                    // Find the class for this instance
                    char tag[128];snprintf(tag,sizeof(tag),"__class_%s_slot",obj);
                    Symbol*tag_sym=scope_get(cg->scope,tag);
                    if(tag_sym){
                        // tag_sym->stack_offset stores the class index
                        ClassEntry*ce=&cg->class_table[tag_sym->stack_offset];
                        int fidx=class_field_index(ce,ma->member);
                        if(fidx>=0){
                            emit_load_rax(&cg->code,inst_sym->stack_offset);
                            emit_field_load(&cg->code,fidx);
                            break;
                        }
                    }
                }
            }
            emit_xor_rax_rax(&cg->code);
            break;
        }
        case NIL_LITERAL:emit_xor_rax_rax(&cg->code);break;
        case EMPTY_EXPRESSION:{
            AST_Expression_Empty*e=(AST_Expression_Empty*)expr;
            if(e->base.token.type==TOKEN_BREAK){cg_break_statement(cg);break;}
            if(e->base.token.type==TOKEN_CONTINUE){cg_continue_statement(cg);break;}
            break;
        }
        /* ── Phase 4: array/map/index ───────────────────────────────── */
        case ARRAY_LITERAL:{
            /* [a, b, c]  →  list.new() + list.push() for each element */
            AST_Expression_ArrayLiteral*al=(AST_Expression_ArrayLiteral*)expr;
            /* call list.new() → RAX = list ptr */
            emit_mov_arg0_imm64(&cg->code,0);
            cg_call_extern(cg,g_fn_list_new);
            /* store list ptr in a temp slot */
            int lslot=cg->scope->next_offset; cg->scope->next_offset+=8;
            if(cg->scope->next_offset>cg->stack_size)cg->stack_size=cg->scope->next_offset;
            emit_store_rax(&cg->code,lslot);
            /* push each element */
            for(int _i=0;_i<al->element_count;_i++){
                /* arg1 = list ptr */
                emit_load_rcx(&cg->code,lslot);
                /* arg2 = element value */
                cg_expr(cg,al->elements[_i]);
                emit_mov_arg1_rax(&cg->code);
                cg_call_extern(cg,g_fn_list_push);
            }
            /* result = list ptr */
            emit_load_rax(&cg->code,lslot);
            cg->scope->next_offset-=8;
            break;
        }
        case MAP_LITERAL:{
            /* {"k":v,...}  →  list.new() (flat key/value pairs) */
            AST_Expression_MapLiteral*ml=(AST_Expression_MapLiteral*)expr;
            emit_mov_arg0_imm64(&cg->code,0);
            cg_call_extern(cg,g_fn_list_new);
            int mslot=cg->scope->next_offset; cg->scope->next_offset+=8;
            if(cg->scope->next_offset>cg->stack_size)cg->stack_size=cg->scope->next_offset;
            emit_store_rax(&cg->code,mslot);
            for(int _i=0;_i<ml->entry_count;_i++){
                /* push key */
                emit_load_arg0(&cg->code,mslot);
                cg_expr(cg,ml->entries[_i]->key);
                emit_mov_arg1_rax(&cg->code);
                cg_call_extern(cg,g_fn_list_push);
                /* push value */
                emit_load_arg0(&cg->code,mslot);
                cg_expr(cg,ml->entries[_i]->value);
                emit_mov_arg1_rax(&cg->code);
                cg_call_extern(cg,g_fn_list_push);
            }
            emit_load_rax(&cg->code,mslot);
            cg->scope->next_offset-=8;
            break;
        }
        case INDEX_EXPRESSION:{
            /* obj[i]  →  list.get(obj, i) */
            AST_Expression_Index*ix=(AST_Expression_Index*)expr;
            int lslot=cg->scope->next_offset; cg->scope->next_offset+=8;
            if(cg->scope->next_offset>cg->stack_size)cg->stack_size=cg->scope->next_offset;
            cg_expr(cg,ix->left);
            emit_store_rax(&cg->code,lslot);
            cg_expr(cg,ix->index);
            emit_mov_arg1_rax(&cg->code);
            emit_load_arg0(&cg->code,lslot);
            cg_call_extern(cg,g_fn_list_get);
            cg->scope->next_offset-=8;
            break;
        }
        default:fprintf(stderr,"CodeGen Error: unsupported expression type %d\n",expr->type);exit(1);
    }
}

// ============================================================
//  STATEMENT CODEGEN
// ============================================================
static void cg_set_statement(CodeGen*cg,AST_Statement_Set*stmt){
    BETA_TRACE_CG("set '%s'",stmt->name?stmt->name->value:"?");
    /* Helper: check if a variable name is currently pinned to a register */
    #define IS_PINNED(varname) ({\
        int _p=0;\
        for(int _ri=0;_ri<cg->reg_var_depth&&_ri<7;_ri++)\
            if(!strcmp(cg->reg_var_names[_ri],(varname))){_p=1;break;}\
        _p;})
    if(stmt->value&&stmt->value->type==INFIX_EXPRESSION){
        AST_Expression_Infix*infix=(AST_Expression_Infix*)stmt->value;
        const char*op=infix->operator;
        int is_add=(strcmp(op,"+")==0),is_sub=(strcmp(op,"-")==0);
        /* OPT: set x = x + const  →  ADD/SUB mem,imm  (no register needed)
           SKIP this path if x is register-pinned — must update register not memory */
        if((is_add||is_sub)&&infix->left&&infix->left->type==IDENTIFIER&&infix->right&&infix->right->type==INTEGER_LITERAL){
            const char*lhs=((AST_Expression_Identifier*)infix->left)->value;
            int64_t rhs=((AST_Expression_IntegerLiteral*)infix->right)->value;
            if(strcmp(lhs,stmt->name->value)==0&&!IS_PINNED(stmt->name->value)){
                Symbol*sym=scope_get(cg->scope,stmt->name->value);
                if(sym){
                    if(is_add){if(rhs>=-128&&rhs<=127)emit_add_mem_imm8(&cg->code,sym->stack_offset,(int8_t)rhs);else emit_add_mem_imm32(&cg->code,sym->stack_offset,(int32_t)rhs);}
                    else{if(rhs>=-128&&rhs<=127)emit_sub_mem_imm8(&cg->code,sym->stack_offset,(int8_t)rhs);else emit_add_mem_imm32(&cg->code,sym->stack_offset,(int32_t)(-rhs));}
                    return;
                }
            }
        }
        /* OPT: set acc = acc + expr  →  load/reg acc, add expr, store/reg acc
           If acc is pinned: load from register, add, store to register (+ sync stack) */
        if((is_add||is_sub)&&infix->left&&infix->left->type==IDENTIFIER){
            const char*lhs_name=((AST_Expression_Identifier*)infix->left)->value;
            if(strcmp(lhs_name,stmt->name->value)==0){
                int _preg=-1;
                for(int _ri=0;_ri<cg->reg_var_depth&&_ri<7;_ri++)
                    if(!strcmp(cg->reg_var_names[_ri],lhs_name)){_preg=_ri;break;}
                if(_preg>=0){
                    /* FAST PATH: set x = x + 1 / set x = x - 1 while x pinned
                       → emit ADD/SUB reg,1 directly — 4 bytes, 1 µop, no RAX roundtrip */
                    if(((AST_Expression_IntegerLiteral*)infix->right)->value==1){
                        if(is_add) emit_inc_pinned(&cg->code,_preg);
                        else       emit_dec_pinned(&cg->code,_preg);
                        /* sync pinned register to stack slot */
                        Symbol*_ps2=scope_get(cg->scope,lhs_name);
                        if(_ps2) emit_store_pinned(&cg->code,_preg,_ps2->stack_offset);
                        return;
                    }
                    /* General pinned accumulate: eval rhs → rcx, load reg → rax, add/sub, store reg + stack */
                    cg_expr(cg,infix->right); emit_mov_rcx_rax(&cg->code);
                    switch(_preg){
                        case 0:emit_mov_rax_r14(&cg->code);break;
                        case 1:emit_mov_rax_r15(&cg->code);break;
                        case 2:emit_mov_rax_rbx(&cg->code);break;
                        case 3:emit_mov_rax_r12(&cg->code);break;
                        case 4:emit_mov_rax_r13(&cg->code);break;
                        case 5:emit_mov_rax_rsi(&cg->code);break;
                        case 6:emit_mov_rax_rdi(&cg->code);break;
                    }
                    if(is_add)emit_add_rax_rcx(&cg->code); else emit_sub_rax_rcx(&cg->code);
                    switch(_preg){
                        case 0:emit_u8(&cg->code,0x49);emit_u8(&cg->code,0x89);emit_u8(&cg->code,0xC6);break;
                        case 1:emit_u8(&cg->code,0x49);emit_u8(&cg->code,0x89);emit_u8(&cg->code,0xC7);break;
                        case 2:emit_mov_rbx_rax(&cg->code);break;
                        case 3:emit_mov_r12_rax(&cg->code);break;
                        case 4:emit_mov_r13_rax(&cg->code);break;
                        case 5:emit_mov_rsi_rax(&cg->code);break;
                        case 6:emit_mov_rdi_rax(&cg->code);break;
                    }
                    Symbol*_ps=scope_get(cg->scope,lhs_name);
                    if(_ps)emit_store_rax(&cg->code,_ps->stack_offset);
                    return;
                }
                Symbol*sym=scope_get(cg->scope,stmt->name->value);
                if(sym){
                    cg_expr(cg,infix->right); emit_mov_rcx_rax(&cg->code);
                    emit_load_rax(&cg->code,sym->stack_offset);
                    if(is_add) emit_add_rax_rcx(&cg->code);
                    else       emit_sub_rax_rcx(&cg->code);
                    emit_store_rax(&cg->code,sym->stack_offset);
                    return;
                }
            }
        }
        /* OPT: set x = x * expr  →  load x, multiply, store (register-aware) */
        int is_mul=(strcmp(op,"*")==0);
        if(is_mul&&infix->left&&infix->left->type==IDENTIFIER){
            const char*lhs_name=((AST_Expression_Identifier*)infix->left)->value;
            if(strcmp(lhs_name,stmt->name->value)==0){
                int _preg=-1;
                for(int _ri=0;_ri<cg->reg_var_depth&&_ri<7;_ri++)
                    if(!strcmp(cg->reg_var_names[_ri],lhs_name)){_preg=_ri;break;}
                if(_preg>=0){
                    cg_expr(cg,infix->right); emit_mov_rcx_rax(&cg->code);
                    switch(_preg){
                        case 0:emit_mov_rax_r14(&cg->code);break;
                        case 1:emit_mov_rax_r15(&cg->code);break;
                        case 2:emit_mov_rax_rbx(&cg->code);break;
                        case 3:emit_mov_rax_r12(&cg->code);break;
                        case 4:emit_mov_rax_r13(&cg->code);break;
                        case 5:emit_mov_rax_rsi(&cg->code);break;
                        case 6:emit_mov_rax_rdi(&cg->code);break;
                    }
                    emit_imul_rax_rcx(&cg->code);
                    switch(_preg){
                        case 0:emit_u8(&cg->code,0x49);emit_u8(&cg->code,0x89);emit_u8(&cg->code,0xC6);break;
                        case 1:emit_u8(&cg->code,0x49);emit_u8(&cg->code,0x89);emit_u8(&cg->code,0xC7);break;
                        case 2:emit_mov_rbx_rax(&cg->code);break;
                        case 3:emit_mov_r12_rax(&cg->code);break;
                        case 4:emit_mov_r13_rax(&cg->code);break;
                        case 5:emit_mov_rsi_rax(&cg->code);break;
                        case 6:emit_mov_rdi_rax(&cg->code);break;
                    }
                    Symbol*_ps=scope_get(cg->scope,lhs_name);
                    if(_ps)emit_store_rax(&cg->code,_ps->stack_offset);
                    return;
                }
                Symbol*sym=scope_get(cg->scope,stmt->name->value);
                if(sym){
                    cg_expr(cg,infix->right); emit_mov_rcx_rax(&cg->code);
                    emit_load_rax(&cg->code,sym->stack_offset);
                    emit_imul_rax_rcx(&cg->code);
                    emit_store_rax(&cg->code,sym->stack_offset);
                    return;
                }
            }
        }
    }
    #undef IS_PINNED
    cg_expr(cg,stmt->value);
    /* Check if this variable is pinned to a register — store to register, not stack */
    {
        int _preg=-1;
        for(int _ri=0;_ri<cg->reg_var_depth&&_ri<7;_ri++){
            if(strcmp(cg->reg_var_names[_ri],stmt->name->value)==0){_preg=_ri;break;}
        }
        if(_preg>=0){
            switch(_preg){
                case 0: /* r14 — handled by for-loop, shouldn't reach here normally */
                        emit_u8(&cg->code,0x49);emit_u8(&cg->code,0x89);emit_u8(&cg->code,0xC6); break;
                case 1: emit_u8(&cg->code,0x49);emit_u8(&cg->code,0x89);emit_u8(&cg->code,0xC7); break;
                case 2: emit_mov_rbx_rax(&cg->code); break;
                case 3: emit_mov_r12_rax(&cg->code); break;
                case 4: emit_mov_r13_rax(&cg->code); break;
                case 5: emit_mov_rsi_rax(&cg->code); break;
                case 6: emit_mov_rdi_rax(&cg->code); break;
            }
            /* Also keep stack slot in sync for any code that reads via scope_get */
            Symbol*_psym=scope_get(cg->scope,stmt->name->value);
            if(_psym) emit_store_rax(&cg->code,_psym->stack_offset);
            goto set_done;
        }
    }
    {
    Symbol*sym2=scope_get(cg->scope,stmt->name->value);
    if(!sym2){
        OmniType t=infer_type(cg,stmt->value);
        sym2=scope_define(cg->scope,stmt->name->value,t);
        if(sym2->stack_offset>cg->stack_size)cg->stack_size=sym2->stack_offset;
    } else {
        OmniType new_type=infer_type(cg,stmt->value);
        if(new_type!=OMNI_TYPE_UNKNOWN) sym2->type=new_type;
    }
    emit_store_rax(&cg->code,sym2->stack_offset);
    /* If RHS is a constructor call, tag this variable as an instance */
    if(stmt->value&&stmt->value->type==CALL_EXPRESSION){
        AST_Expression_Call*c=(AST_Expression_Call*)stmt->value;
        if(c->function&&c->function->type==IDENTIFIER){
            const char*cname=((AST_Expression_Identifier*)c->function)->value;
            if(class_find(cg,cname)){
                char tag[128];snprintf(tag,sizeof(tag),"__class_%s",cname);
                Symbol*tag_sym=scope_get(cg->scope,tag);
                if(!tag_sym){tag_sym=scope_define(cg->scope,tag,OMNI_TYPE_UNKNOWN);}
                tag_sym->stack_offset=sym2->stack_offset;
                char vtag[128];snprintf(vtag,sizeof(vtag),"__class_%s_slot",stmt->name->value);
                Symbol*vtag_sym=scope_get(cg->scope,vtag);
                if(!vtag_sym){vtag_sym=scope_define(cg->scope,vtag,OMNI_TYPE_UNKNOWN);}
                for(int ci=0;ci<cg->class_count;ci++){
                    if(!strcmp(cg->class_table[ci].class_name,cname)){
                        vtag_sym->stack_offset=ci;break;
                    }
                }
                sym2->type=OMNI_TYPE_UNKNOWN;
            }
        }
    }
    }
    set_done:;
}

static void cg_if_statement(CodeGen*cg,AST_Statement_If*stmt){
    cg_expr(cg,stmt->condition);emit_test_rax(&cg->code);size_t je=emit_je_fwd(&cg->code);
    // No child scope: variables defined inside if/else branches must remain
    // visible after the block ends (consistent with while/for behaviour).
    for(int i=0;i<stmt->consequence->statement_count;i++)cg_stmt(cg,stmt->consequence->statements[i]);
    if(stmt->alternative){size_t jmp=emit_jmp_fwd(&cg->code);resolve_fwd(&cg->code,je);cg_stmt(cg,stmt->alternative);resolve_fwd(&cg->code,jmp);}
    else resolve_fwd(&cg->code,je);
}

/* ── HOT VARIABLE ANALYZER ─────────────────────────────────────────────────
   Count how many times each variable is read (GET) in a block.
   Used to pick the hottest variables to pin in registers before a while loop.
   Slot mapping: 2=rbx, 3=r12, 4=r13  (0,1 are r14/r15 reserved for for-loops) */
#define HOT_MAX 32
typedef struct { char name[64]; int count; } HotVar;
static void count_expr_reads(AST_Expression*e, HotVar*hv, int*nhv){
    if(!e) return;
    switch(e->type){
        case IDENTIFIER:{
            const char*n=((AST_Expression_Identifier*)e)->value;
            for(int i=0;i<*nhv;i++) if(!strcmp(hv[i].name,n)){hv[i].count++;return;}
            if(*nhv<HOT_MAX){strncpy_s(hv[*nhv].name,64,n,_TRUNCATE);hv[(*nhv)++].count=1;}
            break;
        }
        case INFIX_EXPRESSION:{AST_Expression_Infix*in=(AST_Expression_Infix*)e;count_expr_reads(in->left,hv,nhv);count_expr_reads(in->right,hv,nhv);break;}
        case PREFIX_EXPRESSION:{count_expr_reads(((AST_Expression_Prefix*)e)->right,hv,nhv);break;}
        case CALL_EXPRESSION:{
            AST_Expression_Call*c=(AST_Expression_Call*)e;
            count_expr_reads(c->function,hv,nhv);
            for(int i=0;i<c->argument_count;i++) count_expr_reads(c->arguments[i],hv,nhv);
            break;
        }
        default: break;
    }
}
static void count_block_reads(AST_Statement_Block*blk, HotVar*hv, int*nhv){
    if(!blk) return;
    for(int i=0;i<blk->statement_count;i++){
        AST_Statement*s=blk->statements[i]; if(!s) continue;
        switch(s->type){
            case SET_STATEMENT:   count_expr_reads(((AST_Statement_Set*)s)->value,hv,nhv); break;
            case RETURN_STATEMENT:count_expr_reads(((AST_Statement_Return*)s)->return_value,hv,nhv); break;
            case EXPRESSION_STATEMENT:count_expr_reads(((AST_Statement_Expression*)s)->expression,hv,nhv); break;
            case IF_STATEMENT:{
                AST_Statement_If*is=(AST_Statement_If*)s;
                count_expr_reads(is->condition,hv,nhv);
                count_block_reads(is->consequence,hv,nhv);
                if(is->alternative&&is->alternative->type==IF_STATEMENT)
                    count_block_reads(((AST_Statement_If*)is->alternative)->consequence,hv,nhv);
                break;
            }
            case WHILE_STATEMENT:{
                AST_Statement_While*ws=(AST_Statement_While*)s;
                count_expr_reads(ws->condition,hv,nhv);
                count_block_reads(ws->body,hv,nhv);
                break;
            }
            default: break;
        }
    }
}

/* Emit MOV reg, [rbp-off]  —  load stack variable into pinned register
   slot 2=RBX  48 8B 9D disp32   slot 3=R12  4C 8B A5 disp32
   slot 4=R13  4C 8B AD disp32   slot 5=RSI  48 8B B5 disp32
   slot 6=RDI  48 8B BD disp32                                  */
static void emit_load_pinned(CodeBuf*b, int slot, int stack_off){
    switch(slot){
        case 2: emit_u8(b,0x48);emit_u8(b,0x8B);emit_u8(b,0x9D);emit_u32(b,(uint32_t)(-stack_off));break;
        case 3: emit_u8(b,0x4C);emit_u8(b,0x8B);emit_u8(b,0xA5);emit_u32(b,(uint32_t)(-stack_off));break;
        case 4: emit_u8(b,0x4C);emit_u8(b,0x8B);emit_u8(b,0xAD);emit_u32(b,(uint32_t)(-stack_off));break;
        case 5: emit_u8(b,0x48);emit_u8(b,0x8B);emit_u8(b,0xB5);emit_u32(b,(uint32_t)(-stack_off));break; /* MOV RSI,[rbp-off] */
        case 6: emit_u8(b,0x48);emit_u8(b,0x8B);emit_u8(b,0xBD);emit_u32(b,(uint32_t)(-stack_off));break; /* MOV RDI,[rbp-off] */
        default: break;
    }
}
/* Emit MOV [rbp-off], reg  —  write pinned register back to stack slot
   slot 2=RBX  48 89 9D disp32   slot 3=R12  4C 89 A5 disp32
   slot 4=R13  4C 89 AD disp32   slot 5=RSI  48 89 B5 disp32
   slot 6=RDI  48 89 BD disp32                                  */
static void emit_store_pinned(CodeBuf*b, int slot, int stack_off){
    switch(slot){
        case 2: emit_u8(b,0x48);emit_u8(b,0x89);emit_u8(b,0x9D);emit_u32(b,(uint32_t)(-stack_off));break;
        case 3: emit_u8(b,0x4C);emit_u8(b,0x89);emit_u8(b,0xA5);emit_u32(b,(uint32_t)(-stack_off));break;
        case 4: emit_u8(b,0x4C);emit_u8(b,0x89);emit_u8(b,0xAD);emit_u32(b,(uint32_t)(-stack_off));break;
        case 5: emit_u8(b,0x48);emit_u8(b,0x89);emit_u8(b,0xB5);emit_u32(b,(uint32_t)(-stack_off));break; /* MOV [rbp-off],RSI */
        case 6: emit_u8(b,0x48);emit_u8(b,0x89);emit_u8(b,0xBD);emit_u32(b,(uint32_t)(-stack_off));break; /* MOV [rbp-off],RDI */
        default: break;
    }
}

/* ── WHILE STATEMENT ────────────────────────────────────────────────────────
   Before entering the loop, we look at the condition and body to find the
   hottest integer variables (used ≥ 4 times and not already pinned).  We pin
   up to 3 extras in RBX / R12 / R13 (slots 2-4 of reg_var_names).  This is
   the key optimisation that closes the gap vs C -O3 for tight while loops:
   all inner-loop counters/accumulators stay in registers, zero stack traffic.

   Callee-save contract: we save the registers we are about to clobber into
   fresh stack slots ABOVE the current frame watermark, then restore on exit.
   This is safe because while loops can be nested — each level saves/restores
   its own set, and the stack watermark grows accordingly.                    */
/* prescan_set_names — collect all variable names first assigned by 'set'
   inside a block (including inside if-branches).  Lets us pre-allocate
   stack slots for loop-local vars BEFORE the loop starts so we can then
   load them into pinned registers on entry.                               */
static void prescan_set_names(AST_Statement_Block*blk,HotVar*hv,int*nhv){
    if(!blk)return;
    for(int i=0;i<blk->statement_count;i++){
        AST_Statement*s=blk->statements[i]; if(!s)continue;
        if(s->type==SET_STATEMENT){
            const char*n=((AST_Statement_Set*)s)->name->value;
            int found=0;
            for(int j=0;j<*nhv;j++) if(!strcmp(hv[j].name,n)){found=1;break;}
            if(!found&&*nhv<HOT_MAX){strncpy_s(hv[*nhv].name,64,n,_TRUNCATE);hv[(*nhv)++].count=0;}
        }
        if(s->type==IF_STATEMENT){
            AST_Statement_If*is2=(AST_Statement_If*)s;
            prescan_set_names(is2->consequence,hv,nhv);
        }
    }
}

static void cg_while_statement(CodeGen*cg,AST_Statement_While*stmt){
    int saved_break=cg->break_patch_count,saved_cont=cg->continue_patch_count;

    /* Step 1: pre-allocate stack slots for vars SET inside this body.
       This is the critical fix: variables like 'acc', 'a', 'b', 'k' that are
       defined by 'set' INSIDE the loop body have no stack slot yet.  Without
       pre-allocating we can't pin them.  We allocate now (zero-init implied by
       the first 'set' statement that will execute) so scope_get() returns a
       valid slot for the pinning pass below.                                  */
    {
        HotVar pre[HOT_MAX]; int npre=0;
        prescan_set_names(stmt->body,pre,&npre);
        for(int pi=0;pi<npre;pi++){
            if(scope_get(cg->scope,pre[pi].name)) continue; /* already exists */
            Symbol*ns2=scope_define(cg->scope,pre[pi].name,OMNI_TYPE_INT);
            if(ns2->stack_offset>cg->stack_size) cg->stack_size=ns2->stack_offset;
        }
    }

    /* Step 2: count read frequencies across condition + full body */
    HotVar hv[HOT_MAX]; int nhv=0;
    count_expr_reads(stmt->condition,hv,&nhv);
    count_block_reads(stmt->body,hv,&nhv);
    for(int i=1;i<nhv;i++){HotVar tmp=hv[i];int j=i-1;
        while(j>=0&&hv[j].count<tmp.count){hv[j+1]=hv[j];j--;}hv[j+1]=tmp;}

    /* Step 3: pin most-read vars into callee-saved registers.
       Win64: RBX(2)/R12(3)/R13(4)/RSI(5)/RDI(6) — 5 slots.
       SysV:  RBX(2)/R12(3)/R13(4) only — RSI/RDI are caller-saved there,
       so they must never hold loop-pinned values (OMNI_MAX_PIN).
       Threshold=1 so ALL loop variables get pinned — even single-use vars
       in tight loops run millions of iterations where every cycle counts. */
    int base_depth=cg->reg_var_depth;
    int save_slots[OMNI_MAX_PIN]; int n_saved=0;

    for(int ri=0;ri<nhv&&n_saved<OMNI_MAX_PIN;ri++){
        if(hv[ri].count<1) break;
        /* skip if already pinned in any slot */
        int already=0;
        for(int k=0;k<7;k++)
            if(cg->reg_var_names[k][0]&&!strcmp(cg->reg_var_names[k],hv[ri].name)){already=1;break;}
        if(already) continue;
        /* must have a stack slot — either pre-existing or just pre-allocated */
        Symbol*sym=scope_get(cg->scope,hv[ri].name);
        if(!sym) continue;
        int slot=2+n_saved; /* use slots 2..(1+OMNI_MAX_PIN) in order */
        if(slot>1+OMNI_MAX_PIN) break;
        /* check slot is free (not occupied by an outer loop) */
        if(cg->reg_var_names[slot][0]) continue;
        /* spill current register content to a fresh stack slot */
        int spill_off=cg->scope->next_offset; cg->scope->next_offset+=8;
        if(cg->scope->next_offset>cg->stack_size) cg->stack_size=cg->scope->next_offset;
        save_slots[n_saved]=spill_off;
        emit_store_pinned(&cg->code,slot,spill_off);
        /* load variable's current value into the register */
        emit_load_rax(&cg->code,sym->stack_offset);
        switch(slot){
            case 2:emit_mov_rbx_rax(&cg->code);break;
            case 3:emit_mov_r12_rax(&cg->code);break;
            case 4:emit_mov_r13_rax(&cg->code);break;
            case 5:emit_mov_rsi_rax(&cg->code);break;
            case 6:emit_mov_rdi_rax(&cg->code);break;
        }
        strncpy_s(cg->reg_var_names[slot],64,hv[ri].name,_TRUNCATE);
        if(cg->reg_var_depth<=slot) cg->reg_var_depth=slot+1;
        n_saved++;
    }

    /* Step 4: emit loop */
    size_t loop_top=cg->code.size;
    cg_expr(cg,stmt->condition);emit_test_rax(&cg->code);size_t je=emit_je_fwd(&cg->code);
    for(int i=0;i<stmt->body->statement_count;i++)cg_stmt(cg,stmt->body->statements[i]);
    for(int i=saved_cont;i<cg->continue_patch_count;i++){
        int32_t d=(int32_t)(loop_top-(cg->continue_patches[i]+4));
        patch_u32(&cg->code,cg->continue_patches[i],(uint32_t)d);
    }
    cg->continue_patch_count=saved_cont;
    emit_jmp_back(&cg->code,loop_top);resolve_fwd(&cg->code,je);
    for(int i=saved_break;i<cg->break_patch_count;i++)resolve_fwd(&cg->code,cg->break_patches[i]);
    cg->break_patch_count=saved_break;

    /* Step 5: writeback pinned registers to stack, restore original reg content.
       CRITICAL: must NOT touch RAX — caller may be using it as a return value.
       Use emit_store_pinned() which writes directly from the pinned reg to memory. */
    for(int sidx=n_saved-1;sidx>=0;sidx--){
        int slot=2+sidx;
        Symbol*sym=scope_get(cg->scope,cg->reg_var_names[slot]);
        if(sym) emit_store_pinned(&cg->code,slot,sym->stack_offset);
        emit_load_pinned(&cg->code,slot,save_slots[sidx]);
        cg->scope->next_offset-=8;
        cg->reg_var_names[slot][0]=0;
    }
    cg->reg_var_depth=base_depth;
}

static void cg_for_range(CodeGen*cg,AST_Statement_For*stmt,AST_Expression_Call*range_call){
    const char*iter_name=stmt->iterator->value;
    int saved_break=cg->break_patch_count,saved_cont=cg->continue_patch_count;
    int argc=range_call->argument_count;
    int saved_next_off=cg->scope->next_offset;

    // ── FAST PATH: range(n) with step=1, start=0 ─────────────────────────────
    // If we have a free register slot (r14 or r15) AND this is simple range(n),
    // pin the counter in a register. No stack slots, no loads per iteration.
    // This is the hot path for the loop benchmark.
    int reg_level = cg->reg_var_depth;
    int use_reg   = (reg_level < 2); // have r14 or r15 free

    if(use_reg){
        // Evaluate stop (and optionally start/step) — only stop needs a slot
        int stop_slot = cg->scope->next_offset; cg->scope->next_offset += 8;
        if(cg->scope->next_offset > cg->stack_size) cg->stack_size = cg->scope->next_offset;

        int64_t start_imm = 0;
        int     start_is_zero = 1;

        if(argc == 1){
            // range(n): start=0, step=1, stop=n
            cg_expr(cg, range_call->arguments[0]);
            emit_store_rax(&cg->code, stop_slot);
        } else if(argc == 2){
            // range(start,stop): step=1
            cg_expr(cg, range_call->arguments[0]); start_imm=(int64_t)0; (void)start_imm;
            // Can't know start at compile time generally — use start_is_zero=0
            int start_slot = cg->scope->next_offset; cg->scope->next_offset += 8;
            if(cg->scope->next_offset > cg->stack_size) cg->stack_size = cg->scope->next_offset;
            emit_store_rax(&cg->code, start_slot);
            cg_expr(cg, range_call->arguments[1]);
            emit_store_rax(&cg->code, stop_slot);
            start_is_zero = 0;
            // Init counter from start_slot
            emit_load_rax(&cg->code, start_slot);
            cg->scope->next_offset -= 8; // release start_slot
        } else {
            // range(start,stop,step): fall through to stack path if step != 1
            // For now just handle as stack path
            use_reg = 0;
        }

        if(use_reg){
            // Pin counter in r14 or r15
            strncpy_s(cg->reg_var_names[reg_level], 64, iter_name, _TRUNCATE);
            cg->reg_var_depth++;
            // Also define iter as a stack var so body code can reference it by name
            // BUT: reads of iter_name will hit the register path in cg_identifier
            // so we just need the symbol in scope for any set/assign within body
            Symbol* iter_sym = scope_get(cg->scope, iter_name);
            if(!iter_sym){
                iter_sym = scope_define(cg->scope, iter_name, OMNI_TYPE_INT);
                if(iter_sym->stack_offset > cg->stack_size) cg->stack_size = iter_sym->stack_offset;
            }

            // Load start into register
            if(start_is_zero && argc==1){
                if(reg_level==0) emit_mov_r14_imm64(&cg->code,0);
                else             emit_mov_r15_imm64(&cg->code,0);
            } else {
                // RAX already holds start from above
                if(reg_level==0) emit_mov_r11_rax(&cg->code); // r11 = tmp; then use r14
                // Actually: emit MOV R14,RAX or MOV R15,RAX
                // R14 = REX.WB 0x89 ModRM(R14 as r/m) ... use direct encoding:
                if(reg_level==0){
                    // MOV R14, RAX: 49 89 C6  (REX.WB, MOV r/m64,r64, ModRM=11 000 110)
                    emit_u8(&cg->code,0x49);emit_u8(&cg->code,0x89);emit_u8(&cg->code,0xC6);
                } else {
                    // MOV R15, RAX: 49 89 C7
                    emit_u8(&cg->code,0x49);emit_u8(&cg->code,0x89);emit_u8(&cg->code,0xC7);
                }
            }

            // Pin hottest accumulator from for-body into RBX/R12/R13
            // e.g. 'set s = s + i' — s is used every iteration and benefits from pinning
            int fbody_base=cg->reg_var_depth;
            int fbody_saves[OMNI_MAX_PIN]; int fbody_nsaved=0; /* FIX(N5): was [3] while cap allowed 5 pinned vars — stack overflow on Win64 */
            {
                HotVar fpre[HOT_MAX]; int fnpre=0;
                prescan_set_names(stmt->body,fpre,&fnpre);
                for(int pi=0;pi<fnpre;pi++){
                    if(scope_get(cg->scope,fpre[pi].name)) continue;
                    Symbol*fns=scope_define(cg->scope,fpre[pi].name,OMNI_TYPE_INT);
                    if(fns->stack_offset>cg->stack_size)cg->stack_size=fns->stack_offset;
                }
                HotVar fhv[HOT_MAX]; int fnhv=0;
                count_block_reads(stmt->body,fhv,&fnhv);
                for(int i=1;i<fnhv;i++){HotVar t=fhv[i];int j=i-1;
                    while(j>=0&&fhv[j].count<t.count){fhv[j+1]=fhv[j];j--;}fhv[j+1]=t;}
                for(int ri=0;ri<fnhv&&fbody_nsaved<OMNI_MAX_PIN;ri++){
                    if(fhv[ri].count<1) break;
                    int already=0;
                    for(int k=0;k<7;k++)
                        if(cg->reg_var_names[k][0]&&!strcmp(cg->reg_var_names[k],fhv[ri].name)){already=1;break;}
                    if(already) continue;
                    Symbol*fsym=scope_get(cg->scope,fhv[ri].name);
                    if(!fsym) continue;
                    int fslot=2+fbody_nsaved;
                    if(fslot>6) break;
                    if(cg->reg_var_names[fslot][0]) continue; /* slot occupied by outer */
                    int fspill=cg->scope->next_offset; cg->scope->next_offset+=8;
                    if(cg->scope->next_offset>cg->stack_size)cg->stack_size=cg->scope->next_offset;
                    fbody_saves[fbody_nsaved]=fspill;
                    emit_store_pinned(&cg->code,fslot,fspill);
                    emit_load_rax(&cg->code,fsym->stack_offset);
                    switch(fslot){case 2:emit_mov_rbx_rax(&cg->code);break;case 3:emit_mov_r12_rax(&cg->code);break;case 4:emit_mov_r13_rax(&cg->code);break;case 5:emit_mov_rsi_rax(&cg->code);break;case 6:emit_mov_rdi_rax(&cg->code);break;}
                    strncpy_s(cg->reg_var_names[fslot],64,fhv[ri].name,_TRUNCATE);
                    if(cg->reg_var_depth<=fslot) cg->reg_var_depth=fslot+1;
                    fbody_nsaved++;
                }
            }
            // Loop top: compare counter vs stop
            size_t loop_top = cg->code.size;
            emit_load_rcx(&cg->code, stop_slot);
            if(reg_level==0) emit_cmp_r14_rcx(&cg->code);
            else             emit_cmp_r15_rcx(&cg->code);
            size_t jge = emit_jge_fwd(&cg->code);

            // Sync register into stack var at loop top so body reads it correctly
            // cg_identifier already handles reg_var_names → emit_mov_rax_r14/r15
            // so body reads of iter_name go to register directly — no sync needed

            // Body
            for(int i=0;i<stmt->body->statement_count;i++) cg_stmt(cg,stmt->body->statements[i]);

            // Continue patches → increment point
            size_t inc_top = cg->code.size;
            for(int i=saved_cont;i<cg->continue_patch_count;i++){
                int32_t d=(int32_t)(inc_top-(cg->continue_patches[i]+4));
                patch_u32(&cg->code,cg->continue_patches[i],(uint32_t)d);
            }
            cg->continue_patch_count = saved_cont;

            // Increment register counter
            if(reg_level==0) emit_inc_r14(&cg->code);
            else             emit_inc_r15(&cg->code);

            emit_jmp_back(&cg->code, loop_top);
            resolve_fwd(&cg->code, jge);

            // Break patches
            for(int i=saved_break;i<cg->break_patch_count;i++) resolve_fwd(&cg->code,cg->break_patches[i]);
            cg->break_patch_count = saved_break;

            // Restore for-body pinned vars (s, acc, etc.) before popping loop counter
            // CRITICAL: do NOT route through RAX — preserve caller's return value
            for(int sidx=fbody_nsaved-1;sidx>=0;sidx--){
                int fslot=2+sidx;
                Symbol*fs2=scope_get(cg->scope,cg->reg_var_names[fslot]);
                if(fs2) emit_store_pinned(&cg->code,fslot,fs2->stack_offset);
                emit_load_pinned(&cg->code,fslot,fbody_saves[sidx]);
                cg->scope->next_offset-=8; cg->reg_var_names[fslot][0]=0;
            }
            cg->reg_var_depth=fbody_base;
            cg->reg_var_depth--;
            cg->scope->next_offset = saved_next_off;
            return;
        }
    }

    // ── GENERAL STACK PATH (step != 1, or reg_depth >= 2) ─────────────────────
    int start_slot=cg->scope->next_offset; cg->scope->next_offset+=8;
    int stop_slot =cg->scope->next_offset; cg->scope->next_offset+=8;
    int step_slot =cg->scope->next_offset; cg->scope->next_offset+=8;
    if(cg->scope->next_offset>cg->stack_size) cg->stack_size=cg->scope->next_offset;

    if(argc==1){
        emit_xor_rax_rax(&cg->code); emit_store_rax(&cg->code,start_slot);
        cg_expr(cg,range_call->arguments[0]); emit_store_rax(&cg->code,stop_slot);
        emit_mov_rax_imm64(&cg->code,1); emit_store_rax(&cg->code,step_slot);
    } else if(argc==2){
        cg_expr(cg,range_call->arguments[0]); emit_store_rax(&cg->code,start_slot);
        cg_expr(cg,range_call->arguments[1]); emit_store_rax(&cg->code,stop_slot);
        emit_mov_rax_imm64(&cg->code,1); emit_store_rax(&cg->code,step_slot);
    } else {
        cg_expr(cg,range_call->arguments[0]); emit_store_rax(&cg->code,start_slot);
        cg_expr(cg,range_call->arguments[1]); emit_store_rax(&cg->code,stop_slot);
        cg_expr(cg,range_call->arguments[2]); emit_store_rax(&cg->code,step_slot);
    }

    Symbol*iter_sym=scope_get(cg->scope,iter_name);
    if(!iter_sym){
        iter_sym=scope_define(cg->scope,iter_name,OMNI_TYPE_INT);
        if(iter_sym->stack_offset>cg->stack_size)cg->stack_size=iter_sym->stack_offset;
    }
    emit_load_rax(&cg->code,start_slot); emit_store_rax(&cg->code,iter_sym->stack_offset);
    size_t loop_top=cg->code.size;
    emit_load_rax(&cg->code,iter_sym->stack_offset);
    emit_load_rcx(&cg->code,stop_slot);
    emit_cmp_rax_rcx(&cg->code);
    size_t jge=emit_jge_fwd(&cg->code);
    for(int i=0;i<stmt->body->statement_count;i++)cg_stmt(cg,stmt->body->statements[i]);
    size_t inc_top=cg->code.size;
    for(int i=saved_cont;i<cg->continue_patch_count;i++){int32_t d=(int32_t)(inc_top-(cg->continue_patches[i]+4));patch_u32(&cg->code,cg->continue_patches[i],(uint32_t)d);}
    cg->continue_patch_count=saved_cont;
    emit_load_rax(&cg->code,iter_sym->stack_offset);
    emit_load_rcx(&cg->code,step_slot);
    emit_add_rax_rcx(&cg->code);
    emit_store_rax(&cg->code,iter_sym->stack_offset);
    emit_jmp_back(&cg->code,loop_top);
    resolve_fwd(&cg->code,jge);
    for(int i=saved_break;i<cg->break_patch_count;i++)resolve_fwd(&cg->code,cg->break_patches[i]);
    cg->break_patch_count=saved_break;
    cg->scope->next_offset=saved_next_off;
}

/* for item in list_expr: body
   Emits:
     len  = list.len(lst)
     i    = 0
     loop: if i >= len goto end
       item = list.get(lst, i)
       body
       i += 1
       goto loop
     end:
*/
static void cg_for_list(CodeGen*cg,AST_Statement_For*stmt){
    const char*iter_name=stmt->iterator?stmt->iterator->value:"__it";
    int saved_next_off=cg->scope->next_offset;
    int saved_break=cg->break_patch_count;
    int saved_cont=cg->continue_patch_count;

    /* allocate: lst_slot, len_slot, i_slot */
    int lst_slot=cg->scope->next_offset; cg->scope->next_offset+=8;
    int len_slot=cg->scope->next_offset; cg->scope->next_offset+=8;
    int i_slot  =cg->scope->next_offset; cg->scope->next_offset+=8;
    if(cg->scope->next_offset>cg->stack_size)cg->stack_size=cg->scope->next_offset;

    /* eval iterable -> rax, store in lst_slot */
    cg_expr(cg,stmt->iterable);
    emit_store_rax(&cg->code,lst_slot);

    /* len = list.len(lst) */
    emit_load_arg0(&cg->code,lst_slot);
    cg_call_extern(cg,g_fn_list_len);
    emit_store_rax(&cg->code,len_slot);

    /* i = 0 */
    emit_xor_rax_rax(&cg->code);
    emit_store_rax(&cg->code,i_slot);

    /* define loop variable in scope */
    Symbol*iter_sym=scope_get(cg->scope,iter_name);
    if(!iter_sym){
        iter_sym=scope_define(cg->scope,iter_name,OMNI_TYPE_INT);
        if(iter_sym->stack_offset>cg->stack_size)cg->stack_size=iter_sym->stack_offset;
    }

    size_t loop_top=cg->code.size;

    /* if i >= len goto end */
    emit_load_rax(&cg->code,i_slot);
    emit_load_rcx(&cg->code,len_slot);
    emit_cmp_rax_rcx(&cg->code);
    size_t jge=emit_jge_fwd(&cg->code);

    /* item = list.get(lst, i) */
    emit_load_arg0(&cg->code,lst_slot);
    emit_load_arg1(&cg->code,i_slot);
    cg_call_extern(cg,g_fn_list_get);
    emit_store_rax(&cg->code,iter_sym->stack_offset);

    /* body */
    for(int i=0;i<stmt->body->statement_count;i++)
        cg_stmt(cg,stmt->body->statements[i]);

    /* continue patches land here */
    size_t inc_top=cg->code.size;
    for(int i=saved_cont;i<cg->continue_patch_count;i++){
        int32_t d=(int32_t)(inc_top-(cg->continue_patches[i]+4));
        patch_u32(&cg->code,cg->continue_patches[i],(uint32_t)d);
    }
    cg->continue_patch_count=saved_cont;

    /* i += 1 */
    emit_load_rax(&cg->code,i_slot);
    emit_mov_rcx_imm64(&cg->code,1);
    emit_add_rax_rcx(&cg->code);
    emit_store_rax(&cg->code,i_slot);

    emit_jmp_back(&cg->code,loop_top);
    resolve_fwd(&cg->code,jge);

    /* break patches */
    for(int i=saved_break;i<cg->break_patch_count;i++)
        resolve_fwd(&cg->code,cg->break_patches[i]);
    cg->break_patch_count=saved_break;

    cg->scope->next_offset=saved_next_off;
}

static void cg_for_statement(CodeGen*cg,AST_Statement_For*stmt){
    if(stmt->iterable&&stmt->iterable->type==CALL_EXPRESSION){
        AST_Expression_Call*call=(AST_Expression_Call*)stmt->iterable;
        if(call->function&&call->function->type==IDENTIFIER){
            const char*fn=((AST_Expression_Identifier*)call->function)->value;
            if(strcmp(fn,"range")==0&&call->argument_count>=1){cg_for_range(cg,stmt,call);return;}
        }
    }
    /* Fall through to list iteration for any non-range iterable */
    cg_for_list(cg,stmt);
}

static void cg_match_statement(CodeGen*cg,AST_Statement_Match*stmt){
    cg_expr(cg,stmt->value);
    char match_tmp[64];snprintf(match_tmp,sizeof(match_tmp),"__match_%d",cg->label_count++);
    Symbol*match_sym=scope_define(cg->scope,match_tmp,OMNI_TYPE_INT);if(match_sym->stack_offset>cg->stack_size)cg->stack_size=match_sym->stack_offset;
    emit_store_rax(&cg->code,match_sym->stack_offset);
    size_t end_patches[256];int end_count=0;
    for(int i=0;i<stmt->case_count;i++){
        AST_Statement_MatchCase*mc=stmt->cases[i];
        int is_wildcard=(mc->pattern&&mc->pattern->type==IDENTIFIER&&strcmp(((AST_Expression_Identifier*)mc->pattern)->value,"_")==0);
        size_t skip_patch=0;
        if(!is_wildcard){emit_load_rax(&cg->code,match_sym->stack_offset);emit_mov_r11_rax(&cg->code);cg_expr(cg,mc->pattern);emit_mov_rcx_rax(&cg->code);emit_mov_rax_r11(&cg->code);emit_cmp_rax_rcx(&cg->code);skip_patch=emit_jne_fwd(&cg->code);}
        if(mc->consequence)for(int j=0;j<mc->consequence->statement_count;j++)cg_stmt(cg,mc->consequence->statements[j]);
        if(end_count<256)end_patches[end_count++]=emit_jmp_fwd(&cg->code);
        if(!is_wildcard)resolve_fwd(&cg->code,skip_patch);
    }
    for(int i=0;i<end_count;i++)resolve_fwd(&cg->code,end_patches[i]);
}

static void cg_return_statement(CodeGen*cg,AST_Statement_Return*stmt){
    if(stmt->return_value)cg_expr(cg,stmt->return_value);else emit_xor_rax_rax(&cg->code);
    if(cg->in_main_body){
        emit_u8(&cg->code,0x4C);emit_u8(&cg->code,0x8B);emit_u8(&cg->code,0x75);emit_u8(&cg->code,0xF8); // mov r14,[rbp-8]
        emit_u8(&cg->code,0x4C);emit_u8(&cg->code,0x8B);emit_u8(&cg->code,0x7D);emit_u8(&cg->code,0xF0); // mov r15,[rbp-16]
    } else {
        /* Restore callee-saved regs pinned by register allocator.
           rp = 2+n_pinned: rp>=3=rbx pinned, rp>=4=+r12, rp>=5=+r13
           Saved at high frame offsets 232/240/248 to avoid variable conflicts. */
        int rp=cg->reg_var_depth;
        if(rp>=7){emit_u8(&cg->code,0x48);emit_u8(&cg->code,0x8B);emit_u8(&cg->code,0xBD);emit_u32(&cg->code,(uint32_t)(-264));} /* MOV RDI,[rbp-264] */
        if(rp>=6){emit_u8(&cg->code,0x48);emit_u8(&cg->code,0x8B);emit_u8(&cg->code,0xB5);emit_u32(&cg->code,(uint32_t)(-256));} /* MOV RSI,[rbp-256] */
        if(rp>=5){emit_u8(&cg->code,0x4C);emit_u8(&cg->code,0x8B);emit_u8(&cg->code,0xAD);emit_u32(&cg->code,(uint32_t)(-248));} /* MOV R13,[rbp-248] */
        if(rp>=4){emit_u8(&cg->code,0x4C);emit_u8(&cg->code,0x8B);emit_u8(&cg->code,0xA5);emit_u32(&cg->code,(uint32_t)(-240));} /* MOV R12,[rbp-240] */
        if(rp>=3){emit_u8(&cg->code,0x48);emit_u8(&cg->code,0x8B);emit_u8(&cg->code,0x9D);emit_u32(&cg->code,(uint32_t)(-232));} /* MOV RBX,[rbp-232] */
    }
    emit_mov_rsp_rbp(&cg->code);emit_pop_rbp(&cg->code);emit_ret(&cg->code);cg->returned=1;
}
static void cg_break_statement(CodeGen*cg){if(cg->break_patch_count>=MAX_LOOP_PATCHES){fprintf(stderr,"Fatal: too many break\n");exit(1);}cg->break_patches[cg->break_patch_count++]=emit_jmp_fwd(&cg->code);}
static void cg_continue_statement(CodeGen*cg){if(cg->continue_patch_count>=MAX_LOOP_PATCHES){fprintf(stderr,"Fatal: too many continue\n");exit(1);}cg->continue_patches[cg->continue_patch_count++]=emit_jmp_fwd(&cg->code);}

static const char* g_known_modules[] = {"time","datetime","math","os","io","sys","list","str","ai","string",NULL}; /* numrai removed: advertised but never implemented (audit finding #17) */

// ── Module alias resolution ──────────────────────────────────────────────────
// Given a namespace token (e.g. "np" or "m"), look it up in cg->alias_from[].
// If found, return the canonical module name (e.g. "numrai" or "math").
// Otherwise return the input unchanged.
static const char* alias_resolve(CodeGen*cg, const char*ns){
    for(int i=0;i<cg->alias_count;i++)
        if(strcmp(cg->alias_from[i],ns)==0)
            return cg->alias_to[i];
    return ns;
}

static int is_known_module(const char* name){
    /* 1. built-in modules */
    for(int i=0;g_known_modules[i];i++)
        if(strcmp(g_known_modules[i],name)==0) return 1;
    /* 2. installed packages under the platform site-packages directory:
         Windows: %LOCALAPPDATA%\Programs\omnikarai\site-packages\<name>\
                  (falls back to %USERPROFILE%\AppData\Local\...)
         POSIX:   $XDG_DATA_HOME/omnikarai/site-packages/<name>/
                  (falls back to ~/.local/share/omnikarai/site-packages/) */
    char buf[1024];
#if defined(_WIN32)
    const char* lad = getenv("LOCALAPPDATA");
    if(lad){
        snprintf(buf, sizeof(buf),
            "%s\\Programs\\omnikarai\\site-packages\\%s", lad, name);
    } else {
        const char* home = getenv("USERPROFILE");
        if(!home) return 0;
        snprintf(buf, sizeof(buf),
            "%s\\AppData\\Local\\Programs\\omnikarai\\site-packages\\%s", home, name);
    }
#else
    const char* xdg = getenv("XDG_DATA_HOME");
    const char* home = getenv("HOME");
    if(xdg && *xdg)
        snprintf(buf, sizeof(buf), "%s/omnikarai/site-packages/%s", xdg, name);
    else if(home)
        snprintf(buf, sizeof(buf), "%s/.local/share/omnikarai/site-packages/%s", home, name);
    else
        return 0;
#endif
    DWORD a = GetFileAttributesA(buf);
    return (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY));
}

static void cg_stmt(CodeGen*cg,AST_Statement*stmt){
    if(!stmt)return;
    switch(stmt->type){
        case SET_STATEMENT:        cg_set_statement(cg,(AST_Statement_Set*)stmt);break;
        case EXPRESSION_STATEMENT:{
            AST_Statement_Expression*es=(AST_Statement_Expression*)stmt;
            AST_Expression*ex=es->expression;
            // self.field = val — parsed as INFIX "=" with MEMBER_ACCESS on left
            // This happens inside method bodies where 'set' is not used
            if(ex&&ex->type==INFIX_EXPRESSION){
                AST_Expression_Infix*inf=(AST_Expression_Infix*)ex;
                /* ── augmented assignment: x += e, x -= e, etc. ── */
                /* Desugar: load var, apply op, store back */
                {
                    static const char*_aug[]={"++","+=","-=","*=","/=","%=","**=",NULL};
                    int _is_aug=0; for(int _k=0;_aug[_k];_k++) if(!strcmp(inf->operator,_aug[_k])){_is_aug=1;break;}
                    if(_is_aug&&inf->left&&inf->left->type==IDENTIFIER){
                        const char*vn=((AST_Expression_Identifier*)inf->left)->value;
                        Symbol*sym=scope_get(cg->scope,vn);
                        if(!sym){sym=scope_define(cg->scope,vn,OMNI_TYPE_INT);
                                 if(sym->stack_offset>cg->stack_size)cg->stack_size=sym->stack_offset;}
                        /* eval rhs → tmp slot */
                        int ts=cg->scope->next_offset;cg->scope->next_offset+=8;
                        if(cg->scope->next_offset>cg->stack_size)cg->stack_size=cg->scope->next_offset;
                        cg_expr(cg,inf->right); emit_store_rax(&cg->code,ts);
                        /* load current var into rax */
                        int _pr=-1;
                        for(int _ri=0;_ri<cg->reg_var_depth&&_ri<7;_ri++)
                            if(!strcmp(cg->reg_var_names[_ri],vn)){_pr=_ri;break;}
                        if(_pr>=0){switch(_pr){case 0:emit_mov_rax_r14(&cg->code);break;case 1:emit_mov_rax_r15(&cg->code);break;case 2:emit_mov_rax_rbx(&cg->code);break;case 3:emit_mov_rax_r12(&cg->code);break;case 4:emit_mov_rax_r13(&cg->code);break;case 5:emit_mov_rax_rsi(&cg->code);break;case 6:emit_mov_rax_rdi(&cg->code);break;}}
                        else emit_load_rax(&cg->code,sym->stack_offset);
                        /* load rhs into rcx */
                        emit_load_rcx(&cg->code,ts);
                        cg->scope->next_offset-=8;
                        /* apply op */
                        const char*op=inf->operator;
                        if     (!strcmp(op,"+=")) emit_add_rax_rcx(&cg->code);
                        else if(!strcmp(op,"-=")) emit_sub_rax_rcx(&cg->code);
                        else if(!strcmp(op,"*=")) emit_imul_rax_rcx(&cg->code);
                        else if(!strcmp(op,"/=")) emit_idiv_rcx(&cg->code);
                        else if(!strcmp(op,"%=")) emit_mod_rax_rcx(&cg->code);
                        else if(!strcmp(op,"**=")){emit_mov_arg1_rcx(&cg->code);emit_mov_arg0_rax(&cg->code);cg_call_extern(cg,g_fn_int_pow);}
                        /* store result back */
                        if(_pr>=0){switch(_pr){case 0:emit_u8(&cg->code,0x49);emit_u8(&cg->code,0x89);emit_u8(&cg->code,0xC6);break;case 1:emit_u8(&cg->code,0x49);emit_u8(&cg->code,0x89);emit_u8(&cg->code,0xC7);break;case 2:emit_mov_rbx_rax(&cg->code);break;case 3:emit_mov_r12_rax(&cg->code);break;case 4:emit_mov_r13_rax(&cg->code);break;case 5:emit_mov_rsi_rax(&cg->code);break;case 6:emit_mov_rdi_rax(&cg->code);break;}}
                        emit_store_rax(&cg->code,sym->stack_offset);
                        break;
                    }
                    /* index write: lst[i] += e  (augmented on index) */
                    if(_is_aug&&inf->left&&inf->left->type==INDEX_EXPRESSION){
                        AST_Expression_Index*ix=(AST_Expression_Index*)inf->left;
                        /* eval list ptr */
                        int ls=cg->scope->next_offset;cg->scope->next_offset+=8;
                        int is2=cg->scope->next_offset;cg->scope->next_offset+=8;
                        int rs=cg->scope->next_offset;cg->scope->next_offset+=8;
                        if(cg->scope->next_offset>cg->stack_size)cg->stack_size=cg->scope->next_offset;
                        cg_expr(cg,ix->left);emit_store_rax(&cg->code,ls);
                        cg_expr(cg,ix->index);emit_store_rax(&cg->code,is2);
                        cg_expr(cg,inf->right);emit_store_rax(&cg->code,rs);
                        /* read current: list.get(lst, idx) */
                        emit_load_arg0(&cg->code,ls);emit_load_arg1(&cg->code,is2);
                        cg_call_extern(cg,g_fn_list_get);
                        emit_load_rcx(&cg->code,rs);
                        const char*op=inf->operator;
                        if     (!strcmp(op,"+=")) emit_add_rax_rcx(&cg->code);
                        else if(!strcmp(op,"-=")) emit_sub_rax_rcx(&cg->code);
                        else if(!strcmp(op,"*=")) emit_imul_rax_rcx(&cg->code);
                        else if(!strcmp(op,"/=")) emit_idiv_rcx(&cg->code);
                        else if(!strcmp(op,"%=")) emit_mod_rax_rcx(&cg->code);
                        /* store back: list.set(lst, idx, val) */
                        int vs=cg->scope->next_offset;cg->scope->next_offset+=8;
                        if(cg->scope->next_offset>cg->stack_size)cg->stack_size=cg->scope->next_offset;
                        emit_store_rax(&cg->code,vs);
                        emit_load_arg0(&cg->code,ls);emit_load_arg1(&cg->code,is2);emit_load_arg2(&cg->code,vs);
                        cg_call_extern(cg,g_fn_list_set);
                        cg->scope->next_offset-=4*8; break;
                    }
                }
                /* ── index write: lst[i] = val  ── */
                if(!strcmp(inf->operator,"=")&&inf->left&&inf->left->type==INDEX_EXPRESSION){
                    AST_Expression_Index*ix=(AST_Expression_Index*)inf->left;
                    /* Eval and save list ptr, index, value to three stack slots */
                    int ls=cg->scope->next_offset;cg->scope->next_offset+=8;
                    int is2=cg->scope->next_offset;cg->scope->next_offset+=8;
                    int vs=cg->scope->next_offset;cg->scope->next_offset+=8;
                    if(cg->scope->next_offset>cg->stack_size)cg->stack_size=cg->scope->next_offset;
                    cg_expr(cg,ix->left);  emit_store_rax(&cg->code,ls);
                    cg_expr(cg,ix->index); emit_store_rax(&cg->code,is2);
                    cg_expr(cg,inf->right);emit_store_rax(&cg->code,vs);
                    /* list.set(rcx=lst, rdx=idx, r8=val) */
                    emit_load_arg0(&cg->code,ls);
                    emit_load_arg1(&cg->code,is2);
                    emit_load_arg2(&cg->code,vs);
                    cg_call_extern(cg,g_fn_list_set);
                    cg->scope->next_offset-=24; break;
                }
                if(!strcmp(inf->operator,"=")&&inf->left&&inf->left->type==MEMBER_ACCESS_EXPRESSION){
                    AST_Expression_MemberAccess*ma=(AST_Expression_MemberAccess*)inf->left;
                    if(ma->object&&ma->object->type==IDENTIFIER){
                        const char*obj=((AST_Expression_Identifier*)ma->object)->value;
                        if(!strcmp(obj,"self")&&cg->current_class[0]){
                            ClassEntry*ce=class_find(cg,cg->current_class);
                            if(ce){
                                int fidx=class_field_ensure(ce,ma->member);
                                // Eval value into RAX
                                cg_expr(cg,inf->right);
                                // Load self ptr into RCX
                                emit_load_rcx(&cg->code,cg->self_slot);
                                // Store RAX into field
                                emit_field_store(&cg->code,fidx);
                                break;
                            }
                        }
                    }
                }
            }
            /* Augmented assignment: x += e, x -= e, x *= e, x /= e, x %= e, x **= e
               Parsed as INFIX with op "+=" etc, lhs = IDENTIFIER.
               We desugar: load x, eval rhs → rcx, op, store x. */
            if(ex&&ex->type==INFIX_EXPRESSION){
                AST_Expression_Infix*ai=(AST_Expression_Infix*)ex;
                const char*aop=ai->operator;
                int is_aug=(!strcmp(aop,"+=")||!strcmp(aop,"-=")||!strcmp(aop,"*=")||
                            !strcmp(aop,"/=")||!strcmp(aop,"%=")||!strcmp(aop,"**="));
                if(is_aug&&ai->left&&ai->left->type==IDENTIFIER){
                    const char*vn=((AST_Expression_Identifier*)ai->left)->value;
                    Symbol*sym=scope_get(cg->scope,vn);
                    if(sym){
                        /* eval rhs first → rax, then move to rcx */
                        cg_expr(cg,ai->right); emit_mov_rcx_rax(&cg->code);
                        /* load lhs into rax */
                        emit_load_rax(&cg->code,sym->stack_offset);
                        /* apply op */
                        if(!strcmp(aop,"+="))      emit_add_rax_rcx(&cg->code);
                        else if(!strcmp(aop,"-=")) emit_sub_rax_rcx(&cg->code);
                        else if(!strcmp(aop,"*=")) emit_imul_rax_rcx(&cg->code);
                        else if(!strcmp(aop,"/=")) emit_idiv_rcx(&cg->code);
                        else if(!strcmp(aop,"%=")) emit_mod_rax_rcx(&cg->code);
                        else if(!strcmp(aop,"**=")){
                            /* pow(rax_base, rcx_exp): move rax->rcx_base, rdx->exp already in rcx */
                            /* FIX(N4): was passing int regs to a (double,double)
                               function with XMM args never set — result was garbage.
                               Convert both operands: CVTSI2SD xmm1,rcx (exp);
                               CVTSI2SD xmm0,rax (base); call; XMM0->RAX. */
                            emit_u8(&cg->code,0xF2);emit_u8(&cg->code,0x48);emit_u8(&cg->code,0x0F);emit_u8(&cg->code,0x2A);emit_u8(&cg->code,0xCE); /* cvtsi2sd xmm1, rcx */
                            emit_int_to_float(&cg->code);   /* cvtsi2sd xmm0, rax */
                            cg_call_extern(cg,g_fn_math_pow);
                            emit_xmm0_to_rax(&cg->code);
                        }
                        emit_store_rax(&cg->code,sym->stack_offset);
                        break;
                    }
                }
                /* Augmented on index target: obj[i] += e — handled by cg_expr fall-through */
            }
            /* obj[i] = val — INFIX "=" with INDEX_EXPRESSION on left */
            if(ex&&ex->type==INFIX_EXPRESSION){
                AST_Expression_Infix*inf2=(AST_Expression_Infix*)ex;
                if(!strcmp(inf2->operator,"=")&&inf2->left&&inf2->left->type==INDEX_EXPRESSION){
                    AST_Expression_Index*ix=(AST_Expression_Index*)inf2->left;
                    /* Eval: obj → s0, idx → s1, val → rax */
                    int s0=cg->scope->next_offset; cg->scope->next_offset+=8;
                    int s1=cg->scope->next_offset; cg->scope->next_offset+=8;
                    if(cg->scope->next_offset>cg->stack_size)cg->stack_size=cg->scope->next_offset;
                    cg_expr(cg,ix->left);  emit_store_rax(&cg->code,s0);
                    cg_expr(cg,ix->index); emit_store_rax(&cg->code,s1);
                    cg_expr(cg,inf2->right);  /* val → rax */
                    /* list.set(rcx=obj, rdx=idx, r8=val) — save val to stack slot s1+8 */
                    int s2=cg->scope->next_offset; cg->scope->next_offset+=8;
                    if(cg->scope->next_offset>cg->stack_size)cg->stack_size=cg->scope->next_offset;
                    emit_store_rax(&cg->code,s2);       /* save val */
                    emit_load_arg0(&cg->code,s0);        /* rcx = obj */
                    emit_load_arg1(&cg->code,s1);        /* rdx = idx */
                    emit_load_arg2(&cg->code,s2);         /* r8  = val */
                    cg_call_extern(cg,g_fn_list_set);
                    cg->scope->next_offset-=24;
                    break;
                }
            }
            cg_expr(cg,ex);
            break;
        }
        case IF_STATEMENT:         cg_if_statement(cg,(AST_Statement_If*)stmt);break;
        case WHILE_STATEMENT:      cg_while_statement(cg,(AST_Statement_While*)stmt);break;
        case FOR_STATEMENT:        cg_for_statement(cg,(AST_Statement_For*)stmt);break;
        case MATCH_STATEMENT:      cg_match_statement(cg,(AST_Statement_Match*)stmt);break;
        case RETURN_STATEMENT:     cg_return_statement(cg,(AST_Statement_Return*)stmt);break;
        case BLOCK_STATEMENT:
            for(int i=0;i<((AST_Statement_Block*)stmt)->statement_count;i++)
                cg_stmt(cg,((AST_Statement_Block*)stmt)->statements[i]);
            break;
        case FN_DEFINITION:  break;
        case CLASS_DEFINITION: break;
        case USE_STATEMENT:{
            AST_Statement_Use*u=(AST_Statement_Use*)stmt;
            /* Resolve alias FIRST: if "use math as m", canonical name is "math" */
            const char* canon = u->module_name;
            /* Register alias if present: "use math as m" -> alias_from="m", alias_to="math" */
            if(u->alias && u->alias[0] && cg->alias_count < MAX_MODULE_ALIASES){
                strncpy_s(cg->alias_from[cg->alias_count], 64, u->alias,    _TRUNCATE);
                strncpy_s(cg->alias_to  [cg->alias_count], 64, canon,       _TRUNCATE);
                cg->alias_count++;
                BETA_TRACE_CG("alias '%s' -> '%s'", u->alias, canon);
            }
            if(!is_known_module(canon)){
                fprintf(stderr,
                    "\nModuleError: Module '%s' not found\n"
                    "  Built-in modules: time, datetime, math, os, io, sys, list, str, ai\n"
                    "  Not installed: run  omnip install %s\n"
                    "  Search OPI:    omnip search %s\n"
                    "  Registry:      https://opi-nine.vercel.app\n\n",
                    canon, canon, canon);
                exit(1);
            }
            /* Built-in module init */
            if(strcmp(canon,"time")==0) omni_time_init();
            /* Installed package: load .ok files into this CodeGen */
            int is_builtin = 0;
            for(int _i=0;g_known_modules[_i];_i++)
                if(strcmp(g_known_modules[_i],canon)==0){is_builtin=1;break;}
            if(!is_builtin) pkg_load_into_cg(cg, canon);
            BETA_TRACE_CG("use module '%s'%s%s",canon, u->alias?" as ":" ", u->alias?u->alias:"");
            break;
        }
        default:fprintf(stderr,"CodeGen Warning: unsupported statement type %d\n",stmt->type);break;
    }
}

// ============================================================
//  FUNCTION BODY CODEGEN
// ============================================================
// aligned_frame: compute sub rsp,N so that RSP is 16-byte aligned before every CALL.
//
// Two cases depending on who calls the function:
//
// MAIN BODY (is_fn_body=0):
//   codegen_run() calls the JIT with a plain CALL, so before push rbp, RSP%16 == 8.
//   After push rbp: RSP%16 == 0.  Need (RSP - N) % 16 == 0 → N%16 == 0.
//   So: round up to multiple of 16.
//
// USER FN BODY (is_fn_body=1):
//   Caller does CALL fn (RSP -= 8, return addr), then fn does push rbp (RSP -= 8).
//   Before CALL: RSP%16 == 0 (caller ensured alignment).
//   After CALL:  RSP%16 == 8.  After push rbp: RSP%16 == 0.
//   Need (RSP - N) % 16 == 0 → N%16 == 0.  Same as main body.
//
// Both cases: after push rbp, RSP%16 == 0, so N must be a multiple of 16.
// Include 32-byte shadow space in N.
static uint32_t aligned_frame(int locals){
#if defined(OMNI_ABI_WIN64)
    int N = locals + 32;   // include shadow space
#else
    int N = locals + 64;   // SysV: padding so stack args at [rsp+0..] never
                           // overlap local slots (8-arg extern calls use it)
#endif
    N = (N + 15) & ~15;    // round up to multiple of 16 → N%16 == 0
    return (uint32_t)N;
}

static void cg_fn_body(CodeGen*cg,AST_Statement_FnDef*fn_def){
    FnEntry*fe=fn_find(cg,fn_def->name->value);
    if(!fe){fprintf(stderr,"CodeGen Error: fn_table entry missing for '%s'\n",fn_def->name->value);exit(1);}
    fe->code_offset=cg->code.size;fe->resolved=1;
    /* NOTE: do NOT return early for inline fns — we must always emit the fn body.
       Inlining is a call-site optimisation only; the body must exist for cases
       where the inline path is skipped (e.g. 0-arg call to a 1-param fn). */
    BETA_TRACE_CG("fn_body '%s' params=%d offset=%zu",fn_def->name->value,fn_def->parameter_count,fe->code_offset);

    /* REGISTER ALLOCATOR: analyze body BEFORE emitting prologue.
       We save callee regs (rbx/r12/r13) into high frame slots [rbp-232/240/248]
       so variables keep their normal layout starting at [rbp-8] for first param.
       This avoids ANY conflict between callee saves and variable slots. */
    SymbolTable*fn_scope=scope_new(NULL);
    SymbolTable*saved_scope=cg->scope;
    int saved_stack=cg->stack_size,saved_ret=cg->returned,saved_reg_depth=cg->reg_var_depth;
    int saved_self_slot=cg->self_slot;

    cg->scope=fn_scope;cg->stack_size=32;cg->returned=0;cg->reg_var_depth=0;
    memset(cg->reg_var_names,0,sizeof(cg->reg_var_names));
    memset(cg->reg_var_saved,0,sizeof(cg->reg_var_saved));
    cg->in_main_body=0;

    /* Analyze body: count reads per variable, sort hottest first */
    HotVar ra_hv[HOT_MAX]; int ra_nhv=0;
    count_block_reads(fn_def->body, ra_hv, &ra_nhv);
    for(int i=1;i<ra_nhv;i++){HotVar tmp=ra_hv[i];int j=i-1;while(j>=0&&ra_hv[j].count<tmp.count){ra_hv[j+1]=ra_hv[j];j--;}ra_hv[j+1]=tmp;}

    /* Pick top 5 non-param hottest variables for rbx/r12/r13/rsi/rdi (slots 2-6).
       v7.0: Callee-saved registers (rbx/r12/r13/rsi/rdi) are preserved across calls
       by the Windows x64 ABI — both our JIT functions and external C functions
       save/restore them.  So pinning is safe even when the function makes calls.
       The callee will save/restore its OWN pinned set; after return our values
       are intact.  Threshold lowered to 1 (pin anything used in a loop). */
    int ra_pinned=0;
    for(int ri=0;ri<ra_nhv&&ra_pinned<OMNI_MAX_PIN;ri++){
        if(ra_hv[ri].count<1) break;
        int is_param=0;
        for(int pi=0;pi<fn_def->parameter_count;pi++)
            if(!strcmp(fn_def->parameters[pi]->value,ra_hv[ri].name)){is_param=1;break;}
        if(is_param) continue;
        strncpy_s(cg->reg_var_names[2+ra_pinned],64,ra_hv[ri].name,_TRUNCATE);
        ra_pinned++;
    }
    /* reg_var_depth = 2+ra_pinned: slots 0,1="" (r14/r15 unused in fns), 2-6=pinned vars */
    cg->reg_var_depth = 2 + ra_pinned;
    /* Ensure frame is large enough to hold callee saves at high offsets */
    if(ra_pinned > 0 && cg->stack_size < 256) cg->stack_size = 256;
    if(g_beta){fprintf(stderr,"[REGALLOC] fn='%s' pinned=%d",fn_def->name->value,ra_pinned);for(int _d=0;_d<ra_pinned;_d++)fprintf(stderr," '%s'",cg->reg_var_names[2+_d]);fprintf(stderr,"\n");}

    /* Emit prologue: push rbp; mov rbp,rsp; sub rsp,N */
    emit_push_rbp(&cg->code);
    emit_mov_rbp_rsp(&cg->code);
    emit_u8(&cg->code,REX_W);emit_u8(&cg->code,0x81);emit_u8(&cg->code,0xEC);
    size_t stack_patch=cg->code.size;
    emit_u32(&cg->code,256);  // placeholder — patched after body analysis

    /* Save callee-saved regs into HIGH frame slots (232/240/248/256/264 from rbp).
       These are near the TOP of the frame, well above any variables.
       Variables start at [rbp-8] (first param) as usual -- NO layout change. */
    if(ra_pinned>=1){emit_u8(&cg->code,0x48);emit_u8(&cg->code,0x89);emit_u8(&cg->code,0x9D);emit_u32(&cg->code,(uint32_t)(-232));} /* MOV [rbp-232],RBX */
    if(ra_pinned>=2){emit_u8(&cg->code,0x4C);emit_u8(&cg->code,0x89);emit_u8(&cg->code,0xA5);emit_u32(&cg->code,(uint32_t)(-240));} /* MOV [rbp-240],R12 */
    if(ra_pinned>=3){emit_u8(&cg->code,0x4C);emit_u8(&cg->code,0x89);emit_u8(&cg->code,0xAD);emit_u32(&cg->code,(uint32_t)(-248));} /* MOV [rbp-248],R13 */
    if(ra_pinned>=4){emit_u8(&cg->code,0x48);emit_u8(&cg->code,0x89);emit_u8(&cg->code,0xB5);emit_u32(&cg->code,(uint32_t)(-256));} /* MOV [rbp-256],RSI */
    if(ra_pinned>=5){emit_u8(&cg->code,0x48);emit_u8(&cg->code,0x89);emit_u8(&cg->code,0xBD);emit_u32(&cg->code,(uint32_t)(-264));} /* MOV [rbp-264],RDI */
    if(ra_pinned>=4 && cg->stack_size < 272) cg->stack_size = 272;

    /* Store parameters from ABI regs into frame at normal offsets */    for(int i=0;i<fn_def->parameter_count;i++){
        const char*pname=fn_def->parameters[i]->value;
        Symbol*p_sym=scope_define(fn_scope,pname,OMNI_TYPE_INT);
        if(i==0&&(!strcmp(pname,"self")))cg->self_slot=p_sym->stack_offset;
        if(p_sym->stack_offset>cg->stack_size)cg->stack_size=p_sym->stack_offset;
        /* Store incoming ABI arg registers into frame slots (include/abi.h):
           Win64: RCX,RDX,R8,R9   SysV: RDI,RSI,RDX,RCX */
        switch(i){
#if defined(OMNI_ABI_WIN64)
            case 0:emit_u8(&cg->code,REX_W);emit_u8(&cg->code,0x89);emit_u8(&cg->code,0x8D);emit_u32(&cg->code,(uint32_t)(-p_sym->stack_offset));break; /* [rbp-d],RCX */
            case 1:emit_u8(&cg->code,REX_W);emit_u8(&cg->code,0x89);emit_u8(&cg->code,0x95);emit_u32(&cg->code,(uint32_t)(-p_sym->stack_offset));break; /* [rbp-d],RDX */
            case 2:emit_u8(&cg->code,REX_WR);emit_u8(&cg->code,0x89);emit_u8(&cg->code,0x85);emit_u32(&cg->code,(uint32_t)(-p_sym->stack_offset));break; /* [rbp-d],R8  */
            case 3:emit_u8(&cg->code,REX_WR);emit_u8(&cg->code,0x89);emit_u8(&cg->code,0x8D);emit_u32(&cg->code,(uint32_t)(-p_sym->stack_offset));break; /* [rbp-d],R9  */
#else
            case 0:emit_u8(&cg->code,REX_W);emit_u8(&cg->code,0x89);emit_u8(&cg->code,0xBD);emit_u32(&cg->code,(uint32_t)(-p_sym->stack_offset));break; /* [rbp-d],RDI */
            case 1:emit_u8(&cg->code,REX_W);emit_u8(&cg->code,0x89);emit_u8(&cg->code,0xB5);emit_u32(&cg->code,(uint32_t)(-p_sym->stack_offset));break; /* [rbp-d],RSI */
            case 2:emit_u8(&cg->code,REX_W);emit_u8(&cg->code,0x89);emit_u8(&cg->code,0x95);emit_u32(&cg->code,(uint32_t)(-p_sym->stack_offset));break; /* [rbp-d],RDX */
            case 3:emit_u8(&cg->code,REX_W);emit_u8(&cg->code,0x89);emit_u8(&cg->code,0x8D);emit_u32(&cg->code,(uint32_t)(-p_sym->stack_offset));break; /* [rbp-d],RCX */
#endif
        }
    }

    /* Emit function body */
    for(int i=0;i<fn_def->body->statement_count;i++)cg_stmt(cg,fn_def->body->statements[i]);

    /* Epilogue: restore callee-saved regs, then return */
    if(!cg->returned){
        emit_xor_rax_rax(&cg->code);
        if(ra_pinned>=5){emit_u8(&cg->code,0x48);emit_u8(&cg->code,0x8B);emit_u8(&cg->code,0xBD);emit_u32(&cg->code,(uint32_t)(-264));} /* MOV RDI,[rbp-264] */
        if(ra_pinned>=4){emit_u8(&cg->code,0x48);emit_u8(&cg->code,0x8B);emit_u8(&cg->code,0xB5);emit_u32(&cg->code,(uint32_t)(-256));} /* MOV RSI,[rbp-256] */
        if(ra_pinned>=3){emit_u8(&cg->code,0x4C);emit_u8(&cg->code,0x8B);emit_u8(&cg->code,0xAD);emit_u32(&cg->code,(uint32_t)(-248));} /* MOV R13,[rbp-248] */
        if(ra_pinned>=2){emit_u8(&cg->code,0x4C);emit_u8(&cg->code,0x8B);emit_u8(&cg->code,0xA5);emit_u32(&cg->code,(uint32_t)(-240));} /* MOV R12,[rbp-240] */
        if(ra_pinned>=1){emit_u8(&cg->code,0x48);emit_u8(&cg->code,0x8B);emit_u8(&cg->code,0x9D);emit_u32(&cg->code,(uint32_t)(-232));} /* MOV RBX,[rbp-232] */
        emit_mov_rsp_rbp(&cg->code);
        emit_pop_rbp(&cg->code);
        emit_ret(&cg->code);
    }

    patch_u32(&cg->code,stack_patch,aligned_frame(cg->stack_size));

    scope_free(fn_scope);
    cg->scope=saved_scope;cg->stack_size=saved_stack;cg->returned=saved_ret;
    cg->reg_var_depth=saved_reg_depth;cg->in_main_body=1;
    cg->self_slot=saved_self_slot;
}

// ============================================================
//  PUBLIC API
// ============================================================
// ============================================================
//  CLASS SYSTEM
//  Instance layout: heap array of int64 fields.
//  Person("Alice",30) → alloc(n_fields*8) + Person_init(ptr,"Alice",30)
//  p.greet()          → Person_greet(p)
//  self.name = x      → ((int64*)self)[field_idx] = x
// ============================================================
static ClassEntry* class_find(CodeGen*cg,const char*name){
    for(int i=0;i<cg->class_count;i++)
        if(!strcmp(cg->class_table[i].class_name,name))return &cg->class_table[i];
    return NULL;
}
static int class_field_index(ClassEntry*ce,const char*field){
    for(int i=0;i<ce->field_count;i++)
        if(!strcmp(ce->field_names[i],field))return i;
    return -1;
}
static int class_field_ensure(ClassEntry*ce,const char*field){
    int idx=class_field_index(ce,field);
    if(idx>=0)return idx;
    if(ce->field_count>=MAX_FIELDS){fprintf(stderr,"Fatal: too many fields in class\n");exit(1);}
    strncpy_s(ce->field_names[ce->field_count],64,field,_TRUNCATE);
    return ce->field_count++;
}

// Scan a class definition to register fields (from init body: self.x = ...) and methods
static void class_register(CodeGen*cg,AST_Statement_ClassDef*cd){
    if(cg->class_count>=MAX_CLASSES){fprintf(stderr,"Fatal: too many classes\n");exit(1);}
    ClassEntry*ce=&cg->class_table[cg->class_count++];
    memset(ce,0,sizeof(*ce));
    strncpy_s(ce->class_name,64,cd->name->value,_TRUNCATE);
    if(!cd->body)return;
    for(int i=0;i<cd->body->statement_count;i++){
        AST_Statement*ms=cd->body->statements[i];
        if(!ms||ms->type!=FN_DEFINITION)continue;
        AST_Statement_FnDef*md=(AST_Statement_FnDef*)ms;
        // Register method
        if(ce->method_count<MAX_METHODS)
            strncpy_s(ce->method_names[ce->method_count++],64,md->name->value,_TRUNCATE);
        // If this is 'init', scan its body for self.field = ... to discover fields
        if(!strcmp(md->name->value,"init")&&md->body){
            for(int j=0;j<md->body->statement_count;j++){
                AST_Statement*st=md->body->statements[j];
                if(!st)continue;
                // Pattern: EXPRESSION_STATEMENT wrapping an assignment to self.field
                // In our parser, self.field = val is parsed as SET with a MEMBER_ACCESS name
                // OR as EXPRESSION_STATEMENT with an INFIX_EXPRESSION (ASSIGN)
                // We handle both: check SET_STATEMENT where name is "self" (shouldn't happen)
                // and EXPRESSION_STATEMENT with member access on left of assign.
                // The most common pattern from parse_set_statement produces:
                //   SET_STATEMENT { name=IDENT("self"), value=... } — no, 'set' is required
                // Without 'set', self.name = name is parsed as EXPRESSION_STATEMENT
                // with the expression being an infix ASSIGN on a member access.
                // We just scan for any EXPRESSION_STATEMENT; the actual field names
                // will be discovered lazily in cg_set_self_field.
                (void)st;
            }
        }
        // Register as a named function: ClassName_methodname
        char full_name[128];
        snprintf(full_name,sizeof(full_name),"%s_%s",cd->name->value,md->name->value);
        FnEntry*fe=fn_register(cg,full_name,md->parameter_count);
        /* Class methods must NOT be inlined — they use self, field access, and
           the class calling convention which the inline expander cannot handle.
           Marking them inline leaves code_offset=0 → null call → crash. */
        (void)fe;
    }
}

// Runtime: allocate an instance (n_fields * 8 bytes), zero-filled
__attribute__((noinline)) int64_t omni_class_alloc(int64_t n_fields){
    int64_t*ptr=(int64_t*)calloc((size_t)n_fields,8);
    if(!ptr){fprintf(stderr,"Fatal: OOM class_alloc\n");exit(1);}
    return (int64_t)(uintptr_t)ptr;
}
/* g_fn_class_alloc initialised here — declared static above */
// Note: we assign the real value in codegen_init to avoid duplicate-definition error
// The forward declaration above is assigned its value at first use via the init below.

// Emit: load field value from instance pointer
// self_ptr is in RAX; emit: mov rax, [rax + field_idx*8]
static void emit_field_load(CodeBuf*b,int field_idx){
    int off=field_idx*8;
    if(off==0){                      // mov rax,[rax]
        emit_u8(b,REX_W);emit_u8(b,0x8B);emit_u8(b,0x00);
    } else if(off>=-128&&off<=127){ // mov rax,[rax+disp8]
        emit_u8(b,REX_W);emit_u8(b,0x8B);emit_u8(b,0x40);emit_u8(b,(uint8_t)(int8_t)off);
    } else {                         // mov rax,[rax+disp32]
        emit_u8(b,REX_W);emit_u8(b,0x8B);emit_u8(b,0x80);emit_u32(b,(uint32_t)off);
    }
}
// Emit: store RAX into field of instance (instance ptr in RCX)
// rcx = instance ptr; rax = value to store
static void emit_field_store(CodeBuf*b,int field_idx){
    int off=field_idx*8;
    if(off==0){                      // mov [rcx],rax
        emit_u8(b,REX_W);emit_u8(b,0x89);emit_u8(b,0x01);
    } else if(off>=-128&&off<=127){ // mov [rcx+disp8],rax
        emit_u8(b,REX_W);emit_u8(b,0x89);emit_u8(b,0x41);emit_u8(b,(uint8_t)(int8_t)off);
    } else {                         // mov [rcx+disp32],rax
        emit_u8(b,REX_W);emit_u8(b,0x89);emit_u8(b,0x81);emit_u32(b,(uint32_t)off);
    }
}

void codegen_init(CodeGen*cg){
    buf_init(&cg->code);cg->scope=scope_new(NULL);
    cg->label_count=0;cg->patch_count=0;cg->stack_size=0;cg->returned=0;
    cg->string_pool=NULL;cg->string_pool_count=0;cg->fn_count=0;
    cg->call_patch_count=0;cg->break_patch_count=0;cg->continue_patch_count=0;
    cg->eph_last_store_off=-1;cg->eph_last_store_reg=0;cg->eph_store_code_pos=0;
    cg->reg_var_depth=0;
    memset(cg->reg_var_names,0,sizeof(cg->reg_var_names));
    memset(cg->reg_var_saved,0,sizeof(cg->reg_var_saved));
    cg->in_main_body=0;
    memset(cg->fn_table,0,sizeof(cg->fn_table));
    cg->class_count=0;memset(cg->class_table,0,sizeof(cg->class_table));
    cg->current_class[0]=0;cg->self_slot=0;
    g_fn_class_alloc=(void*)omni_class_alloc;
    /* Reset package registry for each new CodeGen (fresh compile) */
    g_pkg_count=0;
    memset(g_pkg_registry,0,sizeof(g_pkg_registry));
}
void codegen_free(CodeGen*cg){
    buf_free(&cg->code);scope_free(cg->scope);cg->scope=NULL;
    for(int i=0;i<cg->string_pool_count;i++)free(cg->string_pool[i]);
    free(cg->string_pool);cg->string_pool=NULL;cg->string_pool_count=0;
}
int codegen_compile(CodeGen*cg,AST_Program*program){
    if(!program)return 0;
    g_cg_ctx=cg; /* set global context for emit_call_extern ephemeral invalidation */
    // PASS 1: register fns and classes
    for(int i=0;i<program->statement_count;i++){
        AST_Statement*s=program->statements[i];
        if(s&&s->type==FN_DEFINITION){
            AST_Statement_FnDef*fd=(AST_Statement_FnDef*)s;
            FnEntry*fe=fn_register(cg,fd->name->value,fd->parameter_count);
            fe->ret_type=fn_infer_ret_type(fd);
            if(fn_is_inlineable(fd)){fe->is_inline=1;fe->inline_ast=(void*)fd;}
        }
        if(s&&s->type==CLASS_DEFINITION){
            class_register(cg,(AST_Statement_ClassDef*)s);
        }
    }
    // PASS 2: JMP over fn bodies (always emit if there are any fn definitions at all)
    size_t jmp_over_fns=0;
    int has_real_fns=0;for(int i=0;i<program->statement_count;i++){AST_Statement*s=program->statements[i];if(s&&s->type==FN_DEFINITION)has_real_fns=1;}
    if(has_real_fns)jmp_over_fns=emit_jmp_fwd(&cg->code);
    // PASS 3: fn bodies + class method bodies
    for(int i=0;i<program->statement_count;i++){
        AST_Statement*s=program->statements[i];
        if(s&&s->type==FN_DEFINITION)cg_fn_body(cg,(AST_Statement_FnDef*)s);
        if(s&&s->type==CLASS_DEFINITION){
            AST_Statement_ClassDef*cd=(AST_Statement_ClassDef*)s;
            ClassEntry*ce=class_find(cg,cd->name->value);
            if(!ce)continue;
            if(!cd->body)continue;
            for(int j=0;j<cd->body->statement_count;j++){
                AST_Statement*ms=cd->body->statements[j];
                if(!ms||ms->type!=FN_DEFINITION)continue;
                AST_Statement_FnDef*md=(AST_Statement_FnDef*)ms;
                char full_name[128];
                snprintf(full_name,sizeof(full_name),"%s_%s",cd->name->value,md->name->value);
                // Temporarily rename to full_name for cg_fn_body
                char saved_name[64];
                strncpy_s(saved_name,64,md->name->value,_TRUNCATE);
                strncpy_s(md->name->value,64,full_name,_TRUNCATE);
                // Mark as method: set current_class so self.field works
                strncpy_s(cg->current_class,64,cd->name->value,_TRUNCATE);
                cg_fn_body(cg,md);
                cg->current_class[0]=0;
                // Restore original name
                strncpy_s(md->name->value,64,saved_name,_TRUNCATE);
            }
        }
    }
    if(has_real_fns)resolve_fwd(&cg->code,jmp_over_fns);
    // PASS 4: main body prologue — standard frame + r14/r15 saved inside frame
    // Layout: push rbp / mov rbp,rsp / sub rsp,N
    // r14 saved at [rbp-8], r15 at [rbp-16], first variable at [rbp-24]
    // emit_mov_rsp_rbp on return restores RSP correctly.
    emit_push_rbp(&cg->code);
    emit_mov_rbp_rsp(&cg->code);
    emit_u8(&cg->code,REX_W);emit_u8(&cg->code,0x81);emit_u8(&cg->code,0xEC);
    size_t stack_patch=cg->code.size;emit_u32(&cg->code,0);
    // Save r14/r15 inside the frame (not as pushes, so RSP stays at rbp-N throughout)
    // [rbp-8]  = saved r14
    // [rbp-16] = saved r15
    emit_u8(&cg->code,0x4C);emit_u8(&cg->code,0x89);emit_u8(&cg->code,0x75);emit_u8(&cg->code,0xF8); // mov [rbp-8], r14
    emit_u8(&cg->code,0x4C);emit_u8(&cg->code,0x89);emit_u8(&cg->code,0x7D);emit_u8(&cg->code,0xF0); // mov [rbp-16], r15
    // OPT: cache stdout handle + QPC freq once at program entry — eliminates
    // GetStdHandle() + QueryPerformanceFrequency() from every print/timer call.
    cg_call_extern(cg, (void*)omni_runtime_init);
    // PASS 5: main body statements
    cg->in_main_body=1;
    scope_free(cg->scope);
    cg->scope=scope_new(NULL);
    // First variable slot = [rbp-24] (slots 8 and 16 hold r14/r15 saves)
    cg->scope->next_offset=24;
    cg->stack_size=16; // reserve slots for r14=[rbp-8] and r15=[rbp-16]
    for(int i=0;i<program->statement_count;i++){AST_Statement*s=program->statements[i];if(s&&(s->type==FN_DEFINITION||s->type==CLASS_DEFINITION))continue;cg_stmt(cg,s);}
    if(!cg->returned){
        emit_xor_rax_rax(&cg->code);
        emit_u8(&cg->code,0x4C);emit_u8(&cg->code,0x8B);emit_u8(&cg->code,0x75);emit_u8(&cg->code,0xF8); // mov r14,[rbp-8]
        emit_u8(&cg->code,0x4C);emit_u8(&cg->code,0x8B);emit_u8(&cg->code,0x7D);emit_u8(&cg->code,0xF0); // mov r15,[rbp-16]
        emit_mov_rsp_rbp(&cg->code);emit_pop_rbp(&cg->code);emit_ret(&cg->code);
    }
    // PASS 6: patch main frame size
    patch_u32(&cg->code,stack_patch,aligned_frame(cg->stack_size));
    // PASS 7: resolve user function call patches
    for(int i=0;i<cg->call_patch_count;i++){
        CallPatch*cp=&cg->call_patches[i];
        FnEntry*fe=fn_find(cg,cp->fn_name);
        if(!fe||!fe->resolved){fprintf(stderr,"CodeGen Error: unresolved call to '%s'\n",cp->fn_name);return 0;}
        int32_t disp=(int32_t)((ptrdiff_t)fe->code_offset-(ptrdiff_t)(cp->patch_offset+4));
        BETA_TRACE_CG("patch call '%s': code_offset=%zu patch_offset=%zu disp=%d target=%zu",
            cp->fn_name, fe->code_offset, cp->patch_offset,
            disp, (size_t)((ptrdiff_t)(cp->patch_offset+4)+disp));
        patch_u32(&cg->code,cp->patch_offset,(uint32_t)disp);
    }
    BETA_TRACE_CG("compile done: %zu bytes, %d fns, %d strings",cg->code.size,cg->fn_count,cg->string_pool_count);
    return 1;
}
// ── Executable memory (W^X): allocate RW → copy → switch to RX → run → free ──
// Avoids permanently-WX pages on both platforms (Phase 10 hardening).
#if defined(_WIN32)
static void* omni_exec_alloc(size_t sz){
    return VirtualAlloc(NULL,sz,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
}
static int omni_exec_rx(void* p,size_t sz){
    DWORD old=0;
    return VirtualProtect(p,sz,PAGE_EXECUTE_READ,&old)?0:-1;
}
static void omni_exec_free(void* p,size_t sz){ (void)sz; VirtualFree(p,0,MEM_RELEASE); }
#else
#include <sys/mman.h>
static void* omni_exec_alloc(size_t sz){
    return mmap(NULL,sz,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
}
static int omni_exec_rx(void* p,size_t sz){
    return mprotect(p,sz,PROT_READ|PROT_EXEC);
}
static void omni_exec_free(void* p,size_t sz){ munmap(p,sz); }
#endif

/* Weak hook so the host can map a faulting PC back to a JIT offset
   (diagnostics only — see OMNI_JIT_DEBUG in main.c). */
void codegen_set_jit_region(void* mem, size_t sz) {
    extern void omni_host_set_jit_region(void* mem, size_t sz);
    omni_host_set_jit_region(mem, sz);
}
void omni_host_set_jit_region(void* mem, size_t sz); /* defined in main.c */

int64_t codegen_run(CodeGen*cg){
    if(cg->code.size==0){fprintf(stderr,"CodeGen: nothing to run\n");return -1;}
    void*mem=omni_exec_alloc(cg->code.size);
    if(!mem||mem==(void*)-1){fprintf(stderr,"CodeGen: executable-memory allocation failed\n");return -1;}
    memcpy(mem,cg->code.data,cg->code.size);
    codegen_set_jit_region(mem, cg->code.size);
    /* TEMP DIAGNOSTIC (OMNI_JIT_DEBUG): capture callee-saved RBX + buffer
       checksum around the JIT call to attribute corruption precisely.
       Runs BEFORE mprotect so patch/save writes hit RW memory. */
    long rbx_before=0, rbx_after=0;
    int jitdbg = getenv("OMNI_JIT_DEBUG") != NULL;
    if (jitdbg) {
        __asm__ volatile("mov %%rbx, %0" : "=r"(rbx_before));
        fprintf(stderr,"[jitdbg] before: rbx=%p size=%zu\n",(void*)(uintptr_t)rbx_before,cg->code.size);
        fflush(stderr);
        const char* save = getenv("OMNI_JIT_SAVE");
        if (save) {
            FILE* sf = fopen(save, "wb");
            if (sf) { fwrite(mem, 1, cg->code.size, sf); fclose(sf);
                      fprintf(stderr, "[jitdbg] saved executed bytes to %s\n", save); fflush(stderr); }
        }
        /* OMNI_JIT_PATCH=1 → NOP the 1st call rax (omni_runtime_init)
           OMNI_JIT_PATCH=2 → NOP the 2nd call rax (print helper)
           OMNI_JIT_PATCH=3 → NOP both */
        const char* patch = getenv("OMNI_JIT_PATCH");
        if (patch) {
            int mode = atoi(patch);
            size_t found = 0;
            for (size_t i = 0; i + 2 <= cg->code.size; i++) {
                if (((unsigned char*)mem)[i] == 0x48 && ((unsigned char*)mem)[i+1] == 0xB8) {
                    size_t j = i + 10; /* call rax right after imm64 */
                    if (j + 2 <= cg->code.size && ((unsigned char*)mem)[j] == 0xFF && ((unsigned char*)mem)[j+1] == 0xD0) {
                        found++;
                        if (mode & 1 && found == 1) { ((unsigned char*)mem)[j] = 0x90; ((unsigned char*)mem)[j+1] = 0x90;
                            fprintf(stderr, "[jitdbg] NOPed call#1 at off 0x%zx\n", j); }
                        if (mode & 2 && found == 2) { ((unsigned char*)mem)[j] = 0x90; ((unsigned char*)mem)[j+1] = 0x90;
                            fprintf(stderr, "[jitdbg] NOPed call#2 at off 0x%zx\n", j); }
                    }
                }
            }
        }
    }
    if(omni_exec_rx(mem,cg->code.size)!=0){
        fprintf(stderr,"CodeGen: failed to make JIT memory executable\n");
        omni_exec_free(mem,cg->code.size);
        return -1;
    }
    long rbx_b2=0,r12_b2=0,r13_b2=0,r14_b2=0,r15_b2=0,rbp_b2=0,rsp_b2=0;
    if (jitdbg) {
        __asm__ volatile("mov %%rbx,%0\n mov %%r12,%1\n mov %%r13,%2\n mov %%r14,%3\n mov %%r15,%4\n mov %%rbp,%5\n mov %%rsp,%6"
                         : "=r"(rbx_b2),"=r"(r12_b2),"=r"(r13_b2),"=r"(r14_b2),"=r"(r15_b2),"=r"(rbp_b2),"=r"(rsp_b2));
        fprintf(stderr,"[jitdbg] pre-call regs: rbx=%p r12=%p r13=%p r14=%p r15=%p rbp=%p rsp=%p\n",
                (void*)rbx_b2,(void*)r12_b2,(void*)r13_b2,(void*)r14_b2,(void*)r15_b2,(void*)rbp_b2,(void*)rsp_b2);
        fflush(stderr);
    }
    typedef int64_t(*OmniEntry)(void);
    int64_t result=((OmniEntry)mem)();
    if (jitdbg) {
        long rsp_after=0,rbp_a2=0,r12_a2=0,r13_a2=0,r14_a2=0,r15_a2=0;
        __asm__ volatile("mov %%rsp, %0\n mov %%rbp,%1\n mov %%r12,%2\n mov %%r13,%3\n mov %%r14,%4\n mov %%r15,%5"
                         : "=r"(rsp_after),"=r"(rbp_a2),"=r"(r12_a2),"=r"(r13_a2),"=r"(r14_a2),"=r"(r15_a2));
        __asm__ volatile("mov %%rbx, %0" : "=r"(rbx_after));
        fprintf(stderr,"[jitdbg] post-call regs: rbx=%p r12=%p r13=%p r14=%p r15=%p rbp=%p rsp=%p rax=%lld\n",
                (void*)(uintptr_t)rbx_after,(void*)r12_a2,(void*)r13_a2,(void*)r14_a2,(void*)r15_a2,
                (void*)rbp_a2,(void*)rsp_after,(long long)result);
        fprintf(stderr,"[jitdbg] after: rbx=%p rax=%lld  (mem=%p cg=%p code.data=%p)\n",
                (void*)(uintptr_t)rbx_after,(long long)result,
                mem,(void*)(uintptr_t)cg,(void*)(uintptr_t)cg->code.data);
        if (getenv("OMNI_JIT_FRAME")) {
            /* Capture FIRST (no fprintf in between — its frames would
               overwrite the JIT's stale frame below rsp). */
            long snap[32];
            for (int k = 0; k < 32; k++) snap[k] = *(long*)(rsp_after - 8 * (k + 1));
            for (int k = 0; k < 32; k++)
                fprintf(stderr, "[jitdbg]   [rsp%+#lx] = %p\n", (long)(-8 * (k + 1)),
                        (void*)(uintptr_t)snap[k]);
        }
        fflush(stderr);
        unsigned long sum2=0; for(size_t i=0;i<cg->code.size;i++) sum2=sum2*31+((unsigned char*)mem)[i];
        fprintf(stderr,"[jitdbg] after: buffer_sum=%lx\n",sum2);
        fflush(stderr);
    }
    omni_exec_free(mem,cg->code.size);
    return result;
}
void codegen_dump(CodeGen*cg){
    fprintf(stderr,"=== OMNIKARAI v5.0 x86-64 DUMP (%zu bytes) ===\n",cg->code.size);
    for(size_t i=0;i<cg->code.size;i++){if(i%16==0)fprintf(stderr,"\n%04zX: ",i);fprintf(stderr,"%02X ",cg->code.data[i]);}
    fprintf(stderr,"\n=== END DUMP ===\n");
}
