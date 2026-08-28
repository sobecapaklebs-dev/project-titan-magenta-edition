#!/usr/bin/env python3
"""Guard the native BG visibility bytecode opcode map.

The native C implementation of parse_global_vis_command_list must stay aligned
with the original vis_command_jpt/jpt_80058C80 tables kept beside it in bg.c.
An off-by-one in this map can silently drop authored visibility packets and
leave whole room groups invisible.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


LOW_TABLE = {
    0x00: "break",
    0x01: "push_to_stack",
    0x02: "pull_from_stack",
    0x03: "and_merge_last_two_on_stack",
    0x04: "or_merge_last_two_on_stack",
    0x05: "not_merge_last_two_on_stack",
    0x06: "carrot_merge_last_two_on_stack",
    **{op: "invalid_type_terminate" for op in range(0x07, 0x14)},
    0x14: "push_tf_if_in_range_rooms",
    **{op: "invalid_type_terminate" for op in range(0x15, 0x1E)},
    0x1E: "force_visible",
    0x1F: "match_portal_vis",
    0x20: "add_visible_room",
    0x21: "remove_vis",
    0x22: "visible_if_seen_through_portal",
    0x23: "not_visible_if_seen_through_portal",
    0x24: "disable_room",
    0x25: "disable_room_range",
    0x26: "preload_room",
    0x27: "preload_room_range",
}

HIGH_TABLE = {
    0x50: "if_statement",
    0x51: "dont_exec_commands_even_on_return",
    0x52: "endif_continue_exec",
    **{op: "invalid_type_terminate" for op in range(0x53, 0x5A)},
    0x5A: "if_statement_pull_from_stack",
    0x5B: "toggle_exec_vs_ro",
    0x5C: "endif",
}


def extract_tables(text: str, label: str) -> list[list[str]]:
    tables: list[list[str]] = []
    pattern = re.compile(
        rf"glabel {re.escape(label)}\n(?P<body>.*?)(?=\n(?:/\*|\.text|glabel ))",
        re.S,
    )

    for match in pattern.finditer(text):
        words = re.findall(r"^\s*\.word\s+([A-Za-z0-9_]+)\s*$",
                           match.group("body"),
                           re.M)
        if words:
            tables.append(words)

    return tables


def table_from_words(words: list[str], start_opcode: int) -> dict[int, str]:
    return {start_opcode + index: word for index, word in enumerate(words)}


def extract_c_function(text: str) -> str:
    marker = "void *parse_global_vis_command_list(u8 *pc, s32 flag)"
    start = text.find(marker)
    if start < 0:
        raise ValueError("missing native C parse_global_vis_command_list")

    end = text.find("\n#else", start)
    if end < 0:
        raise ValueError("could not isolate native C parse_global_vis_command_list")

    return text[start:end]


def check_asm_tables(failures: list[str], text: str) -> None:
    expected_tables = {
        "vis_command_jpt": (0x00, LOW_TABLE),
        "jpt_80058C80": (0x50, HIGH_TABLE),
    }

    for label, (start_opcode, expected) in expected_tables.items():
        tables = extract_tables(text, label)
        if not tables:
            failures.append(f"missing original jump table {label}")
            continue

        for table_index, words in enumerate(tables):
            actual = table_from_words(words, start_opcode)
            if actual != expected:
                failures.append(
                    f"{label} copy {table_index} does not match expected opcode map"
                )


def require_comment_case(failures: list[str], c_text: str, opcode: int, label: str) -> None:
    pattern = re.compile(
        rf"/\*\s*0x{opcode:02X}:\s*{re.escape(label)}(?:\s*\([^*]*\))?\s*\*/"
        rf"\s*case\s+0x{opcode:02X}\s*:",
        re.I,
    )
    if not pattern.search(c_text):
        failures.append(f"native C switch does not map 0x{opcode:02X} to {label}")


def check_c_switch(failures: list[str], text: str) -> None:
    c_text = extract_c_function(text)

    for opcode, label in LOW_TABLE.items():
        if label != "invalid_type_terminate":
            require_comment_case(failures, c_text, opcode, label)

    invalid_group = re.search(
        r"/\*\s*0x15-0x1D:\s*invalid\s*\*/(?P<body>.*?)goto\s+invalid_type_terminate;",
        c_text,
        re.S,
    )
    if invalid_group is None:
        failures.append("native C switch is missing the 0x15-0x1D invalid group")
    else:
        body = invalid_group.group("body")
        for opcode in range(0x15, 0x1E):
            if not re.search(rf"case\s+0x{opcode:02X}\s*:", body, re.I):
                failures.append(f"invalid opcode group is missing case 0x{opcode:02X}")

    shifted_patterns = (
        ("case 0x1D:", "force_visible"),
        ("case 0x1E:", "match_portal_vis"),
        ("case 0x1F:", "add_visible_room"),
        ("case 0x20:", "remove_vis"),
    )
    for case_label, old_name in shifted_patterns:
        old_map = re.compile(
            rf"/\*\s*0x[0-9A-Fa-f]+:\s*{re.escape(old_name)}\s*\*/\s*"
            rf"{re.escape(case_label)}"
        )
        if old_map.search(c_text):
            failures.append(f"native C switch still has shifted {old_name} at {case_label}")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", default=".")
    args = parser.parse_args(argv)

    root = Path(args.repo_root)
    bg_path = root / "src/game/bg.c"
    if not bg_path.exists():
        print("FAIL: missing src/game/bg.c", file=sys.stderr)
        return 1

    text = bg_path.read_text(encoding="utf-8", errors="replace")
    failures: list[str] = []

    try:
        check_asm_tables(failures, text)
        check_c_switch(failures, text)
    except ValueError as exc:
        failures.append(str(exc))

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1

    print("PASS: BG visibility opcode map matches original jump tables")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
