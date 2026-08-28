#!/usr/bin/env python3
"""Guard the signed-off glass shard geometry scale (FID-0003).

Ground truth: the retail disassembly of sub_GAME_7F0A2C44 (kept beside the C
reimpl in src/game/unk_0A1DA0.c) builds each falling-shard matrix and emits it
with NO per-piece scalar multiply -- the ROM draws the shard model UNSCALED.

That is only faithful when the projection it draws through (field_10E0 = proj*view)
is likewise unscaled. This port ships field_10E0 SCALED by the level visibility
(bondview.c, GE007_FIELD_10E0_SCALED default on; Dam and both Surfaces use 0.2).
To reproduce the retail NET transform the shard model must therefore invert that
scale: shard *= 1/visibility, cancelling the 0.2 the view basis carries so that
(1/vis)*vis == 1 == the ROM's unscaled shard. The scale is COUPLED to the
field_10E0 decision, not an independent knob.

This lane is ROM-free: it asserts the source invariant so the answer stays locked.
The old six-way A/B scaffold (COMPRESS/BASIS_SCALE/NO_BASIS_SCALE/SQRT_BASIS/
INV_VIS_SCALE) diverged from the ROM on any visibility!=1 level and must not
reappear. Reverting to any of those flags, or dropping the 1/visibility
compensation or the field_10E0 coupling, reddens this guard (fail-on-revert).
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# The retired A/B scale hypotheses. Their env vars must not come back to life in
# the shard emitter -- each failed to invert the visibility scale.
RETIRED_FLAGS = (
    "GE007_GLASS_SHARD_COMPRESS",
    "GE007_GLASS_SHARD_BASIS_SCALE",
    "GE007_GLASS_SHARD_NO_BASIS_SCALE",
    "GE007_GLASS_SHARD_SQRT_BASIS",
    "GE007_GLASS_SHARD_INV_VIS_SCALE",
)


def shard_emitter_body(text: str) -> str | None:
    """Return the body of sub_GAME_7F0A2C44's C reimpl (up to the GLOBAL_ASM ref)."""
    start = text.find("Gfx * sub_GAME_7F0A2C44(Gfx *gdl) {")
    if start < 0:
        return None
    end = text.find("GLOBAL_ASM(", start)
    if end < 0:
        end = len(text)
    return text[start:end]


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", default=".")
    args = parser.parse_args(argv)

    root = Path(args.repo_root)
    src = root / "src/game/unk_0A1DA0.c"
    if not src.exists():
        print("FAIL: missing src/game/unk_0A1DA0.c", file=sys.stderr)
        return 1

    text = src.read_text(encoding="utf-8", errors="replace")
    body = shard_emitter_body(text)
    failures: list[str] = []

    if body is None:
        print("FAIL: could not locate sub_GAME_7F0A2C44 reimpl", file=sys.stderr)
        return 1

    # 1) None of the retired A/B scale flags may be read in the shard emitter.
    for flag in RETIRED_FLAGS:
        if flag in body:
            failures.append(
                f"retired shard A/B flag {flag} reappeared in sub_GAME_7F0A2C44 "
                "-- it never inverts the visibility scale (FID-0003)"
            )

    # 2) The scale must be the 1/visibility compensation, coupled to field_10E0.
    if "bgGetLevelVisibilityScale()" not in body:
        failures.append(
            "shard emitter no longer reads bgGetLevelVisibilityScale() -- the "
            "1/visibility compensation is the signed-off scale (FID-0003)"
        )
    if not re.search(r"matrix_scalar_multiply_3\(\s*1\.0f\s*/\s*vis_scale", body):
        failures.append(
            "shard emitter no longer applies matrix_scalar_multiply_3(1.0f/vis_scale) "
            "-- required to cancel the vis-scaled field_10E0 (FID-0003)"
        )
    if "GE007_FIELD_10E0_SCALED" not in body:
        failures.append(
            "shard scale is no longer coupled to GE007_FIELD_10E0_SCALED -- the two "
            "must stay in lockstep or the net transform desyncs from retail (FID-0003)"
        )

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1

    print(
        "PASS: glass shard scale = 1/visibility, coupled to field_10E0 "
        "(retail-ASM-faithful net transform; FID-0003)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
