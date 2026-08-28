from pathlib import Path
r=Path(__file__).resolve().parents[1]
h=(r/'src/services/titan_theme_scene_graph.h').read_text(); c=(r/'src/services/titan_theme_scene_graph.c').read_text()
assert '#define TITAN_THEME_MAX_DECLARED_FRAMES 128u' in h
assert '#define TITAN_THEME_MAX_RESIDENT_FRAMES 3u' in h
for layer in ['BACKGROUND','MIDGROUND','FOREGROUND','SPRITE','LIGHT','PARTICLE','RIBBON','DECORATION','OVERLAY']: assert 'TITAN_THEME_LAYER_'+layer in h
for mode in ['LOOP','PINGPONG','ONCE','HOLD','RANDOM_START','RANDOM_SEQUENCE']: assert 'TITAN_THEME_TIMELINE_'+mode in h
assert 'TITAN_THEME_STREAM_QUEUE_CAPACITY 16u' in h
assert 'reserved_headroom' in h and 'TitanThemeBudget_CanReserveVram' in c
for q in ['TITAN_THEME_QUALITY_FULL','TITAN_THEME_QUALITY_BALANCED','TITAN_THEME_QUALITY_REDUCED']: assert q in h
assert 'TitanThemeDynamic_ApplyPressure' in c
assert 'fopen(' not in c and 'malloc(' not in c and 'free(' not in c
print('buildfix24 theme stress validation: PASS')
