#!/usr/bin/env python3
"""Generate a complete reference of GE007_* environment flags from the source.

The port gates behavior on GE007_* environment variables. This scans the tracked
C/C++ source for every flag reference and emits a Markdown catalog so the full
flag surface is documented (and can be diffed in CI against the docs, so it can't
silently drift). Four reference forms are recognised:
  - raw getenv("GE007_...")                             (type/default unknown)
  - the registering port_env_bool/int/float accessors  (typed, with default)
  - the presence accessor port_env_set (and ge_env_bool) (typed)
  - settingsRegister{Int,UInt,Float,Enum,String}(...)   mirror sites — the env
    var is a positional arg (after the SETTING_SCOPE_* token); its type and
    default come from the register function and its `def` argument, so flags that
    only exist as a Setting mirror (e.g. GE007_FIRE_RATE_AUTHENTIC) still land
    here with a type and default.

Usage:
  tools/gen_env_reference.py [--repo-root DIR] [--out FILE] [--check FILE]

  (no --out)     print the catalog to stdout
  --out FILE     write the catalog to FILE
  --check FILE   regenerate and compare to FILE; exit 1 if they differ (CI gate)
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys

# One-or-more adjacent C string literals (help args are often split across lines
# for width, e.g. "foo " "bar"), or NULL. Extract contents with STRLIT_RE.findall.
_HELP = r'(?:"(?:[^"\\]|\\.)*"\s*)+|NULL'
# port_env_bool("GE007_X", default, "help")  /  ge_env_bool("GE007_X", default)
REG_RE = re.compile(
    r'\b(port_env_bool|port_env_int|port_env_float|ge_env_bool)\s*\(\s*'
    r'"(GE007_[A-Z0-9_]+)"\s*,\s*([^,]+?)\s*(?:,\s*(' + _HELP + r'))?\s*\)'
)
# port_env_set("GE007_X", "help"|NULL) — the registering *presence* accessor
# (value returned is getenv(name) != NULL; "0"/empty still count as set).
SET_RE = re.compile(
    r'\bport_env_set\s*\(\s*"(GE007_[A-Z0-9_]+)"\s*,\s*(' + _HELP + r')\s*\)'
)
# raw getenv("GE007_X")
GETENV_RE = re.compile(r'\bgetenv\s*\(\s*"(GE007_[A-Z0-9_]+)"\s*\)')

KIND = {
    "port_env_bool": "bool",
    "port_env_int": "int",
    "port_env_float": "float",
    "ge_env_bool": "bool",
}

# settingsRegister<T>(...) — the env var is a positional string arg; the type is
# fixed by <T> and the default is the `def` positional argument at this index
# (0-based, counting from the opening paren). Enum/Int/UInt/Float share the
# (key, &var, def, ...) prefix; String is (key, var, capacity, def, ...).
SETTINGS_KIND = {
    "settingsRegisterInt": ("int", 2),
    "settingsRegisterUInt": ("uint", 2),
    "settingsRegisterFloat": ("float", 2),
    "settingsRegisterEnum": ("enum", 2),
    "settingsRegisterString": ("string", 3),
}
# Extract the contents of one-or-more adjacent C string literals ("a" "b" -> ab).
STRLIT_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')
ENVLIT_RE = re.compile(r'^"(GE007_[A-Z0-9_]+)"$')


def _split_top_level_args(argstr: str) -> list[str]:
    """Split a C argument list at top-level commas, respecting string/char
    literals and nested (){}[]. Returns stripped argument spellings."""
    args: list[str] = []
    cur: list[str] = []
    depth = 0
    i = 0
    n = len(argstr)
    in_str = in_char = False
    while i < n:
        c = argstr[i]
        if in_str:
            cur.append(c)
            if c == '\\' and i + 1 < n:
                cur.append(argstr[i + 1]); i += 2; continue
            if c == '"':
                in_str = False
            i += 1; continue
        if in_char:
            cur.append(c)
            if c == '\\' and i + 1 < n:
                cur.append(argstr[i + 1]); i += 2; continue
            if c == "'":
                in_char = False
            i += 1; continue
        if c == '"':
            in_str = True; cur.append(c); i += 1; continue
        if c == "'":
            in_char = True; cur.append(c); i += 1; continue
        if c in '([{':
            depth += 1; cur.append(c); i += 1; continue
        if c in ')]}':
            depth -= 1; cur.append(c); i += 1; continue
        if c == ',' and depth == 0:
            args.append(''.join(cur).strip()); cur = []; i += 1; continue
        cur.append(c); i += 1
    tail = ''.join(cur).strip()
    if tail:
        args.append(tail)
    return args


def _iter_call_arglists(text: str, fname: str):
    """Yield the raw argument-list spelling (text inside the outermost parens)
    for every `fname(...)` call, tracking strings/chars so parens inside string
    literals (e.g. a help string "(0-100)") don't end the arg list early."""
    start = 0
    n = len(text)
    flen = len(fname)
    while True:
        idx = text.find(fname, start)
        if idx < 0:
            return
        prev = text[idx - 1] if idx > 0 else ' '
        j = idx + flen
        while j < n and text[j] in ' \t\n':
            j += 1
        if (prev.isalnum() or prev == '_') or j >= n or text[j] != '(':
            start = idx + flen
            continue
        depth = 0
        k = j
        in_str = in_char = False
        close = -1
        while k < n:
            c = text[k]
            if in_str:
                if c == '\\':
                    k += 2; continue
                if c == '"':
                    in_str = False
            elif in_char:
                if c == '\\':
                    k += 2; continue
                if c == "'":
                    in_char = False
            elif c == '"':
                in_str = True
            elif c == "'":
                in_char = True
            elif c == '(':
                depth += 1
            elif c == ')':
                depth -= 1
                if depth == 0:
                    close = k
                    break
            k += 1
        if close < 0:
            return
        yield text[j + 1:close]
        start = close + 1


def repo_root(explicit: str | None) -> str:
    if explicit:
        return os.path.abspath(explicit)
    try:
        out = subprocess.check_output(
            ["git", "rev-parse", "--show-toplevel"], text=True, stderr=subprocess.DEVNULL
        )
        return out.strip()
    except (OSError, subprocess.CalledProcessError):
        return os.getcwd()


def source_files(root: str) -> list[str]:
    out = subprocess.check_output(
        ["git", "ls-files", "src/*.c", "src/*.h", "src/**/*.c", "src/**/*.h",
         "src/**/*.cpp", "src/**/*.mm", "src/**/*.inc", "include/**/*.h"],
        cwd=root, text=True,
    )
    return [l for l in out.splitlines() if l]


def build_catalog(root: str) -> dict[str, dict]:
    flags: dict[str, dict] = {}

    def ensure(name: str) -> dict:
        return flags.setdefault(
            name, {"name": name, "kind": "?", "default": "", "help": "", "refs": 0}
        )

    for rel in source_files(root):
        try:
            text = open(os.path.join(root, rel), encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        for m in REG_RE.finditer(text):
            fn, name, default, help_lit = m.group(1), m.group(2), m.group(3), m.group(4)
            e = ensure(name)
            e["kind"] = KIND.get(fn, e["kind"])
            if e["default"] == "":
                e["default"] = default.strip()
            if help_lit and help_lit != "NULL" and not e["help"]:
                e["help"] = "".join(STRLIT_RE.findall(help_lit))
            e["refs"] += 1
        for m in SET_RE.finditer(text):
            name, help_lit = m.group(1), m.group(2)
            e = ensure(name)
            e["kind"] = "presence"
            if e["default"] == "":
                e["default"] = "unset"
            if help_lit and help_lit != "NULL" and not e["help"]:
                e["help"] = "".join(STRLIT_RE.findall(help_lit))
            e["refs"] += 1
        for m in GETENV_RE.finditer(text):
            ensure(m.group(1))["refs"] += 1
        scan_settings(text, ensure)

    return flags


def scan_settings(text: str, ensure) -> None:
    """Scan settingsRegister<T>(...) mirror sites. The env-var mirror is a
    positional string arg; fill type/default/help for flags that exist only as a
    Setting (they are otherwise invisible to the getenv/port_env scanners)."""
    for fname, (kind, def_idx) in SETTINGS_KIND.items():
        for arglist in _iter_call_arglists(text, fname):
            args = _split_top_level_args(arglist)
            env_name = None
            for a in args:
                m = ENVLIT_RE.match(a)
                if m:
                    env_name = m.group(1)
                    break
            if env_name is None:
                continue
            e = ensure(env_name)
            if e["kind"] == "?":
                e["kind"] = kind
            if e["default"] == "" and def_idx < len(args):
                dtok = args[def_idx].strip()
                if kind == "string":
                    parts = STRLIT_RE.findall(dtok)
                    dtok = "".join(parts) if parts else dtok
                e["default"] = dtok
            # help is the last positional arg (a string literal, possibly split).
            if not e["help"] and args:
                parts = STRLIT_RE.findall(args[-1])
                if parts:
                    e["help"] = "".join(parts)
            e["refs"] += 1


def render(flags: dict[str, dict]) -> str:
    lines = [
        "# GE007_* environment flag reference",
        "",
        "_Generated by `tools/gen_env_reference.py` — do not edit by hand._",
        "",
        "The port gates diagnostic and opt-in behavior on `GE007_*` environment",
        "variables. This is the complete flag surface scanned from the source.",
        "Flags read through the registering `port_env_*`/`port_env_set` accessors",
        "(or `ge_env_bool`), or mirrored as a `settingsRegister*` env override, carry a",
        "type, default, and description here; flags still read through a raw `getenv`",
        "show none of those — migrating them to `port_env_*` fills them in.",
        "",
        f"**{len(flags)} flags** found across the source.",
        "",
        "| Flag | Type | Default | Refs | Description |",
        "| --- | --- | --- | --- | --- |",
    ]
    for name in sorted(flags):
        e = flags[name]
        default = e["default"].replace("|", "\\|") if e["default"] else ""
        help_ = e["help"].replace("|", "\\|") if e["help"] else ""
        lines.append(f"| `{name}` | {e['kind']} | {default} | {e['refs']} | {help_} |")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--repo-root", default=None)
    ap.add_argument("--out", default=None)
    ap.add_argument("--check", default=None)
    args = ap.parse_args()

    root = repo_root(args.repo_root)
    catalog = render(build_catalog(root))

    if args.check:
        try:
            existing = open(os.path.join(root, args.check), encoding="utf-8").read()
        except OSError:
            print(f"FAIL: {args.check} does not exist; run gen_env_reference.py --out {args.check}", file=sys.stderr)
            return 1
        if existing != catalog:
            print(f"FAIL: {args.check} is stale; regenerate with tools/gen_env_reference.py --out {args.check}", file=sys.stderr)
            return 1
        print(f"PASS: {args.check} matches the source flag surface.")
        return 0

    if args.out:
        with open(os.path.join(root, args.out), "w", encoding="utf-8") as fh:
            fh.write(catalog)
        print(f"wrote {args.out}")
    else:
        sys.stdout.write(catalog)
    return 0


if __name__ == "__main__":
    sys.exit(main())
