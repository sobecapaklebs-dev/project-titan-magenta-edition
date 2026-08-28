#!/usr/bin/env python3
"""
Fail if native-compiled game sources contain raw ModelFileHeader->Switches[]
access outside approved low-level helper internals.
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from enum import Enum
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET_ROOT = REPO_ROOT / "src" / "game"
ALLOWLIST = {
    TARGET_ROOT / "model.c",
}
SWITCH_RE = re.compile(r"\bSwitches\s*\[")
DIRECTIVE_RE = re.compile(r"^\s*#\s*(\w+)(.*)$")


@dataclass
class Frame:
    parent_active: bool
    current_active: bool
    any_definitely_taken: bool


class TriState(Enum):
    FALSE = 0
    TRUE = 1
    MAYBE = 2


def strip_comments(line: str, in_block_comment: bool) -> tuple[str, bool]:
    out: list[str] = []
    i = 0

    while i < len(line):
        if in_block_comment:
            end = line.find("*/", i)
            if end == -1:
                return "".join(out), True
            i = end + 2
            in_block_comment = False
            continue

        if line.startswith("/*", i):
            in_block_comment = True
            i += 2
            continue

        if line.startswith("//", i):
            break

        out.append(line[i])
        i += 1

    return "".join(out), in_block_comment


TOKEN_RE = re.compile(
    r"""
    \s*(
        && |
        \|\| |
        ! |
        \(
        | \)
        | defined
        | [A-Za-z_]\w*
        | \d+
    )
    """,
    re.VERBOSE,
)


def tri_not(value: TriState) -> TriState:
    if value == TriState.TRUE:
        return TriState.FALSE
    if value == TriState.FALSE:
        return TriState.TRUE
    return TriState.MAYBE


def tri_and(left: TriState, right: TriState) -> TriState:
    if left == TriState.FALSE or right == TriState.FALSE:
        return TriState.FALSE
    if left == TriState.TRUE and right == TriState.TRUE:
        return TriState.TRUE
    return TriState.MAYBE


def tri_or(left: TriState, right: TriState) -> TriState:
    if left == TriState.TRUE or right == TriState.TRUE:
        return TriState.TRUE
    if left == TriState.FALSE and right == TriState.FALSE:
        return TriState.FALSE
    return TriState.MAYBE


class ExprParser:
    def __init__(self, expr: str) -> None:
        self.tokens = TOKEN_RE.findall(expr)
        self.index = 0

    def peek(self) -> str | None:
        if self.index >= len(self.tokens):
            return None
        return self.tokens[self.index]

    def pop(self) -> str | None:
        token = self.peek()
        if token is not None:
            self.index += 1
        return token

    def parse(self) -> TriState | None:
        value = self.parse_or()
        if value is None or self.peek() is not None:
            return None
        return value

    def parse_or(self) -> TriState | None:
        value = self.parse_and()
        if value is None:
            return None

        while self.peek() == "||":
            self.pop()
            right = self.parse_and()
            if right is None:
                return None
            value = tri_or(value, right)

        return value

    def parse_and(self) -> TriState | None:
        value = self.parse_unary()
        if value is None:
            return None

        while self.peek() == "&&":
            self.pop()
            right = self.parse_unary()
            if right is None:
                return None
            value = tri_and(value, right)

        return value

    def parse_unary(self) -> TriState | None:
        token = self.peek()

        if token == "!":
            self.pop()
            value = self.parse_unary()
            if value is None:
                return None
            return tri_not(value)

        if token == "(":
            self.pop()
            value = self.parse_or()
            if value is None or self.pop() != ")":
                return None
            return value

        return self.parse_primary()

    def parse_primary(self) -> TriState | None:
        token = self.pop()

        if token is None:
            return None

        if token == "defined":
            next_token = self.pop()

            if next_token == "(":
                ident = self.pop()
                if ident is None or self.pop() != ")":
                    return None
            else:
                ident = next_token

            if ident == "NATIVE_PORT":
                return TriState.TRUE

            return TriState.MAYBE

        if token == "0":
            return TriState.FALSE

        if token.isdigit():
            return TriState.TRUE

        if token == "NATIVE_PORT":
            return TriState.TRUE

        return TriState.MAYBE


def eval_native_expr(expr: str) -> TriState | None:
    return ExprParser(expr.strip()).parse()


def scan_file(path: Path) -> list[str]:
    failures: list[str] = []
    stack = [Frame(True, True, False)]
    in_block_comment = False

    for lineno, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line, in_block_comment = strip_comments(raw_line, in_block_comment)
        directive = DIRECTIVE_RE.match(line)

        if directive:
            keyword = directive.group(1)
            rest = directive.group(2).strip()
            top = stack[-1]

            if keyword == "ifdef":
                is_native = rest == "NATIVE_PORT"
                active = top.current_active
                stack.append(Frame(top.current_active, active, is_native))
                continue

            if keyword == "ifndef":
                is_native = rest == "NATIVE_PORT"
                active = top.current_active and not is_native
                stack.append(Frame(top.current_active, active, False))
                continue

            if keyword == "if":
                native_eval = eval_native_expr(rest)
                if native_eval is None:
                    stack.append(Frame(top.current_active, top.current_active, False))
                else:
                    active = top.current_active and native_eval != TriState.FALSE
                    stack.append(Frame(top.current_active, active, native_eval == TriState.TRUE))
                continue

            if keyword == "elif":
                if len(stack) == 1:
                    continue
                frame = stack[-1]
                native_eval = eval_native_expr(rest)
                if native_eval is None:
                    frame.current_active = frame.parent_active and not frame.any_definitely_taken
                else:
                    frame.current_active = (
                        frame.parent_active
                        and not frame.any_definitely_taken
                        and native_eval != TriState.FALSE
                    )
                    frame.any_definitely_taken = frame.any_definitely_taken or native_eval == TriState.TRUE
                continue

            if keyword == "else":
                if len(stack) == 1:
                    continue
                frame = stack[-1]
                frame.current_active = frame.parent_active and not frame.any_definitely_taken
                frame.any_definitely_taken = True
                continue

            if keyword == "endif":
                if len(stack) > 1:
                    stack.pop()
                continue

        if not stack[-1].current_active:
            continue

        if SWITCH_RE.search(line):
            failures.append(f"{path.relative_to(REPO_ROOT)}:{lineno}: {raw_line.strip()}")

    return failures


def main() -> int:
    failures: list[str] = []

    for path in sorted(TARGET_ROOT.glob("*.c")):
        if path in ALLOWLIST:
            continue
        failures.extend(scan_file(path))

    if failures:
        print("FAIL: native-visible raw Switches[] access found:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1

    print("PASS: no native-visible raw Switches[] access outside allowlisted helpers")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
