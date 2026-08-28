#!/usr/bin/env python3
from pathlib import Path
r=Path(__file__).resolve().parents[2]; h=(r/'kernel/src/services/titan_theme_v4_runtime.h').read_text(); c=(r/'kernel/src/services/titan_theme_v4_runtime.c').read_text()
assert 'TITAN_THEME_V4_MAX_LAYERS 32u' in h
for token in ('TitanThemeV4_ParseCompiled','TitanThemeV4_EvaluateLayer','TitanThemeV4_EvaluateScene','TITAN_THEME_V4_DEGRADE_DECORATIVE','TITAN_THEME_V4_DEGRADE_ENHANCED'):
 assert token in h
for bad in ('malloc(', 'calloc(', 'realloc(', 'free('): assert bad not in c
assert 'sort_layers' in c and 'declaration_order' in c
print('buildfix28-theme-v4-runtime-contract: OK')
