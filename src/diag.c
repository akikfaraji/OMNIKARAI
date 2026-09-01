/* ============================================================
 *  OMNIKARAI STRUCTURED DIAGNOSTICS — implementation (v0)
 *  Contract + JSON schema: docs/DIAGNOSTICS.md
 * ============================================================ */

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "omni_diag.h"

/* ── list lifecycle ─────────────────────────────────────────── */

void omni_diag_init(OmniDiagList* list) {
    list->head = NULL; list->tail = NULL; list->count = 0;
}

void omni_diag_free(OmniDiagList* list) {
    OmniDiag* d = list->head;
    while (d) { OmniDiag* n = d->next; free(d); d = n; }
    omni_diag_init(list);
}

OmniDiag* omni_diag_add(OmniDiagList* list, OmniDiagSeverity sev,
                        const char* code, const char* file,
                        int line, int column, int span,
                        const char* fmt, ...) {
    OmniDiag* d = (OmniDiag*)calloc(1, sizeof(OmniDiag));
    if (!d) return NULL;
    d->severity = sev;
    snprintf(d->code, sizeof(d->code), "%s", code ? code : "OMNI-E3099");
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(d->message, sizeof(d->message), fmt, ap);
    va_end(ap);
    snprintf(d->file, sizeof(d->file), "%s",
             file && *file ? file : "<input>");
    d->line = line; d->column = column; d->span = span;
    if (list->tail) list->tail->next = d; else list->head = d;
    list->tail = d;
    list->count++;
    return d;
}

void omni_diag_set_hint(OmniDiag* d, const char* fmt, ...) {
    if (!d) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(d->hint, sizeof(d->hint), fmt, ap);
    va_end(ap);
}

const char* omni_diag_severity_name(OmniDiagSeverity s) {
    switch (s) {
        case OMNI_DIAG_ERROR:   return "error";
        case OMNI_DIAG_WARNING: return "warning";
        case OMNI_DIAG_NOTE:    return "note";
    }
    return "error";
}

/* ── JSON string escaping (RFC 8259) ──────────────────────────
 * Input is treated as UTF-8 bytes: bytes >= 0x20 pass through so
 * multi-byte characters survive; control chars, quote and backslash
 * are escaped. */
static void json_escape_to(FILE* out, const char* s) {
    fputc('"', out);
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
        unsigned char c = *p;
        switch (c) {
            case '"':  fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\n': fputs("\\n", out);  break;
            case '\r': fputs("\\r", out);  break;
            case '\t': fputs("\\t", out);  break;
            case '\b': fputs("\\b", out);  break;
            case '\f': fputs("\\f", out);  break;
            default:
                if (c < 0x20) fprintf(out, "\\u%04x", c);
                else fputc((char)c, out);
        }
    }
    fputc('"', out);
}

static int count_severity(const OmniDiagList* list, OmniDiagSeverity s) {
    int n = 0;
    for (const OmniDiag* d = list->head; d; d = d->next)
        if (d->severity == s) n++;
    return n;
}

void omni_diag_print_json(const OmniDiagList* list, int ok, FILE* out) {
    fprintf(out, "{\n  \"schema\": \"omnikarai.diag.v0\",\n");
    fprintf(out, "  \"ok\": %s,\n", ok ? "true" : "false");
    fprintf(out, "  \"diagnostics\": [");
    const OmniDiag* d = list->head;
    if (!d) {
        fprintf(out, "],\n");
    } else {
        fputc('\n', out);
        for (; d; d = d->next) {
            fprintf(out, "    {\n");
            fprintf(out, "      \"severity\": \"%s\",\n",
                    omni_diag_severity_name(d->severity));
            fprintf(out, "      \"code\": ");
            json_escape_to(out, d->code);
            fprintf(out, ",\n      \"message\": ");
            json_escape_to(out, d->message);
            fprintf(out, ",\n      \"file\": ");
            json_escape_to(out, d->file);
            if (d->line > 0)   fprintf(out, ",\n      \"line\": %d", d->line);
            else               fprintf(out, ",\n      \"line\": null");
            if (d->column > 0) fprintf(out, ",\n      \"column\": %d", d->column);
            else               fprintf(out, ",\n      \"column\": null");
            if (d->span > 0)   fprintf(out, ",\n      \"span\": %d", d->span);
            else               fprintf(out, ",\n      \"span\": null");
            fprintf(out, ",\n      \"hint\": ");
            if (d->hint[0]) json_escape_to(out, d->hint);
            else            fprintf(out, "null");
            fprintf(out, "\n    }%s\n", d->next ? "," : "");
        }
        fprintf(out, "  ],\n");
    }
    fprintf(out, "  \"summary\": { \"errors\": %d, \"warnings\": %d }\n}\n",
            count_severity(list, OMNI_DIAG_ERROR),
            count_severity(list, OMNI_DIAG_WARNING));
}

/* ── human-readable text ────────────────────────────────────── */

void omni_diag_print_text(const OmniDiagList* list,
                          char** source_lines, int line_count,
                          FILE* out) {
    for (const OmniDiag* d = list->head; d; d = d->next) {
        fprintf(out, "\n  File \"%s\"", d->file);
        if (d->line > 0) fprintf(out, ", line %d", d->line);
        fputc('\n', out);
        if (d->line >= 1 && d->line <= line_count && source_lines) {
            const char* ln = source_lines[d->line - 1];
            fprintf(out, "    %s\n", ln);
            fprintf(out, "    ");
            for (int i = 1; i < d->column; i++) fputc(' ', out);
            int span = d->span > 0 ? d->span : 1;
            for (int i = 0; i < span; i++) {
                /* keep the caret under the token; tabs widen roughly */
                if (ln && ln[d->column - 1 + i] == '\t') fputc('\t', out);
                else fputc('^', out);
            }
            fputc('\n', out);
        }
        fprintf(out, "%s %s: %s\n", d->code,
                omni_diag_severity_name(d->severity), d->message);
        if (d->hint[0]) fprintf(out, "  hint: %s\n", d->hint);
    }
}

/* ── capture mode ───────────────────────────────────────────── */

static OmniDiagList g_capture;
static int          g_capture_active = 0;

void omni_diag_capture_begin(const char* file) {
    omni_diag_init(&g_capture);
    g_capture_active = 1;
    (void)file; /* file is carried per-diagnostic by the producers */
}

int omni_diag_capture_active(void) { return g_capture_active; }

OmniDiagList* omni_diag_capture_list(void) { return &g_capture; }

void omni_diag_capture_end(void) {
    omni_diag_free(&g_capture);
    g_capture_active = 0;
}
