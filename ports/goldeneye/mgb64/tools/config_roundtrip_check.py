#!/usr/bin/env python3
"""Validate no-ROM config set/save/reset round-trips through the native binary."""

from __future__ import annotations

import argparse
import os
import subprocess
import tempfile
from pathlib import Path


DEFAULTS = {
    "Video.WindowWidth": "1440",
    "Video.WindowHeight": "810",
    "Video.WindowX": "-1",
    "Video.WindowY": "-1",
    "Video.Display": "0",
    "Video.FullscreenWidth": "0",
    "Video.FullscreenHeight": "0",
    "Video.FullscreenRefresh": "0",
    "Video.WindowMode": "windowed",
    "Video.VSync": "adaptive",
    "Video.FrameCap": "60",
    "Video.Gamma": "1",
    "Video.RenderScale": "2",
    "Video.MSAA": "0",
    "Video.FovY": "50",
    "Video.RetroFilter": "auto",
    "Input.MouseSensitivity": "0.15",
    "Input.MouseSensitivityAim": "0.05",
    "Input.InvertY": "0",
    "Input.GamepadLookSpeed": "8",
    "Audio.MasterVolume": "1",
    "Audio.DeviceSamples": "512",
}

SEED_CONFIG = """\
# User-owned comments are not guaranteed to round-trip yet, but unknown keys are.
[Video]
WindowWidth=1024
WindowHeight=768
WindowX=-1
WindowY=-1
Display=0
FullscreenWidth=0
FullscreenHeight=0
FullscreenRefresh=0
WindowMode=windowed
VSync=adaptive
FrameCap=60
Gamma=1
RenderScale=1
MSAA=0
FovY=60
RetroFilter=auto
FutureVideo=keep-me

[Input]
MouseSensitivity=0.25
InvertY=0

[Future]
Token=hello
Number=42
"""


def parse_args() -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    # Probe the webgpu build first (the default backend), then the GL build, so
    # the tool works from either build tree without an explicit --binary.
    default_binary = next(
        (str(p) for p in (repo_root / "build-webgpu" / "ge007",
                          repo_root / "build" / "ge007") if p.is_file()),
        str(repo_root / "build" / "ge007"),
    )
    parser.add_argument(
        "--binary",
        default=default_binary,
        help="native binary to inspect (default: build-webgpu/ge007 or build/ge007)",
    )
    return parser.parse_args()


def run_binary(
    binary: Path,
    savedir: Path,
    *args: str,
    env_extra: dict[str, str] | None = None,
) -> str:
    env = os.environ.copy()
    env.pop("GE007_DEBUG", None)
    if env_extra:
        env.update(env_extra)

    result = subprocess.run(
        [str(binary), "--savedir", str(savedir), *args],
        cwd=Path(__file__).resolve().parents[1],
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    output = result.stdout
    if result.returncode != 0:
        raise SystemExit(
            f"FAIL: {' '.join(args)} exited with {result.returncode}\n{output[-2000:]}"
        )

    forbidden = ("[ROM]", "No GoldenEye ROM", "[GE007-PC] Starting")
    for marker in forbidden:
        if marker in output:
            raise SystemExit(f"FAIL: {' '.join(args)} touched runtime/ROM startup marker {marker!r}")

    return output


def parse_dump(output: str) -> dict[str, str]:
    values: dict[str, str] = {}
    section = ""

    for raw_line in output.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
            continue
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        full_key = f"{section}.{key.strip()}" if section else key.strip()
        values[full_key] = value.strip()

    return values


def assert_values(values: dict[str, str], expected: dict[str, str], label: str) -> None:
    for key, expected_value in expected.items():
        actual = values.get(key)
        if actual != expected_value:
            raise SystemExit(
                f"FAIL: {label}: {key} expected {expected_value!r}, got {actual!r}"
            )


def assert_file_contains(path: Path, needles: list[str], label: str) -> None:
    text = path.read_text(encoding="utf-8")
    for needle in needles:
        if needle not in text:
            raise SystemExit(f"FAIL: {label}: missing {needle!r} in {path}")


def assert_no_tmp(savedir: Path) -> None:
    leftovers = sorted(savedir.glob("*.tmp"))
    if leftovers:
        raise SystemExit(f"FAIL: config save left temp file(s): {leftovers}")


def assert_render_scale_min_clamp(binary: Path) -> None:
    """Pin the release regression where sub-native scene scale broke sky rendering."""
    with tempfile.TemporaryDirectory(prefix="mgb64_render_scale_clamp_") as temp:
        savedir = Path(temp)
        config_path = savedir / "ge007.ini"
        config_path.write_text("[Video]\nRenderScale=0.5\n", encoding="utf-8")

        file_dump = parse_dump(run_binary(binary, savedir, "--dump-config"))
        assert_values(
            file_dump,
            {"Video.RenderScale": "1"},
            "stale file render-scale clamp",
        )

        env_dump = parse_dump(
            run_binary(
                binary,
                savedir,
                "--dump-config",
                env_extra={"GE007_RENDER_SCALE": "0.5"},
            )
        )
        assert_values(
            env_dump,
            {"Video.RenderScale": "1"},
            "env render-scale clamp",
        )

        cli_dump = parse_dump(
            run_binary(
                binary,
                savedir,
                "--config-override",
                "Video.RenderScale=0.5",
                "--dump-config",
            )
        )
        assert_values(
            cli_dump,
            {"Video.RenderScale": "1"},
            "cli render-scale clamp",
        )


FAITHFUL_EXPECTED = {
    "Video.RemasterFX": "0",
    "Video.RenderScale": "1",
    "Video.MSAA": "0",
    "Video.TexturePack": "",
    "Video.FovY": "60",
    "Video.ViewmodelFov": "60",
    "Input.ModernCrosshair": "0",
    "Input.HitMarkers": "0",
    "Input.ReticleTargetFeedback": "0",
    "Input.ViewmodelSway": "0",
    "Input.GamepadLookCurve": "1",
    "Input.GamepadDeadzone": "0.2441",
    "Input.GamepadRadialDeadzone": "0",
    "Input.GamepadFpsScale": "0",
    "Input.MinimapEnabled": "0",
}


def assert_faithful_preset(binary: Path) -> None:
    """Pin the --faithful preset (VISUAL_MODES.md section 1): it forces the
    pre-remaster baseline, wins over a saved remaster ge007.ini, yields to an
    explicit per-setting override, and never persists to disk."""
    with tempfile.TemporaryDirectory(prefix="mgb64_faithful_") as temp:
        savedir = Path(temp)
        config_path = savedir / "ge007.ini"
        # Seed a custom remaster config: --faithful must override it for the run.
        config_path.write_text(
            "[Video]\nRenderScale=4\nFovY=95\nRemasterFX=1\n"
            "[Input]\nMinimapEnabled=1\nModernCrosshair=1\n",
            encoding="utf-8",
        )

        faithful_dump = parse_dump(run_binary(binary, savedir, "--faithful", "--dump-config"))
        assert_values(faithful_dump, FAITHFUL_EXPECTED, "faithful preset")

        # Explicit per-setting overrides win over the faithful baseline.
        override_dump = parse_dump(
            run_binary(
                binary, savedir,
                "--faithful",
                "--config-override", "Video.FovY=90",
                "--dump-config",
            )
        )
        assert_values(
            override_dump,
            {"Video.FovY": "90", "Video.RenderScale": "1", "Video.RemasterFX": "0"},
            "faithful + explicit override precedence",
        )

        # A faithful session is read-only for config: even a save-triggering
        # invocation must leave ge007.ini BYTE-FOR-BYTE unchanged -- neither
        # writing faithful values nor dropping the user's pre-existing custom
        # values (RenderScale=4 / FovY=95 / ...) back to defaults.
        seed_text = config_path.read_text(encoding="utf-8")
        run_binary(binary, savedir, "--faithful", "--config-set", "Input.InvertY=1")
        after_text = config_path.read_text(encoding="utf-8")
        if after_text != seed_text:
            raise SystemExit(
                "FAIL: a --faithful session modified ge007.ini (must be read-only)\n"
                f"--- before ---\n{seed_text}\n--- after ---\n{after_text}"
            )


def assert_env_override_preserves_persisted(binary: Path) -> None:
    """AUDIT-0055: a transient GE007_* override must never erase or change the
    persisted value across a full-file save. --config-set/--reset-config/clean
    shutdown all atomically rewrite the whole ini, so the old writer's "omit
    env-overridden keys" was deletion, not preservation. This drives the full
    two-launch matrix through the real main_pc.c lifecycle for a float, an int,
    and an enum, plus the durable-edit-under-active-override precedence case."""
    with tempfile.TemporaryDirectory(prefix="mgb64_env_shadow_") as temp:
        savedir = Path(temp)
        config_path = savedir / "ge007.ini"

        # Launch 1 -- the user persists durable values.
        run_binary(
            binary, savedir,
            "--config-set", "Video.FovY=70",
            "--config-set", "Video.WindowWidth=1234",
            "--config-set", "Video.WindowMode=borderless",
        )
        seeded = parse_dump(config_path.read_text(encoding="utf-8"))
        assert_values(
            seeded,
            {"Video.FovY": "70", "Video.WindowWidth": "1234", "Video.WindowMode": "borderless"},
            "L1 durable seed",
        )

        # Launch 2 -- transient env overrides active while an UNRELATED key is
        # saved (the report's scenario). The env values must not reach disk; the
        # durable values must survive; the unrelated edit must persist.
        run_binary(
            binary, savedir,
            "--config-set", "Audio.MasterVolume=0.9",
            env_extra={
                "GE007_FOV_Y": "60",
                "GE007_WINDOW_WIDTH": "800",
                "GE007_WINDOW_MODE": "windowed",
            },
        )
        after = parse_dump(config_path.read_text(encoding="utf-8"))
        assert_values(
            after,
            {
                "Video.FovY": "70",
                "Video.WindowWidth": "1234",
                "Video.WindowMode": "borderless",
                "Audio.MasterVolume": "0.9",
            },
            "L2 save under active env override preserves durable + persists unrelated",
        )

        # Launch 3 -- env removed: the exact durable values are revealed.
        restored = parse_dump(run_binary(binary, savedir, "--dump-config"))
        assert_values(
            restored,
            {"Video.FovY": "70", "Video.WindowWidth": "1234", "Video.WindowMode": "borderless"},
            "L3 env removed restores exact durable values",
        )

        # Durable edit to an actively env-overridden key persists the edit (not the
        # shadow, not the env value, and not silently omitted).
        run_binary(
            binary, savedir,
            "--config-set", "Video.FovY=80",
            env_extra={"GE007_FOV_Y": "60"},
        )
        edited = parse_dump(config_path.read_text(encoding="utf-8"))
        assert_values(edited, {"Video.FovY": "80"}, "durable --config-set under active env persists the edit")

        # --reset-config under an active env override must persist the DEFAULT, not
        # the durable shadow and not the env value (reset clears the transient
        # marking). Video.FovY registered default is 50.
        run_binary(binary, savedir, "--reset-config", env_extra={"GE007_FOV_Y": "60"})
        after_reset = parse_dump(config_path.read_text(encoding="utf-8"))
        assert_values(
            after_reset,
            {"Video.FovY": "50"},
            "reset under active env persists default, not shadow/env",
        )
        assert_no_tmp(savedir)


def main() -> int:
    args = parse_args()
    binary = Path(args.binary).resolve()
    if not binary.is_file():
        # Binary-dependent lane: skip cleanly (ctest SKIP_RETURN_CODE) when no
        # native binary has been built, like the other binary-dependent gates.
        print(f"SKIP: native binary not found (build ge007 first): {binary}")
        return 77

    assert_render_scale_min_clamp(binary)
    assert_faithful_preset(binary)
    assert_env_override_preserves_persisted(binary)

    with tempfile.TemporaryDirectory(prefix="mgb64_config_roundtrip_") as temp:
        savedir = Path(temp)
        config_path = savedir / "ge007.ini"
        config_path.write_text(SEED_CONFIG, encoding="utf-8")

        env_dump = parse_dump(
            run_binary(
                binary,
                savedir,
                "--dump-config",
                env_extra={
                    "GE007_WINDOW_WIDTH": "1333",
                    "GE007_WINDOW_X": "11",
                    "GE007_WINDOW_Y": "22",
                    "GE007_DISPLAY": "1",
                    "GE007_FULLSCREEN_WIDTH": "1920",
                    "GE007_FULLSCREEN_HEIGHT": "1080",
                    "GE007_FULLSCREEN_REFRESH": "60",
                    "GE007_WINDOW_MODE": "borderless",
                    "GE007_VSYNC": "on",
                    "GE007_FRAME_CAP": "30",
                    "GE007_GAMMA": "1.2",
                    "GE007_RENDER_SCALE": "1.5",
                    "GE007_MSAA": "4",
                    "GE007_FOV_Y": "70",
                    "GE007_RETRO_FILTER": "on",
                },
            )
        )
        assert_values(
            env_dump,
            {
                "Video.WindowWidth": "1333",
                "Video.WindowX": "11",
                "Video.WindowY": "22",
                "Video.Display": "1",
                "Video.FullscreenWidth": "1920",
                "Video.FullscreenHeight": "1080",
                "Video.FullscreenRefresh": "60",
                "Video.WindowMode": "borderless",
                "Video.VSync": "on",
                "Video.FrameCap": "30",
                "Video.Gamma": "1.2",
                "Video.RenderScale": "1.5",
                "Video.MSAA": "4",
                "Video.FovY": "70",
                "Video.RetroFilter": "on",
            },
            "env override dump",
        )
        assert_file_contains(
            config_path,
            [
                "WindowWidth=1024",
                "WindowX=-1",
                "WindowY=-1",
                "Display=0",
                "FullscreenWidth=0",
                "FullscreenHeight=0",
                "FullscreenRefresh=0",
                "WindowMode=windowed",
                "VSync=adaptive",
                "FrameCap=60",
                "Gamma=1",
                "RenderScale=1",
                "MSAA=0",
                "FovY=60",
                "RetroFilter=auto",
            ],
            "env override is not persisted",
        )

        cli_dump = parse_dump(
            run_binary(
                binary,
                savedir,
                "--config-override",
                "Video.WindowHeight=720",
                "--config-override",
                "Video.WindowX=33",
                "--config-override",
                "Video.WindowY=44",
                "--config-override",
                "Video.Display=2",
                "--config-override",
                "Video.FullscreenWidth=2560",
                "--config-override",
                "Video.FullscreenHeight=1440",
                "--config-override",
                "Video.FullscreenRefresh=120",
                "--config-override",
                "Video.WindowMode=exclusive",
                "--config-override",
                "Video.VSync=off",
                "--config-override",
                "Video.FrameCap=display",
                "--config-override",
                "Video.Gamma=1.3",
                "--config-override",
                "Video.RenderScale=1.25",
                "--config-override",
                "Video.MSAA=2",
                "--config-override",
                "Video.FovY=75",
                "--config-override",
                "Video.RetroFilter=off",
                "--dump-config",
            )
        )
        assert_values(
            cli_dump,
            {
                "Video.WindowHeight": "720",
                "Video.WindowX": "33",
                "Video.WindowY": "44",
                "Video.Display": "2",
                "Video.FullscreenWidth": "2560",
                "Video.FullscreenHeight": "1440",
                "Video.FullscreenRefresh": "120",
                "Video.WindowMode": "exclusive",
                "Video.VSync": "off",
                "Video.FrameCap": "display",
                "Video.Gamma": "1.3",
                "Video.RenderScale": "1.25",
                "Video.MSAA": "2",
                "Video.FovY": "75",
                "Video.RetroFilter": "off",
            },
            "cli override dump",
        )
        assert_file_contains(
            config_path,
            [
                "WindowHeight=768",
                "WindowX=-1",
                "WindowY=-1",
                "Display=0",
                "FullscreenWidth=0",
                "FullscreenHeight=0",
                "FullscreenRefresh=0",
                "WindowMode=windowed",
                "VSync=adaptive",
                "FrameCap=60",
                "Gamma=1",
                "RenderScale=1",
                "MSAA=0",
                "FovY=60",
                "RetroFilter=auto",
            ],
            "cli override is not persisted",
        )

        precedence_dump = parse_dump(
            run_binary(
                binary,
                savedir,
                "--config-override",
                "Video.WindowWidth=1400",
                "--dump-config",
                env_extra={"GE007_WINDOW_WIDTH": "1333"},
            )
        )
        assert_values(precedence_dump, {"Video.WindowWidth": "1400"}, "cli over env precedence")

        run_binary(
            binary,
            savedir,
            "--config-set",
            "Input.InvertY=1",
            "--config-set",
            "Audio.MasterVolume=0.5",
            "--config-set",
            "Video.WindowWidth=1280",
            "--config-set",
            "Video.WindowX=50",
            "--config-set",
            "Video.WindowY=60",
            "--config-set",
            "Video.Display=1",
            "--config-set",
            "Video.FullscreenWidth=1920",
            "--config-set",
            "Video.FullscreenHeight=1080",
            "--config-set",
            "Video.FullscreenRefresh=60",
            "--config-set",
            "Video.WindowMode=borderless",
            "--config-set",
            "Video.VSync=off",
            "--config-set",
            "Video.FrameCap=30",
            "--config-set",
            "Video.Gamma=1.4",
            "--config-set",
            "Video.RenderScale=2",
            "--config-set",
            "Video.MSAA=8",
            "--config-set",
            "Video.FovY=80",
            "--config-set",
            "Video.RetroFilter=on",
        )
        assert_no_tmp(savedir)
        assert_file_contains(
            config_path,
            [
                "# Window width",
                "# type=int scope=restart default=1440 range=320..3840",
                "# Window X",
                "# type=int scope=restart default=-1 range=-1..32767",
                "WindowX=50",
                "# Window Y",
                "# type=int scope=restart default=-1 range=-1..32767",
                "WindowY=60",
                "# Display",
                "# type=int scope=restart default=0 range=0..31",
                "Display=1",
                "# Fullscreen width",
                "# type=int scope=restart default=0 range=0..7680",
                "FullscreenWidth=1920",
                "# Fullscreen height",
                "# type=int scope=restart default=0 range=0..4320",
                "FullscreenHeight=1080",
                "# Fullscreen refresh",
                "# type=int scope=restart default=0 range=0..1000",
                "FullscreenRefresh=60",
                "# Window mode",
                "# type=enum scope=live default=windowed range=windowed|borderless|exclusive",
                "WindowMode=borderless",
                "# VSync",
                "# type=enum scope=live default=adaptive range=off|on|adaptive",
                "VSync=off",
                "# Frame cap",
                "# type=enum scope=live default=60 range=30|60|display",
                "FrameCap=30",
                "# Gamma",
                "# type=float scope=live default=1 range=0.5..2.5",
                "Gamma=1.4",
                "# Render scale",
                "# type=float scope=restart default=2 range=1..4",
                "RenderScale=2",
                "# MSAA",
                "# type=enum scope=live default=0 range=0|2|4|8",
                "MSAA=8",
                "# Vertical FOV",
                "# type=float scope=live default=50 range=45..105",
                "FovY=80",
                "# Retro filter",
                "# type=enum scope=live default=auto range=auto|off|on",
                "RetroFilter=on",
                "# Master volume",
                "# type=float scope=live default=1 range=0..1",
                "FutureVideo=keep-me",
                "[Future]",
                "Token=hello",
                "Number=42",
            ],
            "unknown-key passthrough",
        )

        updated = parse_dump(run_binary(binary, savedir, "--dump-config"))
        assert_values(
            updated,
            {
                "Video.WindowWidth": "1280",
                "Video.WindowHeight": "768",
                "Video.WindowX": "50",
                "Video.WindowY": "60",
                "Video.Display": "1",
                "Video.FullscreenWidth": "1920",
                "Video.FullscreenHeight": "1080",
                "Video.FullscreenRefresh": "60",
                "Video.WindowMode": "borderless",
                "Video.VSync": "off",
                "Video.FrameCap": "30",
                "Video.Gamma": "1.4",
                "Video.RenderScale": "2",
                "Video.MSAA": "8",
                "Video.FovY": "80",
                "Video.RetroFilter": "on",
                "Input.MouseSensitivity": "0.25",
                "Input.InvertY": "1",
                "Audio.MasterVolume": "0.5",
            },
            "updated dump",
        )

        run_binary(binary, savedir, "--reset-config")
        assert_no_tmp(savedir)
        assert_file_contains(
            config_path,
            [
                "FutureVideo=keep-me",
                "[Future]",
                "Token=hello",
                "Number=42",
            ],
            "unknown-key passthrough after reset",
        )

        reset = parse_dump(run_binary(binary, savedir, "--dump-config"))
        assert_values(reset, DEFAULTS, "reset dump")

    print("PASS: config round-trip, reset, unknown-key preservation, env-override shadow")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
