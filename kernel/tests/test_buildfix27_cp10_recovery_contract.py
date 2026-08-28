#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
def t(p): return (root/p).read_text(encoding='utf-8',errors='replace')
def req(v,m):
    if not v: raise SystemExit('buildfix27-cp10-recovery: FAIL: '+m)
sdk=t('kernel/include/titan_app_sdk.h'); rt=t('kernel/src/services/titan_app_runtime.c'); rh=t('kernel/src/services/titan_app_runtime.h')
wle=t('apps/wlaunchelf/src/wlaunchelf_titan.c'); core=t('apps/project-nes/src/project_nes_core.h'); pn=t('apps/project-nes/src/project_nes.c')
req('TitanAppLifecycleState' in rh and 'TitanAppRuntime_QuiesceForSystemShutdown' in rh,'CP02 lifecycle context missing')
req('storage_list_volumes' in sdk and 'storage_commit_replace' in sdk and 'text_input_poll' in sdk,'CP04/05 append-only API tail missing')
req('TitanAppApi_StorageListVolumes' in rt and 'TitanStorageIo_AtomicReplace' in rt,'runtime VFS/atomic bridge missing')
req('fileXio' not in wle and 'gsKit' not in wle and 'ExecPS2' not in wle,'wLaunchELF bypasses Titan ownership')
req('storage_list_volumes' in wle and '.titantmp' in wle and 'storage_commit_replace' in wle,'wLaunchELF CP04/05 transactional path missing')
req(wle.index('storage_commit_replace') < wle.index('storage_remove(g_wle.copy_source)'), 'move may delete source before committed destination')
req('ProjectNesCoreBackend' in core and 'TitanAppInput' not in core,'Project NES core/frontend boundary contaminated by Titan input')
req('ProjectNesCoreVideoTarget' in core and 'render_audio_s16_stereo' in core and 'frame_rate_millihz' in core,'CP09/10 provider media/timing boundary missing')
req('media_present_rgb565' in pn and 'media_audio_free_frames' in pn and 'pn_core_rate_millihz' in pn,'Titan frontend media ownership missing')
req('if (g_pn.api->media_audio_free_frames() < frames) return 0;' in pn,'audio backpressure guard missing')
print('buildfix27-cp10-recovery-contract: OK')
