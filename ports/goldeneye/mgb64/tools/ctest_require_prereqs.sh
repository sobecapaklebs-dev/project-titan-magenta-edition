#!/bin/bash
#
# ctest_require_prereqs.sh -- thin CTest prerequisite gate for ROM-gated
# intro-path smokes (T5).
#
# The intro_* ctest entries registered in CMakeLists.txt need genuine CTest
# SKIPPED status when the ROM and/or instrumented ares binary are absent --
# not a FAIL, and not silent non-registration (the existing
# add_port_validation_smoke convention: the whole test just doesn't get
# configured without baserom.u.z64). A CTest SKIPPED result requires the
# test COMMAND to exit with a dedicated SKIP_RETURN_CODE, so a ROM-less
# checkout (e.g. CI) still enumerates these tests and shows them skipped
# rather than missing.
#
# The underlying validation scripts (intro_parse_digest_gate.sh,
# intro_census_capture.sh, movement_oracle_capture.sh via
# validation_common.sh's validation_require_file/validation_require_binary)
# already exit 2 on a missing ROM/binary -- but they ALSO exit 2 for
# unrelated argument/validation failures (bad flags, malformed route fields,
# out-of-range timeouts, etc: grep any of them for "exit 2"). Claiming exit 2
# as ctest's SKIP_RETURN_CODE would silently turn those real failures into
# skips too. So this wrapper does its own prereq existence check first, with
# a code (125) that means ONLY "prerequisite missing," then execs the real
# command untouched -- its normal PASS/FAIL exit codes still apply and still
# mean PASS/FAIL.
#
# Usage: ctest_require_prereqs.sh --require PATH [--require PATH ...] -- CMD...
set -euo pipefail

readonly SKIP_RETURN_CODE=125

usage() {
    echo "Usage: $0 --require PATH [--require PATH ...] -- CMD..." >&2
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

requires=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --require)
            [[ $# -ge 2 ]] || { echo "ctest_require_prereqs: --require needs a PATH" >&2; exit 2; }
            requires+=("$2")
            shift 2
            ;;
        --)
            shift
            break
            ;;
        *)
            echo "ctest_require_prereqs: unknown arg '$1' (missing '--' before CMD?)" >&2
            usage
            exit 2
            ;;
    esac
done

if [[ "$#" -eq 0 ]]; then
    echo "ctest_require_prereqs: missing CMD after '--'" >&2
    usage
    exit 2
fi

for path in "${requires[@]}"; do
    if [[ ! -e "$path" ]]; then
        echo "SKIP: prerequisite not found: $path" >&2
        exit "$SKIP_RETURN_CODE"
    fi
done

exec "$@"
