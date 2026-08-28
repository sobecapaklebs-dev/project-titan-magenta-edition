#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1]).resolve();k=(r/'kernel/include/titan_app_sdk.h').read_text();pub=(r/'sdk/titan-app/include/titan_app_sdk.h').read_text();read=(r/'sdk/titan-app/README.md').read_text();abi=(r/'sdk/titan-app/ABI_REFERENCE.md').read_text();guide=(r/'sdk/titan-app/PORTING_GUIDE.md').read_text();docs=(r/'docs/TITAN_APP_SDK.md').read_text()
def req(x,m):
 if not x: raise SystemExit('buildfix27-titan-app-sdk-docs: FAIL: '+m)
req(k==pub,'canonical SDK header diverged from kernel ABI')
req('offsetof(TitanAppApiV1, field)' in read and 'struct_size < sizeof(TitanAppApiV1)' not in read,'append-only field gating guidance missing/unsafe')
for term in ['storage_commit_replace','network_get_begin','ftp_start','media_present_image_file']:
 req(term in abi or term in guide,term+' documentation missing')
req('PS2IP' in guide and 'FileXio' in guide and 'do not implement a second socket stack' in guide,'ownership prohibition missing')
req('BuildFix27' in docs and 'TARGET_BLOCKED' in docs,'primary SDK doc lacks BuildFix27 target truthfulness')
print('buildfix27-titan-app-sdk-docs: OK canonical header + ownership/lifecycle/service docs')
