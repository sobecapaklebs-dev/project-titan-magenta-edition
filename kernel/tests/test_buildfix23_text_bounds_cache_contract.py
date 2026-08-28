#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
h=(root/'src/ui/titan_ui_layout_guard.h').read_text(); c=(root/'src/ui/titan_ui_layout_guard.c').read_text(); s=(root/'src/ui/titan_shell_scene.c').read_text()
for f in ('measured_width','measured_height','max_width','max_lines','clip_rect','ellipsis','padding'): assert f in h
assert 'TITAN_UI_TEXT_CACHE_CAPACITY 64u' in c
assert 'TitanUiText_MeasureCached(scale, destination' in s
print('titan-ui-text-bounds-contract: OK formal bounds + fixed metrics cache on fitting hot path')
