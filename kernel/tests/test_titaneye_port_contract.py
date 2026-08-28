#!/usr/bin/env python3
from pathlib import Path
import sys


project = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
kernel = project / "kernel"
port = project / "ports/goldeneye"
mgb = port / "mgb64"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"titaneye-port-contract: FAIL: {message}")


makefile = (port / "Makefile.titan").read_text(encoding="utf-8")
module = (port / "titan_ge_module.c").read_text(encoding="utf-8")
runtime = (port / "titan_ge_runtime.c").read_text(encoding="utf-8")
renderer = (mgb / "src/platform/fast3d/gfx_titan.c").read_text(encoding="utf-8")
audio = (mgb / "src/platform/audio_pc.c").read_text(encoding="utf-8")
boss = (mgb / "src/boss.c").read_text(encoding="utf-8")
gfx_pc = (mgb / "src/platform/fast3d/gfx_pc.c").read_text(encoding="utf-8")
gfx_ptr = (mgb / "src/platform/gfx_ptr.h").read_text(encoding="utf-8")
sdk = (kernel / "include/titan_game_sdk.h").read_text(encoding="utf-8")
lock = (port / "MGB64_SOURCE.lock").read_text(encoding="utf-8")

require(
    "commit=cd9b58f5f91291579b8e551aa925aab000d311cf" in lock,
    "MGB64 source commit is not pinned",
)
for flag in ("-march=r5900", "-mabi=n32", "-fno-pic", "-mno-abicalls", "-fno-stack-protector"):
    require(flag in makefile, f"required compiler flag missing: {flag}")
for source in ("platform_titan.c", "gfx_titan.c", "titan_ge_libc.c", "titan_ge_closed_stubs.c"):
    require(source in makefile, f"target source missing: {source}")
for forbidden_source in ("decor_assets.c", "input_tape.c", "minimap_overlay.c"):
    require(forbidden_source not in makefile, f"desktop-only source leaked into target: {forbidden_source}")

require("game_begin" in module and "game_end" in module, "TGR lifecycle handoff missing")
require("ExecPS2" not in module and "ExecPS2" not in runtime, "native port attempts external handoff")
require("TitanGeRuntime_HasFailed" in module, "fatal runtime state is not observed")
require("PORT_TITAN_SFX_CACHE_BYTES (1536u * 1024u)" in audio, "bounded SFX cache missing")
require("#define PC_POOL_SIZE (5 * 1024 * 1024)" in boss, "5 MiB game pool missing")
require("#define MAX_BUFFERED 512" in gfx_pc, "bounded TGR triangle buffer missing")
require("ROOM_XLU_DEFER_MAX_BATCHES 512" in gfx_pc, "bounded XLU queue missing")
require("#define GFX_PTR_TABLE_BITS 12" in gfx_ptr, "bounded pointer registry missing")

for token in (
    "TITAN_GAME_FEATURE_RENDER_STATE", "TITAN_GAME_FEATURE_PERSPECTIVE_STQ",
    "TITAN_GAME_FEATURE_TWO_TEXTURE_MATERIAL", "TITAN_GAME_FEATURE_VIEWPORT_SCISSOR",
    "TITAN_GAME_FEATURE_HAPTICS", "TITAN_GAME_FEATURE_TEXTURE_UPLOAD_RGBA32",
    "video_draw_material_triangles", "video_upload_texture_rgba32", "set_pad_rumble",
):
    require(token in sdk, f"TGR v2 contract missing {token}")
for token in ("combine_mode", "q0", "q1", "fog", "texture_handle[1]"):
    require(token in renderer, f"Fast3D material bridge missing {token}")

game_branding = [
    path.name for path in (mgb / "src/game").glob("*.c")
    if "TITANEYE" in path.read_text(encoding="utf-8", errors="ignore")
]
require(game_branding == ["front.c"], f"branding escaped front.c: {game_branding}")
require(
    "TITANEYE" not in (mgb / "src/game/title.c").read_text(encoding="utf-8", errors="ignore"),
    "title.c was branded",
)

for path in project.rglob("*"):
    if path.is_file():
        require(path.suffix.lower() not in {".z64", ".v64", ".n64"}, f"ROM leaked into public tree: {path}")

policy = (project / "docs/reference/TITAN_STANDALONE_SDK.md").read_text(encoding="utf-8")
require("Titan Game Runtime (TGR) SDK" in policy, "standalone TGR SDK policy missing")
require("0.3.93-rc17" in policy, "validated runtime baseline missing")
require(not (project / "sdk/titan-game").exists(), "embedded Titan Game SDK must not remain in main tree")

builder = (port / "tools/build_private_package.py").read_text(encoding="utf-8")
require("KNOWN_US_SHA1" in builder and "abe01e4aeb033b6c0836819f549c791b26cfde83" in builder,
        "private package builder does not validate the supported retail ROM")
require('"--type", "titan-game"' in builder, "private builder does not emit titan-game")
require("finally:" in builder and "shutil.rmtree(source)" in builder,
        "private builder leaves raw ROM staging behind")

for script_name in ("build_all.sh", "build_diagnostic_sweep.sh", "package_source.sh"):
    script = (project / "scripts" / script_name).read_text(encoding="utf-8")
    require("--exclude='./ports/goldeneye/build'" in script,
            f"{script_name} can snapshot private TitanEYE build staging")

print("titaneye-port-contract: OK source, lifecycle, memory, branding, SDK, private ROM isolation")
