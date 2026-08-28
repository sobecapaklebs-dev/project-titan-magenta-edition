#!/usr/bin/env bash
set -uo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}
STAMP=$(date -u +%Y%m%dT%H%M%SZ)
LOG_ROOT=${TITAN_DIAG_LOG_DIR:-"$ROOT/build/target-diagnostics/$STAMP"}
KEEP_WORK=${TITAN_DIAG_KEEP_WORK:-0}

usage() {
    cat <<'USAGE'
Usage: bash scripts/build_all.sh --diagnostic-sweep

Runs a non-release diagnostic build sweep. Unlike the normal build, this mode:
  * continues after failed independent stages;
  * uses GNU make -k for target compilation where safe;
  * writes one log per stage plus a combined log;
  * extracts compiler/linker errors, warnings and undefined symbols;
  * exits non-zero only after the sweep is complete if any stage failed.

Environment:
  JOBS=N                    Parallel make jobs.
  TITAN_DIAG_LOG_DIR=PATH   Override the log directory.
  TITAN_DIAG_KEEP_WORK=1    Preserve the disposable source snapshot.
USAGE
}

if [[ ${1:-} == --help || ${1:-} == -h ]]; then
    usage
    exit 0
fi
if (($#)); then
    echo "Unknown diagnostic-sweep option: $1" >&2
    usage >&2
    exit 2
fi

if [[ -z "${PS2DEV:-}" || -z "${PS2SDK:-}" ]]; then
    echo "PS2DEV and PS2SDK must be exported for --diagnostic-sweep." >&2
    exit 2
fi

mkdir -p "$LOG_ROOT/stages"
SUMMARY="$LOG_ROOT/summary.tsv"
FULL_LOG="$LOG_ROOT/full.log"
: > "$SUMMARY"
: > "$FULL_LOG"
printf 'stage\tstatus\texit_code\tlog\n' >> "$SUMMARY"

stage_index=0
failures=0
passes=0
skips=0

declare -A STAGE_RC=()

run_stage() {
    local name=$1
    shift
    local safe idx log rc
    safe=${name//[^A-Za-z0-9_.-]/_}
    stage_index=$((stage_index + 1))
    printf -v idx '%02d' "$stage_index"
    log="$LOG_ROOT/stages/${idx}-${safe}.log"

    printf '\n===== [%s] %s =====\n' "$idx" "$name" | tee -a "$FULL_LOG"
    printf 'COMMAND:' | tee -a "$FULL_LOG"
    printf ' %q' "$@" | tee -a "$FULL_LOG"
    printf '\n' | tee -a "$FULL_LOG"

    set +e
    "$@" > >(tee -a "$log" "$FULL_LOG") \
          2> >(tee -a "$log" "$FULL_LOG" >&2)
    rc=$?
    set +e

    STAGE_RC["$name"]=$rc
    if ((rc == 0)); then
        passes=$((passes + 1))
        printf '%s\tPASS\t0\t%s\n' "$name" "$log" >> "$SUMMARY"
        printf '===== RESULT %s: PASS =====\n' "$name" | tee -a "$FULL_LOG"
    else
        failures=$((failures + 1))
        printf '%s\tFAIL\t%d\t%s\n' "$name" "$rc" "$log" >> "$SUMMARY"
        printf '===== RESULT %s: FAIL rc=%d =====\n' "$name" "$rc" | tee -a "$FULL_LOG"
    fi
    return 0
}

skip_stage() {
    local name=$1 reason=$2 idx safe log
    safe=${name//[^A-Za-z0-9_.-]/_}
    stage_index=$((stage_index + 1))
    printf -v idx '%02d' "$stage_index"
    log="$LOG_ROOT/stages/${idx}-${safe}.log"
    printf 'SKIP: %s\n' "$reason" | tee "$log" | tee -a "$FULL_LOG"
    printf '%s\tSKIP\t-\t%s\n' "$name" "$log" >> "$SUMMARY"
    STAGE_RC["$name"]=125
    skips=$((skips + 1))
}

stage_ok() {
    [[ ${STAGE_RC[$1]:-125} -eq 0 ]]
}

snapshot_source_tree() {
    local destination=$1
    tar -C "$ROOT" \
        --exclude='./.git' \
        --exclude='./build' \
        --exclude='./dist' \
        --exclude='./bootloader/generated' \
        --exclude='./bootloader/stage2/generated' \
        --exclude='./kernel/src/graphics/generated' \
        --exclude='./kernel/src/services/generated' \
        --exclude='./apps/project-nes/build' \
        --exclude='./apps/wlaunchelf/build' \
        --exclude='./ports/goldeneye/build' \
        --exclude='__pycache__' \
        --exclude='*.pyc' \
        --exclude='*.o' \
        --exclude='*.map' \
        --exclude='*.bak' \
        --exclude='*.orig' \
        --exclude='*.rej' \
        -cf - . | tar -C "$destination" -xf -
}

extract_diagnostics() {
    local errors="$LOG_ROOT/errors.txt"
    local warnings="$LOG_ROOT/warnings.txt"
    local undef="$LOG_ROOT/undefined-symbols.txt"
    local unique="$LOG_ROOT/errors.unique.txt"

    grep -nEi \
        'fatal error:|(^|[[:space:]])error:|undefined reference|No rule to make target|collect2: error|ld returned [0-9]+ exit status|iopfixup.*(error|undefined)|make(\[[0-9]+\])?: \*\*\*' \
        "$FULL_LOG" > "$errors" || :
    grep -nEi 'warning:' "$FULL_LOG" > "$warnings" || :
    grep -Ei 'undefined reference to [`'"'"'][^`'"'"']+' "$FULL_LOG" \
        | sed -E 's/.*undefined reference to [`'"'"']([^`'"'"']+).*/\1/' \
        | sort -u > "$undef" || :
    sed -E 's#^[0-9]+:##' "$errors" | awk '!seen[$0]++' > "$unique"

    diag_total_errors=$(wc -l < "$errors" | tr -d ' ')
    diag_unique_errors=$(wc -l < "$unique" | tr -d ' ')
    diag_undefined_symbols=$(wc -l < "$undef" | tr -d ' ')
    diag_failed_stage_list=$(awk -F '\t' '$2 == "FAIL" { if (out != "") out = out ","; out = out $1 } END { print out }' "$SUMMARY")

    {
        echo '# Project Titan target diagnostic sweep'
        echo
        printf -- '- UTC stamp: `%s`\n' "$STAMP"
        printf -- '- PASS stages: `%d`\n' "$passes"
        printf -- '- FAIL stages: `%d`\n' "$failures"
        printf -- '- SKIP stages: `%d`\n' "$skips"
        printf -- '- Total extracted error lines: `%s`\n' "$diag_total_errors"
        printf -- '- Unique extracted error lines: `%s`\n' "$diag_unique_errors"
        printf -- '- Undefined symbols: `%s`\n' "$diag_undefined_symbols"
        printf -- '- Failed stage list: `%s`\n' "${diag_failed_stage_list:-none}"
        echo
        echo '## Stage results'
        echo
        echo '```text'
        column -t -s $'\t' "$SUMMARY" 2>/dev/null || cat "$SUMMARY"
        echo '```'
        echo
        echo '## Files'
        echo
        echo '- `full.log` — complete ordered output'
        echo '- `errors.txt` — all error-like lines with source line numbers in full.log'
        echo '- `errors.unique.txt` — exact duplicate error lines removed'
        echo '- `warnings.txt` — warnings only'
        echo '- `undefined-symbols.txt` — deduplicated linker symbols'
    } > "$LOG_ROOT/REPORT.md"
}

# Hard environment checks are logged but do not suppress host/source checks.
run_stage target-preflight python3 "$ROOT/tools/titan_target_preflight.py"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/titan-diag.XXXXXX")
if [[ "$KEEP_WORK" == 1 ]]; then
    printf 'Diagnostic work tree will be preserved: %s\n' "$WORK" | tee -a "$FULL_LOG"
else
    trap 'rm -rf "$WORK"' EXIT
fi
snapshot_source_tree "$WORK"

export LC_ALL=C TZ=UTC

# Host/source gate. A failure here is recorded, but target compilation still
# runs because the purpose of this mode is to expose as many independent
# target blockers as possible in one invocation.
run_stage source-validation bash "$WORK/scripts/validate_source.sh"

# Each target component is independent at this level. GNU make -k continues
# compiling unrelated objects after an object failure, exposing multiple C/ASM
# errors in a single component instead of stopping at the first one.
run_stage bootloader-target \
    make --output-sync=recurse -k -j"$JOBS" -C "$WORK/bootloader" all

run_stage kernel-target \
    make --output-sync=recurse -k -j"$JOBS" -C "$WORK/kernel" all

run_stage project-nes-host \
    make --output-sync=recurse -k -C "$WORK/apps/project-nes" host-test
run_stage project-nes-target \
    make --output-sync=recurse -k -j"$JOBS" -C "$WORK/apps/project-nes" all verify \
    EE_CC="$PS2DEV/ee/bin/mips64r5900el-ps2-elf-gcc"

run_stage wlaunchelf-host \
    make --output-sync=recurse -k -C "$WORK/apps/wlaunchelf" host-test
run_stage wlaunchelf-target \
    make --output-sync=recurse -k -j"$JOBS" -C "$WORK/apps/wlaunchelf" all verify \
    EE_CC="$PS2DEV/ee/bin/mips64r5900el-ps2-elf-gcc"

run_stage neutrino-target \
    make --output-sync=recurse -k -j"$JOBS" -C "$WORK/modules/titan-neutrino-engine" all \
    GIT_TAG="titan-$(cat "$WORK/VERSION")"
run_stage neutrino-loader-target \
    make --output-sync=recurse -k -j"$JOBS" -C "$WORK/modules/titan-neutrino-engine/ee/loader" all DEBUG=0 \
    GIT_TAG="titan-$(cat "$WORK/VERSION")"
if stage_ok neutrino-target && stage_ok neutrino-loader-target; then
    run_stage neutrino-copy \
        make --output-sync=recurse -k -C "$WORK/modules/titan-neutrino-engine" copy \
        GIT_TAG="titan-$(cat "$WORK/VERSION")"
else
    skip_stage neutrino-copy 'neutrino target and/or loader failed'
fi

# MAKEFLAGS=-k propagates keep-going semantics into the nested GNU make calls
# made by the maintained UL build wrapper while retaining its real gsKit setup.
run_stage titan-ul-target \
    env MAKEFLAGS=-k TITAN_SOURCE_COMMIT=diagnostic-sweep JOBS="$JOBS" \
    bash "$WORK/scripts/build_titan_ul_engine.sh"

# FMCB is independent of the runtime kernel and therefore still gets compiled
# even when an earlier target stage fails.
run_stage fmcb-integrated \
    python3 "$WORK/tools/build_titan_fmcb.py" --project "$WORK" --output "$WORK/build/fmcb"

# The installer shares the kernel EE binary. Run it when the kernel and FMCB
# stages are usable; otherwise skip it rather than producing dependency noise.
if stage_ok kernel-target && stage_ok fmcb-integrated; then
    run_stage titan-installer \
        make --output-sync=recurse -k -j"$JOBS" -C "$WORK/kernel" installer \
        TITAN_AR9271_FW="${TITAN_AR9271_FW:-}"
else
    skip_stage titan-installer 'kernel-target and/or fmcb-integrated failed'
fi

assemble_runtime() {
    local output="$WORK/build/diagnostic-runtime"
    local loader="$WORK/modules/titan-neutrino-engine/ee/loader"
    rm -rf "$output"
    mkdir -p "$output/TITAN/audio" "$output/TITAN/packages" "$output/neutrino"
    cp "$WORK/bootloader/titan.elf" "$output/titan.elf"
    cp "$WORK/kernel/kernel.elf" "$output/kernel.elf"
    cp "$loader/neutrino.elf" "$output/neutrino/neutrino.elf"
    cp -R "$loader/config" "$output/neutrino/"
    cp -R "$loader/modules" "$output/neutrino/"
    cp "$WORK/build/modules/titan-ul-engine/OPNPS2LD.ELF" "$output/TITAN/titan_ul_engine.elf"
    cp -R "$WORK/runtime/TITAN/audio/." "$output/TITAN/audio/"
    cp "$WORK/apps/project-nes/build/PROJECTNES.pkg" "$output/TITAN/packages/PROJECTNES.pkg"
    cp "$WORK/apps/wlaunchelf/build/WLAUNCHELF.pkg" "$output/TITAN/packages/WLAUNCHELF.pkg"
    find "$output" -type f -print0 | sort -z | xargs -0 sha256sum > "$output/DIAGNOSTIC_MANIFEST.txt"
}

# Packaging is dependency-only and intentionally runs only after all required
# compiled artifacts exist. Cascading "file missing" failures would hide the
# compiler/linker blockers this mode is designed to collect.
if stage_ok bootloader-target && stage_ok kernel-target && \
   stage_ok project-nes-target && stage_ok wlaunchelf-target && \
   stage_ok neutrino-copy && stage_ok titan-ul-target; then
    run_stage runtime-assemble assemble_runtime
else
    skip_stage runtime-assemble 'one or more runtime target components failed'
fi

if stage_ok runtime-assemble && stage_ok titan-installer && stage_ok fmcb-integrated; then
    run_stage installer-media \
        python3 "$WORK/tools/build_titan_installer_media.py" \
        --project "$WORK" \
        --runtime "$WORK/build/diagnostic-runtime" \
        --installer-elf "$WORK/titan-installer.elf" \
        --bootloader-elf "$WORK/bootloader/titan.elf" \
        --fmcb-build "$WORK/build/fmcb" \
        --output "$WORK/build/titan-installer-usb"
else
    skip_stage installer-media 'runtime, installer and/or FMCB backend unavailable'
fi

extract_diagnostics

DIAG_BUNDLE="${LOG_ROOT}.tar.gz"
if tar -czf "$DIAG_BUNDLE" -C "$(dirname "$LOG_ROOT")" "$(basename "$LOG_ROOT")"; then
    :
else
    DIAG_BUNDLE=""
    printf 'WARNING: failed to create diagnostic bundle\n' >&2
fi

printf '\n============================================================\n'
printf ' Project Titan target diagnostic sweep complete\n'
printf '============================================================\n'
printf 'Logs: %s\n' "$LOG_ROOT"
printf 'PASS=%d FAIL=%d SKIP=%d\n' "$passes" "$failures" "$skips"
printf 'FAILED_STAGES=%d\n' "$failures"
printf 'FAILED_STAGE_LIST=%s\n' "${diag_failed_stage_list:-none}"
printf 'TOTAL_ERRORS=%s\n' "${diag_total_errors:-0}"
printf 'UNIQUE_ERRORS=%s\n' "${diag_unique_errors:-0}"
printf 'UNDEFINED_SYMBOLS=%s\n' "${diag_undefined_symbols:-0}"
printf 'Summary: %s\n' "$SUMMARY"
printf 'Errors: %s\n' "$LOG_ROOT/errors.unique.txt"
printf 'Undefined symbols: %s\n' "$LOG_ROOT/undefined-symbols.txt"
printf 'Report: %s\n' "$LOG_ROOT/REPORT.md"
if [[ -n "${DIAG_BUNDLE:-}" ]]; then
    printf 'Bundle: %s\n' "$DIAG_BUNDLE"
fi
if [[ "$KEEP_WORK" == 1 ]]; then
    printf 'Work tree: %s\n' "$WORK"
fi

((failures == 0)) || exit 1
exit 0
