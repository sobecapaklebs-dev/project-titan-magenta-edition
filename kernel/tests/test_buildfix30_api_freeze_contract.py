#!/usr/bin/env python3
from pathlib import Path
import json, subprocess
root=Path(__file__).resolve().parents[2]
subprocess.run(['python3',str(root/'tools/titan_api_freeze.py'),'verify','--root',str(root),'--manifest',str(root/'TITAN_API_FREEZE_V1.json')],check=True)
m=json.loads((root/'TITAN_API_FREEZE_V1.json').read_text())
assert m['api_version']=='1.0' and len(m['files'])>=7
app=(root/'kernel/include/titan_app_sdk.h').read_text()
for token in ('TITAN_APP_INPUT_CAP_BATTERY','battery_percent_valid','TITAN_APP_INPUT_CAP_SYSTEM_BUTTON','input_get_device'):
    assert token in app
repo=(root/'docs/reference/TITAN_REPOSITORY_PROTOCOL_V1.md').read_text()
assert 'static hosting' in repo and 'HTTP Range resume is not claimed' in repo
print('titan-api-freeze-v1-contract: OK')
print('titan-input-sdk-preservation-contract: OK')
print('titan-repository-protocol-v1-freeze-contract: OK')
