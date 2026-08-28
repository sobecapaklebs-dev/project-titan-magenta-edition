#!/usr/bin/env python3
"""Exercise the host-side builder against the runtime TPKG contract."""

from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path


KERNEL = Path(__file__).resolve().parents[1]
TOOL = KERNEL / "tools" / "titan_pkg.py"


def run(*arguments: str, success: bool = True) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(
        ["python3", str(TOOL), *arguments],
        check=False,
        capture_output=True,
        text=True,
    )
    if success and completed.returncode != 0:
        raise AssertionError(completed.stdout + completed.stderr)
    if not success and completed.returncode == 0:
        raise AssertionError("command unexpectedly succeeded")
    return completed


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="titan-pkg-") as temporary:
        root = Path(temporary)
        source = root / "source"
        source.mkdir()
        (source / "demo.elf").write_bytes(b"\x7fELF" + bytes(260))
        (source / "data").mkdir()
        (source / "data" / "settings.bin").write_bytes(bytes(range(64)))
        package = root / "demo.pkg"

        run(
            "build",
            "--source",
            str(source),
            "--output",
            str(package),
            "--title-id",
            "DEMO001",
            "--title",
            "Demo USB App",
            "--version",
            "1.0.0",
            "--executable",
            "demo.elf",
        )
        assert package.read_bytes()[:4] == b"TPKG"
        run("verify", str(package))

        damaged = bytearray(package.read_bytes())
        damaged[-1] ^= 1
        corrupt = root / "corrupt.pkg"
        corrupt.write_bytes(damaged)
        run("verify", str(corrupt), success=False)


        # TPKG v2 requires an explicit Home tile icon for both native and
        # external applications. The package format stores the original 64x64
        # RGB565 icon; Titan filters it into its existing runtime atlas.
        v2_source = root / "v2-source"
        v2_source.mkdir()
        (v2_source / "demo.elf").write_bytes(b"\x7fELF" + bytes(260))
        (v2_source / "icon.rgb565").write_bytes(bytes(64 * 64 * 2))
        v2_package = root / "demo-v2.pkg"
        run(
            "build",
            "--source",
            str(v2_source),
            "--output",
            str(v2_package),
            "--title-id",
            "DEMO003",
            "--title",
            "Demo TPKG v2",
            "--version",
            "2.0.0",
            "--type",
            "elf",
            "--format",
            "2",
            "--entrypoint",
            "demo.elf",
            "--author",
            "Titan SDK Test",
            "--description",
            "External ELF using the v2 metadata contract",
        )
        run("verify", str(v2_package))

        native_source = root / "native-source"
        native_source.mkdir()
        # The host-side builder only packages the module; runtime ELF/relocation
        # validation is performed by the installer on the PS2. This fixture
        # exercises the v2 native header/API contract and required icon.
        (native_source / "native.tmod").write_bytes(b"\x7fELF" + bytes(260))
        (native_source / "icon.rgb565").write_bytes(bytes(64 * 64 * 2))
        native_package = root / "native.pkg"
        run(
            "build",
            "--source",
            str(native_source),
            "--output",
            str(native_package),
            "--title-id",
            "NATIVE01",
            "--title",
            "Native Titan App",
            "--version",
            "1.0.0",
            "--type",
            "titan-native",
            "--entrypoint",
            "native.tmod",
            "--author",
            "Titan SDK Test",
            "--description",
            "Native TPKG v2 header contract",
        )
        run("verify", str(native_package))

        game_source = root / "game-source"
        game_source.mkdir()
        (game_source / "titaneye.tmod").write_bytes(b"\x7fELF" + bytes(260))
        (game_source / "icon.rgb565").write_bytes(bytes(64 * 64 * 2))
        (game_source / "GoldenEye 007.z64").write_bytes(bytes(1024))
        game_package = root / "titaneye.pkg"
        run(
            "build",
            "--source",
            str(game_source),
            "--output",
            str(game_package),
            "--title-id",
            "TITANEYE01",
            "--title",
            "TitanEYE 007",
            "--version",
            "0.1.0",
            "--type",
            "titan-game",
            "--entrypoint",
            "titaneye.tmod",
        )
        game_verify = run("verify", str(game_package))
        assert "titan-game" in game_verify.stdout

        # System apps may publish a Home tile without embedding an ELF/tmod.
        # The browser runtime is kernel-resident and the package carries only
        # metadata/assets plus the reserved @system-browser entrypoint.
        browser_source = root / "browser-source"
        browser_source.mkdir()
        (browser_source / "icon.rgb565").write_bytes(bytes(64 * 64 * 2))
        browser_package = root / "internet.pkg"
        run(
            "build",
            "--source",
            str(browser_source),
            "--output",
            str(browser_package),
            "--title-id",
            "TITN00010",
            "--title",
            "Internet",
            "--version",
            "1.1.0",
            "--type",
            "system-browser",
            "--author",
            "Project Titan",
            "--description",
            "Kernel-resident Titan Browser",
        )
        browser_verify = run("verify", str(browser_package))
        assert "system-browser" in browser_verify.stdout
        assert "@system-browser" in browser_verify.stdout

        run(
            "build",
            "--source",
            str(browser_source),
            "--output",
            str(root / "bad-browser.pkg"),
            "--title-id",
            "TITN00011",
            "--title",
            "Bad Browser",
            "--version",
            "1.0.0",
            "--type",
            "system-browser",
            "--entrypoint",
            "browser.elf",
            success=False,
        )

        # Themes use their own .tthm extension and never embed executable
        # code. They carry a bounded declarative theme.cfg and optional
        # 256x128 RGB565 preview instead.
        theme_source = root / "theme-source"
        theme_source.mkdir()
        (theme_source / "theme.cfg").write_text(
            "format=1\n"
            "id=SCARLETFLOW\n"
            "name=Scarlet Flow\n"
            "author=Project Titan\n"
            "version=1.0.0\n"
            "shell_api=1\n"
            "background_engine=flow3d-v1\n"
        )
        (theme_source / "preview.rgb565").write_bytes(bytes(256 * 128 * 2))
        theme_package = root / "Scarlet-Flow.tthm"
        run(
            "build",
            "--source",
            str(theme_source),
            "--output",
            str(theme_package),
            "--title-id",
            "SCARLETFLOW",
            "--title",
            "Scarlet Flow",
            "--version",
            "1.0.0",
            "--type",
            "theme",
            "--format",
            "2",
        )
        theme_verify = run("verify", str(theme_package))
        assert "theme" in theme_verify.stdout
        assert "@theme" in theme_verify.stdout

        original_theme_cfg = (theme_source / "theme.cfg").read_text()
        (theme_source / "theme.cfg").write_text(original_theme_cfg.replace("id=SCARLETFLOW", "id=OTHERID"))
        mismatch = run(
            "build",
            "--source",
            str(theme_source),
            "--output",
            str(root / "mismatch-theme.tthm"),
            "--title-id",
            "SCARLETFLOW",
            "--title",
            "Mismatched Theme",
            "--version",
            "1.0.0",
            "--type",
            "theme",
            success=False,
        )
        assert "manifest id must match package title-id" in mismatch.stderr
        (theme_source / "theme.cfg").write_text(original_theme_cfg)

        (theme_source / "payload.elf").write_bytes(b"\x7fELF" + bytes(64))
        run(
            "build",
            "--source",
            str(theme_source),
            "--output",
            str(root / "unsafe-theme.tthm"),
            "--title-id",
            "SCARLETFLOW",
            "--title",
            "Unsafe Theme",
            "--version",
            "1.0.0",
            "--type",
            "theme",
            success=False,
        )

        missing_icon = root / "missing-icon"
        missing_icon.mkdir()
        (missing_icon / "demo.elf").write_bytes(b"\x7fELF" + bytes(260))
        run(
            "build",
            "--source",
            str(missing_icon),
            "--output",
            str(root / "missing-icon.pkg"),
            "--title-id",
            "DEMO004",
            "--title",
            "Missing Icon",
            "--version",
            "2.0.0",
            "--type",
            "elf",
            "--format",
            "2",
            "--entrypoint",
            "demo.elf",
            success=False,
        )
        run(
            "build",
            "--source",
            str(source),
            "--output",
            str(root / "unsafe.pkg"),
            "--title-id",
            "DEMO002",
            "--title",
            "Unsafe App",
            "--version",
            "1.0.0",
            "--executable",
            "../demo.elf",
            success=False,
        )

    print("TPKG builder and verifier tests passed")


if __name__ == "__main__":
    main()
