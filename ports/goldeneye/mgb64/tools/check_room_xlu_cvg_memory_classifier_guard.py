#!/usr/bin/env python3
"""ROM-free guard for the default room XLU coverage-memory classifier."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GFX_PC = ROOT / "src/platform/fast3d/gfx_pc.c"


def function_body(source: str, name: str) -> str:
    marker = f"{name}("
    start = source.find(marker)
    if start < 0:
        raise AssertionError(f"missing function {name}")
    brace = source.find("{", start)
    if brace < 0:
        raise AssertionError(f"missing body for {name}")

    depth = 0
    for index in range(brace, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : index]
    raise AssertionError(f"unterminated body for {name}")


def require_in_order(source: str, first: str, second: str, message: str) -> None:
    first_at = source.find(first)
    second_at = source.find(second)
    if first_at < 0 or second_at < 0 or first_at > second_at:
        raise AssertionError(message)


def main() -> int:
    source = GFX_PC.read_text(encoding="utf-8")
    gate = function_body(source, "gfx_room_xlu_cvg_memory_gate_reason")
    needed = function_body(source, "gfx_room_xlu_cvg_memory_needed")

    for required in (
        "gfx_room_xlu_cvg_memory_enabled()",
        "g_current_draw_class != DRAWCLASS_ROOM",
        "!settex_active",
        "!use_fog",
        "blend_mode != GFX_BLEND_ALPHA",
        "zmode != ZMODE_XLU",
        "texture_edge",
        "g_sky_tri_mode",
        "rdp.env_color.a != 255",
        "!gfx_raw_mode_has_xlu_wrap_color_on_coverage(raw_mode)",
        'strcmp(dl_which, "secondary") == 0',
        "room_matrix",
        'return "ok";',
        "dl_room < 0 && dl_which == NULL && !room_matrix &&",
        "rdp.prim_color.a == 0",
        'return "ok_generated_room";',
    ):
        assert required in gate, f"missing classifier guard: {required}"

    require_in_order(
        gate,
        "rdp.env_color.a != 255",
        "ok_generated_room",
        "generated-room fallback must stay behind the env-alpha material gate",
    )
    require_in_order(
        gate,
        "!gfx_raw_mode_has_xlu_wrap_color_on_coverage(raw_mode)",
        "ok_generated_room",
        "generated-room fallback must stay behind the raw RDP coverage-wrap gate",
    )

    assert 'strcmp(gate_reason, "ok") == 0' in needed
    assert 'strcmp(gate_reason, "ok_generated_room") == 0' in needed

    print("PASS: room XLU coverage-memory classifier guard")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
