#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1] if len(sys.argv)>1 else '.')
early=(root/'kernel/src/services/titan_early_input.c').read_text()
pademu=(root/'modules/titan-ul-engine/modules/pademu/pademu.c').read_text()
checks={
 'shell-transport-only-bit':'TITAN_PADEMU_SETTINGS_TITAN_TRANSPORT_ONLY' in early,
 'shell-do-not-hook-bit':'TITAN_PADEMU_SETTINGS_SUPPRESS_SIO2_HOOK' in early,
 'loader-deferred-from-init':'g_worker_done=0;g_worker_success=0;g_worker_thread=-1;' in early,
 'worker-started-by-pump':'if(!start_worker())' in early[early.find('void TitanEarlyInput_Pump'):],
 'transport-only-detected':'TITAN_PADEMU_SETTINGS_TITAN_TRANSPORT_ONLY' in pademu,
 'transport-only-eager-init':'pad_inited = PAD_INIT(pad_enable, pad_options);' in pademu,
 'transport-init-fail-open':'if (!pad_inited)' in pademu and 'return MODULE_NO_RESIDENT_END;' in pademu,
 'game-hook-preserved':'if (!vmc)' in pademu and 'install_sio2hook()' in pademu,
}
bad=[k for k,v in checks.items() if not v]
if bad:
 print('b30-bt-bootsafe-transport: FAIL', ', '.join(bad)); raise SystemExit(1)
print('b30-bt-bootsafe-transport: PASS')
