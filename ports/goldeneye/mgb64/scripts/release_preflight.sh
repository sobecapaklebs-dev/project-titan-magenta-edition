#!/usr/bin/env bash
#
# release_preflight.sh -- run the maintainer launch gate from one command.
#
# This is intentionally stricter than ordinary contributor validation. It
# checks a clean tracked tree, release hygiene, warning-clean native build,
# ROM-free tests, Dockerfile context parsing, source-archive packaging, and
# optional ROM-backed runtime, macOS app, and GitHub metadata/ref gates.
#
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

jobs="${GE007_BUILD_JOBS:-4}"
build_dir="build"
build_type="Release"
max_warnings=0
rom="baserom.u.z64"
binary=""
require_rom_smoke=0
deep_runtime=0
run_macos_app=0
macos_app_bundle_sdl2=0
macos_app_strict_deployment_target=0
skip_docker_check=0
run_github_check=0
github_allow_private=0
github_repo=""
strict_ignored=0

usage() {
  cat <<'USAGE'
Usage: scripts/release_preflight.sh [options]

Runs the local public-release preflight:
  - require a clean non-ignored working tree
  - run release hygiene / provenance / documentation guards
  - configure and build the native port
  - fail on build warnings (default threshold: 0)
  - run ROM-free CTest
  - run quick validation, optionally requiring a ROM-backed smoke
  - run Dockerfile/.dockerignore static build-context check
  - create and smoke-test the public source archive with the same warning gate
  - optionally build and asset-audit the unsigned local macOS app bundle

Options:
  --jobs N                Parallel build jobs (default: GE007_BUILD_JOBS or 4).
  --build-dir DIR         CMake build directory (default: build).
  --build-type TYPE       CMake build type (default: Release).
  --max-warnings N        Fail if build/archive warning count exceeds N (default: 0).
  --rom PATH              ROM path for runtime smoke (default: baserom.u.z64).
  --binary PATH           Native binary path for runtime smoke (default: build/ge007).
  --require-rom-smoke     Fail if a ROM-backed quick validation cannot run.
  --deep-runtime          Also run route contract, all-level spawn health,
                          playability, renderer parity, and save persistence.
                          Implies --require-rom-smoke.
  --macos-app             Build build-macos/MGB64.app and verify the app bundle
                          and engine library are asset-free.
  --macos-app-bundle-sdl2 Copy the linked SDL2 dylib into the local .app and
                          rewrite the executable load path. Use with signing
                          and notarization work, not source-only launch proof.
  --macos-app-strict-deployment-target
                          Fail if the local SDL2 dylib requires a newer macOS
                          version than the app build's requested deployment
                          target.
  --strict-ignored        Fail if ignored ROM/media/capture artifacts are present
                          outside normal build output. Use for final launch
                          from a fresh or scrubbed checkout. If runtime smoke is
                          required, pass --rom outside the repository checkout.
  --skip-docker-check     Skip docker build --check . (not for final launch).
  --github                Also run scripts/check_github_launch_ready.sh for
                          GitHub metadata/ref/artifact gates. This does not use
                          hosted CI as a launch gate.
  --allow-private         Pass --allow-private to the GitHub readiness check.
  --repo OWNER/REPO       Pass a repository to the GitHub readiness check.
  -h, --help              Show this help.

For the final pre-public macOS dry run, use at least:
  scripts/release_preflight.sh --deep-runtime --rom /path/outside/repo/baserom.u.z64 --macos-app-bundle-sdl2 --strict-ignored --github --allow-private

After the repository is public, repeat the GitHub readiness check without
--allow-private.

For a redistributable macOS app candidate, add
--macos-app-strict-deployment-target and point pkg-config at a controlled SDL2
build with the intended minimum macOS version.
USAGE
}

abs_path() {
  case "$1" in
    /*) printf '%s\n' "$1" ;;
    *) printf '%s/%s\n' "$(pwd)" "$1" ;;
  esac
}

canonicalish_path() {
  local path="$1"
  local dir
  local base

  dir="$(dirname "$path")"
  base="$(basename "$path")"
  if [ -d "$dir" ]; then
    printf '%s/%s\n' "$(cd "$dir" && pwd -P)" "$base"
  else
    printf '%s\n' "$path"
  fi
}

path_is_inside_repo() {
  local path
  local repo

  path="$(canonicalish_path "$1")"
  repo="$(pwd -P)"
  case "$path" in
    "$repo"|"$repo"/*) return 0 ;;
  esac
  return 1
}

require_non_negative_int() {
  local name="$1"
  local value="$2"
  case "$value" in
    ''|*[!0-9]*)
      echo "${name} must be a non-negative integer, got: ${value}" >&2
      exit 2
      ;;
  esac
}

require_positive_int() {
  local name="$1"
  local value="$2"
  require_non_negative_int "$name" "$value"
  if [ "$value" -eq 0 ]; then
    echo "${name} must be a positive integer, got: ${value}" >&2
    exit 2
  fi
}

section() {
  echo
  echo "== $* =="
}

run() {
  printf '+'
  printf ' %q' "$@"
  printf '\n'
  "$@"
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --jobs)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      jobs="$2"
      shift 2
      ;;
    --build-dir)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      build_dir="$2"
      shift 2
      ;;
    --build-type)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      build_type="$2"
      shift 2
      ;;
    --max-warnings)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      max_warnings="$2"
      shift 2
      ;;
    --rom)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      rom="$2"
      shift 2
      ;;
    --binary)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      binary="$2"
      shift 2
      ;;
    --require-rom-smoke)
      require_rom_smoke=1
      shift
      ;;
    --deep-runtime)
      require_rom_smoke=1
      deep_runtime=1
      shift
      ;;
    --macos-app)
      run_macos_app=1
      shift
      ;;
    --macos-app-bundle-sdl2)
      run_macos_app=1
      macos_app_bundle_sdl2=1
      shift
      ;;
    --macos-app-strict-deployment-target)
      run_macos_app=1
      macos_app_strict_deployment_target=1
      shift
      ;;
    --strict-ignored)
      strict_ignored=1
      shift
      ;;
    --skip-docker-check)
      skip_docker_check=1
      shift
      ;;
    --github)
      run_github_check=1
      shift
      ;;
    --allow-private)
      github_allow_private=1
      shift
      ;;
    --repo)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      github_repo="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      usage >&2
      echo "Unknown arg: $1" >&2
      exit 2
      ;;
  esac
done

require_positive_int "--jobs" "$jobs"
require_non_negative_int "--max-warnings" "$max_warnings"

build_dir="$(abs_path "$build_dir")"
rom="$(abs_path "$rom")"
if [ -z "$binary" ]; then
  binary="${build_dir}/ge007"
else
  binary="$(abs_path "$binary")"
fi

section "Release preflight configuration"
echo "  build dir:     $build_dir"
echo "  build type:    $build_type"
echo "  jobs:          $jobs"
echo "  max warnings:  $max_warnings"
echo "  runtime ROM:   $rom"
echo "  runtime binary: $binary"
if [ "$run_macos_app" -eq 1 ]; then
  echo "  macOS app:      enabled"
  if [ "$macos_app_bundle_sdl2" -eq 1 ]; then
    echo "  macOS SDL2:     bundled"
  fi
  if [ "$macos_app_strict_deployment_target" -eq 1 ]; then
    echo "  macOS target:   strict"
  fi
fi
if [ "$strict_ignored" -eq 1 ]; then
  echo "  strict ignored: enabled"
fi

if [ "$strict_ignored" -eq 1 ] && [ "$require_rom_smoke" -eq 1 ] && path_is_inside_repo "$rom"; then
  echo "--strict-ignored with ROM-backed validation requires --rom outside the repository checkout." >&2
  echo "Use a fresh/scrubbed checkout plus an external ROM path, for example:" >&2
  echo "  scripts/release_preflight.sh --deep-runtime --rom /path/outside/repo/baserom.u.z64 --macos-app-bundle-sdl2 --strict-ignored --github --allow-private" >&2
  exit 2
fi

section "Clean tracked tree"
dirty="$(git status --porcelain --untracked-files=all)"
if [ -n "$dirty" ]; then
  echo "Release preflight requires a clean non-ignored working tree:" >&2
  printf '%s\n' "$dirty" >&2
  exit 1
fi
echo "  OK -- no tracked or untracked non-ignored changes."

ignored_preview="$(git clean -ndX | sed -n '1,40p')"
if [ -n "$ignored_preview" ]; then
  echo "  Ignored local artifacts present; review before launch:"
  printf '%s\n' "$ignored_preview" | sed 's/^/    /'
else
  echo "  OK -- no ignored artifacts reported by git clean -ndX."
fi

if [ "$strict_ignored" -eq 1 ]; then
  section "High-risk ignored artifact gate"
  run ./scripts/ci/check_high_risk_ignored_artifacts.sh
fi

section "Release hygiene"
run ./scripts/ci/check_release_ready.sh

section "Configure native build"
run cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE="$build_type"

section "Build native port"
build_log="${TMPDIR:-/tmp}/mgb64-release-preflight-build.log"
warning_json="${TMPDIR:-/tmp}/mgb64-release-preflight-build-warnings.json"
run cmake --build "$build_dir" --parallel "$jobs" 2>&1 | tee "$build_log"

section "Build warning gate"
run python3 tools/summarize_build_warnings.py \
  "$build_log" \
  --json-out "$warning_json" \
  --max-total "$max_warnings"
echo "  warning summary: $warning_json"

section "ROM-free CTest"
run ctest --test-dir "$build_dir" --output-on-failure

section "Quick validation"
quick_args=(--binary "$binary" --rom "$rom")
if [ "$require_rom_smoke" -eq 1 ]; then
  if [ ! -x "$binary" ]; then
    echo "Required runtime binary is missing or not executable: $binary" >&2
    exit 1
  fi
  if [ ! -f "$rom" ]; then
    echo "Required runtime ROM is missing: $rom" >&2
    exit 1
  fi
fi
run ./tools/validate_quick.sh "${quick_args[@]}"

if [ "$deep_runtime" -eq 1 ]; then
  section "Deep ROM-backed runtime validation"
  run ./tools/route_contract_smoke.sh --native-smoke --all --no-build --binary "$binary" --rom "$rom"
  run ./tools/spawn_health_check.sh --all --no-build --binary "$binary" --rom "$rom"
  run ./tools/playability_smoke.sh --all --no-build --binary "$binary" --rom "$rom"
  run ./tools/renderer_parity_capture.sh --no-build --binary "$binary" --rom "$rom"
  run ./tools/save_persistence_check.sh --no-build --binary "$binary" --rom "$rom"
fi

if [ "$skip_docker_check" -eq 0 ]; then
  section "Docker build-context check"
  run docker build --check .
else
  section "Docker build-context check"
  echo "  SKIP -- --skip-docker-check requested."
fi

section "Public source archive"
run scripts/make_public_source_archive.sh --force
archive="dist/mgb64-$(git rev-parse --short=12 HEAD).tar.gz"
run scripts/smoke_public_source_archive.sh "$archive" --jobs "$jobs" --max-warnings "$max_warnings"

if [ "$run_macos_app" -eq 1 ]; then
  section "macOS app bundle asset gate"
  if [ "$(uname -s)" != "Darwin" ]; then
    echo "--macos-app requires macOS/Darwin." >&2
    exit 1
  fi
  macos_build_mode="--release"
  if [ "$build_type" = "Debug" ]; then
    macos_build_mode="--debug"
  fi
  run ./macos/Scripts/build_universal.sh "$macos_build_mode" --build-dir build-macos-universal
  run ./macos/Scripts/verify_asset_free.sh build-macos-universal/libge007_lib.a
  macos_app_args=(
    ./macos/Scripts/build_app_bundle.sh
    "$macos_build_mode"
    --build-dir build-macos-app
    --output build-macos-app/MGB64.app
  )
  if [ "$macos_app_bundle_sdl2" -eq 1 ]; then
    macos_app_args+=(--bundle-sdl2)
  fi
  if [ "$macos_app_strict_deployment_target" -eq 1 ]; then
    macos_app_args+=(--strict-deployment-target)
  fi
  run "${macos_app_args[@]}"
  run ./macos/Scripts/verify_asset_free.sh build-macos-app/libge007_lib.a
  run ./macos/Scripts/verify_asset_free.sh build-macos-app/MGB64.app
fi

if [ "$run_github_check" -eq 1 ]; then
  section "GitHub launch readiness"
  github_args=()
  if [ -n "$github_repo" ]; then
    github_args+=(--repo "$github_repo")
  fi
  if [ "$github_allow_private" -eq 1 ]; then
    github_args+=(--allow-private)
  fi
  run scripts/check_github_launch_ready.sh "${github_args[@]}"
fi

section "Final tracked tree check"
final_dirty="$(git status --porcelain --untracked-files=all)"
if [ -n "$final_dirty" ]; then
  echo "Release preflight left non-ignored working-tree changes:" >&2
  printf '%s\n' "$final_dirty" >&2
  exit 1
fi
echo "  OK -- working tree remains clean."

echo
echo "Release preflight passed."
