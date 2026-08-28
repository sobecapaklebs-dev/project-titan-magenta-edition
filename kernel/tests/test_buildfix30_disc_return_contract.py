#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
engine=(r/'kernel/src/services/titan_disc_engine.c').read_text()
neu=(r/'kernel/src/services/titan_neutrino.c').read_text()
pad=(r/'modules/titan-neutrino-engine/ee/ee_core/src/padhook.c').read_text()
shell=(r/'kernel/src/ui/titan_shell_scene.c').read_text()
req=lambda c,m: (_ for _ in ()).throw(AssertionError(m)) if not c else None
req('TITAN_SESSION_ENGINE_DISC' in engine,'disc session owner missing')
req('-titan-exit=mc0:/BOOT/BOOT.ELF' in neu,'resident return path missing')
req('-titan-igr=return-only' in neu,'unknown disc not return-only')
req('TITAN_PADEMU_SYSTEM_CMD_ID' in pad,'System Button resident ingress missing')
req('IGR_COMBO_R1_L1_R2_L2' in pad and 'IGR_COMBO_START_SELECT' in pad,'DS2 IGR fallback missing')
req('TITAN_THEME_LIFECYCLE_RETURN_HOME' in shell,'Return Home theme lifecycle missing')
req('TitanShellScene_BeginGameReturn' in shell,'Titan return animation missing')
print('titan-disc-smartigr-contract: OK')
print('titan-disc-overlay-contract: OK return-only default / profile-gated visual overlay')
print('titan-disc-return-contract: OK resident return + RETURN_HOME lifecycle')
print('titan-disc-ds2-fallback-contract: OK')
