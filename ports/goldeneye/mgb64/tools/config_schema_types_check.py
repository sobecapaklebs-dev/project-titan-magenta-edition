#!/usr/bin/env python3
"""Compile and run a no-ROM harness for enum/string config schema types."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import tempfile
from pathlib import Path


HARNESS_SOURCE = r'''
#include <stdio.h>
#include <string.h>

#include "config_pc.h"
#include "savedir.h"
#include "settings.h"

static s32 g_window_mode = 0;
static char g_profile_name[32] = "unset";

static const ConfigEnumOption k_window_modes[] = {
    { "windowed", 0 },
    { "borderless", 1 },
    { "exclusive", 2 },
};

static int fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        return fail("missing savedir argument");
    }

    savedirInit(argv[1]);
    settingsRegisterEnum("Video.WindowMode", &g_window_mode, 1,
                         k_window_modes, 3,
                         SETTING_SCOPE_RESTART, "GE007_WINDOW_MODE",
                         "--config-override Video.WindowMode=VALUE",
                         "Window mode",
                         "Desktop window mode.");
    settingsRegisterString("User.ProfileName", g_profile_name, sizeof(g_profile_name),
                           "agent",
                           SETTING_SCOPE_RESTART, "GE007_PROFILE_NAME",
                           "--config-override User.ProfileName=VALUE",
                           "Profile name",
                           "Local profile display name.");

    configInit();

    if (g_window_mode != 2) {
        return fail("enum token did not load as exclusive");
    }
    if (strcmp(g_profile_name, "custom") != 0) {
        return fail("string value did not load");
    }

    if (!configSetValue("Video.WindowMode", "windowed")) {
        return fail("enum configSetValue failed");
    }
    if (!configSetValue("User.ProfileName", "updated")) {
        return fail("string configSetValue failed");
    }
    if (!configSave()) {
        return fail("configSave failed");
    }

    settingsResetAllToDefaults();
    if (g_window_mode != 1) {
        return fail("enum default reset failed");
    }
    if (strcmp(g_profile_name, "agent") != 0) {
        return fail("string default reset failed");
    }

    return 0;
}
'''

SEED_CONFIG = """\
[Video]
WindowMode=exclusive

[User]
ProfileName=custom

[Future]
Token=keep
"""


def parse_args() -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", default=str(repo_root), help="repository root")
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"), help="C compiler")
    return parser.parse_args()


def require_contains(path: Path, needles: list[str]) -> None:
    text = path.read_text(encoding="utf-8")
    for needle in needles:
        if needle not in text:
            raise SystemExit(f"FAIL: missing {needle!r} in {path}")


def main() -> int:
    args = parse_args()
    repo = Path(args.repo_root).resolve()
    cc = shutil.which(args.cc)
    if not cc:
        raise SystemExit(f"FAIL: C compiler not found: {args.cc}")

    with tempfile.TemporaryDirectory(prefix="mgb64_schema_types_") as temp:
        tempdir = Path(temp)
        harness = tempdir / "schema_types_probe.c"
        binary = tempdir / "schema_types_probe"
        savedir = tempdir / "save"
        savedir.mkdir()
        (savedir / "ge007.ini").write_text(SEED_CONFIG, encoding="utf-8")
        harness.write_text(HARNESS_SOURCE, encoding="utf-8")

        cmd = [
            cc,
            "-std=c99",
            "-DNATIVE_PORT",
            "-DNONMATCHING",
            "-D_LANGUAGE_C",
            "-I.",
            "-Iinclude",
            "-Isrc",
            "-Isrc/platform",
            str(harness),
            "src/platform/config_pc.c",
            "src/platform/settings.c",
            "src/platform/savedir.c",
            "-o",
            str(binary),
        ]
        result = subprocess.run(
            cmd,
            cwd=repo,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        if result.returncode != 0:
            raise SystemExit(f"FAIL: schema type harness compile failed\n{result.stdout}")

        result = subprocess.run(
            [str(binary), str(savedir)],
            cwd=repo,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        if result.returncode != 0:
            raise SystemExit(f"FAIL: schema type harness failed\n{result.stdout}")

        require_contains(
            savedir / "ge007.ini",
            [
                "# type=enum scope=restart default=borderless range=windowed|borderless|exclusive",
                "WindowMode=windowed",
                "# type=string scope=restart default=agent range=len<32",
                "ProfileName=updated",
                "[Future]",
                "Token=keep",
            ],
        )

    print("PASS: enum/string schema types")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
