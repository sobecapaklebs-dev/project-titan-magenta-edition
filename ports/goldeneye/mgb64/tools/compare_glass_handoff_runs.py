#!/usr/bin/env python3
"""Compare two glass handoff point-summary runs."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def parse_run_spec(spec: str) -> tuple[str, Path]:
    if "=" in spec:
        label, path = spec.split("=", 1)
        label = label.strip()
        if not label:
            raise argparse.ArgumentTypeError("run label must not be empty")
        return label, Path(path)
    path = Path(spec)
    return path.stem, path


def points_by_label(payload: dict[str, Any]) -> dict[str, dict[str, Any]]:
    points = payload.get("points")
    if not isinstance(points, list):
        raise ValueError("summary JSON does not contain a points list")
    out: dict[str, dict[str, Any]] = {}
    for index, point in enumerate(points):
        if not isinstance(point, dict):
            raise ValueError(f"point #{index} is not an object")
        label = point.get("label")
        if not isinstance(label, str) or not label:
            raise ValueError(f"point #{index} has no label")
        if label in out:
            raise ValueError(f"duplicate point label {label!r}")
        out[label] = point
    return out


def mean_abs_rgb(point: dict[str, Any]) -> float | None:
    delta = point.get("native_post_vs_stock_rgb")
    if not isinstance(delta, dict):
        return None
    value = delta.get("mean_abs_rgb")
    if value is None:
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def field_matches(baseline: dict[str, Any], candidate: dict[str, Any], field: str) -> bool | None:
    if field not in baseline and field not in candidate:
        return None
    return baseline.get(field) == candidate.get(field)


def classify_delta(delta: float, epsilon: float) -> str:
    if delta < -epsilon:
        return "win"
    if delta > epsilon:
        return "regression"
    return "neutral"


def compare_points(
    baseline_points: dict[str, dict[str, Any]],
    candidate_points: dict[str, dict[str, Any]],
    epsilon: float,
) -> tuple[list[dict[str, Any]], list[str]]:
    failures: list[str] = []
    comparisons: list[dict[str, Any]] = []
    missing_in_candidate = sorted(set(baseline_points) - set(candidate_points))
    missing_in_baseline = sorted(set(candidate_points) - set(baseline_points))
    for label in missing_in_candidate:
        failures.append(f"{label}: missing from candidate run")
    for label in missing_in_baseline:
        failures.append(f"{label}: missing from baseline run")

    for label in sorted(set(baseline_points) & set(candidate_points)):
        baseline = baseline_points[label]
        candidate = candidate_points[label]
        baseline_error = mean_abs_rgb(baseline)
        candidate_error = mean_abs_rgb(candidate)
        if baseline_error is None:
            failures.append(f"{label}: baseline missing mean_abs_rgb")
            continue
        if candidate_error is None:
            failures.append(f"{label}: candidate missing mean_abs_rgb")
            continue
        error_delta = candidate_error - baseline_error
        comparisons.append(
            {
                "label": label,
                "classification": classify_delta(error_delta, epsilon),
                "baseline_mean_abs_rgb": baseline_error,
                "candidate_mean_abs_rgb": candidate_error,
                "error_delta": error_delta,
                "stock_target_match": field_matches(baseline, candidate, "stock_target"),
                "stock_rgba_match": field_matches(baseline, candidate, "stock_rgba"),
                "stock_framebuffer_input_match": field_matches(
                    baseline,
                    candidate,
                    "stock_framebuffer_input_rgba",
                ),
                "stock_hidden_transition_match": field_matches(
                    baseline,
                    candidate,
                    "stock_hidden_transition",
                ),
                "native_source_match": field_matches(baseline, candidate, "native_source_rgba"),
                "baseline_stock_rgba": baseline.get("stock_rgba"),
                "baseline_native_post": baseline.get("native_post"),
                "candidate_native_post": candidate.get("native_post"),
                "native_source_rgba": candidate.get("native_source_rgba"),
            }
        )
    return comparisons, failures


def summarize_comparisons(comparisons: list[dict[str, Any]]) -> dict[str, Any]:
    deltas = [float(item["error_delta"]) for item in comparisons]
    baseline_errors = [float(item["baseline_mean_abs_rgb"]) for item in comparisons]
    candidate_errors = [float(item["candidate_mean_abs_rgb"]) for item in comparisons]
    return {
        "shared_points": len(comparisons),
        "wins": sum(1 for item in comparisons if item["classification"] == "win"),
        "regressions": sum(1 for item in comparisons if item["classification"] == "regression"),
        "neutral": sum(1 for item in comparisons if item["classification"] == "neutral"),
        "mean_abs_rgb": {
            "baseline_mean": sum(baseline_errors) / len(baseline_errors) if baseline_errors else None,
            "candidate_mean": sum(candidate_errors) / len(candidate_errors) if candidate_errors else None,
            "delta_mean": sum(deltas) / len(deltas) if deltas else None,
            "delta_min": min(deltas) if deltas else None,
            "delta_max": max(deltas) if deltas else None,
        },
    }


def build_interpretation(
    baseline_label: str,
    candidate_label: str,
    summary: dict[str, Any],
    comparisons: list[dict[str, Any]],
) -> list[str]:
    mean_abs = summary["mean_abs_rgb"]
    notes = [
        f"{candidate_label} vs {baseline_label}: "
        f"{summary['wins']} wins, {summary['regressions']} regressions, "
        f"{summary['neutral']} neutral across {summary['shared_points']} shared points",
    ]
    if mean_abs.get("delta_mean") is not None:
        notes.append(
            "candidate mean_abs_rgb delta: "
            f"mean={mean_abs['delta_mean']:.3f} "
            f"min={mean_abs['delta_min']:.3f} max={mean_abs['delta_max']:.3f}"
        )
    changed_inputs = [
        item["label"]
        for item in comparisons
        if item.get("stock_rgba_match") is False
        or item.get("stock_framebuffer_input_match") is False
        or item.get("stock_hidden_transition_match") is False
    ]
    if changed_inputs:
        notes.append(
            "stock authority fields changed for: " + ", ".join(changed_inputs)
        )
    return notes


def compare_runs(
    baseline_spec: tuple[str, Path],
    candidate_spec: tuple[str, Path],
    epsilon: float,
    fail_on_regression: bool,
) -> dict[str, Any]:
    baseline_label, baseline_path = baseline_spec
    candidate_label, candidate_path = candidate_spec
    failures: list[str] = []
    baseline_payload = load_json(baseline_path)
    candidate_payload = load_json(candidate_path)
    try:
        baseline_points = points_by_label(baseline_payload)
        candidate_points = points_by_label(candidate_payload)
    except ValueError as exc:
        return {
            "status": "fail",
            "failures": [str(exc)],
            "baseline": {"label": baseline_label, "path": str(baseline_path)},
            "candidate": {"label": candidate_label, "path": str(candidate_path)},
        }

    comparisons, point_failures = compare_points(baseline_points, candidate_points, epsilon)
    failures.extend(point_failures)
    summary = summarize_comparisons(comparisons)
    if fail_on_regression and summary["regressions"]:
        failures.append(
            f"candidate has {summary['regressions']} regression points beyond epsilon {epsilon}"
        )

    payload = {
        "status": "fail" if failures else "pass",
        "baseline": {"label": baseline_label, "path": str(baseline_path)},
        "candidate": {"label": candidate_label, "path": str(candidate_path)},
        "epsilon": epsilon,
        "summary": summary,
        "comparisons": comparisons,
        "failures": failures,
    }
    payload["interpretation"] = build_interpretation(
        baseline_label,
        candidate_label,
        summary,
        comparisons,
    )
    return payload


def print_human(payload: dict[str, Any]) -> None:
    baseline = payload["baseline"]
    candidate = payload["candidate"]
    summary = payload.get("summary") or {}
    mean_abs = summary.get("mean_abs_rgb") or {}
    print(
        "glass handoff run comparison: "
        f"{candidate['label']} vs {baseline['label']} "
        f"shared={summary.get('shared_points')} wins={summary.get('wins')} "
        f"regressions={summary.get('regressions')} neutral={summary.get('neutral')} "
        f"delta_mean={mean_abs.get('delta_mean')}"
    )
    for item in payload.get("comparisons") or []:
        print(
            f"- {item['label']}: {item['classification']} "
            f"{item['baseline_mean_abs_rgb']} -> {item['candidate_mean_abs_rgb']} "
            f"delta={item['error_delta']} "
            f"stock={item.get('baseline_stock_rgba')} "
            f"native={item.get('baseline_native_post')}->{item.get('candidate_native_post')} "
            f"source={item.get('native_source_rgba')}"
        )
    if payload.get("failures"):
        print("failures:")
        for failure in payload["failures"]:
            print(f"  {failure}")
    print("interpretation:")
    for item in payload.get("interpretation") or []:
        print(f"  {item}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=parse_run_spec, help="BASELINE_LABEL=summary.json")
    parser.add_argument("candidate", type=parse_run_spec, help="CANDIDATE_LABEL=summary.json")
    parser.add_argument("--epsilon", type=float, default=0.0)
    parser.add_argument("--fail-on-regression", action="store_true")
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    payload = compare_runs(args.baseline, args.candidate, args.epsilon, args.fail_on_regression)
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if args.json:
        print(json.dumps(payload, indent=2, sort_keys=True))
    else:
        print_human(payload)
    return 0 if payload["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
