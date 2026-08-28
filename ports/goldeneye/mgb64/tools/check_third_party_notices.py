#!/usr/bin/env python3
"""Validate public third-party notice coverage.

This is intentionally narrow: it checks the specific vendored components whose
license/provenance posture matters for public source archives.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Check MGB64 third-party notice coverage.")
    parser.add_argument("--repo-root", default=".", help="repository root")
    return parser.parse_args()


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def tracked_files(root: Path, rel_dir: str) -> set[str] | None:
    if not (root / ".git").exists():
        return None
    result = subprocess.run(
        ["git", "-C", str(root), "ls-files", rel_dir],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode != 0:
        return None
    prefix = rel_dir.rstrip("/") + "/"
    return {
        line[len(prefix) :]
        for line in result.stdout.splitlines()
        if line.startswith(prefix)
    }


def archive_files(root: Path, rel_dir: str) -> set[str]:
    base = root / rel_dir
    if not base.exists():
        return set()
    return {
        str(path.relative_to(base))
        for path in base.rglob("*")
        if path.is_file()
    }


def main() -> int:
    args = parse_args()
    root = Path(args.repo_root).resolve()
    problems: list[str] = []

    required_files = {
        "lib/glad/LICENSE": ("The glad source code", "Copyright (c) 2013-2021 David Herberth"),
        "lib/glad/README.md": ("OpenGL function loader", "LICENSE"),
        "tools/asm-processor/LICENSE": ("This is free and unencumbered software",),
        "tools/gzipsrc/COPYING": ("GNU GENERAL PUBLIC LICENSE",),
        "tools/extractor/puff.h": ("Copyright (C) 2002-2013 Mark Adler",),
        "tools/mktex/LICENSE.perfect_dark": ("Copyright (c) 2022 Ryan Dwyer",),
        "tools/mktex/PROVENANCE.md": ("Perfect Dark decompilation", "LICENSE.perfect_dark"),
        "tools/ido5.3_recomp/README.md": (
            "does not vendor IDO static-recompilation source",
            "must not be committed or redistributed",
        ),
        "tools/smaa/LICENSE": (
            "Copyright (C) 2013 Jorge Jimenez",
            "Permission is hereby granted, free of charge",
        ),
        "tools/smaa/README.md": (
            "github.com/iryoku/smaa",
            "MIT",
            "gen_luts.py",
        ),
        "tools/smaa/AreaTex.py": ("This texture allows to obtain the area",),
        "tools/smaa/SearchTex.py": ("This texture allows to know how many pixels",),
        "src/platform/fast3d/PROVENANCE.md": (
            "Copyright (c) 2020 Emill, MaikelChan",
            "modified BSD-2-Clause",
            "binary contains no assets you do not have the right to distribute",
        ),
    }

    for rel_path, needles in required_files.items():
        path = root / rel_path
        if not path.is_file():
            problems.append(f"missing third-party notice file: {rel_path}")
            continue
        text = read_text(path)
        for needle in needles:
            if needle not in text:
                problems.append(f"{rel_path} is missing expected notice text: {needle}")

    armips = root / "tools/armips.cpp"
    if not armips.is_file():
        problems.append("missing armips source: tools/armips.cpp")
    else:
        armips_text = read_text(armips)
        if "The MIT License (MIT)" not in armips_text or "Copyright (c) 2009-2020 Kingcom" not in armips_text:
            problems.append("tools/armips.cpp is missing the armips MIT notice")
        if "Boost Software License - Version 1.0" not in armips_text:
            problems.append("tools/armips.cpp is missing the embedded tinyformat Boost notice")

    third_party = root / "THIRD_PARTY.md"
    if third_party.is_file():
        third_party_text = read_text(third_party)
        for needle in (
            "lib/glad/LICENSE",
            "LICENSE.perfect_dark",
            "tinyformat Boost Software License",
            "No recompilation source or IDO/IRIX compiler input files are redistributed here",
            "tools/smaa/LICENSE",
            "SMAA reference lookup-table generator",
            "n64-fast3d-engine",
            "binary redistribution restricted to asset-free binaries",
        ):
            if needle not in third_party_text:
                problems.append(f"THIRD_PARTY.md is missing expected text: {needle}")
    else:
        problems.append("missing THIRD_PARTY.md")

    notice = root / "NOTICE.md"
    if notice.is_file():
        notice_text = read_text(notice)
        for needle in (
            "SMAA",
            "github.com/iryoku/smaa",
            "Copyright (C) 2013 Jorge Jimenez",
            "Permission is hereby granted, free of charge",
        ):
            if needle not in notice_text:
                problems.append(f"NOTICE.md is missing expected SMAA notice text: {needle}")
    else:
        problems.append("missing NOTICE.md")

    allowed_ido_files = {".gitignore", "README.md"}
    ido_files = tracked_files(root, "tools/ido5.3_recomp")
    if ido_files is None:
        ido_files = archive_files(root, "tools/ido5.3_recomp")
    unexpected_ido_files = sorted(ido_files - allowed_ido_files)
    if unexpected_ido_files:
        problems.append(
            "tools/ido5.3_recomp must remain a local-only placeholder; "
            f"unexpected public file(s): {', '.join(unexpected_ido_files)}"
        )

    if problems:
        print("FAIL: third-party notice guard found issue(s):", file=sys.stderr)
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)
        return 1

    print("PASS: third-party notice guard passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
