#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1])
s=(root/'modules/titan-ul-engine/modules/pademu/xbox_protocol.c').read_text()
u=(root/'modules/titan-ul-engine/modules/pademu/ds34usb.c').read_text()
for x in ['titan_xbox_gip_build_power_on','titan_xbox_gip_build_s_init','titan_xbox_gip_build_virtual_key_ack']:
 assert x in s and x in u, x
assert 'TITAN_XBOX_GIP_CMD_POWER' in s
assert 'TITAN_XBOX_GIP_CMD_VIRTUAL_KEY' in s
assert 'titan_xbox_usb_needs_s_init' in u
print('buildfix28-xbox-gip-init-contract: OK')
