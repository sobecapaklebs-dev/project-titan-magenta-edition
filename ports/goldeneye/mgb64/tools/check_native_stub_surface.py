#!/usr/bin/env python3
"""Guard against silent native gameplay/helper fallbacks.

This is intentionally narrower than a blanket "no stubs" check. The native
port still needs compatibility shims for libultra OS, controller, PI, VI, and
diagnostic surfaces. What should fail release checks is a game helper or audio
helper regressing into a no-op fallback that can link cleanly while changing
gameplay.
"""

import argparse
from pathlib import Path
import sys


def strip_if0_blocks(text):
    """Remove simple #if 0 blocks before checking for live fallback symbols."""
    lines = text.splitlines(keepends=True)
    out = []
    disabled_depth = 0

    for line in lines:
        stripped = line.strip()

        if stripped.startswith("#if 0"):
            disabled_depth += 1
            continue

        if disabled_depth:
            if stripped.startswith("#if"):
                disabled_depth += 1
            elif stripped.startswith("#endif"):
                disabled_depth -= 1
            continue

        out.append(line)

    return "".join(out)


def require_contains(failures, text, needle, message):
    if needle not in text:
        failures.append(message)


def require_absent(failures, text, needle, message):
    if needle in text:
        failures.append(message)


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", default=".")
    args = parser.parse_args(argv)

    root = Path(args.repo_root)
    failures = []

    stubs_path = root / "src/platform/stubs.c"
    if not stubs_path.exists():
        failures.append("missing native platform shim file: src/platform/stubs.c")
    else:
        stubs_text = stubs_path.read_text(errors="replace")
        live_stubs = strip_if0_blocks(stubs_text)

        require_absent(
            failures,
            stubs_text,
            "They will be replaced with real implementations as the\n * port progresses through phases.",
            "src/platform/stubs.c has stale phase-scaffolding public wording",
        )

        for symbol in (
            "sub_GAME_7F0CE794",
            "objectiveGetStatus_WEAK",
            "alSynAllocVoice",
            "alSynAllocFX",
            "alSndpAllocate",
        ):
            require_absent(
                failures,
                live_stubs,
                symbol,
                f"src/platform/stubs.c contains live fallback for gameplay/audio helper: {symbol}",
            )

    objecthandler = root / "src/game/objecthandler_2.c"
    if not objecthandler.exists():
        failures.append("missing object display-list loader: src/game/objecthandler_2.c")
    else:
        text = objecthandler.read_text(errors="replace")
        require_absent(
            failures,
            text,
            "sub_GAME_7F0CE794",
            "objecthandler_2.c still references stale texture-copy stub symbol",
        )
        require_contains(
            failures,
            text,
            "texCopyGdls(",
            "objecthandler_2.c no longer calls the real texCopyGdls helper",
        )

    objective_status = root / "src/game/objective_status.c"
    if not objective_status.exists():
        failures.append("missing objective status implementation: src/game/objective_status.c")
    else:
        text = objective_status.read_text(errors="replace")
        require_contains(
            failures,
            text,
            "OBJECTIVESTATUS objectiveGetStatus_WEAK(s32 objectiveNum, s32 unused)",
            "objective_status.c is missing the native AI bytecode objective-status adapter",
        )
        require_contains(
            failures,
            text,
            "return get_status_of_objective(objectiveNum);",
            "objectiveGetStatus_WEAK no longer routes to get_status_of_objective",
        )

    objective_header = root / "src/game/objective_status.h"
    if not objective_header.exists():
        failures.append("missing objective status header: src/game/objective_status.h")
    else:
        require_contains(
            failures,
            objective_header.read_text(errors="replace"),
            "OBJECTIVESTATUS  objectiveGetStatus_WEAK(s32 objectiveNum, s32 unused);",
            "objective_status.h is missing the shared objectiveGetStatus_WEAK prototype",
        )

    chrai = root / "src/game/chrai.c"
    if not chrai.exists():
        failures.append("missing AI script interpreter: src/game/chrai.c")
    else:
        require_absent(
            failures,
            chrai.read_text(errors="replace"),
            "extern s32 objectiveGetStatus_WEAK",
            "chrai.c reintroduced a local objectiveGetStatus_WEAK extern instead of using objective_status.h",
        )

    asset_stubs = root / "src/platform/asset_stubs.c"
    if not asset_stubs.exists():
        failures.append("missing ROM-backed asset placeholder file: src/platform/asset_stubs.c")
    else:
        text = asset_stubs.read_text(errors="replace")
        require_contains(
            failures,
            text,
            "Link-time placeholders for ROM-backed asset symbols",
            "asset_stubs.c no longer describes its ROM-backed placeholder role",
        )
        for stale in (
            "These will be replaced with real asset loading",
            "Phase 2+",
        ):
            require_absent(
                failures,
                text,
                stale,
                f"asset_stubs.c contains stale placeholder wording: {stale}",
            )

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1

    print("PASS: native gameplay/helper stub surface guard passed")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
