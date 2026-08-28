#!/usr/bin/env python3
from pathlib import Path
import subprocess,sys
r=Path(sys.argv[1])
tool=r/'kernel/tools/titan_source_audit.py'
assert tool.is_file()
p=subprocess.run([sys.executable,str(tool),str(r)],text=True,capture_output=True)
if p.returncode:
 print(p.stdout); print(p.stderr,file=sys.stderr); raise SystemExit(p.returncode)
out=p.stdout
for token in ['TITAN_SOURCE_AUDIT=PASS','ProjectNesTest.nes','titan_wave_vu1_micro.bin']:
 assert token in out,token
# Test that an unapproved ROM is rejected without mutating production paths.
probe=r/'kernel/tests/.bf29_unapproved_payload.nes'
try:
 probe.write_bytes(b'NES\\x1a' + b'\\0'*32)
 q=subprocess.run([sys.executable,str(tool),str(r)],text=True,capture_output=True)
 assert q.returncode!=0
 assert 'UNAPPROVED_PAYLOAD kernel/tests/.bf29_unapproved_payload.nes' in q.stdout
finally:
 if probe.exists(): probe.unlink()
print('titan-open-source-readiness-contract: OK')
