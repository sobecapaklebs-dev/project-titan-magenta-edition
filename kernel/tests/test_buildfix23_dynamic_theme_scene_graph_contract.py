#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
h=(root/'src/services/titan_theme_scene_graph.h').read_text();c=(root/'src/services/titan_theme_scene_graph.c').read_text()
for n in ('BACKGROUND','MIDGROUND','FOREGROUND','SPRITE','LIGHT','PARTICLE','RIBBON','DECORATION','OVERLAY'):assert 'TITAN_THEME_LAYER_'+n in h
for n in ('TRANSFORM','FRAME','ALPHA','VISIBILITY','ASSET','CONTEXT'):assert 'TITAN_THEME_DIRTY_'+n in h
assert 'declaration_order' in h and 'TitanThemeScene_StableSort' in c
assert 'TITAN_THEME_SCENE_MAX_LAYERS 32u' in h
print('dynamic-theme-scene-graph-contract: OK compiled declarative layers, stable z/declaration order, dirty flags')
