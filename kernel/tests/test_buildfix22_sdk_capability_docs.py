#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
status=(root/'docs/PIKAVR_VLOMBISCORE.md').read_text()
policy=(root/'docs/reference/TITAN_STANDALONE_SDK.md').read_text()
for token in ['vram_saved','POINT/LINEAR','PSM8+CLUT','HOST VALIDATED','no public pikaVR ABI']:
    assert token.lower() in status.lower(), token
assert 'capability declaration with zero measured events is insufficient' in status.replace('\n',' ').lower()
assert 'no public direct `pikaVR_*`' in policy
assert 'distributed separately' in ' '.join(policy.split())
print('buildfix22-sdk-capability-docs: OK standalone SDK boundary + pikaVR ownership')
