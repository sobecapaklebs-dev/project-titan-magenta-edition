#!/usr/bin/env python3
from pathlib import Path
r=Path(__file__).resolve().parents[2]
files=['kernel/src/services/titan_session_service.c','kernel/src/services/titan_ingame_overlay.c','kernel/src/services/titan_unified_input.c','kernel/src/services/titan_platform_overhead.c']
for name in files:
 s=(r/name).read_text()
 for bad in ('malloc(', 'calloc(', 'realloc(', 'free('):
  assert bad not in s, f'{name}: frame/runtime allocation token {bad}'
print('buildfix28-no-frame-allocation-contract: OK')
