#!/usr/bin/env python3
"""Check local Markdown links and anchors in tracked public docs."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import urllib.parse
from pathlib import Path


INLINE_LINK_RE = re.compile(r"(?<!!)\[[^\]\n]+\]\(([^)\n]+)\)")
IMAGE_LINK_RE = re.compile(r"!\[[^\]\n]*\]\(([^)\n]+)\)")
REFERENCE_LINK_RE = re.compile(r"^\s*\[[^\]]+\]:\s*(\S+)", re.MULTILINE)
HEADING_RE = re.compile(r"^(#{1,6})\s+(.+?)\s*#*\s*$")
HTML_TAG_RE = re.compile(r"<[^>]+>")
MARKDOWN_DECORATION_RE = re.compile(r"[`*_~]")
LINK_TEXT_RE = re.compile(r"\[([^\]]+)\]\([^)]+\)")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate local Markdown links and GitHub-style heading anchors."
    )
    parser.add_argument(
        "--repo-root",
        default=None,
        help="repository root; defaults to git top-level or current directory",
    )
    return parser.parse_args()


def git_root() -> Path:
    try:
        out = subprocess.check_output(
            ["git", "rev-parse", "--show-toplevel"],
            stderr=subprocess.DEVNULL,
            text=True,
        )
        return Path(out.strip()).resolve()
    except (OSError, subprocess.CalledProcessError):
        return Path.cwd().resolve()


def tracked_markdown_files(root: Path) -> list[Path]:
    try:
        out = subprocess.check_output(
            ["git", "ls-files", "*.md", "*.markdown"],
            cwd=root,
            stderr=subprocess.DEVNULL,
            text=True,
        )
        files = [root / line for line in out.splitlines() if line]
        if files:
            return sorted(files)
    except (OSError, subprocess.CalledProcessError):
        pass

    skipped_dirs = {".git", "build", "dist", "__pycache__"}
    return sorted(
        path
        for path in root.rglob("*")
        if path.suffix.lower() in {".md", ".markdown"}
        and not any(part in skipped_dirs for part in path.parts)
    )


def strip_fenced_code(text: str) -> str:
    lines: list[str] = []
    in_fence = False
    fence_marker = ""
    for line in text.splitlines():
        stripped = line.lstrip()
        if not in_fence and (stripped.startswith("```") or stripped.startswith("~~~")):
            in_fence = True
            fence_marker = stripped[:3]
            lines.append("")
            continue
        if in_fence and stripped.startswith(fence_marker):
            in_fence = False
            fence_marker = ""
            lines.append("")
            continue
        lines.append("" if in_fence else line)
    return "\n".join(lines)


def slug_heading(text: str) -> str:
    text = HTML_TAG_RE.sub("", text)
    text = LINK_TEXT_RE.sub(r"\1", text)
    text = MARKDOWN_DECORATION_RE.sub("", text)
    text = text.strip().lower()

    chars: list[str] = []
    for char in text:
        if char.isalnum() or char in {"_", "-", " "}:
            chars.append(char)
    return "".join(chars).replace(" ", "-")


def anchors_for(path: Path) -> set[str]:
    text = strip_fenced_code(path.read_text(encoding="utf-8", errors="replace"))
    anchors: set[str] = set()
    seen: dict[str, int] = {}
    for line in text.splitlines():
        match = HEADING_RE.match(line)
        if not match:
            continue
        base = slug_heading(match.group(2))
        count = seen.get(base, 0)
        seen[base] = count + 1
        anchors.add(base if count == 0 else f"{base}-{count}")
    return anchors


def extract_target(raw: str) -> str:
    raw = raw.strip()
    if raw.startswith("<") and ">" in raw:
        return raw[1 : raw.index(">")]
    return raw.split()[0]


def is_external(target: str) -> bool:
    parsed = urllib.parse.urlparse(target)
    return bool(parsed.scheme) or target.startswith("//")


def display_path(root: Path, path: Path) -> str:
    return path.relative_to(root).as_posix()


def validate_link(
    root: Path,
    source: Path,
    target: str,
    file_anchors: dict[Path, set[str]],
) -> str | None:
    target = extract_target(target)
    if not target or is_external(target):
        return None

    path_text, has_fragment, fragment = target.partition("#")
    fragment = urllib.parse.unquote(fragment)

    if not path_text:
        resolved = source
    else:
        path_text = urllib.parse.unquote(path_text)
        candidate = (source.parent / path_text).resolve()
        try:
            resolved = candidate.relative_to(root)
        except ValueError:
            return f"points outside repository: {target}"
        resolved = root / resolved

    if path_text and not resolved.exists():
        return f"missing target: {target}"

    if has_fragment and resolved.is_file() and resolved.suffix.lower() in {".md", ".markdown"}:
        if fragment and fragment not in file_anchors.get(resolved, set()):
            return f"missing anchor '#{fragment}' in {display_path(root, resolved)}"

    return None


def main() -> int:
    args = parse_args()
    root = Path(args.repo_root).resolve() if args.repo_root else git_root()
    files = tracked_markdown_files(root)
    file_anchors = {path: anchors_for(path) for path in files}
    problems: list[str] = []

    for path in files:
        text = strip_fenced_code(path.read_text(encoding="utf-8", errors="replace"))
        for regex in (INLINE_LINK_RE, IMAGE_LINK_RE, REFERENCE_LINK_RE):
            for match in regex.finditer(text):
                issue = validate_link(root, path, match.group(1), file_anchors)
                if issue:
                    problems.append(f"{display_path(root, path)}: {issue}")

    if problems:
        print("Markdown link check failed:", file=sys.stderr)
        for problem in problems:
            print(f"  {problem}", file=sys.stderr)
        return 1

    print(f"PASS: checked {len(files)} Markdown files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
