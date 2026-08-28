#!/usr/bin/env python3
from pathlib import Path
r=Path(__file__).resolve().parents[2]
h=(r/'kernel/src/services/titan_theme_v4_runtime.h').read_text()
b=(r/'kernel/src/ui/titan_shell_background.c').read_text()
s=(r/'kernel/src/ui/titan_shell_scene.c').read_text()
for token in ('active_layers','visible_layers','animated_layers','static_layers','draw_count','texture_uploads','upload_bytes','theme_vram','theme_vram_peak','clut_usage','pikavr_hits','pikavr_misses','evictions','animation_update_us','theme_render_us','degradation_state'):
    assert token in h, token
assert 'TitanShellBackground_GetThemeV4Telemetry' in b
assert 'Theme v4: active=' in s and 'Theme v4 perf:' in s and 'Theme v4 resources:' in s
print('buildfix28-theme-v4-profiler-contract: OK')
