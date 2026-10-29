#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1]).resolve()
provider=(r/'apps/project-nes/src/project_nes_fceumm_provider.c').read_text(errors='replace')
manifest=r/'external/fceumm/FCEUMM_IMPORTED_SHA256SUMS'
license_path=r/'third_party/fceumm/modern/Copying'

def req(x,m):
 if not x: raise SystemExit('buildfix27-fceumm-materialization-gate: FAIL: '+m)
req('b5e3566515c27dc66c9c20572171673126532e06' in provider,'modern frozen commit lost')
req('GPL-2.0-only' in provider,'GPL obligation missing from provider metadata')
req(manifest.exists() and license_path.exists(),'materialized FCEUmm source/license evidence missing')
req('Legacy NROM fallback' in (r/'apps/project-nes/src/project_nes.c').read_text(errors='replace'),'fallback provider identity lost')
print('buildfix27-fceumm-materialization-gate: OK exact refs/GPL/real materialization truthful')
