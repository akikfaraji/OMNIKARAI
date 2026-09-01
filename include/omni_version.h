#ifndef OMNIKARAI_VERSION_H
#define OMNIKARAI_VERSION_H

/* ============================================================
 *  OMNIKARAI VERSION — SINGLE SOURCE OF TRUTH
 *
 *  FRAZIYM versioning convention (docs/VERSIONING.md):
 *
 *      VXX.YY.ZZZ-beta-NN
 *
 *      XX     platform/language generation (V01 = this generation)
 *      YY     feature generation   (one bump per roadmap release)
 *      ZZZ    patch generation     (zero-padded, three digits)
 *      NN     prerelease iteration (zero-padded, two digits)
 *
 *  Consumers — every version display MUST derive from this header:
 *
 *    src/main.c           CLI banner, `omnicc version [--machine]`
 *    src/codegen.c        platform banner + sys.omni_ver() /
 *                         sys.version() strings
 *    omnikarai.toml       project metadata mirror — must be bumped
 *                         in the SAME commit (no preprocessor here);
 *                         enforced by tests/check_version.py
 *    tests/run_tests.py   expectations parsed from this header at
 *                         runtime (never hardcoded)
 *
 *  Rule: to bump the version, edit OMNI_VERSION here and the
 *  `version` field in omnikarai.toml in one commit. No other file
 *  may hardcode a version string.
 *
 *  Banner format (sys.version()):
 *      "Omnikarai <OMNI_VERSION> (x86-64 Windows|Linux)"
 *  The lowercase-`v` prefix is reserved for git tags:
 *      vV01.00.000-beta-01   (docs/VERSIONING.md)
 * ============================================================ */

#define OMNI_VERSION "V01.00.000-beta-01"

/* Last legacy (pre-V01) release. History/reference only — never
   reported by tools as the current version. */
#define OMNI_VERSION_LEGACY_LAST "7.1.0"

/* Schema revision of `omnicc version --machine` key=value output.
   Bump when the machine output gains/changes keys. */
#define OMNI_VERSION_MACHINE_SCHEMA 1

#endif /* OMNIKARAI_VERSION_H */
