#!/usr/bin/env python3
"""
W5.E2.T1 guard — validate the in-game settings-menu curation table against the
live settings registry.

Fails when:
  * a curation-table `.key` (in src/game/pc_settings_menu.c) has NO matching
    settingsRegister*("<key>", ...) call in platform_sdl.c / audio_pc.c, UNLESS
    that entry is tagged SM_FUTURE (the W6 audio bus rows a later workstream
    registers);
  * a page's declared `count` disagrees with the number of rows in its SmEntry
    array (page/entry self-consistency);
  * an SmEntry array is orphaned (never referenced by g_smPages) or a page
    references a missing array;
  * the model does not curate exactly 7 pages.

ROM-free. Exits 0 on success, 1 on any failure.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

DEFAULT_REPO_ROOT = Path(__file__).resolve().parent.parent

# Pages the model is expected to curate (doc 05 §4.2).
EXPECTED_PAGE_COUNT = 7

TABLE_REL = Path("src/game/pc_settings_menu.c")
REGISTRY_RELS = [
    Path("src/platform/platform_sdl.c"),
    Path("src/platform/audio_pc.c"),
]

REGISTER_RE = re.compile(r'settingsRegister\w+\s*\(\s*"([^"]+)"')
BLOCK_RE = re.compile(
    r"static\s+const\s+SmEntry\s+(\w+)\s*\[\s*\]\s*=\s*\{(.*?)\};",
    re.DOTALL,
)
PAGES_RE = re.compile(
    r"const\s+SmPage\s+g_smPages\s*\[\s*\]\s*=\s*\{(.*?)\};",
    re.DOTALL,
)
PAGE_ROW_RE = re.compile(
    r'\{\s*"([^"]*)"\s*,\s*(\w+)\s*,\s*(\d+)\s*\}',
)
ENTRY_ROW_RE = re.compile(r"^\s*\{(.*)\}\s*,?\s*$")
FIRST_STRING_RE = re.compile(r'^\s*"((?:[^"\\]|\\.)*)"')
FUTURE_TOKEN_RE = re.compile(r"\bSM_FUTURE\b")


class TableEntry:
    __slots__ = ("array", "key", "future")

    def __init__(self, array: str, key: str | None, future: bool) -> None:
        self.array = array
        self.key = key
        self.future = future


def strip_line_comment(text: str) -> str:
    # Remove a trailing /* ... */ block comment (single line) so the note text
    # inside a comment can never be mistaken for a token.
    return re.sub(r"/\*.*?\*/", "", text)


def parse_entry_rows(body: str) -> list[TableEntry]:
    entries: list[TableEntry] = []
    for raw in body.splitlines():
        line = strip_line_comment(raw)
        m = ENTRY_ROW_RE.match(line)
        if not m:
            continue
        inner = m.group(1).strip()
        future = bool(FUTURE_TOKEN_RE.search(inner))
        if inner.startswith("NULL"):
            key = None
        else:
            sm = FIRST_STRING_RE.match(inner)
            key = sm.group(1) if sm else None
        entries.append(TableEntry(array="", key=key, future=future))
    return entries


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--repo-root", type=Path, default=DEFAULT_REPO_ROOT)
    args = ap.parse_args()
    repo = args.repo_root.resolve()

    errors: list[str] = []

    # --- Load registered keys -------------------------------------------------
    registered: set[str] = set()
    for rel in REGISTRY_RELS:
        path = repo / rel
        if not path.is_file():
            errors.append(f"registry source missing: {rel}")
            continue
        registered.update(REGISTER_RE.findall(path.read_text(encoding="utf-8")))

    if not registered:
        errors.append("no settingsRegister*() keys found in registry sources")

    # --- Load curation table --------------------------------------------------
    table_path = repo / TABLE_REL
    if not table_path.is_file():
        print(f"FAIL: curation table missing: {TABLE_REL}", file=sys.stderr)
        return 1
    table_src = table_path.read_text(encoding="utf-8")

    # SmEntry arrays: name -> list[TableEntry]
    arrays: dict[str, list[TableEntry]] = {}
    for m in BLOCK_RE.finditer(table_src):
        name = m.group(1)
        rows = parse_entry_rows(m.group(2))
        for r in rows:
            r.array = name
        arrays[name] = rows

    if not arrays:
        print("FAIL: no SmEntry arrays found in curation table", file=sys.stderr)
        return 1

    # Page table
    pages_m = PAGES_RE.search(table_src)
    if not pages_m:
        print("FAIL: g_smPages[] table not found", file=sys.stderr)
        return 1
    page_rows = PAGE_ROW_RE.findall(pages_m.group(1))

    # --- Page/entry self-consistency -----------------------------------------
    if len(page_rows) != EXPECTED_PAGE_COUNT:
        errors.append(
            f"model curates {len(page_rows)} pages; expected {EXPECTED_PAGE_COUNT}"
        )

    referenced: set[str] = set()
    for title, array_name, declared in page_rows:
        declared_n = int(declared)
        referenced.add(array_name)
        if array_name not in arrays:
            errors.append(
                f'page "{title}" references unknown array {array_name}'
            )
            continue
        actual_n = len(arrays[array_name])
        if declared_n != actual_n:
            errors.append(
                f'page "{title}" ({array_name}) declares count={declared_n} '
                f"but array has {actual_n} rows"
            )

    for name in arrays:
        if name not in referenced:
            errors.append(f"SmEntry array {name} is never referenced by g_smPages")

    # --- Registration check ---------------------------------------------------
    keyed = 0
    future_keys: list[str] = []
    for name, rows in arrays.items():
        for r in rows:
            if r.key is None:
                continue  # HEADER / ACTION row
            keyed += 1
            if r.future:
                future_keys.append(r.key)
                continue  # whitelisted regardless of registration
            if r.key not in registered:
                errors.append(
                    f"table key not registered and not SM_FUTURE: "
                    f"{r.key} (in {name})"
                )

    # --- Report ---------------------------------------------------------------
    if errors:
        print("check_settings_menu_model: FAIL", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1

    registered_in_table = keyed - len(future_keys)
    print("check_settings_menu_model: OK")
    print(f"  pages                : {len(page_rows)}")
    print(f"  keyed rows           : {keyed}")
    print(f"  registered keys used : {registered_in_table}")
    print(f"  SM_FUTURE rows       : {len(future_keys)} ({', '.join(future_keys)})")
    print(f"  registry keys total  : {len(registered)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
