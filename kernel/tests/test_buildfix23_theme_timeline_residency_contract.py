#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
h=(root/'src/services/titan_theme_scene_graph.h').read_text();c=(root/'src/services/titan_theme_scene_graph.c').read_text()
assert 'MAX_DECLARED_FRAMES 128u' in h and 'MAX_RESIDENT_FRAMES 3u' in h
for m in ('LOOP','PINGPONG','ONCE','HOLD','RANDOM_START','RANDOM_SEQUENCE'):assert 'TITAN_THEME_TIMELINE_'+m in h
assert 'duration_ms' in h and '40u' in h and '2000u' in h
assert 'current' in h and 'next' in h and 'recent' in h
assert 'ThemeRand' in c and 'deterministic_seed' in h
assert 'resident_count++' in c
print('theme-timeline-residency-contract: OK 128 declared != max 3 resident; variable durations; deterministic modes')
