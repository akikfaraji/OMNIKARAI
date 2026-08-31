#!/usr/bin/env python3
# ================================================================
#  Omnikarai reproducible benchmark runner (POSIX / Windows)
#
#  Measures the self-timed Omnikarai benchmarks (bench_*_timed.ok,
#  which use the language's own time module) and the equivalent
#  C and Python programs, then prints everything it measured.
#
#  Results are machine- and flag-dependent — this runner exists so
#  numbers are always reproducible WITH their context, not so a
#  single table can be quoted as universal truth.
#
#  Usage:
#    python3 benchmarks/run_benchmarks.py            # omnicc + python
#    python3 benchmarks/run_benchmarks.py --with-c   # also compile+run C
#    python3 benchmarks/run_benchmarks.py --quick    # single rep, no C
# ================================================================
import argparse
import os
import platform
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

FAMILIES = ["fib", "loop", "primes", "matmul", "dotprod"]


def find_omnicc():
    for name in ("omnicc.exe", "omnicc"):
        p = os.path.join(ROOT, "bin", name)
        if os.path.exists(p):
            return p
    return None


def run_omni(omnicc, ok_file):
    """Run a self-timed .ok benchmark; returns (result_line, ms, exit)."""
    p = subprocess.run([omnicc, "run", "--quiet", ok_file],
                       capture_output=True, text=True, timeout=900)
    lines = [l.strip() for l in (p.stdout + p.stderr).splitlines() if l.strip()]
    if p.returncode != 0 or len(lines) < 2:
        return None, None, p.returncode
    try:
        return lines[-2], float(lines[-1]), p.returncode
    except ValueError:
        return None, None, p.returncode


def time_cmd(cmd, timeout=900):
    """Wall-time an external command; returns seconds or None."""
    t0 = time.perf_counter()
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except (subprocess.TimeoutExpired, OSError):
        return None
    if p.returncode != 0:
        return None
    return time.perf_counter() - t0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--with-c", action="store_true",
                    help="also compile and run the C equivalents (gcc -O2)")
    ap.add_argument("--quick", action="store_true",
                    help="skip the multi-rep median (single run each)")
    args = ap.parse_args()

    omnicc = find_omnicc()
    if not omnicc:
        print("omnicc not found — run make first")
        return 127
    py = sys.executable
    cc = shutil.which("gcc") or shutil.which("clang") if args.with_c else None

    print("=" * 72)
    print("  OMNIKARAI BENCHMARKS — reproducible run")
    print("=" * 72)
    print(f"  host    : {platform.system()} {platform.machine()}")
    print(f"  cpu     : {platform.processor() or platform.machine()}")
    print(f"  omnicc  : {omnicc}")
    print(f"  date    : {time.strftime('%Y-%m-%d %H:%M:%S')}")
    print()

    header = f"{'bench':<10} {'omnikarai ms':>14} {'python ms':>12} {'c -O2 ms':>12}"
    if not cc:
        header = f"{'bench':<10} {'omnikarai ms':>14} {'python ms':>12}"
    print(header)
    print("-" * len(header))

    rows = []
    for fam in FAMILIES:
        okf = os.path.join(HERE, f"bench_{fam}_timed.ok")
        if not os.path.exists(okf):
            continue
        result, ms, code = run_omni(omnicc, okf)
        if ms is None:
            print(f"{fam:<10} {'FAILED':>14}")
            continue

        # Python equivalent: wall-time it (it does not self-time)
        pyf = os.path.join(HERE, f"bench_{fam}.py")
        py_ms = None
        if os.path.exists(pyf):
            secs = time_cmd([py, pyf])
            py_ms = secs * 1000 if secs is not None else None

        c_ms = None
        if cc:
            cf = os.path.join(HERE, f"bench_{fam}.c")
            cbin = os.path.join(HERE, f"bench_{fam}.c.bin")
            if os.path.exists(cf):
                comp = subprocess.run(
                    [cc, "-O2", "-o", cbin, cf, "-lm"],
                    capture_output=True, text=True)
                if comp.returncode == 0:
                    secs = time_cmd([cbin])
                    c_ms = secs * 1000 if secs is not None else None
                    try:
                        os.remove(cbin)
                    except OSError:
                        pass

        row = {"bench": fam, "omni": ms, "py": py_ms, "c": c_ms,
               "result": result}
        rows.append(row)
        py_s = f"{py_ms:12.1f}" if py_ms is not None else f"{'—':>12}"
        c_s = f"{c_ms:12.1f}" if c_ms is not None else (f"{'—':>12}" if cc else "")
        print(f"{fam:<10} {ms:14.1f} {py_s} {c_s}")

    print("-" * len(header))
    print()
    print("  All timings from a single run of each program on this machine;")
    print("  re-run this script on your hardware to reproduce. Do not quote")
    print("  these numbers without the host context above.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
