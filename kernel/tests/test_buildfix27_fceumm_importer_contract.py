#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1]).resolve(); s=(r/'scripts/import_fceumm_sources.py').read_text(); runner=(r/'kernel/tests/run_host_tests.sh').read_text()
def req(v,m):
 if not v: raise SystemExit('buildfix27-fceumm-importer-contract: FAIL: '+m)
for x in ('b5e3566515c27dc66c9c20572171673126532e06','dc77e3ca4baad08f0023f61dba9846550735b74d'): req(x in s,x+' pin missing')
for x in ('6250f2a46fd10f1b16531238fb3b42cced7f30d8','fb78963e07cee28619550ed6b572c015db70c378','bf1e49d5923f5b0f45b055b3e40bedba4425ffc7','9dd4328879b5d9f6a31c47da70c546852985a8c5'): req(x in s,x+' critical blob fingerprint missing')
req('GNU GENERAL PUBLIC LICENSE' in s,'GPL gate missing')
req('".." in p.parts' in s and 'member.isdir() or member.isfile()' in s,'archive traversal/link guard missing')
req('test_buildfix27_fceumm_importer_contract.py' in runner,'official runner importer gate missing')
print('buildfix27-fceumm-importer-contract: OK deterministic frozen-source import gate')
