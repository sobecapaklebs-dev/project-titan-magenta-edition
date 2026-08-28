#!/usr/bin/env python3
"""ROM-free guard for live WebGPU draw-boundary framebuffer readback."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GFX_WEBGPU = ROOT / "src/platform/fast3d/gfx_webgpu.c"


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


def require_in_order(source: str, *tokens: str) -> None:
    cursor = -1
    for token in tokens:
        found = source.find(token, cursor + 1)
        if found < 0:
            raise AssertionError(f"missing ordered token: {token}")
        cursor = found


def main() -> int:
    source = GFX_WEBGPU.read_text(encoding="utf-8")
    submit = function_body(source, "wgpu_submit_live_scene_for_readback")
    readback = function_body(source, "wgpu_read_framebuffer_rgb")

    # Only the readback slow path may split the ordinary single-submit frame.
    assert source.count("wgpu_submit_live_scene_for_readback(") == 2
    assert "bool live_scene = s_frame_open;" in readback
    assert "wgpu_submit_live_scene_for_readback()" in readback
    assert "? s_scene_tex" in readback
    assert "s_present_target_tex ? s_present_target_tex : s_scene_tex" in readback

    # WEB-023's deferred vertex data has to reach the queue before the partial
    # command buffer, after which recording resumes in a Load/Store pass.
    require_in_order(
        submit,
        "wgpuRenderPassEncoderEnd(s_pass)",
        "wgpuQueueWriteBuffer",
        "wgpuCommandEncoderFinish(s_encoder",
        "wgpuQueueSubmit",
        "wgpuDeviceCreateCommandEncoder",
        "wgpuCommandEncoderBeginRenderPass",
        "wgpu_reset_pass_dynamic_state()",
    )
    assert submit.count("WGPULoadOp_Load") >= 2
    assert submit.count("WGPUStoreOp_Store") >= 2
    assert "wgpuRenderPassEncoderRelease(s_pass)" in submit
    assert "s_pass = NULL;" in submit

    print("PASS: WebGPU draw-boundary readback submits and resumes the live scene")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
