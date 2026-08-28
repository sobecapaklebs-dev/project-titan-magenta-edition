#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
h=(root/'src/services/titan_theme_scene_graph.h').read_text();c=(root/'src/services/titan_theme_scene_graph.c').read_text()
for q in ('FULL','BALANCED','REDUCED'):assert 'TITAN_THEME_QUALITY_'+q in h
for b in ('max_particles','spawn_budget','update_budget','draw_budget'):assert b in h
for x in ('navigation','focus','transition','idle'):assert x in h
assert 'Norm(' in c and 'ApplyPressure' in c
assert 'VU1' not in c and 'PATH1' not in c
print('theme-dynamic-effects-contract: OK normalized declarative parallax, particle budgets, FULL/BALANCED/REDUCED degradation, no new VU ownership')
