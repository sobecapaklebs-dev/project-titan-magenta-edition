#!/usr/bin/env python3
"""ROM-free guard for the OpenGL RDP coverage-memory backend contract."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GFX_OPENGL = ROOT / "src/platform/fast3d/gfx_opengl.c"


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


def coverage_from_alpha_byte(alpha_byte: int) -> int:
    return alpha_byte // 32


def main() -> int:
    source = GFX_OPENGL.read_text(encoding="utf-8")

    room_toggle = function_body(source, "gfx_opengl_room_xlu_cvg_memory_enabled")
    assert "GE007_DISABLE_ROOM_XLU_CVG_MEMORY" in room_toggle
    assert "GE007_ROOM_XLU_CVG_MEMORY" in room_toggle
    assert "g_room_xlu_cvg_memory_enabled = 1;" in room_toggle
    assert "g_room_xlu_cvg_memory_enabled = 0;" in room_toggle

    scene_target = function_body(source, "gfx_opengl_scene_target_enabled")
    assert "gfx_opengl_room_xlu_cvg_memory_enabled()" in scene_target
    assert "gfx_diag_xlu_rdp_cvg_memory_blend_enabled()" in scene_target

    set_blend = function_body(source, "gfx_opengl_set_blend_mode")
    assert "preserve_coverage_alpha" in set_blend
    assert "gfx_opengl_room_xlu_cvg_memory_enabled()" in set_blend
    assert "gfx_diag_xlu_rdp_cvg_memory_blend_enabled()" in set_blend
    assert "mode == GFX_BLEND_ALPHA ||" in set_blend
    assert "mode == GFX_BLEND_MODULATE ||" in set_blend
    assert "mode == GFX_BLEND_ALPHA_COVERAGE ||" in set_blend
    assert "mode == GFX_BLEND_ALPHA_CVG_WRAP_STENCIL" in set_blend
    assert "preserve_coverage_alpha ? GL_FALSE : GL_TRUE" in set_blend

    start_frame = function_body(source, "gfx_opengl_start_frame")
    require_in_order(
        start_frame,
        "glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);",
        "glClearColor(g_clear_r, g_clear_g, g_clear_b, 1.0f);",
        "start_frame must restore alpha writes before clearing coverage memory",
    )

    resolve = function_body(source, "gfx_opengl_resolve_scene_target")
    require_in_order(
        resolve,
        "glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);",
        "glBlitFramebuffer(0, 0, g_scene_w, g_scene_h,",
        "scene resolve must restore alpha writes before blitting",
    )

    assert coverage_from_alpha_byte(255) == 7
    assert coverage_from_alpha_byte(191) == 5
    assert coverage_from_alpha_byte(128) == 4

    print("PASS: RDP coverage-memory backend preserves a deterministic alpha coverage store")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
