#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1]); overlay=(r/'kernel/src/services/titan_ingame_overlay.c').read_text(); ul=(r/'modules/titan-ul-engine/ee_core/src/padhook.c').read_text(); ne=(r/'modules/titan-neutrino-engine/ee/ee_core/src/padhook.c').read_text(); rpc=(r/'modules/titan-ul-engine/modules/pademu/pademu_telemetry_rpc.c').read_text()
assert 'input->pressed & TITAN_NBTN_SYSTEM' in overlay
assert 'TitanOverlay_Close(TITAN_OVERLAY_ACTION_RESUME)' in overlay
for name,src in [('UL',ul),('Neutrino',ne)]:
 for token in ['TitanSystemButton_Handler','TITAN_PADEMU_SYSTEM_CMD_ID','SifAddCmdHandler','TitanOverlay_TriggeredBySystem','TITAN_ENGINE_OVERLAY_RESUME']:
  assert token in src,(name,token)
assert 'sceSifSendCmd(TITAN_PADEMU_SYSTEM_CMD_ID' in rpc
assert 'if(!pressed)return' in rpc.replace(' ','')
print('titan-system-button-contract: OK')
print('titan-system-button-native-contract: OK')
print('titan-system-button-ul-contract: OK')
print('titan-system-button-neutrino-contract: OK')
