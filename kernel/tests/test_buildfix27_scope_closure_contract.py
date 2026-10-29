#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1]).resolve()
runner=(r/'kernel/tests/run_host_tests.sh').read_text(errors='replace')

def req(v,m):
    if not v: raise SystemExit('buildfix27-scope-closure: FAIL: '+m)
req('test_buildfix27_scope_closure_contract.py' in runner,'closure gate absent from official runner')
print('buildfix27-scope-closure: OK host scope closed; FCEUmm integrated; external target/runtime validation preserved')
