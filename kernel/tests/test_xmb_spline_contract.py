#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = (ROOT / "src/graphics/titan_wave_vu1.h").read_text()
WAVE = (ROOT / "src/graphics/titan_wave_vu1.c").read_text()
SPLINE = (ROOT / "src/ui/titan_xmb_spline.c").read_text()
BACKGROUND = (ROOT / "src/ui/titan_shell_background.c").read_text()
MAKEFILE = (ROOT / "Makefile").read_text()
TEXTURE = ROOT / "assets/ui/titan_wave_silk.rgba32"

for token in (
    "#define TITAN_WAVE_VU1_POINT_COUNT 65u",
    "#define TITAN_WAVE_VU1_LAYER_COUNT 7u",
    "float fresnel_bias;",
    "float fresnel_scale;",
):
    assert token in HEADER, token

for token in (
    "#define TITAN_XMB_SPLINE_ROW_COUNT (TITAN_WAVE_VU1_LAYER_COUNT + 1u)",
    "#define TITAN_XMB_SPLINE_CONTROL_COUNT 16u",
    "#define TITAN_XMB_SPLINE_TABLE_COUNT 361u",
    "#define TITAN_XMB_SPLINE_KERNEL_ITERATIONS 8u",
    "TitanXmbSpline_BuildDescriptorTable",
    "TitanXmbSpline_BuildBasisTable",
    "TitanXmbSpline_UpdateKernel",
    "TitanXmbSpline_EvaluateControlSpline",
    "TitanXmbSpline_BuildStrips",
    "layers[draw_index].half_width = 1.0f;",
):
    assert token in SPLINE, token

assert "TitanXmbSpline_Draw(\n        frame, transition, g_idle_blend," in BACKGROUND
assert "TitanShellBackground_BuildLayer" not in BACKGROUND
assert "src/ui/titan_xmb_spline.o" in MAKEFILE
assert "surface_rise" in WAVE
assert "layer->fresnel_bias" in WAVE
assert "layer->fresnel_scale" in WAVE
assert TEXTURE.stat().st_size == 128 * 32 * 4

print("xmb-spline-contract: OK")
print("geometry: 8 rows, 7 strips, 65 samples, 910 vertices")
