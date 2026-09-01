#!/usr/bin/env python3
# ================================================================
#  Omnikarai — Portable Test Suite Runner (POSIX + Windows)
#  Replaces the PowerShell-only runners so CI can run everywhere.
#
#  Usage:
#    python3 tests/run_tests.py            # run unit tests t01-t21
#    python3 tests/run_tests.py --stress   # also run stress01-09
#    python3 tests/run_tests.py --verbose  # print full output
#
#  Exit code = number of failed/crashed tests (capped at 125).
# ================================================================
import argparse
import os
import platform as pyplatform
import re
import subprocess
import sys

HERE     = os.path.dirname(os.path.abspath(__file__))
ROOT     = os.path.dirname(HERE)
IS_WIN   = (os.name == "nt")

# ── Version single-sourcing (docs/VERSIONING.md) ─────────────────
# The runner NEVER hardcodes a version: it parses include/omni_version.h,
# so a version bump cannot desynchronize the suite.
def _omni_version():
    hdr = os.path.join(ROOT, "include", "omni_version.h")
    with open(hdr, "r", encoding="utf-8") as f:
        m = re.search(r'#define\s+OMNI_VERSION\s+"([^"]+)"', f.read())
    if not m:
        raise SystemExit(f"ERROR: OMNI_VERSION not found in {hdr}")
    return m.group(1)

OMNI_VERSION = _omni_version()

# Prefer the .exe whenever it exists (MinGW/MSYS2 builds report os.name
# "posix" but still produce bin/omnicc.exe); fall back to the POSIX name.
_cand_exe = os.path.join(ROOT, "bin", "omnicc.exe")
_cand_bin = os.path.join(ROOT, "bin", "omnicc")
OMNICC    = _cand_exe if os.path.exists(_cand_exe) else _cand_bin

# Platform-adjusted expectations (honest per-host values, no weakening:
# the same assertions hold, only the platform strings differ per host).
# They follow the BINARY that was found, not the Python interpreter — an
# MSYS2 Python on Windows reports os.name "posix" but runs omnicc.exe.
if OMNICC.endswith(".exe"):
    OS_NAME, SYS_PLAT, SYS_VER = "windows", "windows-x64", f"Omnikarai {OMNI_VERSION} (x86-64 Windows)"
else:
    OS_NAME, SYS_PLAT, SYS_VER = "linux", "linux-x64", f"Omnikarai {OMNI_VERSION} (x86-64 Linux)"

TESTS = [
    ("t01_core_arithmetic.ok", "Core Arithmetic",      "10,3,13,7,30,3,1,true,true,true,true,true,true"),
    ("t02_logic.ok",           "Logic operators",      "true,false,true,false,false,true"),
    ("t03_if_elif_else.ok",    "if/elif/else",         "A,B,C,F,pass,fail"),
    ("t04_loops.ok",           "Loops+break+continue", "0,1,2,3,4,1,2,4,5,6,0,1,2,3,4"),
    ("t05_functions.ok",       "Functions+recursion",  "15,20,120,25,55"),
    ("t06_match.ok",           "Match/case",           "one,two,three,other,small,big"),
    ("t07_strings.ok",         "Strings",              "42,hello world,11,helloworld,5,100"),
    ("t08_time.ok",            "Time module",          "*"),
    ("t09_math.ok",            "Math module",          "7,10,3,4,5,1,10"),
    ("t10_datetime.ok",        "Datetime module",      "*"),
    ("t11_os.ok",              "OS module",            f"{OS_NAME},*,*,1"),
    ("t12_io.ok",              "IO module",            "1,1,Hello Omnikarai,1,1,0"),
    ("t13_sys.ok",             "Sys module",           f"{SYS_VER},{SYS_PLAT},{OMNI_VERSION},64"),
    ("t14_list.ok",            "List module",          "0,3,10,20,30,30,2,1,0"),
    ("t15_assert.ok",          "Assert builtin",       "ok,done"),
    ("t16_ai_alloc.ok",        "AI alloc/set/get/free","1065353216,1090519040,0"),
    ("t17_ai_relu.ok",         "AI relu (AVX2)",       "0,0,0,1073741824"),
    ("t18_ai_dot.ok",          "AI dot product",       "1106247680"),
    ("t19_ai_matmul.ok",       "AI matmul",            "1077936128,1088421888"),
    ("t20_ai_dot_i8.ok",       "AI dot_i8 (INT8)",     "36"),
    ("t21_fixes.ok",           "Bug fixes",            "8,100,1,hello,world,done,10,99,15"),
]

STRESS = [
    ("stress01_arithmetic.ok",   "Arithmetic Extremes"),
    ("stress02_control_flow.ok", "Control Flow Extremes"),
    ("stress03_functions.ok",    "Functions Extremes"),
    ("stress04_strings.ok",      "Strings Extremes"),
    ("stress05_lists.ok",        "Lists Extremes"),
    ("stress06_modules.ok",      "Modules Extremes"),
    ("stress07_logic.ok",        "Logic Extremes"),
    ("stress08_algorithms.ok",   "Algorithms"),
    ("stress09_combined.ok",     "Combined"),
]


def run_one(path):
    """Run omnicc on one file; returns (exit_code, stdout+stderr text)."""
    try:
        p = subprocess.run(
            [OMNICC, "run", "--quiet", path],
            capture_output=True, text=True, timeout=60,
        )
        return p.returncode, (p.stdout + p.stderr)
    except subprocess.TimeoutExpired:
        return 124, "(timeout after 60s)"
    except OSError as e:
        return 127, f"(cannot execute omnicc: {e})"


def output_lines(text):
    return [ln.strip() for ln in text.splitlines() if ln.strip()]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stress",  action="store_true", help="also run stress tests")
    ap.add_argument("--verbose", action="store_true", help="print full program output")
    args = ap.parse_args()

    if not os.path.exists(OMNICC):
        print(f"ERROR: omnicc not found at {OMNICC}")
        print("Build first:  make        (POSIX)   |   make windows   (MinGW cross)")
        return 127

    print()
    print("=" * 62)
    print("   OMNIKARAI  --  FULL TEST SUITE")
    print("=" * 62)
    print(f"   Compiler: {OMNICC}")
    print(f"   Host:     {pyplatform.system()} {pyplatform.machine()}")
    print()

    cases = list(TESTS)
    if args.stress:
        cases += [(f, name, "*") for f, name in STRESS]

    passed, failed = [], []
    for fname, name, expect in cases:
        fpath = os.path.join(HERE, fname)
        if not os.path.exists(fpath):
            failed.append((name, "SKIP", "file not found", ""))
            continue
        code, out = run_one(fpath)
        if code != 0:
            failed.append((name, "CRASH", f"exit {code}", out))
            continue
        lines = output_lines(out)
        if expect == "*":
            passed.append((name, out))
            continue
        expected = [x.strip() for x in expect.split(",")]
        reason = None
        for i, exp in enumerate(expected):
            if exp == "*":
                continue
            act = lines[i] if i < len(lines) else "(missing)"
            if act != exp:
                reason = f"line {i+1}: expected '{exp}' got '{act}'"
                break
        if reason:
            failed.append((name, "FAIL", reason, out))
        else:
            passed.append((name, out))

    print("-" * 62)
    for name, out in passed:
        print(f"  {name:.<40} [PASS]")
        if args.verbose:
            for ln in output_lines(out):
                print(f"      >> {ln}")
    for name, status, reason, out in failed:
        print(f"  {name:.<40} [{status}] {reason}")
        if args.verbose:
            for ln in output_lines(out):
                print(f"      >> {ln}")
    print("-" * 62)
    n_fail = len(failed)
    print(f"   Results: {len(passed)}/{len(cases)} passed | {n_fail} failed/crashed")
    print()
    if n_fail == 0:
        print("   ALL TESTS PASSED")
    else:
        print("   SOME TESTS FAILED — debug with:  bin/omnicc run --beta tests/<file>.ok")
    print()
    return min(n_fail, 125)


if __name__ == "__main__":
    sys.exit(main())
