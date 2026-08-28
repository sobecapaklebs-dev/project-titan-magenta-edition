#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]).resolve()
runtime=(root/'src/services/titan_app_runtime.c').read_text(encoding='utf-8',errors='replace')
apps_h=(root/'src/services/titan_applications.h').read_text(encoding='utf-8',errors='replace')
apps_c=(root/'src/services/titan_applications.c').read_text(encoding='utf-8',errors='replace')

def req(v,msg):
    if not v: raise SystemExit('buildfix27-lifecycle-classification-closure: FAIL: '+msg)
reset=runtime[runtime.index('static void TitanAppRuntime_ResetSessionMemory'):runtime.index('static void TitanAppRuntime_AbortLaunch')]
close=runtime[runtime.index('void TitanAppRuntime_Close(void)'):runtime.index('int TitanAppRuntime_IsActive')]
req('TitanAppNetwork_AbandonSession();' in reset,'network generation/session abandon missing')
req('TitanTextInput_Initialize();' in reset,'text input reclaim missing')
req('TitanAudio_EndExternalStream();' in reset,'audio reclaim missing')
req('TitanAppRuntime_ReleaseAllAllocations();' in reset,'tracked heap reclaim missing')
req('TitanTmod_Unload(&g_app_runtime.module);' in reset,'module unload missing')
req('free(g_app_runtime.background_pixels);' in reset and 'free(g_app_runtime.media_pixels);' in reset,'media/background reclaim missing')
req('TitanFtpService_Stop();' in close,'FTP service not forced down at app close')
req('TitanAppRuntime_ReleaseStorageExclusive();' in close,'storage exclusive lane not released')
req('TITAN_APPLICATION_EXECUTION_NATIVE_IN_PROCESS' in apps_h and 'TITAN_APPLICATION_EXECUTION_EXTERNAL_HANDOFF' in apps_h,'execution model classes missing')
req('application->classification == TITAN_APPLICATION_CLASS_SAS' in apps_c,'SAS classification branch missing')
req('return TITAN_APPLICATION_EXECUTION_EXTERNAL_HANDOFF;' in apps_c,'SAS/external handoff independence lost')
req('SELECT+CIRCLE remains a kernel-owned escape hatch' in runtime,'kernel-owned exit chord invariant missing')
print('buildfix27-lifecycle-classification-closure: OK reclaim + SAS/runtime independence')
