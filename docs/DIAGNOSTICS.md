# Diagnostics (V01.00 — v0)

`omnicc` produces structured diagnostics for humans and machines.
This document is the **contract** for tooling: editors, CI systems,
IDE tooling and AI coding agents. The code scheme, the JSON schema and
the exit codes below are stable; breaking changes bump the schema tag
and are announced in [VERSION_MATRIX.md](VERSION_MATRIX.md) and the
release notes. See also [COMPILER.md](COMPILER.md) (where diagnostics
originate) and [ROADMAP.md](ROADMAP.md) (diagnostics GA in V01.09).

## Diagnostic model

Every diagnostic carries:

| Field      | Meaning                                                        |
|------------|----------------------------------------------------------------|
| `severity` | `error` · `warning` · `note` (v0 emits errors only)            |
| `code`     | stable identifier, e.g. `OMNI-E2001` (scheme below)            |
| `message`  | human-readable, no internal token numbers                      |
| `file`     | source path as given on the command line                       |
| `line`     | 1-based; `null` when unknown                                   |
| `column`   | 1-based; `null` when unknown                                   |
| `span`     | visible characters the diagnostic covers; `null` when unknown  |
| `hint`     | optional fix suggestion; `null` when absent                    |

Internal debug detail (raw token state) exists on the model but is
shown only with `--beta`; it never appears in JSON or default text.

## Code scheme — `OMNI-<S><NNNN>`

`S` = `E` (error) or `W` (warning). Published codes are permanent:
a code never changes meaning and is never reused.

| Range    | Area                    | Assigned codes (v0)                                                                                            |
|----------|-------------------------|----------------------------------------------------------------------------------------------------------------|
| `0NNN`   | CLI / usage / IO        | `E0001` unknown command · `E0002` unknown flag · `E0003` missing file argument · `E0004` cannot open source file |
| `1NNN`   | lexer                   | reserved (the lexer defers errors to the parser in v0)                                                          |
| `2NNN`   | parser / syntax         | `E2001` unexpected/missing token · `E2002` expected identifier · `E2003` expected `:` · `E2004` expected indented block · `E2005` unexpected token with no parse rule · `E2099` unclassified syntax error |
| `3NNN`   | semantic / codegen      | `E3001` name not defined · `E3002` type error · `E3003` value error · `E3099` unclassified semantic error        |
| `9NNN`   | internal errors         | explicitly **not** stable for tooling; they indicate compiler defects — please report them                       |

## JSON output — schema `omnikarai.diag.v0`

`omnicc check --json <file>` writes exactly one JSON document to
**stdout** (stderr stays silent). Deterministic key order; new keys
are appended only; removals/renames bump the schema tag.

Failure (one parse error, one codegen error, or an IO error):

```json
{
  "schema": "omnikarai.diag.v0",
  "ok": false,
  "diagnostics": [
    {
      "severity": "error",
      "code": "OMNI-E2001",
      "message": "expected ':' but found identifier 'x'",
      "file": "bad.ok",
      "line": 2,
      "column": 9,
      "span": 1,
      "hint": null
    }
  ],
  "summary": { "errors": 1, "warnings": 0 }
}
```

Success:

```json
{
  "schema": "omnikarai.diag.v0",
  "ok": true,
  "diagnostics": [],
  "summary": { "errors": 0, "warnings": 0 }
}
```

Notes:

- `check --json` runs the full compile validation (parse **and**
  codegen), not just the parser — a semantic error such as an
  undefined name (`OMNI-E3001`) is reported in the same document.
- Strings are UTF-8; bytes ≥ 0x20 pass through unescaped (RFC 8259).
- Golden tests for this document live in `tests/run_regression.py`
  (the `json:` checks); edit both together.

## Text output

Text diagnostics go to **stderr** with source context and a caret:

```
  File "bad.ok", line 2
    if x > 5
           ^
OMNI-E2001 error: expected ':' but found end of line
```

The legacy `--quiet`, `--ut` and `--beta` flags behave as before.

## Exit codes (deterministic)

| Code | Meaning                                                                 |
|------|-------------------------------------------------------------------------|
| `0`  | success — or the program's own exit code for `run` (e.g. `sys.exit(7)`) |
| `1`  | diagnostics reported (parse or compile errors)                          |
| `2`  | usage or IO error (unknown command/flag, cannot open file)              |

## Machine version output

`omnicc version --machine` prints one `key=value` per line on stdout
(`schema`, `version`, `platform`, `arch`, `abi`, `modules`). Existing
keys are never reordered or renamed; the `schema` value
(`OMNI_VERSION_MACHINE_SCHEMA` in `include/omni_version.h`) is bumped
when they change. Verified by `tests/check_version.py`.

## Cross-links

- Where diagnostics originate: [COMPILER.md](COMPILER.md)
- What ships in which version: [VERSION_MATRIX.md](VERSION_MATRIX.md)
- Diagnostics GA (LSP-grade): [ROADMAP.md](ROADMAP.md) V01.09
