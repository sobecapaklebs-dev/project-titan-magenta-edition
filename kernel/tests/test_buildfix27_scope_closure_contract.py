#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1]).resolve()
closure=(r/'BUILDFIX27_SCOPE_CLOSURE.md').read_text(errors='replace')
handoff=(r/'BUILDFIX27_TARGET_HANDOFF.md').read_text(errors='replace')
runner=(r/'kernel/tests/run_host_tests.sh').read_text(errors='replace')
progress=(r/'BUILDFIX27_PROGRESS.md').read_text(errors='replace')
status=(r/'BUILDFIX27_FINAL_SOURCE_STATUS.md').read_text(errors='replace')
validation=(r/'BUILDFIX27_FINAL_VALIDATION.md').read_text(errors='replace')

def req(v,m):
    if not v: raise SystemExit('buildfix27-scope-closure: FAIL: '+m)
req('STATUS: HOST_SCOPE_CLOSED_WITH_EXTERNAL_BLOCKERS' in closure,'closure status missing')
for token in ('recursive copy/move','VMC inspection','transactional export','FTP control','RETR','transactional STOR','Browser no longer owns direct PS2IP','lifecycle/reclaim cycles'):
    req(token in closure, token+' host disposition missing')
for token in ('BLOCKED_EXTERNAL_BINARY','BLOCKED_EXTERNAL_MATERIALIZATION','BLOCKED_EXTERNAL_TOOLCHAIN'):
    req(token in closure, token+' missing')
req('b5e3566515c27dc66c9c20572171673126532e06' in closure,'modern FCEUmm pin missing')
req('dc77e3ca4baad08f0023f61dba9846550735b74d' in closure,'PS2 FCEUmm pin missing')
req('No external blocker is reclassified as complete' in closure,'truthfulness guard missing')
req('PS2DEV' in handoff and 'PCSX2' in handoff and 'physical PS2' in handoff,'target handoff incomplete')
req('test_buildfix27_scope_closure_contract.py' in runner,'closure gate absent from official runner')
req('TARGET_BUILD_VALIDATION_PENDING_EXTERNAL_PS2DEV' in progress and 'EXTERNAL_VALIDATION_PENDING_PS2DEV' in status,'target external-validation state lost')
req('FCEUMM_REAL_INTEGRATED=1' in progress and 'FCEUMM_REAL_INTEGRATED=1' in status,'FCEUmm integrated state lost')
req(all(x in (status+validation) for x in ('ps2atad.irx','ps2hdd.irx','ps2fs.irx')),'PFS external runtime dependencies lost')
print('buildfix27-scope-closure: OK host scope closed; FCEUmm integrated; external target/runtime validation preserved')
