#!/usr/bin/env python3
from pathlib import Path
import subprocess,sys,tempfile
r=Path(sys.argv[1]); f=r/'kernel/tests/fixtures/repository_v1/catalog.trepo'
p=subprocess.run([sys.executable,str(r/'kernel/tools/titan_repository.py'),str(f)],text=True,capture_output=True); assert p.returncode==0,p.stderr; assert 'packages=2' in p.stdout
bad=f.read_text().replace('platform=ps2','platform=arm64',1)
with tempfile.NamedTemporaryFile('w',delete=False,suffix='.trepo') as t:t.write(bad);name=t.name
q=subprocess.run([sys.executable,str(r/'kernel/tools/titan_repository.py'),name],text=True,capture_output=True);Path(name).unlink();assert q.returncode!=0
h=(r/'kernel/src/services/titan_repository_protocol.h').read_text(); c=(r/'kernel/src/services/titan_repository_protocol.c').read_text()
for token in ['TITAN_REPOSITORY_MAX_PACKAGES','mips-r5900','sha256','package_size','dependencies']: assert token in h
assert 'malloc(' not in c and 'calloc(' not in c and 'realloc(' not in c
print('titan-repository-protocol-contract: OK')
