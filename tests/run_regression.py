#!/usr/bin/env python3
# ================================================================
#  Omnikarai — Permanent Regression Suite (V01.00)
#
#  Every historical correctness bug class gets a test that verifies
#  ACTUAL PROGRAM OUTPUT AND EXIT STATUS — not just "the compiler
#  exited 0". The memory-safety side of the historical bugs (strncpy_s
#  shim OOB, JIT register corruption) is additionally covered by the
#  ASan+UBSan CI lane running this same suite.
#
#  Usage:
#    python3 tests/run_regression.py            # programs + JSON checks
#    python3 tests/run_regression.py --verbose  # print program output
#
#  Exit code = number of failed checks (capped at 125).
# ================================================================
import argparse
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
REG  = os.path.join(HERE, "regression")

_cand_exe = os.path.join(ROOT, "bin", "omnicc.exe")
_cand_bin = os.path.join(ROOT, "bin", "omnicc")
OMNICC = _cand_exe if os.path.exists(_cand_exe) else _cand_bin

# ── Architecture tier (docs/AARCH64.md) ──────────────────────────────
# The r01–r13 programs assert the OUTPUT of compiled x86-64 machine code;
# the JSON goldens assert parser/semantic diagnostics and are
# architecture-independent. On a non-x86-64 host the program checks are
# SKIPPED with an explicit reason; the JSON goldens still run.
import platform as _pl

def _host_machine():
    if os.name == "nt":
        return _pl.machine().lower()
    return os.uname().machine.lower()

HOST_MACHINE    = _host_machine()
CODEGEN_ON_HOST = HOST_MACHINE in ("x86_64", "amd64")
_SKIP_NOTE = (f"[SKIP] x86-64 backend only (host {HOST_MACHINE}; "
              "native AArch64 = V01.06, docs/AARCH64.md)")

# (file, [expected stdout lines]) — each case also asserts exit 0
PROGRAMS = [
    ("r01_inline_return.ok",   ["1", "2", "10", "20", "1", "0"]),
    ("r02_const_fold.ok",      ["14", "14", "11", "10", "4", "10", "11", "7", "3",
                               "20", "83", "24", "21", "24", "21", "16", "11", "11"]),
    ("r03_signed_divmod.ok",   ["3", "-3", "-3", "3", "1", "-1", "1", "-1",
                               "3", "-3", "-3", "3", "0", "0", "0", "0",
                               "0", "0", "0", "-1"]),
    ("r04_float_literals.ok",  ["3.14", "5", "1.5", "4", "3", "4.28571", "0.333333",
                               "2.5", "-0.75", "4.5", "3.14159", "2", "3", "true", "true"]),
    ("r05_abi_args.ok",        ["7", "-3", "10", "100", "7891",
                               "Hello, Omnikarai!", "21", "10"]),
    ("r06_for_register.ok",    ["10", "103", "19", "18", "12", "6", "4"]),
    ("r07_int8_api.ok",        ["255", "128", "0", "1", "127", "-128", "-1", "36", "done"]),
    ("r08_string_infer.ok",    ["hi omni", "pos", "nonpos", "direct", "hi x!", "6"]),
    ("r09_string_shims.ok",    ["HELLO, WORLD", "hello, world", "12", "dlroW ,olleH",
                               "hey, World", "1", "Hello"]),
    ("r10_exit_codes.ok",      ["before", "42"]),
    ("r13_memory.ok",          ["1065353216", "6", "7", "ok", "7", "released"]),
]

# JSON diagnostics golden checks:
# (source-snippet or (None -> missing file), expect_ok, expect_exit,
#  [(code, line-or-None) ...])
import re as _re


def run_prog(path):
    try:
        p = subprocess.run([OMNICC, "run", "--quiet", path],
                           capture_output=True, text=True, timeout=60)
        return p.returncode, p.stdout
    except subprocess.TimeoutExpired:
        return 124, "(timeout)"
    except OSError as e:
        return 127, f"(cannot execute: {e})"


def run_json(source_or_path, tmpdir, missing=False):
    if missing:
        path = os.path.join(tmpdir, "definitely_missing_%d.ok" % os.getpid())
    else:
        fd, path = tempfile.mkstemp(suffix=".ok", dir=tmpdir)
        with os.fdopen(fd, "w") as f:
            f.write(source_or_path)
    try:
        p = subprocess.run([OMNICC, "check", "--json", path],
                           capture_output=True, text=True, timeout=60)
        try:
            doc = json.loads(p.stdout)
        except json.JSONDecodeError:
            doc = None
        return p.returncode, doc, p.stderr
    finally:
        if not missing:
            os.unlink(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.exists(OMNICC):
        print(f"ERROR: omnicc not found at {OMNICC}")
        return 127

    print()
    print("=" * 62)
    print("   OMNIKARAI -- REGRESSION SUITE (historical bug classes)")
    print("=" * 62)
    failures = 0
    skipped = 0

    for fname, expected in PROGRAMS:
        path = os.path.join(REG, fname)
        if not os.path.exists(path):
            print(f"  {fname:.<44} [MISSING]")
            failures += 1
            continue
        if not CODEGEN_ON_HOST:
            print(f"  {fname:.<44} {_SKIP_NOTE}")
            skipped += 1
            continue
        code, out = run_prog(path)
        lines = [ln.strip() for ln in out.splitlines() if ln.strip()]
        if code != 0:
            print(f"  {fname:.<44} [CRASH] exit {code}")
            failures += 1
            if args.verbose:
                print("      " + out.replace("\n", "\n      "))
            continue
        if lines != expected:
            print(f"  {fname:.<44} [FAIL]")
            failures += 1
            for i, (e, a) in enumerate(zip(expected, lines + [None] * len(expected))):
                a = a if a is not None else "(missing)"
                mark = " " if e == a else "!"
                if e != a or args.verbose:
                    print(f"      {mark} line {i+1}: expected {e!r} got {a!r}")
            if args.verbose:
                print("      full output:\n      " + out.replace("\n", "\n      "))
        else:
            print(f"  {fname:.<44} [PASS]")

    # exit-code propagation (r11): stdout pinned + process exit code 7
    if CODEGEN_ON_HOST:
        path = os.path.join(REG, "r11_exit_code7.ok")
        code, out = run_prog(path)
        lines = [ln.strip() for ln in out.splitlines() if ln.strip()]
        if code == 7 and lines == ["quitting"]:
            print(f"  {'r11_exit_code7.ok (exit 7)':.<44} [PASS]")
        else:
            print(f"  {'r11_exit_code7.ok (exit 7)':.<44} [FAIL] exit={code} out={lines}")
            failures += 1
    else:
        print(f"  {'r11_exit_code7.ok (exit 7)':.<44} {_SKIP_NOTE}")
        skipped += 1

    # standalone build (r12): the artifact must run and print correctly.
    # `omnicc build` swaps the .ok extension for .exe on every platform.
    if CODEGEN_ON_HOST:
        fd, src = tempfile.mkstemp(suffix=".ok", prefix="omni_r12_")
        with os.fdopen(fd, "w") as f:
            f.write('print("standalone")\nprint(6 * 7)\n')
        exe = src[:-3] + ".exe"
        try:
            b = subprocess.run([OMNICC, "build", src], capture_output=True, text=True, timeout=60)
            r = subprocess.run([exe], capture_output=True, text=True, timeout=60)
            got = [ln.strip() for ln in r.stdout.splitlines() if ln.strip()]
            if b.returncode == 0 and r.returncode == 0 and got == ["standalone", "42"]:
                print(f"  {'r12_standalone_build':.<44} [PASS]")
            else:
                print(f"  {'r12_standalone_build':.<44} [FAIL] build={b.returncode} run={r.returncode} out={got}")
                failures += 1
        finally:
            for pth in (src, exe):
                if os.path.exists(pth):
                    os.unlink(pth)
    else:
        print(f"  {'r12_standalone_build':.<44} {_SKIP_NOTE}")
        skipped += 1

    # ── JSON diagnostics golden checks (omnikarai.diag.v0) ──────────
    with tempfile.TemporaryDirectory() as tmpdir:
        cases = [
            # (source, missing, exit, [(code, line)…], ok)
            ("set x = 1\nprint(x)\n", False, 0, [], True),
            ("if x > 5\n    print(1)\n", False, 1,
             [("OMNI-E2001", 1)], False),
            ("print(nope)\n", False, 1,
             [("OMNI-E3001", 1)], False),
            ("print(2.5 % 1.0)\n", False, 1,
             [("OMNI-E3002", 1)], False),
            ("fn f(:\n    return 1\n", False, 1,
             [("OMNI-E2001", 1)], False),
            (None, True, 2, [("OMNI-E0004", None)], False),
        ]
        for src, missing, exp_exit, exp_diags, exp_ok in cases:
            code, doc, err = run_json(src, tmpdir, missing)
            name = "missing-file" if missing else (src.splitlines()[0][:36] if src else "?")
            label = f"json: {name}"
            if doc is None:
                print(f"  {label:.<44} [FAIL] stdout is not valid JSON")
                failures += 1
                continue
            problems = []
            if code != exp_exit:
                problems.append(f"exit {code} != {exp_exit}")
            if doc.get("schema") != "omnikarai.diag.v0":
                problems.append(f"schema {doc.get('schema')!r}")
            if doc.get("ok") is not exp_ok:
                problems.append(f"ok {doc.get('ok')!r} != {exp_ok!r}")
            got = [(d.get("code"), d.get("line")) for d in doc.get("diagnostics", [])]
            for ecode, eline in exp_diags:
                if (ecode, eline) not in got:
                    problems.append(f"missing diagnostic ({ecode}, {eline}); got {got}")
            if exp_ok and doc.get("diagnostics") != []:
                problems.append(f"expected empty diagnostics, got {got}")
            summary = doc.get("summary", {})
            if exp_ok and summary.get("errors") != 0:
                problems.append(f"summary.errors {summary.get('errors')}")
            if problems:
                print(f"  {label:.<44} [FAIL] {'; '.join(problems)}")
                failures += 1
            else:
                print(f"  {label:.<44} [PASS]")

    print("-" * 62)
    if failures == 0:
        if skipped:
            print(f"   ALL RAN REGRESSION CHECKS PASSED "
                  f"({skipped} skipped: x86-64 codegen tier not on this host)")
        else:
            print("   ALL REGRESSION CHECKS PASSED")
    else:
        print(f"   {failures} REGRESSION CHECK(S) FAILED")
    print()
    return min(failures, 125)


if __name__ == "__main__":
    sys.exit(main())
