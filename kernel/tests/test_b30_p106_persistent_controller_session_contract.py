#!/usr/bin/env python3
from pathlib import Path
import re, sys
root=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
def read(p): return (root/p).read_text()
scene=read('kernel/src/ui/titan_shell_scene.c')
assign=read('kernel/src/services/titan_controller_assignment.c')
assign_h=read('kernel/src/services/titan_controller_assignment.h')
wire=read('kernel/src/services/titan_wireless_accessories.c')
wire_h=read('kernel/src/services/titan_wireless_accessories.h')
kernel=read('kernel/src/core/titan_kernel.c')
input_c=read('kernel/src/platform/ps2/titan_input.c')
sched=read('kernel/src/services/titan_storage_scheduler.c')
make=read('kernel/Makefile')
unified_h=read('kernel/src/services/titan_unified_input.h')
neut=read('kernel/src/services/titan_neutrino.c')
opl=read('modules/titan-ul-engine/src/opl.c')
opl_h=read('modules/titan-ul-engine/include/opl.h')
support=read('modules/titan-ul-engine/src/supportbase.c')
early=read('modules/titan-ul-engine/include/titan_early_input_wire.h')
early_c=read('kernel/src/services/titan_early_input.c')
bt=read('modules/titan-ul-engine/modules/pademu/ds34bt.c')

def need(v,m):
    if not v: raise AssertionError(m)

need('src/services/titan_controller_assignment.o' in make,'assignment service not linked')
need('TITAN_UNIFIED_INPUT_MAX_PLAYERS 4u' in unified_h,'P10.6 must remain four logical players')
for token in ('controller_assignment.bin','TitanStorageIo_AtomicReplace','TitanStorageIo_WriteExact','checksum'):
    need(token in assign, 'persistent assignment missing '+token)
setter=assign[assign.index('int TitanControllerAssignment_AssignDevice'):assign.index('int TitanControllerAssignment_SetAutomatic')]
need('TitanStorageIo_' not in setter,'Settings setter performs filesystem I/O')
worker=assign[assign.index('int TitanControllerAssignment_StorageWorkerPump'):]
need('TitanStorageIo_WriteExact' in worker and 'TitanStorageIo_AtomicReplace' in worker,'storage-worker persistence not transactional')
need(all(x in assign for x in ('stable_key','binding->type == device->type','binding->transport == device->transport')),'stable identity tuple incomplete')
need(kernel.find('TitanControllerAssignment_Initialize();') > kernel.find('TitanProfileStorage_Initialize('),'assignment loads before UI only after storage rebind')
main=kernel[kernel.index('for (;;)'):]
need(main.find('TitanInput_Update();') < main.find('TitanControllerAssignment_Update();') < main.find('TitanControllerUx_Update('),'assignment not restored between input and UI')
need('TitanWirelessAccessories_BeginStartupReconnect();' in scene,'Health & Safety does not start reconnect')
need('TitanWirelessAccessories_EndStartupReconnect();' in scene,'startup reconnect not ended at Home')
start=wire[wire.index('void TitanWirelessAccessories_BeginStartupReconnect'):wire.index('void TitanWirelessAccessories_EndStartupReconnect')]
need('TitanBluetoothRuntime_Request()' in start,'startup path does not request async BT runtime')
update=wire[wire.index('void TitanWirelessAccessories_Update'):wire.index('int TitanWirelessAccessories_BeginScan')]
for token in ('TITAN_PADEMU_WIRELESS_PAIRED','TITAN_PADEMU_WIRELESS_CONTROLLER','TITAN_PADEMU_WIRELESS_CONNECT'):
    need(token in update,'startup reconnect missing '+token)
need('TITAN_PADEMU_WIRELESS_BEGIN_SCAN' not in update,'startup reconnect performs discovery')
wm=scene.split('static void TitanShellScene_DrawWirelessMenu',1)[1].split('static void TitanShellScene_DrawWirelessDetail',1)[0]
need(wm.count('TitanShellScene_DrawSettingsListRow(')==2,'Wireless root must remain exactly two rows')
for token in ('"Atribuição de controles"','"Multitap"','TITAN_SETTINGS_LEAF_CONTROLLER_ASSIGNMENT','TITAN_SETTINGS_LEAF_MULTITAP'):
    need(token in scene,'Accessories UX missing '+token)
need('TitanControllerAssignment_GetWelcomeCandidate()' in scene,'Welcome does not prefer persisted P1')
for token in ('return "X"','return "HOME"','return "PS"'):
    need(token in scene,'Welcome adaptive glyph regressed: '+token)
mirror=input_c[input_c.index('static void TitanInput_MirrorActivePad'):input_c.index('static int TitanInput_OpenPhysicalPort')]
need('TitanUnifiedInput_GetPlayer(0u)' in mirror,'shell does not prioritize P1')
need('buttons |= d->state.buttons' not in mirror,'shell still merges all controllers')
need('chosen->state.buttons' in mirror and 'chosen->state.pressed' in mirror,'shell is not single-owner')
for token in ('-titan-pademu=','-titan-mtap=','-titan-mtap-port='):
    need(token in neut and token in opl,'UL handoff/parser missing '+token)
need('#define TITAN_NEUTRINO_LAUNCH_ARGUMENT_CAPACITY 16' in neut,'UL launch argv capacity not raised')

# P10.6.1 — Neutrino host tests intentionally link only the catalog/runtime subset.
# Assignment policy must therefore be referenced only by the real EE launch path.
p1061_start=neut.index('#if defined(__EE__) || defined(_EE)\n        /* P10.6.1:')
p1061_end=neut.index('#endif',p1061_start)
p1061=neut[p1061_start:p1061_end]
for token in ('TitanControllerAssignment_GetGameplayPademuMode()',
              'TitanControllerAssignment_GetMultitapEnabled()',
              'TitanControllerAssignment_GetMultitapPort()'):
    need(token in p1061,'Neutrino EE policy guard missing '+token)
    need(token not in (neut[:p1061_start]+neut[p1061_end+len('#endif'):]),
         'Neutrino assignment API leaks into partial host link: '+token)
need('gTitanHeadlessPadEmuMode' in opl_h and 'gTitanHeadlessMtapEnabled' in opl_h,'headless policy ABI missing')
need('(1 << 24)' in support and '(1 << 25)' in support,'established MTAP bits not used')
need('(padmask << 8)' in support and '(padmask << 16)' in support,'PADEMU pad/vibration mask not materialized')
need('0x00800000u' in early and 'TITAN_PADEMU_SETTINGS_TITAN_TRANSPORT_ONLY' in early,'Titan transport-only bit23 regressed')
need('#include \"titan_early_input_wire.h\"' in early_c and 'TITAN_PADEMU_SETTINGS_TITAN_TRANSPORT_ONLY' in early_c,'kernel Early Input is not consuming canonical Titan UL wire ABI')
need('0x01000000u' not in early or 'MTAP' in early,'bit24 may not be repurposed as Titan transport flag')
need('TitanControllerAssignment_HasStorageWork()' in sched and 'TitanControllerAssignment_StorageWorkerPump()' in sched,'assignment persistence not scheduled')
for marker in ('P10.5.7: HCI ACL -> complete L2CAP PDU reassembly before HID parsing.',
               'P10.5.6: Sony basic Bluetooth HID + bounded post-connect RX tracing.',
               'P10.5.6 Sony basic BT report 0x01: 10-byte gamepad payload.',
               'P10.5.6: successful A1 HID traffic is high-rate; sample it.'):
    need(marker in bt,'P10.5.6 marker regressed')
need('iomanX_open(' not in bt and 'iomanX_write(' not in bt,'P10.5 storage ownership regressed')
need('PAD_INIT(' not in read('modules/titan-ul-engine/modules/pademu/pademu_telemetry_rpc.c'),'PAD_INIT returned to SIF RPC path')
print('b30-p106-persistent-controller-session: PASS Health auto-reconnect + persistent P1-P4 + Welcome glyph + P1 shell ownership + MTAP handoff')
