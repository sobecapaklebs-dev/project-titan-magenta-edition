#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]

def req(cond,msg):
    if not cond:
        raise SystemExit('rc1-hardware-diag-recovery-contract: FAIL '+msg)

kernel=(r/'src/core/titan_kernel.c').read_text()
guard=(r/'src/graphics/titan_vram_boot_guard.c').read_text()
guard_h=(r/'src/graphics/titan_vram_boot_guard.h').read_text()
recovery=(r/'src/graphics/titan_boot_recovery.c').read_text()
recovery_h=(r/'src/graphics/titan_boot_recovery.h').read_text()
video=(r/'src/graphics/titan_video.c').read_text()
pika=(r/'src/graphics/titan_pikavr.c').read_text()
app=(r/'src/services/titan_app_runtime.c').read_text()
vl=(r/'src/services/titan_vlombiscore.c').read_text()
diag=(r/'src/services/titan_diagnostics.c').read_text()
scene=(r/'src/ui/titan_shell_scene.c').read_text()

# Recovery must be armed only after the persisted/final video mode has been applied.
pos_load=kernel.index('TitanVideoOutput_LoadPersistentSetting()')
pos_arm=kernel.index('TitanVramBootGuard_ArmRecovery()', pos_load)
pos_theme=kernel.index('TitanTheme_LoadPersistentSetting()', pos_arm)
req(pos_load < pos_arm < pos_theme, 'VRAM recovery is not armed after final video mode and before theme load')
req('if(g_guard.recovery_armed)TitanBootRecovery_NotifyVramPressure' in guard.replace('\n',''), 'bootstrap VRAM pressure can still latch recovery before arm')
req('TitanVramBootGuard_ReevaluateAfterVideoModeChange' in guard_h and 'TitanBootRecovery_ClearTransientVramCritical' in recovery_h, 'video-mode recovery reevaluation API missing')
req('g_recovery.reason != TITAN_BOOT_RECOVERY_REASON_VRAM_CRITICAL' in recovery, 'transient clear is not restricted to VRAM_CRITICAL')
req('g_guard.fail_critical==0u' in guard.replace(' ',''), 'transient recovery clear ignores real critical allocation failures')
req('TitanPikaVr_UpdateVramUsed(TitanVideoMemory_GetReservedBytes())' in video and 'TitanVramBootGuard_ReevaluateAfterVideoModeChange()' in video, 'successful video switch does not refresh VRAM telemetry/recovery')
req('g_pikavr.telemetry.vram_free' in pika[pika.index('void TitanPikaVr_UpdateVramUsed'):pika.index('void TitanPikaVr_SetVramAccounting')], 'pikaVR vram_free is stale after allocator changes')

# Titan/Project NES target is N32/ABI2; O32 must not be accepted as the in-process app ABI.
compat=app[app.index('static int TitanTmod_TargetFlagsCompatible'):app.index('static int TitanTmod_Bounds')]
req('TITAN_TMOD_EF_MIPS_ABI2) == 0u' in compat, 'runtime does not require N32/ABI2')
req('abi != 0u' in compat and 'ABI N32' in compat, 'runtime ABI guard/message not aligned with N32 target')

# Vsync timer jitter must not permanently throttle background I/O.
frame=vl[vl.index('void TitanVlombisCore_RecordFrameBudget'):vl.index('uint32_t TitanVlombisCore_GetRecommendedIoQuantum')]
req('jitter_slack_us' in frame and '500u' in frame, 'bounded frame-budget jitter tolerance missing')
req('pressure_level = 0u' in frame and 'budget_us + jitter_slack_us' in frame, 'normal vsync frames do not clear Vlombis pressure')

# Spike counters stay exact, but flight-recorder detail must be sampled and include phase attribution.
end=diag[diag.index('void TitanDiagnostics_EndFrame'):diag.index('void TitanDiagnostics_StorageHeartbeat')]
req('++g_diag.perf.spikes' in end, 'exact spike counter was lost')
req('diag_is_sample_count(spike_count)' in end, 'frame-spike detail is not sampled')
req('0x101' in end and 'phase_audio_us' in end and 'phase_render_us' in end, 'sampled spike event lacks phase attribution')

# Debug report must distinguish allocator truth from pikaVR internal accounting.
req('VRAM allocator used/free:' in scene and 'TitanVideo_GetVramBytesReserved(), TitanVideo_GetVramBytesFree()' in scene, 'debug report does not expose current allocator VRAM')

# A manual video-mode recovery must repopulate theme assets in the same session.
start=scene[scene.index('static void TitanShellScene_StartVideoTest'):scene.index('static void TitanShellScene_ConfirmVideoTest')]
cancel=scene[scene.index('static void TitanShellScene_CancelVideoTest'):scene.index('static void TitanShellScene_StartVideoTest')]
req('TitanShellBackground_RefreshThemeAssets();' in start and 'TitanShellBackground_RefreshThemeAssets();' in cancel, 'theme assets are not refreshed after video-layout recovery')

print('rc1-hardware-diag-recovery-contract: PASS final-layout VRAM recovery + N32 app ABI + Vlombis jitter QoS + sampled phase diagnostics')
