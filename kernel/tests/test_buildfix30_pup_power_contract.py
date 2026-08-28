#!/usr/bin/env python3
from pathlib import Path
root=Path(__file__).resolve().parents[1]
update=(root/'src/services/titan_system_update.c').read_text()
scene=(root/'src/ui/titan_shell_scene.c').read_text()
assert 'TitanStorageIo_AtomicReplace(stage, target, backup)' in update
assert 'if (published != g_update.selected.file_count)' in update
assert 'TitanStorageIo_Rename(backup, target)' in update
assert 'TitanSystemUpdate_VerifyExtractedFile(g_update.stage_path, entry)' in update
assert 'status->result == TITAN_SYSTEM_UPDATE_OK' in scene
success=scene.index('status->result == TITAN_SYSTEM_UPDATE_OK')
reboot=scene.index('TitanShellScene_RestartConsoleNow(', success)
release=scene.index('TitanStorageScheduler_SetPackageExclusiveMode(0);', success)
assert reboot < release, 'successful PUP must reboot before package lane is released'
shutdown=scene[scene.index('static void TitanShellScene_ShutdownRuntimeLayers'):scene.index('static int TitanShellScene_PrepareConsolePowerTransition')]
for token in ('TitanOverlay_Reset();','TitanDiscEngine_Shutdown();','TitanDiscService_Shutdown();','TitanStoreService_Shutdown();','TitanDownloadService_Shutdown();','TitanSession_Shutdown();','TitanDateTime_Shutdown();','TitanShellBackground_Shutdown();'):
    assert token in shutdown, token
quiesce=scene[scene.index('static int TitanShellScene_QuiesceStorageForPower'):scene.index('static int TitanShellScene_InitializePoweroffServer')]
assert 'TitanAppRuntime_QuiesceForSystemShutdown();' in quiesce
assert 'TitanStorageScheduler_SetPackageExclusiveMode(1);' in quiesce
assert 'TitanStorageScheduler_IsPackageWorkerParked()' in quiesce
print('titan-pup-transaction-contract: OK')
print('titan-pup-auto-reboot-after-commit-contract: OK')
print('titan-shutdown-reboot-quiesce-contract: OK')
print('titan-disc-power-action-contract: OK')
