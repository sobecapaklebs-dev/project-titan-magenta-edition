#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1] if len(sys.argv)>1 else '.')
s=(root/'scripts/build_all.sh').read_text()
k=(root/'kernel/Makefile').read_text()
host=(root/'kernel/tests/run_host_tests.sh').read_text()
required=[
'make -C "$work/modules/titan-ul-engine/modules/pademu" clean USE_USB=1',
'make -C "$work/modules/titan-ul-engine/modules/pademu" clean USE_BT=1',
'make -C "$work/modules/titan-ul-engine/modules/pademu" USE_USB=1',
'make -C "$work/modules/titan-ul-engine/modules/pademu" USE_BT=1',
]
for x in required:
    assert x in s, f'missing PADEMU freshness step: {x}'
pre=s.index(required[0])
kernel=s.index('make -C "$work/kernel" all')
assert pre < kernel, 'PADEMU transports must be rebuilt before kernel embedding'
assert 'TITAN_BT_PADEMU_IRX = ../modules/titan-ul-engine/modules/pademu/bt_pademu.irx' in k
assert '$(TITAN_BT_PADEMU_HEADER): $(TITAN_BT_PADEMU_IRX)' in k
assert '$(BIN2C) $< $@ titan_bt_pademu_irx' in k
assert 'python3 "$PROJECT_ROOT/kernel/tests/test_b30_p6_pademu_embed_freshness_contract.py" "$PROJECT_ROOT"' in host
assert '$ROOT/kernel/tests/test_b30_p6_pademu_embed_freshness_contract.py' not in host
print('b30-p6-pademu-embed-freshness: PASS fresh USB/BT PADEMU rebuilt before kernel bin2c embed')
