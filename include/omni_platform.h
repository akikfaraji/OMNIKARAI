// ============================================================
//  omni_platform.h — platform abstraction for the omnicc host
//
//  On Windows (_WIN32) the native Win32 API is used directly.
//  On POSIX (Linux/macOS) this header provides a thin, honest
//  compatibility layer with the same shapes the compiler and its
//  embedded language runtime were written against, so the whole
//  host program stays platform-agnostic above this line.
//
//  Design rule: shims here must be SEMANTICALLY equivalent to the
//  Win32 behaviour the call sites rely on — not "close enough".
// ============================================================
#ifndef OMNI_PLATFORM_H
#define OMNI_PLATFORM_H

#ifndef _WIN32

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

// ── Basic types ─────────────────────────────────────────────
typedef uint32_t DWORD;
typedef int      BOOL;
typedef uint16_t WORD;
#define TRUE  1
#define FALSE 0

typedef FILE* HANDLE;
#define INVALID_HANDLE_VALUE NULL
#define STD_INPUT_HANDLE  0
#define STD_OUTPUT_HANDLE 1
#define STD_ERROR_HANDLE  2

#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFu
#define FILE_ATTRIBUTE_DIRECTORY 0x10u
#define FILE_ATTRIBUTE_NORMAL    0x80u
#define MAX_PATH 1024

typedef struct { int64_t QuadPart; } LARGE_INTEGER;
typedef struct { DWORD dwLowDateTime; DWORD dwHighDateTime; } FILETIME;
typedef struct {
    WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME;
typedef struct { long Bias; } TIME_ZONE_INFORMATION;
typedef struct {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD nFileSizeHigh, nFileSizeLow;
} WIN32_FILE_ATTRIBUTE_DATA;
#define GetFileExInfoStandard 0

// CreateFileA constants (only the modes the compiler actually uses)
#define GENERIC_READ        0x80000000u
#define GENERIC_WRITE       0x40000000u
#define CREATE_ALWAYS       2
#define CREATE_NEW          1
#define OPEN_EXISTING       3
#define OPEN_ALWAYS         4
#define TRUNCATE_EXISTING   5
#define FILE_SHARE_READ     1
#define FILE_SHARE_WRITE    2
#define FILE_ATTRIBUTE_ARCHIVE 0x20u

// ── Console / file I/O ──────────────────────────────────────
static inline HANDLE GetStdHandle(int which) {
    return which == STD_OUTPUT_HANDLE ? stdout
         : which == STD_ERROR_HANDLE  ? stderr
         : stdin;
}
static inline BOOL WriteFile(HANDLE h, const void* buf, DWORD n, DWORD* written, void* overlapped) {
    (void)overlapped;
    if (!buf || n == 0) { if (written) *written = 0; return 1; }
    size_t r = fwrite(buf, 1, (size_t)n, (FILE*)h);
    if (written) *written = (DWORD)r;
    return r == (size_t)n;
}
static inline BOOL ReadFile(HANDLE h, void* buf, DWORD n, DWORD* read, void* overlapped) {
    (void)overlapped;
    size_t got = fread(buf, 1, (size_t)n, (FILE*)h);
    if (read) *read = (DWORD)got;
    return 1; /* EOF is reported via *read == 0, as the call sites expect */
}
static inline BOOL FlushFileBuffers(HANDLE h) { return fflush((FILE*)h) == 0; }

static inline BOOL CloseHandle(HANDLE h) {
    if (!h) return 0;
    return fclose((FILE*)h) == 0;
}

// ── Time ────────────────────────────────────────────────────
// FILETIME: 100ns units since 1601-01-01 UTC.
#define OMNI_FT_EPOCH_OFFSET 116444736000000000LL /* 1601→1970, in 100ns */
static inline void GetSystemTimeAsFileTime(FILETIME* ft) {
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    int64_t huns = (int64_t)ts.tv_sec * 10000000LL + ts.tv_nsec / 100LL
                 + OMNI_FT_EPOCH_OFFSET;
    ft->dwHighDateTime = (DWORD)((uint64_t)huns >> 32);
    ft->dwLowDateTime  = (DWORD)(uint64_t)huns;
}
static inline BOOL QueryPerformanceCounter(LARGE_INTEGER* c) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    c->QuadPart = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    return 1;
}
static inline BOOL QueryPerformanceFrequency(LARGE_INTEGER* f) {
    f->QuadPart = 1000000000LL; /* counter ticks in ns */
    return 1;
}
static inline BOOL FileTimeToSystemTime(const FILETIME* ft, SYSTEMTIME* st) {
    int64_t huns = ((int64_t)ft->dwHighDateTime << 32) | ft->dwLowDateTime;
    int64_t secs = huns / 10000000LL - 11644473600LL;
    time_t t = (time_t)secs;
    struct tm tmv; gmtime_r(&t, &tmv);
    st->wYear         = (WORD)(tmv.tm_year + 1900);
    st->wMonth        = (WORD)(tmv.tm_mon + 1);
    st->wDay          = (WORD)tmv.tm_mday;
    st->wHour         = (WORD)tmv.tm_hour;
    st->wMinute       = (WORD)tmv.tm_min;
    st->wSecond       = (WORD)tmv.tm_sec;
    st->wDayOfWeek    = (WORD)tmv.tm_wday;
    st->wMilliseconds = (WORD)((huns % 10000000LL) / 10000LL);
    return 1;
}
static inline BOOL SystemTimeToFileTime(const SYSTEMTIME* st, FILETIME* ft) {
    struct tm tmv; memset(&tmv, 0, sizeof(tmv));
    tmv.tm_year = (int)st->wYear - 1900;
    tmv.tm_mon  = (int)st->wMonth - 1;
    tmv.tm_mday = (int)st->wDay;
    tmv.tm_hour = (int)st->wHour;
    tmv.tm_min  = (int)st->wMinute;
    tmv.tm_sec  = (int)st->wSecond;
    time_t t = timegm(&tmv);
    if (t == (time_t)-1) return 0;
    int64_t huns = ((int64_t)t + 11644473600LL) * 10000000LL
                 + (int64_t)st->wMilliseconds * 10000LL;
    ft->dwHighDateTime = (DWORD)((uint64_t)huns >> 32);
    ft->dwLowDateTime  = (DWORD)(uint64_t)huns;
    return 1;
}
// Returns 0 (no DST information concept used by the call site).
static inline DWORD GetTimeZoneInformation(TIME_ZONE_INFORMATION* tz) {
    time_t t = time(NULL);
    struct tm l; localtime_r(&t, &l);
    tz->Bias = -(long)(l.tm_gmtoff / 60); /* Win: Bias = UTC−local */
    return 0;
}

// ── Environment / process / filesystem ──────────────────────
static inline DWORD GetEnvironmentVariableA(const char* name, char* buf, DWORD size) {
    const char* v = getenv(name);
    if (!v || size == 0) return 0;
    size_t l = strlen(v);
    if (l >= (size_t)size) return (DWORD)l; /* "required size" semantics */
    memcpy(buf, v, l + 1);
    return (DWORD)l;
}
static inline DWORD GetCurrentDirectoryA(DWORD size, char* buf) {
    if (getcwd(buf, (size_t)size)) return (DWORD)strlen(buf);
    return 0;
}
static inline DWORD GetCurrentProcessId(void) { return (DWORD)getpid(); }
static inline DWORD GetFileAttributesA(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return INVALID_FILE_ATTRIBUTES;
    return S_ISDIR(st.st_mode) ? (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_NORMAL)
                               : FILE_ATTRIBUTE_NORMAL;
}
static inline BOOL CreateDirectoryA(const char* path, void* sa) {
    (void)sa; return mkdir(path, 0777) == 0;
}
static inline BOOL DeleteFileA(const char* path) { return unlink(path) == 0; }
static inline BOOL GetFileAttributesExA(const char* path, int level, WIN32_FILE_ATTRIBUTE_DATA* d) {
    (void)level;
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    d->dwFileAttributes = GetFileAttributesA(path);
    memset(&d->ftCreationTime, 0, sizeof(FILETIME));
    memset(&d->ftLastAccessTime, 0, sizeof(FILETIME));
    memset(&d->ftLastWriteTime, 0, sizeof(FILETIME));
    uint64_t sz = (uint64_t)st.st_size;
    d->nFileSizeHigh = (DWORD)(sz >> 32);
    d->nFileSizeLow  = (DWORD)(uint32_t)sz;
    return 1;
}
static inline DWORD GetLastError(void) { return (DWORD)errno; }
static inline void omni_posix_sleep_ms(int64_t ms) {
    struct timespec ts = { (time_t)(ms / 1000), (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
#define Sleep(ms) omni_posix_sleep_ms((int64_t)(ms))
#define ExitProcess(code) exit((int)(code))

// ── Safe CRT shims (MSVC-shaped) ────────────────────────────
static inline int omni_fopen_s(FILE** pf, const char* path, const char* mode) {
    *pf = fopen(path, mode);
    return *pf ? 0 : 1;
}
#define fopen_s(pf, p, m) omni_fopen_s((pf), (p), (m))
static inline int omni_strcpy_s(char* dst, size_t dsz, const char* src) {
    if (!dst || dsz == 0) return 1;
    if (!src) { dst[0] = '\0'; return 1; }
    size_t l = strlen(src);
    if (l >= dsz) { memcpy(dst, src, dsz - 1); dst[dsz - 1] = '\0'; return 1; }
    memcpy(dst, src, l + 1);
    return 0;
}
#define strcpy_s(d, dsz, s) omni_strcpy_s((d), (size_t)(dsz), (s))
static inline int omni_strncpy_s(char* dst, size_t dsz, const char* src, size_t count) {
    if (!dst || dsz == 0) return 1;
    size_t k = (count == (size_t)-1) ? dsz - 1 : (count < dsz - 1 ? count : dsz - 1);
    if (src && k > 0) memcpy(dst, src, k);
    dst[k] = '\0';
    return 0;
}
#define strncpy_s(d, dsz, s, c) omni_strncpy_s((d), (size_t)(dsz), (s), (size_t)(c))
static inline int omni_strncat_s(char* dst, size_t dsz, const char* src, size_t count) {
    if (!dst || dsz == 0) return 1;
    size_t l = strlen(dst);
    if (l >= dsz - 1) return 1;
    size_t room = dsz - l - 1;
    size_t k = (count == (size_t)-1) ? room : (count < room ? count : room);
    if (src) memcpy(dst + l, src, k);
    dst[l + k] = '\0';
    return 0;
}
#define strncat_s(d, dsz, s, c) omni_strncat_s((d), (size_t)(dsz), (s), (size_t)(c))
#define _stricmp  strcasecmp
#define _strnicmp strncasecmp
#define _TRUNCATE ((size_t)-1)
#define sprintf_s(buf, sz, ...) snprintf((buf), (size_t)(sz), __VA_ARGS__)
static inline void* omni_aligned_malloc(size_t size, size_t align) {
    void* p = NULL;
    if (align < sizeof(void*)) align = sizeof(void*);
    if (posix_memalign(&p, align, size) != 0) return NULL;
    return p;
}
#define _aligned_malloc(sz, al) omni_aligned_malloc((size_t)(sz), (size_t)(al))
#define _aligned_free(p) free(p)

#endif /* !_WIN32 */
#endif /* OMNI_PLATFORM_H */
