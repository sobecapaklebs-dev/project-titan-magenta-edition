#!/usr/bin/env python3
"""RC14 contracts for the Home grid, APP_HOME artwork and UL catalog."""

from __future__ import annotations

from pathlib import Path
import sys

from PIL import Image


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} KERNEL_ROOT", file=sys.stderr)
        return 2

    root = Path(sys.argv[1]).resolve()
    scene = (root / "src/ui/titan_shell_scene.c").read_text(encoding="utf-8")
    icons = (root / "src/ui/titan_icons.c").read_text(encoding="utf-8")
    scanner = (root / "src/services/titan_library_scan.c").read_text(encoding="utf-8")
    cache = (root / "src/services/titan_library_cache.c").read_text(encoding="utf-8")
    folders = (root / "src/services/titan_home_folders.c").read_text(encoding="utf-8")

    require(
        "#define TITAN_SHELL_TOP_MENU_GRID_SHIFT 244.0f" in scene,
        "top-menu Home grid shift is not pinned above the footer",
    )
    require(
        "menu_transition * TITAN_SHELL_TOP_MENU_GRID_SHIFT" in scene and
        "menu_transition * 284.0f" not in scene,
        "clipped RC13 Home grid offset returned",
    )
    require(
        90.0 + 244.0 + (80.0 * 0.78) < 414.0,
        "collapsed Home tiles overlap the footer",
    )
    require(
        "TitanShellScene_DrawFooterText" in scene and
        "available_width / measured_width" in scene,
        "footer text is not fitted to the logical viewport",
    )

    require(
        "TitanUiAssets_DrawAppHome(" in icons and
        "TitanIcons_DrawAppHomeFilled" not in icons,
        "APP_HOME does not use the supplied alpha artwork",
    )
    raw = (root / "assets/ui/titan_ui_app_home.a8").read_bytes()
    require(len(raw) == 128 * 128, "APP_HOME A8 mask has the wrong size")
    active = [index for index, value in enumerate(raw) if value > 8]
    require(active, "APP_HOME A8 mask is empty")
    xs = [index % 128 for index in active]
    ys = [index // 128 for index in active]
    left, right = min(xs), max(xs)
    top, bottom = min(ys), max(ys)
    require(left >= 18 and 127 - right >= 18, "APP_HOME lacks horizontal padding")
    require(top >= 18 and 127 - bottom >= 18, "APP_HOME lacks vertical padding")
    require(abs((left + right) - 127) <= 4, "APP_HOME is not horizontally centered")
    require(abs((top + bottom) - 127) <= 4, "APP_HOME is not vertically centered")

    image = Image.open(root / "assets/ui/titan_ui_app_home.png").convert("RGBA")
    require(image.size == (128, 128), "APP_HOME PNG has the wrong dimensions")
    for point in ((0, 0), (127, 0), (0, 127), (127, 127)):
        require(image.getpixel(point)[3] == 0, "APP_HOME background is not transparent")

    require(
        "candidate->format == TITAN_NEUTRINO_GAME_USBEXTREME" in scanner and
        "current->startup" in scanner and
        "candidate->startup" in scanner,
        "UL records are not deduplicated by startup ID",
    )
    require(
        "games[left].format == TITAN_NEUTRINO_GAME_USBEXTREME" in cache and
        "games[left].startup" in cache and
        "games[right].startup" in cache,
        "persistent cache still rejects multiple records from one ul.cfg",
    )
    require(
        "if (g_library_scan.device_pass == 0u)" in scanner,
        "mass0 UL fallback is skipped when an ISO already exists on mass:",
    )
    require(
        "TitanHomeFolders_MakeCatalogGameKey" in folders and
        'static const char ul_prefix[] = "titan:ul:"' in folders,
        "UL folder membership still collides on the shared ul.cfg path",
    )

    print("home-grid-ul-contract: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
