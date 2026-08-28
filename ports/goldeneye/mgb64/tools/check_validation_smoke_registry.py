#!/usr/bin/env python3
"""Guard: every runtime-validation smoke registered in CMakeLists must ship.

The CMake `add_port_validation_smoke(<name> <script.sh> <timeout> ...)` helper
skips-with-message when its script is missing, which means a registration can
silently advertise a test lane whose script was never committed. That makes the
advertised coverage exceed what a fresh checkout can actually run.

This guard parses every `add_port_validation_smoke(...)` call in CMakeLists.txt
and fails if any referenced `tools/<script>` does not exist in the tree. It is
ROM-free and runs in the always-on CTest suite (and in CI), so the registry can
never drift back into listing phantom lanes.

Exit 0 = every registered smoke has a backing script. Exit 1 = phantom(s) found.
"""
import argparse
import os
import re
import sys

REGISTER_RE = re.compile(
    r"add_port_validation_smoke\(\s*[A-Za-z0-9_]+\s+([A-Za-z0-9_./-]+\.sh)\b"
)


def find_repo_root(start: str) -> str:
    d = os.path.abspath(start)
    while True:
        if os.path.exists(os.path.join(d, "CMakeLists.txt")):
            return d
        parent = os.path.dirname(d)
        if parent == d:
            raise SystemExit("error: could not locate repo root (no CMakeLists.txt)")
        d = parent


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--repo-root",
        default=None,
        help="Repo root (defaults to the tree containing this script's CMakeLists.txt).",
    )
    args = ap.parse_args()

    root = args.repo_root or find_repo_root(os.path.dirname(__file__))
    cmake = os.path.join(root, "CMakeLists.txt")
    with open(cmake, "r", encoding="utf-8") as fh:
        text = fh.read()

    scripts = REGISTER_RE.findall(text)
    if not scripts:
        print("error: found no add_port_validation_smoke registrations to check", file=sys.stderr)
        return 1

    missing = []
    for script in scripts:
        # Scripts are resolved relative to tools/ by the CMake helper.
        path = os.path.join(root, "tools", script)
        if os.path.isfile(path):
            continue
        # tools/fidelity/** is export-ignored from the public source archive
        # and the fresh public-launch repo (D1, docs/design/
        # FIDELITY_REVIEW_AND_PLAN_2026-07-10.md). A checkout with no
        # tools/fidelity/ at all (the public tree) is the expected, on-purpose
        # case for a fidelity/-prefixed registration, not a phantom lane --
        # add_port_validation_smoke() itself already skips registering it
        # when the script is absent. Only flag a fidelity/ script as missing
        # when the fidelity tree exists but the specific script inside it
        # does not (a genuine phantom).
        #
        # Check a load-bearing FILE (ledger.py, matching CMakeLists.txt's
        # GE007_HAVE_FIDELITY_PROGRAM gate), not os.path.isdir() (2026-07-10,
        # CR-1 fix). `git archive` / GitHub "Download ZIP" still emit a bare,
        # EMPTY tools/fidelity/ directory entry for the export-ignored tree,
        # and extracting that archive materializes an empty directory --
        # os.path.isdir() is TRUE for that empty directory, so this guard was
        # treating the real public-archive artifact as "the fidelity tree
        # exists but the script is missing" (a false phantom) and failing out
        # of the box for any public-archive consumer who runs
        # `cmake -B build -DPORT_VALIDATION_TESTS=ON && ctest`.
        if script.startswith("fidelity/") and not os.path.isfile(os.path.join(root, "tools", "fidelity", "ledger.py")):
            continue
        missing.append(script)

    total = len(scripts)
    if missing:
        print(f"FAIL: {len(missing)}/{total} registered validation smokes reference a missing script:", file=sys.stderr)
        for m in sorted(set(missing)):
            print(f"  tools/{m}", file=sys.stderr)
        print(
            "\nEither commit the script or remove its add_port_validation_smoke(...) line.",
            file=sys.stderr,
        )
        return 1

    print(f"PASS: all {total} registered validation smokes have a backing script in tools/.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
