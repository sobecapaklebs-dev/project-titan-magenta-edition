#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VIDEO = (ROOT / "src/graphics/titan_video.c").read_text()
MODE = (ROOT / "src/graphics/titan_video_mode.c").read_text()
MEMORY = (ROOT / "src/graphics/titan_video_memory.c").read_text()
HEADER = (ROOT / "src/graphics/titan_video.h").read_text()
WAVE_HEADER = (ROOT / "src/graphics/titan_wave_vu1.h").read_text()
WAVE = (ROOT / "src/graphics/titan_wave_vu1.c").read_text()
VSM = (ROOT / "src/graphics/vu1/titan_wave_vu1.vsm").read_text()
SHELL = (ROOT / "src/ui/titan_shell_scene.c").read_text()
FONT_METRICS = (ROOT / "src/graphics/titan_font_metrics.h").read_text()
MAKEFILE = (ROOT / "Makefile").read_text()

# Both modes use real render targets and tear-free page flipping. Native 720p
# has 48 hidden storage rows so each PSM16 surface occupies whole GS pages.
for token in (
    "#define TITAN_VIDEO_LEGACY_WIDTH 640u",
    "#define TITAN_VIDEO_LEGACY_HEIGHT 448u",
    "#define TITAN_VIDEO_HD_WIDTH 1280u",
    "#define TITAN_VIDEO_HD_HEIGHT 720u",
    "#define TITAN_VIDEO_HD_STORAGE_HEIGHT 768u",
):
    assert token in HEADER, token

for token in (
    "config.width = TITAN_VIDEO_HD_WIDTH;",
    "config.height = TITAN_VIDEO_HD_HEIGHT;",
    "config.storage_height = TITAN_VIDEO_HD_STORAGE_HEIGHT;",
    "config.count = 2u;",
    "config.psm = GS_PSM_16;",
    "config.width = TITAN_VIDEO_LEGACY_WIDTH;",
    "config.height = TITAN_VIDEO_LEGACY_HEIGHT;",
    "config.storage_height = TITAN_VIDEO_LEGACY_HEIGHT;",
    "config.psm = GS_PSM_32;",
):
    assert token in VIDEO, token

assert "config.count = 1u;" not in VIDEO
assert "640 x 360" not in SHELL
assert "1280 x 720 PSM16 / double" in SHELL
assert "640 x 448 PSM32 / double" in SHELL

# CRTC ownership stays outside the primitive renderer.
for forbidden in (
    "graph_set_mode(",
    "graph_set_screen(",
    "GS_REG_DISPLAY1",
    "GS_REG_DISPLAY2",
):
    assert forbidden not in VIDEO, forbidden

for token in (
    "GRAPH_MODE_HDTV_720P",
    "width == TITAN_VIDEO_HD_WIDTH",
    "height == TITAN_VIDEO_HD_HEIGHT",
    "count == 2u",
    "psm == GS_PSM_16",
    "width == TITAN_VIDEO_LEGACY_WIDTH",
    "height == TITAN_VIDEO_LEGACY_HEIGHT",
    "psm == GS_PSM_32",
    "TitanVideoMode_BeginReconfigure",
    "graph_disable_output();",
):
    assert token in MODE, token

assert "graph_set_screen(" not in MODE
assert "GS_SET_DISPLAY" in MODE
assert "#define TITAN_VIDEO_HDTV_720P_DX 302" in MODE
assert "#define TITAN_VIDEO_HDTV_720P_DY 24" in MODE
assert "timing.default_x = TITAN_VIDEO_HDTV_720P_DX" in MODE
assert "timing.default_y = TITAN_VIDEO_HDTV_720P_DY" in MODE
assert "timing.active_width = 1280u" in MODE
assert "timing.active_height = 720u" in MODE

# Switching output rebuilds the complete VRAM layout. This is required because
# two native surfaces do not fit beside the full PSM32 resident atlases.
assert "graph_vram_allocate(" not in VIDEO
assert "graph_vram_allocate(" not in WAVE
assert "graph_vram_allocate(" in MEMORY
assert "TitanVideoMemory_Initialize()" in VIDEO
assert "TitanVideo_AllocateResourceLayout(&new_config)" in VIDEO
assert "TitanVideo_ReuploadResidentResources()" in VIDEO
assert "TitanVideo_ConfigureOutputMapping(new_config.output);" in VIDEO
assert "TitanVideoMemory_FindAddress" in MEMORY
assert "TitanVideoMemory_FindAddress" in WAVE

wave_position = VIDEO.index("wave_texture_address = TitanVideoMemory_Allocate(")
framebuffer_position = VIDEO.index("static int TitanVideo_AllocateFramebuffers(")
assert wave_position < framebuffer_position

# Native mode keeps alpha masks bit-exact as PSM8 indices with one shared CLUT,
# and streams original PSM16 avatars through a bounded scratch texture.
for token in (
    '"font-atlas-indexed"',
    '"ui-atlas-indexed"',
    '"alpha-clut"',
    '"avatar-scratch"',
    "GS_PSM_8",
    "TitanVideo_GetClutStorageIndex",
    "CLUT_LOAD",
    "g_video_font_indices",
    "g_video_ui_indices",
    "g_video_avatar_scratch",
    "TitanVideo_LoadAvatarChunk",
):
    assert token in VIDEO, token

assert "#define TITAN_FONT_ATLAS_WIDTH 320" in FONT_METRICS
assert "#define TITAN_FONT_ATLAS_HEIGHT 144" in FONT_METRICS

# Every supported output presents a completed back buffer only at VSync.
assert "if (g_video.framebuffer_count == 1u)" not in VIDEO
assert "if (g_video.framebuffer_count > 1u)" not in VIDEO
assert "graph_wait_vsync();" in VIDEO
assert "g_video.framebuffers[g_video.drawing_buffer].address" in VIDEO
assert "g_video.drawing_buffer = g_video.displayed_buffer ^ 1u;" in VIDEO

# Exact 4 MiB high-water budget using graph_vram's width and pointer alignment.
TOTAL_WORDS = (4 * 1024 * 1024) // 4
PSM32 = 0
PSM16 = 2
PSM8 = 19
BLOCK_WORDS = 64
PAGE_WORDS = 2048


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def size_words(width: int, height: int, psm: int, alignment: int) -> int:
    if width > 16:
        width = align_up(width, 128 if psm == PSM8 else 64)
    if psm == PSM32:
        words = width * height
    elif psm == PSM16:
        words = width * (height // 2)
    elif psm == PSM8:
        words = width * (height // 4)
    else:
        raise AssertionError(psm)
    return align_up(words, alignment)


def allocate(pointer: int, width: int, height: int, psm: int, alignment: int):
    size = size_words(width, height, psm, alignment)
    new_pointer = align_up(pointer + size, alignment)
    address = new_pointer - size
    return new_pointer, address, size


def allocate_layout(items):
    pointer = 0
    allocations = []
    for item in items:
        pointer, address, size = allocate(pointer, *item)
        allocations.append((address, size))
    return pointer, allocations


legacy_items = [
    (128, 32, PSM32, BLOCK_WORDS),
    (512, 256, PSM16, BLOCK_WORDS),
    (512, 512, PSM16, BLOCK_WORDS),
    (512, 256, PSM32, BLOCK_WORDS),
    (512, 256, PSM32, BLOCK_WORDS),
    (640, 448, PSM32, PAGE_WORDS),
    (640, 448, PSM32, PAGE_WORDS),
]
hd_items = [
    (128, 32, PSM32, BLOCK_WORDS),
    (384, 192, PSM8, BLOCK_WORDS),
    (512, 256, PSM8, BLOCK_WORDS),
    (16, 16, PSM32, BLOCK_WORDS),
    (256, 64, PSM16, PAGE_WORDS),
    (1280, 768, PSM16, PAGE_WORDS),
    (1280, 768, PSM16, PAGE_WORDS),
]

legacy_words, _ = allocate_layout(legacy_items)
hd_words, hd_allocations = allocate_layout(hd_items)
hd_prefix_end = hd_allocations[4][0] + hd_allocations[4][1]

assert legacy_words * 4 == 4_145_152
assert (TOTAL_WORDS - legacy_words) * 4 == 49_152
assert hd_prefix_end * 4 == 262_144
assert hd_allocations[5][0] == hd_prefix_end
assert hd_words == TOTAL_WORDS

# Titan App media support must not silently add a sixth resident 720p texture.
# Native 720p is already at the exact 4 MiB high-water mark, so media frames
# are streamed through the existing avatar scratch in ordered tiles.
assert '"media-surface"' not in VIDEO
assert "TitanVideo_DrawCompactMediaTile16" in VIDEO
assert "g_video_media_scratch" in VIDEO
assert "TITAN_VIDEO_AVATAR_SCRATCH_HEIGHT" in VIDEO
assert "TitanVideo_DrawMediaImage16" in VIDEO

assert "src/graphics/titan_video_mode.o" in MAKEFILE
assert "src/graphics/titan_video_memory.o" in MAKEFILE

# EE and VU1 continue to share one exact data-memory contract.
for c_token, vsm_token in (
    ("TITAN_WAVE_VU1_ADDR_COLORS 160u", "iaddiu vi02, vi00, 160"),
    ("TITAN_WAVE_VU1_ADDR_UVS 240u", "iaddiu vi03, vi00, 240"),
    ("TITAN_WAVE_VU1_ADDR_PATH1_STATE 384u", "iaddiu vi07, vi00, 384"),
    ("TITAN_WAVE_VU1_ADDR_OUTPUT 390u", "iaddiu vi04, vi00, 390"),
):
    assert c_token in WAVE, c_token
    assert vsm_token in VSM, vsm_token

for token in (
    '#define TITAN_WAVE_TEXTURE_ALLOCATION_NAME "wave-texture"',
    "#define TITAN_WAVE_TEXTURE_WIDTH 128u",
    "#define TITAN_WAVE_TEXTURE_HEIGHT 32u",
):
    assert token in WAVE_HEADER, token

assert "alpha = layer->alpha" in WAVE
assert "layers[layer_index].texture_u_scale" in WAVE
assert "g_wave_alpha_state" in WAVE

print("renderer-native-480i-720p-double-buffer-contract: OK")
print(f"legacy-vram: {legacy_words * 4} bytes, free: {(TOTAL_WORDS - legacy_words) * 4}")
print(f"native-720p-vram: {hd_words * 4} bytes, free: {(TOTAL_WORDS - hd_words) * 4}")
