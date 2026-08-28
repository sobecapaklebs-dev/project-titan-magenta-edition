#!/usr/bin/env python3
"""Summarize compiler/linker warnings from a build log."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from pathlib import Path


WARNING_RE = re.compile(r"^(?P<prefix>.*?)(?:warning:|Warning:)\s*(?P<message>.*)$")
FILE_WARNING_RE = re.compile(
    r"^(?P<file>[^:\s][^:]*):(?P<line>\d+)(?::(?P<col>\d+))?:\s*(?:warning:|Warning:)\s*(?P<message>.*)$"
)
OPTION_RE = re.compile(r"\[(-W[^\]]+)\]")


def read_lines(paths: list[str]) -> list[str]:
    if not paths:
        return [line.rstrip("\n") for line in sys.stdin]

    lines: list[str] = []
    for path in paths:
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            lines.extend(line.rstrip("\n") for line in handle)
    return lines


def normalize_message(message: str) -> str:
    message = OPTION_RE.sub("", message)
    return " ".join(message.split())


def classify(prefix: str, message: str) -> str:
    option_match = OPTION_RE.search(message)
    if option_match:
        return option_match.group(1)

    text = f"{prefix} {message}".lower()
    if prefix.strip().startswith(("ld", "collect2")) or "linker" in text:
        return "linker"
    if "strict-alias" in text or "type-pun" in text:
        return "strict-aliasing"
    if "maybe-uninitialized" in text or "uninitialized" in text:
        return "uninitialized"
    if "array bounds" in text or "array subscript" in text:
        return "array-bounds"
    if "packed" in text and "member" in text:
        return "packed-member"
    if "format" in text and ("trunc" in text or "overflow" in text):
        return "format"
    if "enum" in text:
        return "enum"
    return "unclassified"


def warning_file(line: str) -> str:
    match = FILE_WARNING_RE.match(line)
    if match:
        return match.group("file")
    if line.startswith("ld:") or line.startswith("collect2:"):
        return "<linker>"
    return "<unknown>"


def parse_warnings(lines: list[str]) -> list[dict[str, str]]:
    warnings: list[dict[str, str]] = []
    for index, line in enumerate(lines, start=1):
        match = WARNING_RE.match(line)
        if not match:
            continue
        prefix = match.group("prefix")
        message = match.group("message")
        category = classify(prefix, message)
        warnings.append(
            {
                "log_line": str(index),
                "file": warning_file(line),
                "category": category,
                "message": normalize_message(message),
                "raw": line,
            }
        )
    return warnings


def summarize(warnings: list[dict[str, str]]) -> dict[str, object]:
    by_category = Counter(warning["category"] for warning in warnings)
    by_file = Counter(warning["file"] for warning in warnings)
    by_message = Counter(
        (warning["category"], warning["file"], warning["message"])
        for warning in warnings
    )

    return {
        "total": len(warnings),
        "by_category": dict(sorted(by_category.items(), key=lambda item: (-item[1], item[0]))),
        "by_file": dict(sorted(by_file.items(), key=lambda item: (-item[1], item[0]))),
        "messages": [
            {
                "count": count,
                "category": category,
                "file": file,
                "message": message,
            }
            for (category, file, message), count in sorted(
                by_message.items(),
                key=lambda item: (-item[1], item[0][0], item[0][1], item[0][2]),
            )
        ],
        "warnings": warnings,
    }


def print_summary(summary: dict[str, object], top: int) -> None:
    total = int(summary["total"])
    if total == 0:
        print("PASS: no warnings found")
        return

    print(f"Warnings: {total}")
    print("")
    print("By category:")
    for category, count in list(summary["by_category"].items())[:top]:  # type: ignore[union-attr]
        print(f"  {count:5d}  {category}")

    print("")
    print("Top files:")
    for file, count in list(summary["by_file"].items())[:top]:  # type: ignore[union-attr]
        print(f"  {count:5d}  {file}")

    print("")
    print("Top messages:")
    for item in summary["messages"][:top]:  # type: ignore[index]
        print(
            f"  {item['count']:5d}  {item['category']}  {item['file']}: {item['message']}"
        )


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="*", help="build logs to scan; stdin is used when omitted")
    parser.add_argument("--json-out", help="write machine-readable summary JSON")
    parser.add_argument("--top", type=int, default=12, help="number of rows per text section")
    parser.add_argument(
        "--max-total",
        type=int,
        default=None,
        help="fail if the total warning count exceeds this threshold",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        warnings = parse_warnings(read_lines(args.logs))
    except OSError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    summary = summarize(warnings)
    print_summary(summary, args.top)

    if args.json_out:
        out_path = Path(args.json_out)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        with out_path.open("w", encoding="utf-8") as handle:
            json.dump(summary, handle, indent=2, sort_keys=True)
            handle.write("\n")

    if args.max_total is not None and int(summary["total"]) > args.max_total:
        print(
            f"FAIL: warning count {summary['total']} exceeds --max-total {args.max_total}",
            file=sys.stderr,
        )
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
