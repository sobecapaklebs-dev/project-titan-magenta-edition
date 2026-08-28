#!/usr/bin/env python3
from pathlib import Path
import json,sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
fixture=root/'kernel/tests/fixtures/theme_v4_flagship/vice_city_flagship.synthetic.json'
spec=json.loads(fixture.read_text())
assert len(spec['layers'])>=12
assert len(spec.get('rare_events',[]))>=2
periods={int(x.get('duration_ms',x.get('animation',{}).get('duration_ms',0))) for x in spec['layers'] if x.get('mode')!='STATIC'}
assert len(periods)>=6, periods
assert all(Path(x['asset']).suffix.lower() not in {'.mp4','.avi','.mkv','.gif','.elf','.irx','.tmod','.so','.vsm'} for x in spec['layers'])
assert max(x['width']*x['height'] for x in spec['layers']) <= 64*48
assert sum(x['width']*x['height']*2 for x in spec['layers']) < spec['ee_ram_budget']
scene=(root/'kernel/src/ui/titan_shell_scene.c').read_text(errors='ignore')
hdr=(root/'kernel/src/services/titan_theme_v4_runtime.h').read_text(errors='ignore')
for token in ('Theme v4: active=%u visible=%u animated=%u static=%u draws=%u degrade=%u','Theme v4 perf: update_us=%u render_us=%u pika_hits=%u pika_misses=%u evictions=%u rare_events=%u'):
    assert token in scene
for token in ('theme_vram_peak','clut_usage','pikavr_hits','pikavr_misses','evictions','animation_update_us','theme_render_us','rare_events_active'):
    assert token in hdr
print('buildfix28-theme-v4-flagship-contract: OK multilayer/no-video/independent-timelines/DEX-profiler')
