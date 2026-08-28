#!/usr/bin/env python3
"""ROM-free regression checks for glass handoff run comparisons."""

from __future__ import annotations

import json
from pathlib import Path
import tempfile

import compare_glass_handoff_runs as compare


def write_summary(path: Path, points: list[dict[str, object]]) -> None:
    path.write_text(
        json.dumps(
            {
                "status": "pass",
                "summary": {"points_with_stock_and_native": len(points)},
                "points": points,
                "failures": [],
            },
            indent=2,
        ),
        encoding="utf-8",
    )


def point(label: str,
          stock_rgb: list[int],
          native_post: list[int],
          mean_abs_rgb: float,
          source_rgb: list[int]) -> dict[str, object]:
    return {
        "label": label,
        "stock_target": [10, 20],
        "stock_rgba": stock_rgb + [224],
        "stock_framebuffer_input_rgba": [4, 5, 6, 224],
        "stock_hidden_transition": {"before": "0x00000001", "after": "0x00000003", "delta": 2},
        "native_post": native_post,
        "native_source_rgba": source_rgb + [102],
        "native_post_vs_stock_rgb": {
            "delta": [native_post[i] - stock_rgb[i] for i in range(3)],
            "mean_abs_rgb": mean_abs_rgb,
            "luma_delta": float(native_post[0] - stock_rgb[0]),
        },
    }


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="mgb64_glass_handoff_run_compare_") as tmp:
        root = Path(tmp)
        baseline = root / "baseline.json"
        candidate = root / "candidate.json"
        write_summary(
            baseline,
            [
                point("center", [24, 24, 24], [20, 20, 20], 4.0, [32, 32, 32]),
                point("left", [32, 32, 32], [22, 22, 22], 10.0, [0, 0, 0]),
                point("lower_right", [32, 32, 32], [7, 7, 7], 25.0, [22, 22, 22]),
            ],
        )
        write_summary(
            candidate,
            [
                point("center", [24, 24, 24], [23, 23, 23], 1.0, [32, 32, 32]),
                point("left", [32, 32, 32], [22, 22, 22], 10.0, [0, 0, 0]),
                point("lower_right", [32, 32, 32], [6, 6, 6], 26.0, [22, 22, 22]),
            ],
        )
        payload = compare.compare_runs(
            ("baseline", baseline),
            ("candidate", candidate),
            epsilon=0.0,
            fail_on_regression=False,
        )
        strict_payload = compare.compare_runs(
            ("baseline", baseline),
            ("candidate", candidate),
            epsilon=0.0,
            fail_on_regression=True,
        )

    by_label = {item["label"]: item for item in payload["comparisons"]}
    assert payload["status"] == "pass"
    assert payload["summary"]["shared_points"] == 3
    assert payload["summary"]["wins"] == 1
    assert payload["summary"]["regressions"] == 1
    assert payload["summary"]["neutral"] == 1
    assert payload["summary"]["mean_abs_rgb"]["delta_mean"] == -2.0 / 3.0
    assert by_label["center"]["classification"] == "win"
    assert by_label["left"]["classification"] == "neutral"
    assert by_label["lower_right"]["classification"] == "regression"
    assert by_label["center"]["stock_rgba_match"] is True
    assert by_label["lower_right"]["native_source_match"] is True
    assert strict_payload["status"] == "fail"
    assert any("regression points" in failure for failure in strict_payload["failures"])
    assert any("1 wins, 1 regressions, 1 neutral" in item for item in payload["interpretation"])

    print("PASS: glass handoff run comparison regression")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
