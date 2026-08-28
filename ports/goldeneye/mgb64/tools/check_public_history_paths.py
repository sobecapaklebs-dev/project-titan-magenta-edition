#!/usr/bin/env python3
"""Check reachable git history for launch-blocking public path provenance."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


LOCAL_ONLY_IDO_ALLOWED = {
    "tools/ido5.3_recomp/.gitignore",
    "tools/ido5.3_recomp/README.md",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Check public git history path provenance.")
    parser.add_argument("--repo-root", default=".", help="repository root")
    return parser.parse_args()


def run_text(root: Path, args: list[str]) -> str:
    result = subprocess.run(
        ["git", "-C", str(root), *args],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return result.stdout


def history_paths(root: Path) -> set[str]:
    output = run_text(root, ["log", "--all", "--name-only", "--format="])
    return {line.strip() for line in output.splitlines() if line.strip()}


def newest_touch(root: Path, path: str) -> str:
    output = run_text(root, ["log", "--all", "--format=%H", "--", path])
    return (output.splitlines() or ["unknown"])[0]


def main() -> int:
    args = parse_args()
    root = Path(args.repo_root).resolve()

    if not (root / ".git").exists():
        print("SKIP: not a git checkout; no reachable history to scan")
        return 0

    problems: list[tuple[str, str]] = []
    for path in sorted(history_paths(root)):
        if path.startswith("tools/ido5.3_recomp/") and path not in LOCAL_ONLY_IDO_ALLOWED:
            problems.append((path, "local-only IDO recompilation source/tooling path"))

    if problems:
        print("FAIL: reachable git history contains launch-blocking public path(s).", file=sys.stderr)
        for path, reason in problems:
            commit = newest_touch(root, path)
            print(f"  - {path}: {reason}; touched by {commit[:12]}", file=sys.stderr)
        print(
            "Remove these paths with an approved history rewrite, or publish from a fresh "
            "single-root launch repository created from the current clean tree.",
            file=sys.stderr,
        )
        return 1

    print("PASS: no launch-blocking public paths found in reachable git history")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
