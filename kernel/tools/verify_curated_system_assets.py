#!/usr/bin/env python3
"""Verify that Project Titan curated UI assets still match the protected set."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "assets" / "ui",
    )
    parser.add_argument("--manifest", type=Path, default=None)
    args = parser.parse_args()

    asset_root = args.root.resolve()
    manifest = (args.manifest or asset_root / "CURATED_ASSET_SHA256SUMS").resolve()
    if not manifest.is_file():
        print(f"curated-assets: missing manifest: {manifest}")
        return 1

    failures: list[str] = []
    checked = 0
    for line_number, raw_line in enumerate(
        manifest.read_text(encoding="utf-8").splitlines(), 1
    ):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        try:
            expected, relative = line.split(None, 1)
        except ValueError:
            failures.append(f"manifest line {line_number}: malformed")
            continue
        relative = relative.strip().lstrip("*")
        candidate = (asset_root / relative).resolve()
        try:
            candidate.relative_to(asset_root)
        except ValueError:
            failures.append(f"manifest line {line_number}: unsafe path {relative}")
            continue
        if not candidate.is_file():
            failures.append(f"missing: {relative}")
            continue
        actual = sha256(candidate)
        if actual != expected.lower():
            failures.append(f"changed: {relative} ({actual})")
        checked += 1

    if failures:
        print("curated-assets: FAILED")
        for failure in failures:
            print(f"  {failure}")
        return 1

    print(f"curated-assets: OK files={checked}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
