#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[1])
installer=(root/'src/installer/titan_installer.c').read_text()
input_c=(root/'src/platform/ps2/titan_input.c').read_text()
input_h=(root/'src/platform/ps2/titan_input.h').read_text()
def req(v,m):
    if not v: raise SystemExit('FAIL: '+m)
req(('0.2.3-poc' in installer) or ('0.2.4-poc' in installer) or ('0.2.5-poc' in installer) or ('0.2.6-poc' in installer) or ('0.2.7-poc' in installer) or ('0.2.8-poc' in installer), 'version not bumped')
req('adopt_launcher_storage' in installer, 'launcher storage adoption missing')
req('probe_inherited_usb_once' in installer, 'single-pass inherited USB probe missing')
req('TitanInput_InitializePreserveIop' in installer, 'installer does not use preserve-IOP input path')
req('TitanInput_InitializePreserveIop' in input_c and 'TitanInput_InitializePreserveIop' in input_h, 'preserve-IOP input API missing')
preserve=input_c[input_c.index('TitanInput_InitializePreserveIop'):input_c.index('TitanInput_Initialize(', input_c.index('TitanInput_InitializePreserveIop')+1)]
req('TitanIopServiceManager_Initialize(' not in preserve, 'preserve path must not invoke reset-owning IOP manager')
req('s.storage_ready = adopt_launcher_storage(&s);' in installer, 'inherited USB must be attempted before reset path')
req(installer.index('s.storage_ready = adopt_launcher_storage(&s);') < installer.index('result = TitanInput_Initialize(0,0);'), 'reset path executes before inherited USB adoption')
req('fileXioExit()' in installer and 'SifLoadFileExit()' in installer, 'fallback must release inherited EE clients')
print('installer-preserve-iop-v2.0.3: OK')
