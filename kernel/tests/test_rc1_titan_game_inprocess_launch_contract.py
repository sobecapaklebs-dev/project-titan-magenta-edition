#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
if root.name == "kernel":
    root = root.parent

scene = (root / "kernel/src/ui/titan_shell_scene.c").read_text(encoding="utf-8")
apps = (root / "kernel/src/services/titan_applications.c").read_text(encoding="utf-8")
errors = []

def span(text, start_token, end_token):
    start = text.find(start_token)
    if start < 0:
        return ""
    end = text.find(end_token, start + len(start_token))
    if end < 0:
        return ""
    return text[start:end]

backend = span(
    apps,
    "TitanApplicationResult TitanApplications_Launch",
    "int TitanApplications_GetUninstallTarget",
)

if not backend:
    errors.append("TitanApplications_Launch span missing")
else:
    for token in (
        "TITAN_APPLICATION_TITAN_NATIVE",
        "TITAN_APPLICATION_TITAN_GAME",
        "TitanAppRuntime_Open(",
        "return TITAN_APPLICATION_OK;",
    ):
        if token not in backend:
            errors.append("applications backend missing " + token)

launcher = span(
    scene,
    "static void TitanShellScene_LaunchApplication",
    "static int TitanShellScene_FindHomeDisplayIndexForShell",
)

if not launcher:
    errors.append("central application launcher span missing")
else:
    native_pos = launcher.find("TITAN_APPLICATION_TITAN_NATIVE")
    game_pos = launcher.find("TITAN_APPLICATION_TITAN_GAME")
    handoff_pos = launcher.find("TitanAudio_PrepareHandoff")
    launch_pos = launcher.find("TitanApplications_Launch")

    if native_pos < 0:
        errors.append("central launcher lost Titan Native classification")
    if game_pos < 0:
        errors.append("central launcher lost Titan Game classification")
    if launch_pos < 0:
        errors.append("central launcher lost TitanApplications_Launch")
    if handoff_pos < 0:
        errors.append("central launcher lost external ELF audio handoff")

    if native_pos >= 0 and handoff_pos >= 0 and native_pos > handoff_pos:
        errors.append("Titan Native classification occurs after external handoff")

    if game_pos >= 0 and handoff_pos >= 0 and game_pos > handoff_pos:
        errors.append("Titan Game classification occurs after external handoff")

    if "if (launch_result == TITAN_APPLICATION_OK)" not in launcher:
        errors.append("accepted in-process launch no longer returns cleanly")

needle = "if (g_scene.page == TITAN_SHELL_PAGE_APPLICATIONS)"
first = scene.find(needle)
page_start = scene.find(needle, first + 1) if first >= 0 else -1

if page_start < 0:
    errors.append("Applications update page missing")
else:
    page_end = scene.find(
        "if (g_scene.page == TITAN_SHELL_PAGE_SETTINGS)",
        page_start,
    )

    if page_end < 0:
        errors.append("Applications update page end missing")
    else:
        page = scene[page_start:page_end]

        if "TITAN_INPUT_BUTTON_CROSS" not in page:
            errors.append("Applications CROSS launch input missing")

        legacy_direct = (
            "TITAN_APPLICATION_TITAN_NATIVE" in page
            and "TITAN_APPLICATION_TITAN_GAME" in page
            and "TitanApplications_Launch" in page
        )

        centralized = (
            "TitanShellScene_BeginApplicationLaunch" in page
            or "TitanShellScene_LaunchApplication" in page
        )

        if not legacy_direct and not centralized:
            errors.append(
                "Applications page neither launches directly nor routes to central launcher"
            )

if "TitanShellScene_BeginApplicationLaunch" in scene:
    begin_pos = scene.rfind("static void TitanShellScene_BeginApplicationLaunch")
    begin = scene[begin_pos:begin_pos + 6000]

    if "game_launch_application" not in begin:
        errors.append("application choreography ownership flag missing")
    if "game_launch_application_index" not in begin:
        errors.append("application choreography index retention missing")

    run_pos = scene.rfind("static void TitanShellScene_RunGameLaunch")
    if run_pos < 0:
        errors.append("common launch runner missing")
    else:
        run = scene[run_pos:run_pos + 12000]
        if "game_launch_application" not in run:
            errors.append("common runner lost application branch")
        if "game_launch_application_index" not in run:
            errors.append("common runner lost application index")
        if "TitanShellScene_LaunchApplication" not in run:
            errors.append("common runner does not converge on central launcher")

if errors:
    for error in errors:
        print("rc1-titan-game-inprocess-launch: FAIL:", error)
    raise SystemExit(1)

print(
    "rc1-titan-game-inprocess-launch: PASS "
    "Native+TitanGame remain in-process and centralized choreography "
    "converges before external ELF/audio handoff"
)
