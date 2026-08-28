#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1]); h=(r/'kernel/include/titan_app_sdk.h').read_text(); rt=(r/'kernel/src/services/titan_app_runtime.c').read_text(); d=(r/'docs/TITAN_API_PRE_FREEZE.md').read_text(); p=(r/'docs/PIKAVR_API_PRE_FREEZE.md').read_text(); v=(r/'docs/VLOMBISCORE_API_PRE_FREEZE.md').read_text()
for token in ['TitanAppNormalizedInput','TitanAppInputDeviceInfo','input_get_state','input_get_device','TITAN_APP_INPUT_CAP_SYSTEM_BUTTON','TITAN_APP_NBTN_SYSTEM']:
 assert token in h,token
for token in ['TitanAppApi_InputGetState','TitanAppApi_InputGetDevice','reserved_mask = TITAN_NBTN_SYSTEM']:
 assert token in rt,token
assert 'High' in d and 'Mid' in d and 'Advanced' in d
assert 'no direct `pikaVR_*` public ABI' in p
assert 'capability/evidence driven' in v
copies=[r/'apps/project-nes/include/titan_app_sdk.h',r/'apps/wlaunchelf/include/titan_app_sdk.h']
for c in copies: assert c.read_bytes()==(r/'kernel/include/titan_app_sdk.h').read_bytes(),c
print('titan-api-pre-freeze-contract: OK')
print('pikavr-api-pre-freeze-contract: OK')
print('vlombiscore-api-pre-freeze-contract: OK')
