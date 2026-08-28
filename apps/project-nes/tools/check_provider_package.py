#!/usr/bin/env python3
import argparse, pathlib, sys
MODERN='b5e3566515c27dc66c9c20572171673126532e06'
PS2='dc77e3ca4baad08f0023f61dba9846550735b74d'

def fail(msg):
    print('project-nes-provider-package: FAIL:', msg, file=sys.stderr); raise SystemExit(2)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--provider',choices=('legacy','fceumm'),required=True)
    ap.add_argument('--source-dir',required=True)
    ap.add_argument('--package-dir',required=True)
    ns=ap.parse_args(); src=pathlib.Path(ns.source_dir); pkg=pathlib.Path(ns.package_dir)
    if ns.provider=='legacy':
        lic=(src/'LICENSE').read_text(errors='ignore') if (src/'LICENSE').exists() else ''
        if 'Permission is hereby granted, free of charge' not in lic or 'THE SOFTWARE IS PROVIDED "AS IS"' not in lic: fail('legacy source license is not recognizable MIT text')
        for marker in ('FCEUMM_SOURCE_PROVENANCE.txt','THIRD_PARTY_FCEUMM_LICENSE.txt'):
            if (pkg/marker).exists(): fail('legacy package unexpectedly carries FCEUmm marker '+marker)
        print('project-nes-provider-package: OK legacy MIT-only package gate')
        return
    prov=pkg/'FCEUMM_SOURCE_PROVENANCE.txt'; lic=pkg/'THIRD_PARTY_FCEUMM_LICENSE.txt'; combined=pkg/'LICENSE.txt'
    if not prov.exists() or not lic.exists() or not combined.exists(): fail('FCEUmm package missing provenance/license files')
    text=prov.read_text(errors='ignore')
    if MODERN not in text and PS2 not in text: fail('FCEUmm provenance does not contain a frozen commit')
    ltext=lic.read_text(errors='ignore')
    if 'GNU GENERAL PUBLIC LICENSE' not in ltext or 'Version 2' not in ltext: fail('FCEUmm GPL-2.0 license text missing')
    ctext=combined.read_text(errors='ignore')
    if 'GPL-2.0' not in ctext: fail('combined module is still presented as MIT-only')
    print('project-nes-provider-package: OK FCEUmm GPL/provenance package gate')
if __name__=='__main__': main()
