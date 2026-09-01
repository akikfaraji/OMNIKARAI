#ifndef OMNIKARAI_DIAG_H
#define OMNIKARAI_DIAG_H

/* ============================================================
 *  OMNIKARAI STRUCTURED DIAGNOSTICS — V01.00 (diagnostics v0)
 *
 *  Machine-readable diagnostics for editors, IDE tooling, CI and
 *  AI coding agents. Contract + JSON schema: docs/DIAGNOSTICS.md.
 *
 *  Stability rules for tooling:
 *    - diagnostic codes are permanent once published; a code never
 *      changes meaning and is never reused
 *    - JSON key order is fixed; new keys are appended only
 *    - the schema tag changes (omnikarai.diag.vN) on breaking moves
 *
 *  Code scheme:
 *      OMNI-<S><NNNN>   S = E (error) | W (warning)
 *      0NNN  CLI / usage / IO
 *      1NNN  lexer            (reserved; lexer defers to parser in v0)
 *      2NNN  parser / syntax
 *      3NNN  semantic / codegen
 *      9NNN  internal errors  (explicitly NOT stable for tooling)
 * ============================================================ */

#include <stdio.h>

typedef enum {
    OMNI_DIAG_ERROR = 0,
    OMNI_DIAG_WARNING = 1,
    OMNI_DIAG_NOTE = 2
} OmniDiagSeverity;

typedef struct OmniDiag {
    OmniDiagSeverity severity;
    char code[24];
    char message[512];
    char detail[256];   /* internal token debug; --beta only, never JSON */
    char hint[256];     /* "" = no hint (JSON: null) */
    char file[512];
    int  line;          /* 1-based; 0 = unknown */
    int  column;        /* 1-based; 0 = unknown */
    int  span;          /* visible chars the diag covers; 0 = unknown */
    struct OmniDiag* next;
} OmniDiag;

typedef struct OmniDiagList {
    OmniDiag* head;
    OmniDiag* tail;
    int count;
} OmniDiagList;

/* ── list lifecycle ─────────────────────────────────────────── */
void      omni_diag_init(OmniDiagList* list);
void      omni_diag_free(OmniDiagList* list);

/* Add a diagnostic (printf-style message). Returns the node so the
   caller may attach a hint via omni_diag_set_hint. */
OmniDiag* omni_diag_add(OmniDiagList* list, OmniDiagSeverity sev,
                        const char* code, const char* file,
                        int line, int column, int span,
                        const char* fmt, ...);
void      omni_diag_set_hint(OmniDiag* d, const char* fmt, ...);

/* ── emitters ───────────────────────────────────────────────── */
/* Human-readable output with source line + caret when lines given. */
void omni_diag_print_text(const OmniDiagList* list,
                          char** source_lines, int line_count,
                          FILE* out);
/* Full JSON document (schema omnikarai.diag.v0), single line+pretty
   mixed: pretty 2-space indented, deterministic key order. */
void omni_diag_print_json(const OmniDiagList* list, int ok, FILE* out);

const char* omni_diag_severity_name(OmniDiagSeverity s);

/* ── capture mode (process-exit code paths) ───────────────────
 * codegen errors currently terminate via exit(1). In JSON mode the
 * CLI enables capture: omni_error records into the capture list and
 * the atexit handler flushes the JSON document before termination.
 * This keeps error paths' exit codes unchanged (deterministic). */
void          omni_diag_capture_begin(const char* file);
int           omni_diag_capture_active(void);
OmniDiagList* omni_diag_capture_list(void);
void          omni_diag_capture_end(void);

#endif /* OMNIKARAI_DIAG_H */
