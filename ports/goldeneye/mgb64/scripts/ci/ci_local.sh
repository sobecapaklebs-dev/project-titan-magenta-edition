#!/usr/bin/env bash
#
# ci_local.sh — run the ROM-free CI gates locally.
#
# MGB64 runs a deliberate "local-CI posture": GitHub Actions hosted runners are
# not enabled by default (see .github/workflows/ci.yml), so contributors self-
# check before pushing rather than relying on PR automation. This script is the
# one-command version of that: it runs the same ROM-free gates the hosted CI
# workflow would (.github/workflows/ci.yml — the no-rom-data and linux-build
# jobs), needs no ROM or game data, and reports a single pass/fail summary.
#
# Fast pre-push subset only:   tools/validate_quick.sh
# Full pre-PR gate (this):     scripts/ci/ci_local.sh
#
# Usage:
#   scripts/ci/ci_local.sh [--build-dir DIR] [--no-build] [--jobs N]
#
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="build"
DO_BUILD=1
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

while [ $# -gt 0 ]; do
  case "$1" in
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --no-build)  DO_BUILD=0; shift ;;
    --jobs)      JOBS="$2"; shift 2 ;;
    -h|--help)   sed -n '2,20p' "$0"; exit 0 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

pass=0; fail=0; failed_steps=""

step() {
  local name="$1"; shift
  printf '\n\033[1m== %s ==\033[0m\n' "$name"
  if "$@"; then
    pass=$((pass + 1)); printf '\033[32m  PASS: %s\033[0m\n' "$name"
  else
    fail=$((fail + 1)); failed_steps="${failed_steps}\n  - ${name}"
    printf '\033[31m  FAIL: %s\033[0m\n' "$name"
  fi
}

# --- Release / provenance hygiene (the most important gate for a decomp) ---
step "Release hygiene (no ROM data)"   bash scripts/ci/check_release_ready.sh
step "Ignored-artifact hygiene"        bash scripts/ci/check_high_risk_ignored_artifacts.sh
step "Timing lock (R2)"                bash scripts/ci/check_timing_lock.sh
step "Static source validation"        bash tools/validate_quick.sh --no-spawn

# --- Build the native port (ROM-free) + warning gate ---
if [ "$DO_BUILD" -eq 1 ]; then
  step "CMake configure" cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
  build_one() {
    set -o pipefail
    cmake --build "$BUILD_DIR" --parallel "$JOBS" 2>&1 | tee "$BUILD_DIR/mgb64-build.log"
  }
  step "Build native port" build_one
  step "Build warnings (budget 0)" \
    python3 tools/summarize_build_warnings.py "$BUILD_DIR/mgb64-build.log" --max-total 0
fi

step "Sim/render separation (R1)"      bash scripts/ci/check_sim_render_separation.sh

# --- ROM-free test suite ---
if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
  step "ROM-free CTest suite"          ctest --test-dir "$BUILD_DIR" --output-on-failure
else
  echo "  (skipping ctest — no configured build at $BUILD_DIR; run without --no-build)"
fi

# --- Summary ---
printf '\n\033[1m== ci_local summary ==\033[0m\n'
printf '  passed: %d\n  failed: %d\n' "$pass" "$fail"
if [ "$fail" -ne 0 ]; then
  printf '\033[31mFAILED steps:%b\033[0m\n' "$failed_steps"
  exit 1
fi
printf '\033[32mAll ROM-free CI gates passed.\033[0m\n'
