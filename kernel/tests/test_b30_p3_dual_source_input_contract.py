#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1] if len(sys.argv)>1 else '.')
tele=(r/'modules/titan-ul-engine/include/titan_pademu_telemetry.h').read_text()
bt=(r/'modules/titan-ul-engine/modules/pademu/ds34bt.c').read_text()
usb=(r/'modules/titan-ul-engine/modules/pademu/ds34usb.c').read_text()
bridge=(r/'kernel/src/services/titan_pademu_input_bridge.c').read_text()
inputc=(r/'kernel/src/platform/ps2/titan_input.c').read_text()
mirror=inputc[inputc.index('static void TitanInput_MirrorActivePad(void)'):inputc.index('static int TitanInput_OpenPhysicalPort')]
checks={
 'telemetry-v2':'TITAN_PADEMU_TELEMETRY_VERSION 2u' in tele,
 'live-buttons':'uint16_t buttons' in tele and 'input_valid' in tele,
 'bt-live-input':'t->buttons=(u16)(~p->ds2.nButtonState)' in bt and 't->input_valid=1u' in bt,
 'usb-live-input':'t->buttons=(u16)(~p->ds2.nButtonState)' in usb and 't->input_valid=1u' in usb,
 'async-every-frame':'(g_frame % 15u)' not in bridge and 'SIF_RPC_M_NOWAIT' in bridge,
 'no-physical-consume':'return p->device_type!=TITAN_PADEMU_DEVICE_NONE' not in bridge,
 'bridge-uses-telemetry':'s.buttons=(uint32_t)p->buttons' in bridge and 'p->input_valid' in bridge,
 'ds2-unconditional':'TitanUnifiedInput_SubmitLegacyPad(port, g_pads[port].connected' in inputc,
 'p1-shell-ownership':('TitanUnifiedInput_GetPlayer(0u)' in mirror and
                       'chosen->state.buttons' in mirror and
                       'chosen->state.pressed' in mirror and
                       'buttons |= d->state.buttons' not in mirror and
                       'TitanUnifiedInput_GetDeviceCapacity()' in mirror),
}
bad=[k for k,v in checks.items() if not v]
if bad:
 print('b30-p3-dual-source-input: FAIL '+', '.join(bad)); raise SystemExit(1)
print('b30-p3-dual-source-input: PASS DS2 + transport-only modern input coexist; P10.6 P1 shell ownership')
