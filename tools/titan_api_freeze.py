#!/usr/bin/env python3
"""Generate/verify the Magenta v1 public API freeze manifest."""
from __future__ import annotations
import argparse, hashlib, json
from pathlib import Path
PUBLIC = (
    'kernel/include/titan_app_sdk.h',
    'kernel/include/titan_game_sdk.h',
    'sdk/titan-app/include/titan_app_sdk.h',
    'sdk/titan-app/ABI_REFERENCE.md',
    'sdk/titan-game/ABI_REFERENCE.md',
    'THEME_AUTHORING_V4.md',
    'TITAN_THEME_DYNAMIC_LIFECYCLE.md',
)
def digest(p: Path) -> str: return hashlib.sha256(p.read_bytes()).hexdigest()
def snapshot(root: Path) -> dict:
    files={name:digest(root/name) for name in PUBLIC}
    return {'schema':'project-titan-api-freeze-v1','api_version':'1.0','files':files}
def main()->int:
    ap=argparse.ArgumentParser(); ap.add_argument('command',choices=('write','verify')); ap.add_argument('--root',type=Path,default=Path('.')); ap.add_argument('--manifest',type=Path,required=True); a=ap.parse_args()
    current=snapshot(a.root.resolve())
    if a.command=='write': a.manifest.write_text(json.dumps(current,indent=2,sort_keys=True)+'\n',encoding='utf-8'); return 0
    expected=json.loads(a.manifest.read_text(encoding='utf-8'))
    if expected!=current: raise SystemExit('public API freeze mismatch')
    print('Titan Magenta v1 API freeze: OK'); return 0
if __name__=='__main__': raise SystemExit(main())
