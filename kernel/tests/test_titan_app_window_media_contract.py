#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
runtime = (root / 'src/services/titan_app_runtime.c').read_text()
runtime_h = (root / 'src/services/titan_app_runtime.h').read_text()
shell = (root / 'src/ui/titan_shell_scene.c').read_text()
video = (root / 'src/graphics/titan_video.c').read_text()
software = (root / 'src/graphics/backends/titan_video_software.inc').read_text()
sdk = (root / 'include/titan_app_sdk.h').read_text()

assert '#define TITAN_APP_WINDOW_SCALE 0.72f' in runtime
assert 'g_app_runtime.fullscreen = !g_app_runtime.fullscreen;' in runtime
assert 'TITAN_APP_BUTTON_R3' in runtime
assert 'R3  Full screen' in runtime
assert 'int TitanAppRuntime_IsFullscreen(void);' in runtime_h
assert 'if (!TitanAppRuntime_IsFullscreen())' in shell
assert 'TitanShellScene_DrawHome(0.0f, 158u, 1.0f, 1.0f);' in shell
assert 'TitanAppRuntime_MapRect' in runtime and 'TitanAppRuntime_MapPoint' in runtime
assert 'Do not install a global TitanVideo transform while foreign module code is' in runtime
assert 'TitanVideo_SetTransform(' not in runtime
assert 'TitanAppRuntime_ReleaseAllAllocations' in runtime
assert 'TitanAppRuntime_ReadableSpan' in runtime and 'TitanAppRuntime_WritableSpan' in runtime
assert 'TitanAppRuntime_SaneRect' in runtime

# The public API remains conventional RGB565. Hardware GS PSMCT16 is R5G5B5A1,
# so the conversion must reorder channels and force A=1 before DMA upload.
for source in (video, software):
    assert 'TitanVideo_Rgb565ToPsm16' in source
    assert '(pixel >> 11u) & 0x1fu' in source
    assert '(pixel >> 5u) & 0x3fu' in source
    assert 'pixel & 0x1fu' in source
    assert '(blue << 10u) | 0x8000u' in source

assert 'conventional RGB565 512x256' in sdk
assert 'converts them to the GS native PSMCT16 layout' in sdk

# Known-color oracle for the intended conversion.
def convert(pixel: int) -> int:
    red = (pixel >> 11) & 0x1f
    green = (pixel >> 5) & 0x3f
    blue = pixel & 0x1f
    green = min(0x1f, (green + 1) >> 1)
    return red | (green << 5) | (blue << 10) | 0x8000

assert convert(0x0000) == 0x8000
assert convert(0xF800) == 0x801F
assert convert(0x07E0) == 0x83E0
assert convert(0x001F) == 0xFC00
assert convert(0xFFFF) == 0xFFFF

print('titan-app-window-media-contract: OK')
