#!/usr/bin/env python3
"""Audit captured screenshots for basic visual health.

This is a capture-validity guard, not a renderer oracle. It catches blank,
missing, wrong-size, or nearly monochrome images before downstream comparison
tools treat them as useful evidence.
"""

import argparse
import json
import math
import os
import sys
from collections import Counter


def parse_size(value):
    parts = value.lower().split("x")
    if len(parts) != 2:
        raise argparse.ArgumentTypeError("expected WIDTHxHEIGHT")
    try:
        width = int(parts[0])
        height = int(parts[1])
    except ValueError:
        raise argparse.ArgumentTypeError("expected WIDTHxHEIGHT")
    if width <= 0 or height <= 0:
        raise argparse.ArgumentTypeError("width and height must be positive")
    return width, height


def percent(part, total):
    return 0.0 if total == 0 else (100.0 * float(part) / float(total))


def finite_percent(value, name):
    try:
        parsed = float(value)
    except ValueError:
        raise argparse.ArgumentTypeError("%s must be a number" % name)
    if not math.isfinite(parsed) or parsed < 0.0 or parsed > 100.0:
        raise argparse.ArgumentTypeError("%s must be between 0 and 100" % name)
    return parsed


def non_negative_float(value, name):
    try:
        parsed = float(value)
    except ValueError:
        raise argparse.ArgumentTypeError("%s must be a number" % name)
    if not math.isfinite(parsed) or parsed < 0.0:
        raise argparse.ArgumentTypeError("%s must be a non-negative finite number" % name)
    return parsed


def positive_int(value, name):
    try:
        parsed = int(value)
    except ValueError:
        raise argparse.ArgumentTypeError("%s must be an integer" % name)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("%s must be positive" % name)
    return parsed


def load_image(path):
    try:
        from PIL import Image
    except ImportError:
        raise SystemExit(
            "FAIL: Pillow is required for screenshot health audits. "
            "Install it with: python3 -m pip install pillow"
        )

    try:
        with Image.open(path) as image:
            rgb = image.convert("RGB")
            return rgb.size, rgb.tobytes()
    except FileNotFoundError:
        raise
    except Exception as exc:
        raise RuntimeError("could not read image: %s" % exc)


def analyze_image(path):
    size, data = load_image(path)
    total = size[0] * size[1]
    if total <= 0 or len(data) != total * 3:
        raise RuntimeError(
            "invalid RGB payload size: expected %d bytes, got %d"
            % (total * 3, len(data))
        )

    colors = Counter(data[i : i + 3] for i in range(0, len(data), 3))
    dominant = max(colors.values()) if colors else 0
    black = 0
    white = 0
    r_sum = 0
    g_sum = 0
    b_sum = 0
    luma_sum = 0.0

    for i in range(0, len(data), 3):
        r = data[i]
        g = data[i + 1]
        b = data[i + 2]
        r_sum += r
        g_sum += g
        b_sum += b
        luma_sum += (0.2126 * r) + (0.7152 * g) + (0.0722 * b)
        if r <= 5 and g <= 5 and b <= 5:
            black += 1
        if r >= 250 and g >= 250 and b >= 250:
            white += 1

    return {
        "path": path,
        "size": {"width": size[0], "height": size[1]},
        "pixels": total,
        "unique_colors": len(colors),
        "dominant_pct": percent(dominant, total),
        "black_pct": percent(black, total),
        "white_pct": percent(white, total),
        "mean_rgb": {
            "r": float(r_sum) / float(total),
            "g": float(g_sum) / float(total),
            "b": float(b_sum) / float(total),
        },
        "mean_luma": luma_sum / float(total),
    }


def check_result(result, args):
    failures = []
    width = result["size"]["width"]
    height = result["size"]["height"]

    if args.expect_size and (width, height) != args.expect_size:
        failures.append(
            "size %dx%d != expected %dx%d"
            % (width, height, args.expect_size[0], args.expect_size[1])
        )
    if result["unique_colors"] < args.min_unique_colors:
        failures.append(
            "unique colors %d < %d"
            % (result["unique_colors"], args.min_unique_colors)
        )
    if result["dominant_pct"] > args.max_dominant_pct:
        failures.append(
            "dominant color %.2f%% > %.2f%%"
            % (result["dominant_pct"], args.max_dominant_pct)
        )
    if result["black_pct"] > args.max_black_pct:
        failures.append(
            "near-black pixels %.2f%% > %.2f%%"
            % (result["black_pct"], args.max_black_pct)
        )
    if result["white_pct"] > args.max_white_pct:
        failures.append(
            "near-white pixels %.2f%% > %.2f%%"
            % (result["white_pct"], args.max_white_pct)
        )
    if result["mean_luma"] < args.min_mean_luma:
        failures.append(
            "mean luma %.2f < %.2f" % (result["mean_luma"], args.min_mean_luma)
        )
    if result["mean_luma"] > args.max_mean_luma:
        failures.append(
            "mean luma %.2f > %.2f" % (result["mean_luma"], args.max_mean_luma)
        )
    return failures


def format_result(result, failures):
    size = result["size"]
    status = "PASS" if not failures else "FAIL"
    print(
        "%s: %s size=%dx%d pixels=%d unique=%d dominant=%.2f%% "
        "black=%.2f%% white=%.2f%% mean_luma=%.2f mean_rgb=(%.1f,%.1f,%.1f)"
        % (
            status,
            result["path"],
            size["width"],
            size["height"],
            result["pixels"],
            result["unique_colors"],
            result["dominant_pct"],
            result["black_pct"],
            result["white_pct"],
            result["mean_luma"],
            result["mean_rgb"]["r"],
            result["mean_rgb"]["g"],
            result["mean_rgb"]["b"],
        )
    )
    for failure in failures:
        print("  - %s" % failure)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("images", nargs="+", help="screenshot image paths")
    parser.add_argument("--label", default="screenshot health", help="summary label")
    parser.add_argument(
        "--expect-size",
        type=parse_size,
        help="require WIDTHxHEIGHT, for example 640x480",
    )
    parser.add_argument(
        "--min-unique-colors",
        type=lambda value: positive_int(value, "--min-unique-colors"),
        default=128,
        help="minimum distinct RGB colors (default: 128)",
    )
    parser.add_argument(
        "--max-dominant-pct",
        type=lambda value: finite_percent(value, "--max-dominant-pct"),
        default=95.0,
        help="maximum percentage occupied by one RGB color (default: 95)",
    )
    parser.add_argument(
        "--max-black-pct",
        type=lambda value: finite_percent(value, "--max-black-pct"),
        default=90.0,
        help="maximum near-black pixel percentage (default: 90)",
    )
    parser.add_argument(
        "--max-white-pct",
        type=lambda value: finite_percent(value, "--max-white-pct"),
        default=90.0,
        help="maximum near-white pixel percentage (default: 90)",
    )
    parser.add_argument(
        "--min-mean-luma",
        type=lambda value: non_negative_float(value, "--min-mean-luma"),
        default=1.0,
        help="minimum mean luma (default: 1)",
    )
    parser.add_argument(
        "--max-mean-luma",
        type=lambda value: non_negative_float(value, "--max-mean-luma"),
        default=254.0,
        help="maximum mean luma (default: 254)",
    )
    parser.add_argument("--json-out", help="write machine-readable audit results")
    args = parser.parse_args(argv)

    all_results = []
    failed = False

    print("=== %s ===" % args.label)
    for path in args.images:
        result = None
        failures = []

        if not os.path.isfile(path):
            result = {"path": path, "error": "missing file"}
            failures = ["missing file"]
        elif os.path.getsize(path) <= 0:
            result = {"path": path, "error": "empty file"}
            failures = ["empty file"]
        else:
            try:
                result = analyze_image(path)
                failures = check_result(result, args)
            except RuntimeError as exc:
                result = {"path": path, "error": str(exc)}
                failures = [str(exc)]
            except FileNotFoundError:
                result = {"path": path, "error": "missing file"}
                failures = ["missing file"]

        result["failures"] = failures
        all_results.append(result)
        if failures:
            failed = True
            if "size" in result:
                format_result(result, failures)
            else:
                print("FAIL: %s" % path)
                for failure in failures:
                    print("  - %s" % failure)
        else:
            format_result(result, failures)

    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as handle:
            json.dump(
                {
                    "label": args.label,
                    "ok": not failed,
                    "thresholds": {
                        "expect_size": args.expect_size,
                        "min_unique_colors": args.min_unique_colors,
                        "max_dominant_pct": args.max_dominant_pct,
                        "max_black_pct": args.max_black_pct,
                        "max_white_pct": args.max_white_pct,
                        "min_mean_luma": args.min_mean_luma,
                        "max_mean_luma": args.max_mean_luma,
                    },
                    "images": all_results,
                },
                handle,
                indent=2,
                sort_keys=True,
            )
            handle.write("\n")

    if failed:
        print("FAIL: %s" % args.label)
        return 1
    print("PASS: %s" % args.label)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
