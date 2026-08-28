#!/usr/bin/env python3
"""T3 -- emulator-free intro-stream parse digest tool.

Boots each of the 20 solo-campaign stages headless just far enough to reach
the level-intro parse (the N64->PC intro-record widening loop in
src/game/prop.c, ~line 2972), captures the machine-readable
`[INTRO-DIGEST]` line stream that widening loop emits under
`GE007_TRACE_INTRO_PARSE=1`, canonicalizes it, and SHA-256s the result.

The point: two runs of the SAME stage must produce an IDENTICAL digest (see
tools/intro_parse_digest_gate.sh) -- any divergence is a nondeterministic
parse (a loader/walker regression), caught without needing an N64 emulator
oracle or a rendered frame at all.

Also builds a per-stage coverage table (cinema-cam count, swirl-point count
+ which flag bits (&1/&2/&4) occur, eligible/total spawn count, item/ammo
counts, anim index, watch h:m, credits presence) -- a table that doesn't
exist anywhere else in the codebase and feeds the next task's per-stage
route authoring.

CLI:
    tools/intro_parse_digest.py --binary build/ge007 --rom baserom.u.z64 \\
        --stages "33 34 22 26 36 35 9 20 43 27 24 29 30 25 37 23 39 41 28 32" \\
        --out-dir /tmp/intro_digest

All outputs (per-stage JSON, coverage_table.md) are ROM-derived local
validation data -- do not commit anything written to --out-dir.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


# --- Canonical stage list + slugs ---------------------------------------------------

# The 20 solo-campaign stages, as raw LEVELID values -- same list/order as
# tools/intro_census_capture.sh's ALL_LEVELS (and thus `--level <id>` below
# resolves each one directly via main_pc.c's pcFindStageByLevelId).
DEFAULT_STAGES: tuple[int, ...] = (
    33, 34, 22, 26, 36, 35, 9, 20, 43, 27, 24, 29, 30, 25, 37, 23, 39, 41, 28, 32,
)

# Hardcoded from src/platform/main_pc.c's kPcStartStages table (stable,
# rarely-touched startup code) -- maps raw LEVELID -> the --level slug name.
STAGE_SLUGS: dict[int, str] = {
    9: "bunker1",
    20: "silo",
    22: "statue",
    23: "control",
    24: "archives",
    25: "train",
    26: "frigate",
    27: "bunker2",
    28: "aztec",
    29: "streets",
    30: "depot",
    32: "egypt",
    33: "dam",
    34: "facility",
    35: "runway",
    36: "surface1",
    37: "jungle",
    39: "caverns",
    41: "cradle",
    43: "surface2",
}

# INTROTYPE_* enum from src/bondconstants.h. Order is load-bearing: it
# matches the n64_intro_sizes[]/pc_intro_sizes[] tables in src/game/prop.c
# that the C-side [INTRO-DIGEST] emission walks.
INTROTYPE_SPAWN = 0
INTROTYPE_ITEM = 1
INTROTYPE_AMMO = 2
INTROTYPE_SWIRL = 3
INTROTYPE_ANIM = 4
INTROTYPE_CUFF = 5
INTROTYPE_CAMERA = 6
INTROTYPE_WATCH = 7
INTROTYPE_CREDITS = 8
INTROTYPE_END = 9

INTROTYPE_NAMES: dict[int, str] = {
    INTROTYPE_SPAWN: "SPAWN",
    INTROTYPE_ITEM: "ITEM",
    INTROTYPE_AMMO: "AMMO",
    INTROTYPE_SWIRL: "SWIRL",
    INTROTYPE_ANIM: "ANIM",
    INTROTYPE_CUFF: "CUFF",
    INTROTYPE_CAMERA: "CAMERA",
    INTROTYPE_WATCH: "WATCH",
    INTROTYPE_CREDITS: "CREDITS",
    INTROTYPE_END: "END",
}

# The three swirl-flag bits the faithfulness plan cares about (unk04 in
# SetupIntroSwirl / src/bondtypes.h; see src/game/bondview.c
# sub_GAME_7F07B2A0 for how &1/&2/&4 are consumed).
SWIRL_FLAG_BITS: tuple[int, ...] = (1, 2, 4)


class DigestParseError(RuntimeError):
    """Raised when the [INTRO-DIGEST] stderr stream is malformed or
    incomplete. Always raised, never silently swallowed."""


class StageRunError(RuntimeError):
    """Raised when a single stage's headless capture fails (missing
    binary/ROM, timeout, nonzero exit, or a malformed digest stream)."""


_DIGEST_RE = re.compile(r"^\[INTRO-DIGEST\]\s+(\{.*\})\s*$")
_DIGEST_END_RE = re.compile(r"^\[INTRO-DIGEST-END\]\s+(\{.*\})\s*$")


def parse_digest_stream(text: str) -> list[dict[str, Any]]:
    """Parse `[INTRO-DIGEST]` / `[INTRO-DIGEST-END]` lines out of raw stderr
    text, ignoring every other line -- including the existing human-readable
    `[INTRO-PARSE]` / `[INTRO-WALK]` trace lines emitted under the same
    GE007_TRACE_INTRO_PARSE gate, and any other stdout/stderr noise.

    Returns the list of per-record dicts in stream order. Raises
    DigestParseError (never silently drops data) if:
      - a `[INTRO-DIGEST]` line's JSON payload doesn't parse or isn't a JSON
        object,
      - a `[INTRO-DIGEST-END]` line's JSON payload doesn't parse or is
        missing "count",
      - no `[INTRO-DIGEST-END]` line is present, or
      - the END line's count doesn't match the number of parsed records.
    """
    records: list[dict[str, Any]] = []
    end_count: int | None = None

    for lineno, line in enumerate(text.splitlines(), start=1):
        m = _DIGEST_RE.match(line)
        if m:
            try:
                rec = json.loads(m.group(1))
            except json.JSONDecodeError as exc:
                raise DigestParseError(
                    f"malformed [INTRO-DIGEST] JSON at stderr line {lineno}: {line!r} ({exc})"
                ) from exc
            if not isinstance(rec, dict):
                raise DigestParseError(
                    f"[INTRO-DIGEST] payload at stderr line {lineno} is not a JSON object: {line!r}"
                )
            records.append(rec)
            continue

        m = _DIGEST_END_RE.match(line)
        if m:
            try:
                end = json.loads(m.group(1))
            except json.JSONDecodeError as exc:
                raise DigestParseError(
                    f"malformed [INTRO-DIGEST-END] JSON at stderr line {lineno}: {line!r} ({exc})"
                ) from exc
            if not isinstance(end, dict) or "count" not in end:
                raise DigestParseError(
                    f"[INTRO-DIGEST-END] payload at stderr line {lineno} missing 'count': {line!r}"
                )
            end_count = end["count"]
            continue

        # Any other line (e.g. [INTRO-PARSE], [SETUP-PC], SDL/driver notes)
        # is intentionally ignored: this parser only understands the two
        # digest line kinds above.

    if end_count is None:
        raise DigestParseError("no [INTRO-DIGEST-END] line found in stderr stream")
    if end_count != len(records):
        raise DigestParseError(
            f"[INTRO-DIGEST-END] count={end_count} does not match "
            f"{len(records)} parsed [INTRO-DIGEST] record(s)"
        )
    return records


def canonicalize_records(records: list[dict[str, Any]]) -> str:
    """Canonical serialization for digesting: sort records by ordinal 'i',
    then re-encode each with sorted keys and compact (no incidental
    whitespace) separators. Stable under input list order and dict-key
    insertion order; changes if and only if a record's actual values
    change."""
    ordered = sorted(records, key=lambda r: r["i"])
    lines = [json.dumps(r, sort_keys=True, separators=(",", ":")) for r in ordered]
    return "\n".join(lines) + ("\n" if lines else "")


def digest_sha256(records: list[dict[str, Any]]) -> str:
    """SHA-256 hex digest over the canonical byte encoding of `records`."""
    canonical = canonicalize_records(records)
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()


# --- Stage capture (subprocess) ------------------------------------------------------

def run_stage(
    binary: Path,
    rom: Path,
    stage_id: int,
    timeout: float = 120.0,
) -> list[dict[str, Any]]:
    """Boot `binary` headless on `stage_id`, capture the [INTRO-DIGEST]
    stream from a temp working directory (so the repo's ge007.ini is
    untouched and no screenshot BMP lands in the repo), and return the
    parsed records. Raises StageRunError with a readable message on any
    failure (missing files, timeout, nonzero exit, malformed digest
    stream)."""
    if not binary.is_file():
        raise StageRunError(f"binary not found: {binary}")
    if not rom.is_file():
        raise StageRunError(f"ROM not found: {rom}")

    env = dict(os.environ)
    env.update(
        {
            "SDL_AUDIODRIVER": "dummy",
            "GE007_MUTE": "1",
            "GE007_BACKGROUND": "1",
            "GE007_NO_INPUT_GRAB": "1",
            "GE007_NO_VSYNC": "1",
            "GE007_DETERMINISTIC_STABLE_COUNT": "1",
            "GE007_TRACE_INTRO_PARSE": "1",
        }
    )

    with tempfile.TemporaryDirectory(prefix=f"mgb64_intro_digest_stage{stage_id}_") as tmp:
        tmp_path = Path(tmp)
        cmd = [
            str(binary),
            "--rom", str(rom.resolve()),
            "--level", str(stage_id),
            "--deterministic",
            "--savedir", str(tmp_path),
            "--screenshot-frame", "5",
            "--screenshot-label", "digest",
            "--screenshot-exit",
        ]
        try:
            result = subprocess.run(
                cmd,
                cwd=tmp_path,
                env=env,
                capture_output=True,
                text=True,
                timeout=timeout,
                check=False,
            )
        except subprocess.TimeoutExpired as exc:
            raise StageRunError(f"stage {stage_id} timed out after {timeout}s") from exc

        if result.returncode != 0:
            tail = "\n".join(result.stderr.splitlines()[-40:])
            raise StageRunError(
                f"stage {stage_id} exited with code {result.returncode}\n{tail}"
            )

        try:
            return parse_digest_stream(result.stderr)
        except DigestParseError as exc:
            raise StageRunError(f"stage {stage_id}: {exc}") from exc


# --- Coverage table -------------------------------------------------------------------

def build_coverage_row(stage_id: int, records: list[dict[str, Any]]) -> dict[str, Any]:
    """Summarize one stage's parsed intro-record stream into the coverage
    fields the faithfulness plan asks for: cinema-cam count, swirl-point
    count + which flag bits occur, eligible/total spawn count, item/ammo
    counts, anim index, watch h:m, credits presence."""
    counts: dict[str, int] = {name: 0 for name in INTROTYPE_NAMES.values()}
    swirl_flag_union = 0
    spawn_total = 0
    spawn_eligible = 0
    anim_indices: list[int] = []
    watch: tuple[int, int] | None = None
    credits_present = False

    for rec in records:
        type_id = rec.get("type")
        name = INTROTYPE_NAMES.get(type_id, f"UNKNOWN{type_id}")
        counts[name] = counts.get(name, 0) + 1
        w = rec.get("w") or []

        if type_id == INTROTYPE_SWIRL and len(w) > 1:
            swirl_flag_union |= w[1]
        elif type_id == INTROTYPE_SPAWN and len(w) > 2:
            spawn_total += 1
            # w[2] is is_demo_playback (SetupIntroSpawn); "eligible" mirrors
            # bondview_r.c's `check_ramrom_flags() == is_demo_playback` gate
            # for a normal (non-RAMROM-replay) boot, where that check is 0.
            if w[2] == 0:
                spawn_eligible += 1
        elif type_id == INTROTYPE_ANIM and len(w) > 1:
            anim_indices.append(w[1])
        elif type_id == INTROTYPE_WATCH and len(w) > 2:
            watch = (w[1], w[2])
        elif type_id == INTROTYPE_CREDITS:
            credits_present = True

    flags_str = ",".join(f"&{bit}" for bit in SWIRL_FLAG_BITS if swirl_flag_union & bit) or "none"

    return {
        "stage": stage_id,
        "slug": STAGE_SLUGS.get(stage_id, "?"),
        "counts": counts,
        "swirl_flags": flags_str,
        "spawn_eligible": spawn_eligible,
        "spawn_total": spawn_total,
        "anim_indices": anim_indices,
        "watch": watch,
        "credits_present": credits_present,
        "total_records": len(records),
    }


def render_coverage_table_markdown(rows: list[dict[str, Any]]) -> str:
    header = (
        "| stage | slug | spawn (elig/total) | item | ammo | swirl | swirl flags | "
        "anim | cuff | camera | watch | credits | total |"
    )
    sep = "|" + "---|" * 13
    lines = [header, sep]
    for row in rows:
        c = row["counts"]
        anim = ",".join(str(i) for i in row["anim_indices"]) if row["anim_indices"] else "-"
        watch = f"{row['watch'][0]}:{row['watch'][1]:02d}" if row["watch"] else "-"
        credits = "yes" if row["credits_present"] else "no"
        lines.append(
            "| {stage} | {slug} | {se}/{st} | {item} | {ammo} | {swirl} | {flags} | "
            "{anim} | {cuff} | {camera} | {watch} | {credits} | {total} |".format(
                stage=row["stage"],
                slug=row["slug"],
                se=row["spawn_eligible"],
                st=row["spawn_total"],
                item=c["ITEM"],
                ammo=c["AMMO"],
                swirl=c["SWIRL"],
                flags=row["swirl_flags"],
                anim=anim,
                cuff=c["CUFF"],
                camera=c["CAMERA"],
                watch=watch,
                credits=credits,
                total=row["total_records"],
            )
        )
    return "\n".join(lines) + "\n"


# --- CLI --------------------------------------------------------------------------------

def _default_repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def main(argv: list[str] | None = None) -> int:
    root = _default_repo_root()
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", default=str(root / "build" / "ge007"), help="path to the ge007 binary")
    parser.add_argument("--rom", default=str(root / "baserom.u.z64"), help="path to baserom.u.z64")
    parser.add_argument(
        "--stages",
        default=" ".join(str(s) for s in DEFAULT_STAGES),
        help="space-separated raw LEVELID list (default: all 20 solo stages)",
    )
    parser.add_argument("--out-dir", default=None, help="output directory (default: a fresh mkdtemp)")
    parser.add_argument("--timeout", type=float, default=120.0, help="per-stage timeout in seconds")
    args = parser.parse_args(argv)

    binary = Path(args.binary)
    rom = Path(args.rom)
    try:
        stages = [int(s) for s in args.stages.split()]
    except ValueError:
        print(f"FAIL: --stages must be a space-separated integer list, got: {args.stages!r}", file=sys.stderr)
        return 2
    out_dir = Path(args.out_dir) if args.out_dir else Path(tempfile.mkdtemp(prefix="mgb64_intro_digest_"))
    out_dir.mkdir(parents=True, exist_ok=True)

    failures: list[str] = []
    rows: list[dict[str, Any]] = []

    for stage_id in stages:
        slug = STAGE_SLUGS.get(stage_id, f"stage{stage_id}")
        print(f"=== stage {stage_id} ({slug}) ===", file=sys.stderr)
        try:
            records = run_stage(binary, rom, stage_id, timeout=args.timeout)
        except StageRunError as exc:
            failures.append(f"stage {stage_id} ({slug}): {exc}")
            print(f"FAIL: stage {stage_id} ({slug}): {exc}", file=sys.stderr)
            continue

        sha256 = digest_sha256(records)
        stage_json = out_dir / f"intro_digest_{stage_id}.json"
        stage_json.write_text(
            json.dumps({"stage": stage_id, "sha256": sha256, "records": records}, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        print(f"  sha256={sha256} records={len(records)} -> {stage_json}", file=sys.stderr)
        rows.append(build_coverage_row(stage_id, records))

    if rows:
        table = render_coverage_table_markdown(rows)
        (out_dir / "coverage_table.md").write_text(table, encoding="utf-8")
        print(f"\ncoverage table: {out_dir / 'coverage_table.md'}", file=sys.stderr)

    if failures:
        print("\nFAIL: intro parse digest capture failed for one or more stages:", file=sys.stderr)
        for f in failures:
            print(f"  - {f}", file=sys.stderr)
        return 1

    print(f"\nOK: {len(rows)}/{len(stages)} stages captured -> {out_dir}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
