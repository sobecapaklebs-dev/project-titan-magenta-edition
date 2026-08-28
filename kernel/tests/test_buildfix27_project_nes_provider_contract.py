#!/usr/bin/env python3
import pathlib, subprocess, sys, tempfile
root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
core=(root/'apps/project-nes/src/project_nes.c').read_text()
hdr=(root/'apps/project-nes/src/project_nes_core.h').read_text()
mk=(root/'apps/project-nes/Makefile').read_text()
checker=root/'apps/project-nes/tools/check_provider_package.py'
def req(v,m):
    if not v: raise SystemExit('buildfix27-project-nes-provider-contract: FAIL: '+m)
req('ProjectNesProviderDescriptor' in hdr and 'PROJECT_NES_PROVIDER_ABI_VERSION' in hdr,'provider descriptor ABI missing')
req('ProjectNesFceumm_QueryProvider' in core and '__attribute__((weak))' in core,'optional weak provider resolver missing')
req('pn_provider_valid' in core and 'pn_select_backend_candidate' in core,'provider validation/selection boundary missing')
req('GPL-2.0-only' in core and 'GPL-2.0-or-later' in core,'GPL SPDX gate missing')
for pin in ('b5e3566515c27dc66c9c20572171673126532e06','dc77e3ca4baad08f0023f61dba9846550735b74d'):
    req(pin in core, 'frozen provider pin missing '+pin)
req('host_provider_selection_test.c' in mk and 'check_provider_package.py --provider fceumm' in mk,'final FCEUmm provider gates not permanent in host-test')
with tempfile.TemporaryDirectory() as td:
    t=pathlib.Path(td); src=t/'src'; pkg=t/'pkg'; src.mkdir(); pkg.mkdir()
    (src/'LICENSE').write_text('Permission is hereby granted, free of charge\nTHE SOFTWARE IS PROVIDED "AS IS"\n')
    subprocess.run([sys.executable,str(checker),'--provider','legacy','--source-dir',str(src),'--package-dir',str(pkg)],check=True,stdout=subprocess.DEVNULL)
    bad=subprocess.run([sys.executable,str(checker),'--provider','fceumm','--source-dir',str(src),'--package-dir',str(pkg)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    req(bad.returncode!=0,'FCEUmm package without GPL/provenance was accepted')
    (pkg/'FCEUMM_SOURCE_PROVENANCE.txt').write_text('commit=b5e3566515c27dc66c9c20572171673126532e06\n')
    (pkg/'THIRD_PARTY_FCEUMM_LICENSE.txt').write_text('GNU GENERAL PUBLIC LICENSE\nVersion 2, June 1991\n')
    (pkg/'LICENSE.txt').write_text('Combined Project NES + FCEUmm module: GPL-2.0-only\n')
    subprocess.run([sys.executable,str(checker),'--provider','fceumm','--source-dir',str(src),'--package-dir',str(pkg)],check=True,stdout=subprocess.DEVNULL)
print('buildfix27-project-nes-provider-contract: OK frozen GPL provider selection + packaging gate')
