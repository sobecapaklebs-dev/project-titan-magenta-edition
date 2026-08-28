#!/usr/bin/env python3
from pathlib import Path
import re, sys
root=Path(sys.argv[1] if len(sys.argv)>1 else 'kernel')
s=(root/'src/graphics/titan_wave_vu1.c').read_text().replace('\\\n',' ')
# PRIM order: PRIM,IIP,TME,FGE,ABE,AA1,FST,CTXT,FIX.
m=re.search(r'GS_SET_PRIM\(\s*GS_PRIM_TRIANGLE_STRIP,\s*1u,\s*1u,\s*0u,\s*1u,\s*(\d+)u,\s*1u,\s*0u,\s*0u', s, re.S)
assert m, 'VU1 triangle-strip PRIM contract not found'
assert m.group(1) == '0', 'AA1 must remain disabled: it exposes internal strip edges on translucent wave geometry'
assert 'anti-aliased alpha profile' in s
print('vu1-wave-no-internal-edges: OK')
