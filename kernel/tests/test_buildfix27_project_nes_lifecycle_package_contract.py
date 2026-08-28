#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
def text(rel): return (root/rel).read_text(encoding='utf-8',errors='replace')
def req(ok,msg):
    if not ok: raise SystemExit('buildfix27-project-nes-lifecycle-package-contract: FAIL: '+msg)
life=text('apps/project-nes/tests/host_lifecycle_test.c')
app=text('apps/project-nes/src/project_nes.c')
readme=text('apps/project-nes/README.md')
make=text('apps/project-nes/Makefile')
req('cycle<10u' in life and 'run_cycle(&api,cycle)' in life,
    '10-cycle Project NES lifecycle harness missing')
req('g_fail_next_save_write' in life and 'g_save_failures != 1u' in life,
    'save publication failure/retry injection missing')
req('PN_SCREEN_PAUSED' in life and 'prg_ram_dirty' in life,
    'failed pause flush does not prove dirty state is retained')
req('g_alloc_count != 0u' in life and 'guard_ok' in life,
    'allocation/guard cleanup assertions missing')
req('if (written != (int)size)' in app and 'save_ram_clear_dirty' in app,
    'save dirty state may clear before complete publication')
# A materialized FCEUmm tree is accepted only when the frozen import proof exists.
modern=root/'third_party/fceumm/modern'
if modern.exists():
    sums=root/'external/fceumm/FCEUMM_IMPORTED_SHA256SUMS'
    copying=modern/'Copying'
    report=root/'BUILDFIX27_FCEUMM_REAL_INTEGRATION_REPORT.md'
    req(sums.exists() and copying.exists() and report.exists(),
        'materialized FCEUmm tree lacks manifest/license/integration proof')
    rtext=report.read_text(encoding='utf-8',errors='replace')
    req('b5e3566515c27dc66c9c20572171673126532e06' in rtext and
        '6adfb382bf8a369f1a6ee224e89c08d981e22cec' in rtext,
        'materialized FCEUmm proof does not match frozen BF27 identity')
else:
    raise SystemExit('buildfix27-project-nes-lifecycle-package-contract: FAIL: frozen FCEUmm source is not materialized')
req('BuildFix27 release provider' in readme and 'FCEUmm' in readme and 'Legacy NROM' in readme,
    'final FCEUmm provider / legacy fixture status is not explicit')
req('GPL-2.0' in readme and 'MIT' in readme,
    'combined FCEUmm distribution license boundary is not documented')
req('--version 0.1.5' in make and 'FCEUMM_SOURCE_PROVENANCE.txt' in make and 'THIRD_PARTY_FCEUMM_LICENSE.txt' in make,
    'final package identity/provenance/license payload is incomplete')
print('buildfix27-project-nes-lifecycle-package-contract: OK 10-cycle cleanup/save retry/license truthfulness')
