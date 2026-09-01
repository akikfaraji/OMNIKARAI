#!/usr/bin/env python3
# ================================================================
#  Omnikarai — Version Consistency Gate (V01.00, docs/VERSIONING.md)
#
#  Verifies that every version display in the repository agrees with
#  the single source of truth, include/omni_version.h:
#
#    1. omnikarai.toml `version` mirrors OMNI_VERSION
#    2. no stale hardcoded version strings in src/ or include/
#    3. `omnicc version --machine` reports OMNI_VERSION
#    4. a compiled program's sys.version()/sys.omni_ver() agree
#
#  Exit 0 = consistent; exit 1 = drift detected (release gate fails).
# ================================================================
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
IS_WIN = (os.name == "nt")

_cand_exe = os.path.join(ROOT, "bin", "omnicc.exe")
_cand_bin = os.path.join(ROOT, "bin", "omnicc")
OMNICC = _cand_exe if os.path.exists(_cand_exe) else _cand_bin


def fail(msg):
    print(f"  VERSION-DRIFT: {msg}")
    return 1


def omni_version_from_header():
    hdr = os.path.join(ROOT, "include", "omni_version.h")
    with open(hdr, "r", encoding="utf-8") as f:
        m = re.search(r'#define\s+OMNI_VERSION\s+"([^"]+)"', f.read())
    if not m:
        return None
    return m.group(1)


def main():
    print()
    print("=" * 62)
    print("   OMNIKARAI -- VERSION CONSISTENCY GATE")
    print("=" * 62)
    errors = 0
    ver = omni_version_from_header()
    if not ver:
        return fail("cannot parse OMNI_VERSION from include/omni_version.h")
    print(f"   Single source (include/omni_version.h): {ver}")

    # 1. omnikarai.toml mirror
    toml = os.path.join(ROOT, "omnikarai.toml")
    with open(toml, "r", encoding="utf-8") as f:
        m = re.search(r'^\s*version\s*=\s*"([^"]+)"', f.read(), re.M)
    if not m:
        errors += fail("omnikarai.toml has no `version` field")
    elif m.group(1) != ver:
        errors += fail(f"omnikarai.toml says {m.group(1)!r}, header says {ver!r}")
    else:
        print("   omnikarai.toml mirror:                    OK")

    # 2. No stale hardcoded version strings in compiler sources.
    #    The only legitimate occurrence of the legacy string is the
    #    OMNI_VERSION_LEGACY_LAST definition in the version header.
    legacy = "7.1.0"
    scan_dirs = [os.path.join(ROOT, "src"), os.path.join(ROOT, "include")]
    hits = []
    for d in scan_dirs:
        for name in sorted(os.listdir(d)):
            if not name.endswith((".c", ".h")):
                continue
            path = os.path.join(d, name)
            with open(path, "r", encoding="utf-8", errors="replace") as f:
                for i, ln in enumerate(f, 1):
                    if legacy in ln and "OMNI_VERSION_LEGACY_LAST" not in ln:
                        hits.append(f"{os.path.relpath(path, ROOT)}:{i}")
    if hits:
        errors += fail(f"hardcoded legacy version outside the header: {', '.join(hits)}")
    else:
        print("   No hardcoded legacy versions in src/include: OK")

    if not os.path.exists(OMNICC):
        errors += fail(f"omnicc not found at {OMNICC} — build first (make)")
        print()
        return 1 if errors else 0

    # 3. Machine-parsable CLI output
    try:
        p = subprocess.run([OMNICC, "version", "--machine"],
                           capture_output=True, text=True, timeout=30)
        keys = dict(
            ln.split("=", 1) for ln in p.stdout.splitlines() if "=" in ln
        )
        if keys.get("version") != ver:
            errors += fail(f"omnicc version --machine reports {keys.get('version')!r}, expected {ver!r}")
        else:
            print("   omnicc version --machine:                 OK")
    except (OSError, subprocess.TimeoutExpired) as e:
        errors += fail(f"omnicc version --machine failed to run: {e}")

    # 4. Runtime-reported version (sys module) agrees — requires the
    # codegen backend (JIT-executed probe program). On non-x86-64 hosts
    # `omnicc run` refuses with OMNI-E0005 (docs/AARCH64.md), so the
    # runtime probes SKIP there; all other gate checks still run.
    import platform as _pl
    _mach = (os.uname().machine.lower() if os.name != "nt"
             else _pl.machine().lower())
    if _mach not in ("x86_64", "amd64"):
        print(f"   sys.omni_ver()/sys.version():             "
              f"[SKIP] backend probes need x86-64 (host {_mach}; OMNI-E0005)")
    else:
        prog = (
            "use sys\n"
            "print(sys.omni_ver())\n"
            "print(sys.version())\n"
        )
        fd, path = tempfile.mkstemp(suffix=".ok", prefix="omni_verchk_")
        with os.fdopen(fd, "w") as f:
            f.write(prog)
        try:
            p = subprocess.run([OMNICC, "run", "--quiet", path],
                               capture_output=True, text=True, timeout=60)
            lines = [ln.strip() for ln in p.stdout.splitlines() if ln.strip()]
            if p.returncode != 0 or len(lines) < 2:
                errors += fail(f"version probe program failed (exit {p.returncode})")
            else:
                if lines[0] != ver:
                    errors += fail(f"sys.omni_ver() = {lines[0]!r}, expected {ver!r}")
                else:
                    print("   sys.omni_ver():                           OK")
                if ver not in lines[1]:
                    errors += fail(f"sys.version() = {lines[1]!r}, does not contain {ver!r}")
                else:
                    print("   sys.version():                            OK")
        finally:
            os.unlink(path)

    print("-" * 62)
    if errors == 0:
        print("   VERSION CONSISTENT")
    else:
        print(f"   {errors} DRIFT ISSUE(S) — fix before release")
    print()
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
