#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
shell=(root/'kernel/src/ui/titan_shell_scene.c').read_text(errors='ignore')
app=(root/'kernel/src/services/titan_app_runtime.c').read_text(errors='ignore')
inputc=(root/'kernel/src/platform/ps2/titan_input.c').read_text(errors='ignore')
bg=(root/'kernel/src/ui/titan_shell_background.c').read_text(errors='ignore')
early=(root/'kernel/src/services/titan_early_input.c').read_text(errors='ignore')
for token in ('TitanOverlay_Reset();','TitanSession_Shutdown();','TitanShellBackground_Shutdown();','TitanAudio_Shutdown();','TitanInput_Shutdown();','TitanIopServiceManager_Shutdown();'):
    assert token in shell, token
assert shell.index('TitanOverlay_Reset();') < shell.index('TitanSession_Shutdown();') < shell.index('TitanShellBackground_Shutdown();')
for token in ('TitanSession_Fail()','TitanSession_End()','TitanOverlay_Reset();','TitanAppRuntime_ResetSessionMemory();'):
    assert token in app, token
assert 'TitanUnifiedInput_Shutdown();' in inputc
assert 'TitanBootRecovery_IsMinimum()' in bg and 'TITAN_VRAM_PRESSURE_CRITICAL' in bg
for token in ('TITAN_EARLY_INPUT_STATE_FAILED','TITAN_EARLY_INPUT_DEADLINE_MS','TitanEarlyInput_Pump'):
    assert token in early, token
print('buildfix28-lifecycle-recovery-contract: OK session/overlay/input/theme/power/fail-safe')
