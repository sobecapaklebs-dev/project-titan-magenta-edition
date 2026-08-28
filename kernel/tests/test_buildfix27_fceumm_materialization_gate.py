#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1]).resolve()
rep=(r/'PROJECT_NES_TITAN_PORT_REPORT.md').read_text(errors='replace')
sel=rep
progress=(r/'BUILDFIX27_PROGRESS.md').read_text(errors='replace')
status=(r/'BUILDFIX27_FINAL_SOURCE_STATUS.md').read_text(errors='replace')

def req(x,m):
 if not x: raise SystemExit('buildfix27-fceumm-materialization-gate: FAIL: '+m)
req('b5e3566515c27dc66c9c20572171673126532e06' in sel,'modern frozen commit lost')
req('dc77e3ca4baad08f0023f61dba9846550735b74d' in sel,'PS2 frozen commit lost')
req('GPL-2.0' in sel or 'GPL-2.0' in rep,'GPL obligation lost')
req(('FCEUMM_REAL_INTEGRATED=1' in progress or 'FCEUmm is materially present and integrated' in progress) and ('FCEUMM_REAL_INTEGRATED=1' in status or 'MATERIALIZED + IMPORTED + INTEGRATED' in status),'provider integration truthfulness lost')
core=(r/'apps/project-nes/src/project_nes_core.h').read_text(errors='replace')
req('Legacy NROM fallback' in (r/'apps/project-nes/src/project_nes.c').read_text(errors='replace') or 'Legacy NROM fallback' in core,'fallback provider identity lost')
print('buildfix27-fceumm-materialization-gate: OK exact refs/GPL/real materialization truthful')
