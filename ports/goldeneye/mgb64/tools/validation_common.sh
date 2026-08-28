#!/bin/bash
#
# Shared helpers for GoldenEye PC validation scripts.
#

set -euo pipefail

validation_repo_root() {
    local script_path script_dir oldpwd

    script_path="${BASH_SOURCE[0]}"
    script_dir="${script_path%/*}"

    if [[ "$script_dir" == "$script_path" ]]; then
        script_dir="."
    fi

    oldpwd="$PWD"
    cd "$script_dir/.." || return 1
    pwd
    cd "$oldpwd" || return 1
}

validation_default_build_dir() {
    printf '%s\n' "build"
}

validation_default_rom() {
    local root
    root="$(validation_repo_root)"
    printf '%s\n' "${root}/baserom.u.z64"
}

validation_resolve_path() {
    local path="$1"
    local root
    root="$(validation_repo_root)"

    if [[ "$path" = /* ]]; then
        printf '%s\n' "$path"
    else
        printf '%s/%s\n' "$root" "$path"
    fi
}

validation_binary_path() {
    local build_dir="$1"
    local resolved_build_dir
    resolved_build_dir="$(validation_resolve_path "$build_dir")"
    printf '%s/ge007\n' "$resolved_build_dir"
}

validation_resolve_timeout_cmd() {
    if command -v timeout >/dev/null 2>&1; then
        printf '%s\n' "timeout"
    elif command -v gtimeout >/dev/null 2>&1; then
        printf '%s\n' "gtimeout"
    else
        printf '%s\n' ""
    fi
}

validation_run_with_timeout() {
    local seconds="$1"
    shift

    local timeout_bin
    timeout_bin="$(validation_resolve_timeout_cmd)"

    if [[ -n "$timeout_bin" ]]; then
        "$timeout_bin" --kill-after=5 "$seconds" "$@"
    else
        "$@"
    fi
}

validation_silent_audio_enabled() {
    [[ "${GE007_VALIDATION_LIVE_AUDIO:-0}" != "1" ]]
}

validation_silent_audio_driver() {
    printf '%s\n' "${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}"
}

validation_add_silent_audio_env() {
    if validation_silent_audio_enabled; then
        export SDL_AUDIODRIVER
        export GE007_MUTE
        SDL_AUDIODRIVER="$(validation_silent_audio_driver)"
        GE007_MUTE=1
    fi
}

validation_automation_env() {
    local audio_env=()

    if validation_silent_audio_enabled; then
        audio_env+=(SDL_AUDIODRIVER="$(validation_silent_audio_driver)" GE007_MUTE=1)
    fi

    env -u GE007_DEBUG "${audio_env[@]}" GE007_DETERMINISTIC_STABLE_COUNT=1 GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 "$@"
}

validation_build_jobs() {
    local jobs="${GE007_BUILD_JOBS:-4}"

    if [[ ! "$jobs" =~ ^[1-9][0-9]*$ ]]; then
        echo "FAIL: GE007_BUILD_JOBS must be a positive integer, got: $jobs" >&2
        exit 2
    fi

    printf '%s\n' "$jobs"
}

validation_configure_build() {
    local build_dir="$1"
    shift

    local root
    local resolved_build_dir
    root="$(validation_repo_root)"
    resolved_build_dir="$(validation_resolve_path "$build_dir")"

    if [[ ! -f "${resolved_build_dir}/Makefile" && ! -f "${resolved_build_dir}/build.ninja" ]]; then
        cmake -S "$root" -B "${resolved_build_dir}" "$@"
    fi
}

validation_build() {
    local build_dir="$1"
    shift

    local jobs
    local resolved_build_dir
    jobs="$(validation_build_jobs)"
    resolved_build_dir="$(validation_resolve_path "$build_dir")"
    cmake --build "${resolved_build_dir}" --parallel "$jobs" "$@"
}

validation_require_file() {
    local path="$1"
    local label="$2"

    if [[ ! -e "$path" ]]; then
        echo "FAIL: ${label} not found: $path" >&2
        exit 2
    fi
}

validation_require_binary() {
    local binary="$1"
    validation_require_file "$binary" "binary"

    if [[ ! -x "$binary" ]]; then
        echo "FAIL: binary is not executable: $binary" >&2
        exit 2
    fi
}

validation_runtime_lock_dir() {
    printf '%s\n' "/tmp/ge007_runtime_validation.lock"
}

validation_acquire_runtime_lock() {
    local timeout_seconds="${1:-120}"
    local lock_dir
    local pid_file
    local owner_pid
    local waited=0

    lock_dir="$(validation_runtime_lock_dir)"
    pid_file="${lock_dir}/pid"

    while ! mkdir "$lock_dir" 2>/dev/null; do
        owner_pid=""

        if [[ -f "$pid_file" ]]; then
            owner_pid="$({ tr -d '[:space:]' < "$pid_file"; } 2>/dev/null || true)"
        fi

        if [[ -z "$owner_pid" ]]; then
            if [[ "$waited" -ge "$timeout_seconds" ]]; then
                echo "FAIL: timed out waiting for validation runtime lock owner: $lock_dir" >&2
                exit 2
            fi
            sleep 1
            waited=$((waited + 1))
            continue
        fi

        if ! kill -0 "$owner_pid" 2>/dev/null; then
            if [[ "$({ tr -d '[:space:]' < "$pid_file"; } 2>/dev/null || true)" == "$owner_pid" ]]; then
                rm -f "$pid_file"
                rmdir "$lock_dir" 2>/dev/null || true
            fi
            continue
        fi

        if [[ "$waited" -ge "$timeout_seconds" ]]; then
            echo "FAIL: timed out waiting for validation runtime lock: $lock_dir" >&2
            exit 2
        fi
        sleep 1
        waited=$((waited + 1))
    done

    printf '%s\n' "$$" > "${lock_dir}/pid"
}

validation_release_runtime_lock() {
    local lock_dir
    local pid_file
    local owner_pid

    lock_dir="$(validation_runtime_lock_dir)"
    pid_file="${lock_dir}/pid"

    owner_pid="$({ tr -d '[:space:]' < "$pid_file"; } 2>/dev/null || true)"

    if [[ "$owner_pid" == "$$" ]]; then
        rm -f "$pid_file"
        rmdir "$lock_dir" 2>/dev/null || true
    fi
}
