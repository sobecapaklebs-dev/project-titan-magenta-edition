#!/bin/bash
#
# validate_quick.sh -- Fast, dependency-clean validation for the ge007 port.
#
# Runs ONLY the shipped, self-contained checks:
#   1. Static native switch-access guard        (always; no ROM, no build)
#   2. Settings schema introspection            (only if build/ge007 exists; no ROM)
#   3. Config round-trip/reset guard            (only if build/ge007 exists; no ROM)
#   4. Enum/string schema type harness          (always if python3 + cc exist)
#   5. Short boot/spawn smoke on a few levels   (only if build/ge007 + ROM exist)
#
# This intentionally does NOT run the emulator parity, RAMROM, soundplayer, or
# baseline-comparison lanes -- those are dev-only (see docs/INSTRUMENTATION.md).
#
# Usage:
#   ./tools/validate_quick.sh
#   ./tools/validate_quick.sh --rom path/to/baserom.u.z64
#   ./tools/validate_quick.sh --no-spawn        # skip the ROM boot/spawn lane
#
set -u

# Resolve repo root from this script's location (tools/ is one level down).
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

BINARY="${REPO_ROOT}/build/ge007"
ROM="${REPO_ROOT}/baserom.u.z64"
RUN_SPAWN=1

usage() {
    echo "Usage: $0 [--rom PATH] [--binary PATH] [--no-spawn]"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --rom)
            if [[ $# -lt 2 ]]; then
                usage >&2
                exit 2
            fi
            ROM="$2"
            shift 2
            ;;
        --binary)
            if [[ $# -lt 2 ]]; then
                usage >&2
                exit 2
            fi
            BINARY="$2"
            shift 2
            ;;
        --no-spawn)  RUN_SPAWN=0; shift ;;
        -h|--help)
            usage
            exit 0 ;;
        *) usage >&2; echo "Unknown arg: $1" >&2; exit 2 ;;
    esac
done

FAILURES=0
SKIPS=0

echo "=================================================="
echo " MGB64 quick validation"
echo " repo: ${REPO_ROOT}"
echo "=================================================="

# ---------------------------------------------------------------------------
# Lane 1: static native switch-access guard (no prerequisites)
# ---------------------------------------------------------------------------
echo ""
echo "--- [1/5] static: native switch-access guard ---"
if ! command -v python3 >/dev/null 2>&1; then
    echo "SKIP: python3 not found (required for the static guard)"
    SKIPS=$((SKIPS + 1))
elif [[ ! -f "tools/check_native_switch_access.py" ]]; then
    echo "SKIP: tools/check_native_switch_access.py not present"
    SKIPS=$((SKIPS + 1))
else
    if python3 tools/check_native_switch_access.py; then
        echo "PASS: static switch-access guard"
    else
        echo "FAIL: static switch-access guard reported violations"
        FAILURES=$((FAILURES + 1))
    fi
fi

# ---------------------------------------------------------------------------
# Lane 2: settings schema introspection (needs build/ge007, no ROM)
# ---------------------------------------------------------------------------
echo ""
echo "--- [2/5] config: settings schema introspection ---"
if ! command -v python3 >/dev/null 2>&1; then
    echo "SKIP: python3 not found (required for settings schema check)"
    SKIPS=$((SKIPS + 1))
elif [[ ! -f "tools/settings_schema_check.py" ]]; then
    echo "SKIP: tools/settings_schema_check.py not present"
    SKIPS=$((SKIPS + 1))
elif [[ ! -x "${BINARY}" ]]; then
    echo "SKIP: native binary not found/executable at: ${BINARY}"
    echo "      Build it first (see docs/BUILDING.md), or pass --binary PATH."
    SKIPS=$((SKIPS + 1))
else
    if python3 tools/settings_schema_check.py --binary "${BINARY}"; then
        echo "PASS: settings schema introspection"
    else
        echo "FAIL: settings schema introspection reported violations"
        FAILURES=$((FAILURES + 1))
    fi
fi

# ---------------------------------------------------------------------------
# Lane 3: config round-trip/reset guard (needs build/ge007, no ROM)
# ---------------------------------------------------------------------------
echo ""
echo "--- [3/5] config: round-trip/reset guard ---"
if ! command -v python3 >/dev/null 2>&1; then
    echo "SKIP: python3 not found (required for config round-trip check)"
    SKIPS=$((SKIPS + 1))
elif [[ ! -f "tools/config_roundtrip_check.py" ]]; then
    echo "SKIP: tools/config_roundtrip_check.py not present"
    SKIPS=$((SKIPS + 1))
elif [[ ! -x "${BINARY}" ]]; then
    echo "SKIP: native binary not found/executable at: ${BINARY}"
    echo "      Build it first (see docs/BUILDING.md), or pass --binary PATH."
    SKIPS=$((SKIPS + 1))
else
    if python3 tools/config_roundtrip_check.py --binary "${BINARY}"; then
        echo "PASS: config round-trip/reset guard"
    else
        echo "FAIL: config round-trip/reset guard reported violations"
        FAILURES=$((FAILURES + 1))
    fi
fi

# ---------------------------------------------------------------------------
# Lane 4: enum/string schema type harness (needs python3 + C compiler, no ROM)
# ---------------------------------------------------------------------------
echo ""
echo "--- [4/5] config: enum/string schema types ---"
if ! command -v python3 >/dev/null 2>&1; then
    echo "SKIP: python3 not found (required for enum/string schema check)"
    SKIPS=$((SKIPS + 1))
elif [[ ! -f "tools/config_schema_types_check.py" ]]; then
    echo "SKIP: tools/config_schema_types_check.py not present"
    SKIPS=$((SKIPS + 1))
elif ! command -v "${CC:-cc}" >/dev/null 2>&1; then
    echo "SKIP: C compiler not found: ${CC:-cc}"
    SKIPS=$((SKIPS + 1))
else
    if python3 tools/config_schema_types_check.py; then
        echo "PASS: enum/string schema types"
    else
        echo "FAIL: enum/string schema check reported violations"
        FAILURES=$((FAILURES + 1))
    fi
fi

# ---------------------------------------------------------------------------
# Lane 5: short boot/spawn smoke (needs build/ge007 + ROM)
# ---------------------------------------------------------------------------
echo ""
echo "--- [5/5] boot: spawn health smoke ---"
if [[ "${RUN_SPAWN}" -eq 0 ]]; then
    echo "SKIP: --no-spawn requested"
    SKIPS=$((SKIPS + 1))
elif [[ ! -f "tools/spawn_health_check.sh" ]]; then
    echo "SKIP: tools/spawn_health_check.sh not present"
    SKIPS=$((SKIPS + 1))
elif [[ ! -x "${BINARY}" ]]; then
    echo "SKIP: native binary not found/executable at: ${BINARY}"
    echo "      Build it first (see docs/BUILDING.md), or pass --binary PATH."
    SKIPS=$((SKIPS + 1))
elif [[ ! -f "${ROM}" ]]; then
    echo "SKIP: ROM not found at: ${ROM}"
    echo "      Provide your GoldenEye ROM as baserom.u.z64, or pass --rom PATH."
    SKIPS=$((SKIPS + 1))
else
    echo "Using binary: ${BINARY}"
    echo "Using ROM:    ${ROM}"
    if ./tools/spawn_health_check.sh --no-build --binary "${BINARY}" --rom "${ROM}"; then
        echo "PASS: spawn health smoke"
    else
        echo "FAIL: spawn health smoke reported failing levels"
        FAILURES=$((FAILURES + 1))
    fi
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo ""
echo "=================================================="
echo " Summary: ${FAILURES} failed, ${SKIPS} skipped"
echo "=================================================="
if [[ "${FAILURES}" -gt 0 ]]; then
    exit 1
fi
echo "OK"
exit 0
