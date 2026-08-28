#!/usr/bin/env bash
#
# audio_ab_gate.sh — Deterministic audio A/B gate (remaster W6.E1.T5).
#
# The enforcement machine every W6 audio task validates through. It proves, for a
# single opt-in audio flag, that:
#
#   (1) OFF lane   — with the flag at its identity/default value the current build
#                    is BYTE-IDENTICAL to the pre-change baseline binary (R3).
#   (2) ON lane    — with the flag on, the mix is not static/silent/blown-up
#                    (tools/compare_audio.py, RMS diff <=500 / ZCR / silence).
#   (3) clamp lane — the flag-on run does not introduce new clipping vs OFF
#                    (summed GE007_AUDIO_TRACE clamp deltas, ON <= OFF * 1.05).
#   (4) sim-hash   — [--sim-hash] the flag OFF vs ON yields an IDENTICAL whole-sim
#                    state hash (R1). This lane exists because
#                    tools/sim_invariance_gate.sh is hardcoded to Video.* toggles
#                    (sim_invariance_gate.sh:53-57) and cannot exercise audio flags;
#                    we replicate its run() pattern (:42-50) with --sim-state-hash-out.
#
# Usage:
#   tools/audio_ab_gate.sh --level N --frames N --flag Key=Value \
#       [--baseline-binary PATH] [--music-pack DIR] [--sfx-pack DIR] [--sim-hash]
#
#   --level N     Solo mission number (1-20; the doc's "--level 2" == mission 2 ==
#                 Facility) OR a stage slug (e.g. facility). An integer is booted
#                 with --mission N because the game now rejects small integers
#                 passed to --level (main_pc.c:620-637 guard).
#   --frames N    Deterministic screenshot/exit frame (== --screenshot-frame).
#   --flag K=V    The audio flag under test. A dotted key (Video.*/Audio.*) is
#                 applied via --config-override; a GE007_*/ALL_CAPS key is applied
#                 as an environment variable (H1 is env-only until W6.E1.T2 lands
#                 Audio.OutputFilter).
#   --baseline-binary PATH  Skip the merge-base rebuild; cmp against this binary.
#   --music-pack DIR / --sfx-pack DIR  Forward-compat hooks for the E4.T4 / E5.T3
#                 pack validation lanes (applied as Audio.MusicPack/SfxPack
#                 overrides on the ON lane). Inert until those keys are registered.
#   --sim-hash    Also run lane (4).
#
# Determinism & hygiene: every run is --deterministic in an isolated CWD (no
# ge007.ini drift) with SDL_AUDIODRIVER=dummy. All audio dumps/traces live in a
# private temp dir OUTSIDE the repo (never committed; R2). Set
# AUDIO_AB_GATE_KEEP_TMP=1 to retain that dir for debugging. Exit 0 = PASS.
#
# Robustness: when many SDL/GL runs are spawned back-to-back, a run can rarely
# receive a spurious quit and shut down before --screenshot-exit (truncated
# output). Each run is verified to reach screenshot-exit and retried up to 3x
# (cf. the GL-hang retry discipline); every COMPLETED deterministic run is
# bit-identical, so this never masks a real change.
#
# Seeded-fault self-test (the W6.E1.T5 acceptance): leaking an audio flag such as
# GE007_ENABLE_LIBAUDIO_LOWPASS=1 into the environment makes the OFF-lane current
# run diverge from the hermetic baseline, so lane (1)'s cmp FAILS the gate.
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"
ROOT="$(pwd)"

# --------------------------------------------------------------------------- #
# Arg parsing
# --------------------------------------------------------------------------- #
LEVEL=""; FRAMES=""; FLAG=""; BASELINE_BIN=""; MUSIC_PACK=""; SFX_PACK=""; SIM_HASH=0
usage() { sed -n '2,57p' "$0" >&2; exit 2; }

while [ $# -gt 0 ]; do
    case "$1" in
        --level)           LEVEL="${2:?--level needs a value}"; shift 2 ;;
        --frames)          FRAMES="${2:?--frames needs a value}"; shift 2 ;;
        --flag)            FLAG="${2:?--flag needs Key=Value}"; shift 2 ;;
        --baseline-binary) BASELINE_BIN="${2:?--baseline-binary needs a path}"; shift 2 ;;
        --music-pack)      MUSIC_PACK="${2:?--music-pack needs a dir}"; shift 2 ;;
        --sfx-pack)        SFX_PACK="${2:?--sfx-pack needs a dir}"; shift 2 ;;
        --sim-hash)        SIM_HASH=1; shift ;;
        -h|--help)         usage ;;
        *) echo "audio_ab_gate: unknown argument '$1'" >&2; usage ;;
    esac
done

[ -n "$LEVEL" ]  || { echo "audio_ab_gate: --level is required" >&2; usage; }
[ -n "$FRAMES" ] || { echo "audio_ab_gate: --frames is required" >&2; usage; }
[ -n "$FLAG" ]   || { echo "audio_ab_gate: --flag Key=Value is required" >&2; usage; }
case "$FLAG" in *=*) : ;; *) echo "audio_ab_gate: --flag must be Key=Value (got '$FLAG')" >&2; exit 2 ;; esac

ROM="${GE007_ROM:-$ROOT/baserom.u.z64}"
CURRENT_BIN="${GE007_BIN:-$ROOT/build/ge007}"
[ -e "$ROM" ]        || { echo "audio_ab_gate: ROM $ROM not found (local gate)" >&2; exit 2; }
[ -x "$CURRENT_BIN" ]|| { echo "audio_ab_gate: current build $CURRENT_BIN not built (cmake --build build)" >&2; exit 2; }

FLAG_KEY="${FLAG%%=*}"
# Dotted keys (Video.X / Audio.X) are config overrides; GE007_*/ALL_CAPS are env vars.
case "$FLAG_KEY" in
    *.*)     FLAG_MODE="config" ;;
    *)       FLAG_MODE="env" ;;
esac

# The gate's integer --level means "solo mission N" (doc: "mission 2 = Facility").
# The game rejects a small integer passed to --level, so route integers to --mission.
case "$LEVEL" in
    ''|*[!0-9]*) LEVEL_ARGS=(--level "$LEVEL") ;;   # slug
    *)           LEVEL_ARGS=(--mission "$LEVEL") ;; # integer mission number
esac

# --------------------------------------------------------------------------- #
# Isolated temp workspace (OUTSIDE the repo — dumps/traces never committed, R2)
# --------------------------------------------------------------------------- #
TMP="$(mktemp -d "${TMPDIR:-/tmp}/audio_ab_gate.XXXXXX")"
trap '[ -n "${AUDIO_AB_GATE_KEEP_TMP:-}" ] && echo "audio_ab_gate: kept workdir $TMP" >&2 || rm -rf "$TMP"' EXIT

# Canonical headless audio-capture env (matches 06-audio-remaster.md §8.1). No
# GE007_MUTE (would zero the dump) and no forced determinism knob beyond
# --deterministic, which already makes the dump bit-reproducible (§2.5).
AUDIO_ENV=(SDL_AUDIODRIVER=dummy GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 GE007_NO_VSYNC=1)

# The hermetic baseline must represent PURE defaults, so scrub every GE007_* the
# caller happens to have exported (they are all feature flags) before re-adding
# only the canonical capture vars. The OFF *current* run is deliberately NOT
# scrubbed: a leaked audio flag must reach it so the cmp catches the regression.
SCRUB_ARGS=()
while IFS= read -r _n; do
    [ -n "$_n" ] && SCRUB_ARGS+=(-u "$_n")
done < <(env | sed -n 's/^\(GE007_[A-Za-z0-9_][A-Za-z0-9_]*\)=.*/\1/p')
scrub() { env ${SCRUB_ARGS[@]+"${SCRUB_ARGS[@]}"} "$@"; }

# Extra config overrides for the ON lane (forward-compat pack hooks + the flag).
ON_CFG=()
[ "$FLAG_MODE" = config ] && ON_CFG+=(--config-override "$FLAG")
[ -n "$MUSIC_PACK" ] && ON_CFG+=(--config-override "Audio.MusicPack=$MUSIC_PACK")
[ -n "$SFX_PACK" ]   && ON_CFG+=(--config-override "Audio.SfxPack=$SFX_PACK")
ON_ENV=()
[ "$FLAG_MODE" = env ] && ON_ENV+=("$FLAG")

# Common game args (dump/trace/label appended per run).
GAME_ARGS=(--rom "$ROM" "${LEVEL_ARGS[@]}" --deterministic --screenshot-frame "$FRAMES" --screenshot-exit)

fail() { echo "audio_ab_gate: FAIL — $*" >&2; exit 1; }

# A run that reached --screenshot-exit prints this; a spurious early quit prints
# "Quit requested, shutting down." instead.
COMPLETE_MARK="Auto-screenshot complete"

# run_ge007 <label> <cwd> <verify_file> -- <command (env/scrub ... bin ... args)>
# Runs ge007 in an isolated CWD, retrying up to 3x if it quits before reaching
# --screenshot-exit or leaves the expected output missing/empty.
run_ge007() {
    local label="$1" cwd="$2" vf="$3"; shift 3
    local a
    for a in 1 2 3; do
        ( cd "$cwd" && "$@" >"$cwd/log" 2>&1 ) || true
        if grep -q "$COMPLETE_MARK" "$cwd/log" && [ -s "$vf" ]; then
            [ "$a" -gt 1 ] && echo "audio_ab_gate:   ($label completed on attempt $a)" >&2
            return 0
        fi
        if grep -q "Quit requested" "$cwd/log"; then
            echo "audio_ab_gate:   note: $label interrupted before screenshot-exit (attempt $a/3) — retrying." >&2
        else
            echo "audio_ab_gate:   note: $label did not complete (attempt $a/3) — retrying." >&2
        fi
    done
    echo "audio_ab_gate:   last $label log:" >&2; tail -15 "$cwd/log" >&2
    fail "$label run never reached screenshot-exit after 3 attempts."
}

echo "audio_ab_gate: level=$LEVEL frames=$FRAMES flag=$FLAG (mode=$FLAG_MODE) sim-hash=$SIM_HASH"
LANES=$((3 + SIM_HASH))

# --------------------------------------------------------------------------- #
# Baseline binary — either supplied, or rebuilt from the merge-base with main
# into a gitignored, SHA-stamped worktree cache (build-baseline/, matches
# .gitignore build-*/). This is the "flags-default golden" reference.
# --------------------------------------------------------------------------- #
if [ -n "$BASELINE_BIN" ]; then
    [ -x "$BASELINE_BIN" ] || { echo "audio_ab_gate: --baseline-binary $BASELINE_BIN not executable" >&2; exit 2; }
    echo "audio_ab_gate: baseline = supplied binary $BASELINE_BIN"
else
    MAINREF=main
    git rev-parse --verify -q "$MAINREF" >/dev/null 2>&1 || MAINREF=origin/main
    BASE_SHA="$(git merge-base HEAD "$MAINREF")"
    WT="$ROOT/build-baseline"
    STAMP="$WT/build/.gate_baseline_sha"
    BASELINE_BIN="$WT/build/ge007"
    if [ -x "$BASELINE_BIN" ] && [ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$BASE_SHA" ]; then
        echo "audio_ab_gate: baseline = cached build of merge-base $BASE_SHA ($BASELINE_BIN)"
    else
        echo "audio_ab_gate: building baseline from merge-base $BASE_SHA (ref $MAINREF) -> $WT ..."
        git worktree remove --force "$WT" >/dev/null 2>&1 || true
        rm -rf "$WT"
        git worktree prune
        git worktree add --detach "$WT" "$BASE_SHA" >&2
        ln -sf "$ROM" "$WT/baserom.u.z64"   # parity with main's build tree (optional)
        cmake -S "$WT" -B "$WT/build" >&2
        cmake --build "$WT/build" --parallel "$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" >&2
        [ -x "$BASELINE_BIN" ] || { echo "audio_ab_gate: FAIL — baseline build produced no ge007" >&2; exit 2; }
        echo "$BASE_SHA" > "$STAMP"
        echo "audio_ab_gate: baseline built ($BASELINE_BIN)"
    fi
fi

# --------------------------------------------------------------------------- #
# Lane 1 — OFF lane: baseline (hermetic defaults) vs current build (as-run).
#          Both runs are traced so the two invocations differ only in the binary.
#          The dumps must be BYTE-IDENTICAL (R3). Any difference is a real
#          default-output change: a C regression, or a flag leaking into the
#          default environment (the seeded fault differs at char 461).
# --------------------------------------------------------------------------- #
echo "audio_ab_gate: [lane 1/$LANES] OFF byte-identity ..."
BO="$TMP/off_baseline"; CO="$TMP/off_current"; mkdir -p "$BO" "$CO"
BASELINE_RAW="$TMP/baseline.raw"; OFF_RAW="$TMP/off.raw"; OFF_JSONL="$TMP/off.jsonl"

run_ge007 off_base "$BO" "$BASELINE_RAW" \
    scrub "${AUDIO_ENV[@]}" GE007_AUDIO_DUMP="$BASELINE_RAW" GE007_AUDIO_TRACE="$BO/trace.jsonl" \
    "$BASELINE_BIN" "${GAME_ARGS[@]}" --screenshot-label off_base
run_ge007 off_cur "$CO" "$OFF_RAW" \
    env "${AUDIO_ENV[@]}" GE007_AUDIO_DUMP="$OFF_RAW" GE007_AUDIO_TRACE="$OFF_JSONL" \
    "$CURRENT_BIN" "${GAME_ARGS[@]}" --screenshot-label off_cur

if cmp -s "$BASELINE_RAW" "$OFF_RAW"; then
    echo "audio_ab_gate:   OK — flags-default output byte-identical to baseline ($(wc -c <"$OFF_RAW" | tr -d ' ') bytes)."
else
    echo "audio_ab_gate:   cmp baseline vs current-default:" >&2
    cmp "$BASELINE_RAW" "$OFF_RAW" >&2 || true
    fail "OFF lane not byte-identical — the current build's DEFAULT audio differs from baseline.
       Either the C change moved default output (an R3 violation) or an audio flag
       (e.g. GE007_ENABLE_LIBAUDIO_LOWPASS) is leaking into the default environment."
fi

# --------------------------------------------------------------------------- #
# Lane 2 — ON lane: current build, flag applied (hermetic + the one flag). The
#          mix must not be static / silent / blown-up vs baseline.
# --------------------------------------------------------------------------- #
echo "audio_ab_gate: [lane 2] ON sanity (compare_audio.py) ..."
ON="$TMP/on"; mkdir -p "$ON"
ON_RAW="$TMP/on.raw"; ON_JSONL="$TMP/on.jsonl"

run_ge007 on "$ON" "$ON_RAW" \
    scrub "${AUDIO_ENV[@]}" ${ON_ENV[@]+"${ON_ENV[@]}"} \
    GE007_AUDIO_DUMP="$ON_RAW" GE007_AUDIO_TRACE="$ON_JSONL" \
    "$CURRENT_BIN" "${GAME_ARGS[@]}" ${ON_CFG[@]+"${ON_CFG[@]}"} --screenshot-label on

if python3 "$ROOT/tools/compare_audio.py" "$BASELINE_RAW" "$ON_RAW"; then
    echo "audio_ab_gate:   OK — ON mix passed compare_audio.py (no static/silence/blowup)."
else
    fail "ON lane — compare_audio.py flagged a static/silent/blown-up mix (output above)."
fi

# --------------------------------------------------------------------------- #
# Lane 3 — clamp-delta guard: the flag-on run must not add new clipping. Sum the
#          per-stage clamp deltas (skip warm-up rows [30:]); ON <= OFF * 1.05.
#          (06-audio-remaster.md §8.1 snippet.)
# --------------------------------------------------------------------------- #
echo "audio_ab_gate: [lane 3] clamp-delta guard ..."
[ -s "$OFF_JSONL" ] || fail "OFF trace $OFF_JSONL missing/empty (GE007_AUDIO_TRACE)"
[ -s "$ON_JSONL" ]  || fail "ON trace $ON_JSONL missing/empty (GE007_AUDIO_TRACE)"
if python3 - "$OFF_JSONL" "$ON_JSONL" <<'PY'
import json, sys
KEYS = ("env_mixer_clamp_delta", "mix_clamp_delta", "pole_filter_clamp_delta")
def clamp_sum(path):
    rows = [json.loads(l) for l in open(path) if l.strip()]
    return sum(sum(r.get(k, 0) for k in KEYS) for r in rows[30:])
off = clamp_sum(sys.argv[1]); on = clamp_sum(sys.argv[2])
print(f"audio_ab_gate:   clamp deltas (rows[30:]): OFF={off} ON={on} budget={off*1.05:.1f}")
sys.exit(0 if on <= off * 1.05 else 1)
PY
then
    echo "audio_ab_gate:   OK — no new clipping introduced by the flag."
else
    fail "clamp lane — flag-on run introduces new clipping (ON clamp deltas > OFF * 1.05)."
fi

# --------------------------------------------------------------------------- #
# Lane 4 — [--sim-hash] R1 sim-invariance. Replicates sim_invariance_gate.sh's
#          run() pattern (isolated CWD + --sim-state-hash-out) toggling ONLY the
#          audio flag; the whole-sim hash must be identical. Needed because
#          sim_invariance_gate.sh is hardcoded to Video.* (:53-57).
# --------------------------------------------------------------------------- #
if [ "$SIM_HASH" -eq 1 ]; then
    echo "audio_ab_gate: [lane 4] --sim-hash (audio flag OFF vs ON) ..."
    SIM_ENV=(GE007_DETERMINISTIC_STABLE_COUNT=1 SDL_AUDIODRIVER=dummy GE007_MUTE=1 \
             GE007_BACKGROUND=1 GE007_NO_VSYNC=1 GE007_NO_INPUT_GRAB=1 GE007_DISABLE_LEVEL_INTRO=1)
    sim_run() {  # $1=label ; $2=apply_flag(0/1) -> echoes the 16-hex sim hash
        local label="$1" apply="$2"
        local dir="$TMP/$label"; mkdir -p "$dir"
        local env_extra=() cfg_extra=()
        if [ "$apply" -eq 1 ]; then
            [ "$FLAG_MODE" = env ]    && env_extra+=("$FLAG")
            [ "$FLAG_MODE" = config ] && cfg_extra+=(--config-override "$FLAG")
        fi
        run_ge007 "$label" "$dir" "$dir/hash.json" \
            scrub "${SIM_ENV[@]}" ${env_extra[@]+"${env_extra[@]}"} \
            "$CURRENT_BIN" "${GAME_ARGS[@]}" ${cfg_extra[@]+"${cfg_extra[@]}"} \
            --screenshot-label "$label" --sim-state-hash-out "$dir/hash.json"
        grep -o '[0-9a-f]\{16\}' "$dir/hash.json" | head -1
    }
    SIM_OFF="$(sim_run sim_off 0)"
    SIM_ON="$(sim_run sim_on 1)"
    echo "audio_ab_gate:   sim hash OFF=$SIM_OFF  ON=$SIM_ON"
    [ -n "$SIM_OFF" ] && [ -n "$SIM_ON" ] || fail "--sim-hash — a run produced no hash"
    if [ "$SIM_OFF" = "$SIM_ON" ]; then
        echo "audio_ab_gate:   OK — audio flag perturbs no simulation byte (R1)."
    else
        fail "--sim-hash — audio flag changed the sim-state hash (R1 leak); localize with tools/compare_state.py."
    fi
fi

echo "audio_ab_gate: PASS"
