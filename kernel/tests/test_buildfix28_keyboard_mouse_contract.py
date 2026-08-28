#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[2])
def req(x,m):
 if not x: raise SystemExit('FAIL: '+m)
mk=(r/'kernel/Makefile').read_text(); h=(r/'kernel/src/services/titan_hid_input.c').read_text(); u=(r/'kernel/src/services/titan_unified_input.c').read_text(); k=(r/'kernel/src/core/titan_kernel.c').read_text(); inp=(r/'kernel/src/platform/ps2/titan_input.c').read_text()
for t in ['ps2kbd.irx','ps2mouse.irx','titan_hid_input.o']: req(t in mk,t+' missing from target build')
req('usbkbd.irx' not in mk and 'usbmouse.irx' not in mk,'stale pre-PS2SDK HID IRX names remain in target build')
for t in ['PS2KBD_READMODE_RAW','PS2KBD_NONBLOCKING','PS2KBD_RAWKEY_DOWN','mouse_bind_bounded','PS2MOUSE_BIND_RPC_ID','PS2MOUSE_READ']: req(t in h,t+' missing')
req('for(i=0;i<16u;i++)' in h,'mouse RPC bind is not bounded')
req('while(n++<8u' in h,'keyboard per-frame drain is not bounded')
req('TitanUnifiedInput_SubmitKeyboardEvent' in u and 'TITAN_UNIFIED_INPUT_KEY_QUEUE' in (r/'kernel/src/services/titan_unified_input.h').read_text(),'keyboard event queue missing')
req('TITAN_INPUT_DEVICE_MOUSE' in h and 'TITAN_INPUT_CAP_POINTER' in h,'mouse not registered in Unified Input')
req(k.index('TitanHidInput_Initialize();') < k.index('for (;;)'),'HID init not before startup runtime loop')
req('TitanHidInput_Update();' in inp,'HID backend not polled through Titan Input')
print('buildfix28-keyboard-mouse-contract: OK')
