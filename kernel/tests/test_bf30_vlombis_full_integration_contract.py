#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
project=r.parent
vlh=(r/'src/services/titan_vlombiscore.h').read_text()
vlc=(r/'src/services/titan_vlombiscore.c').read_text()
sched=(r/'src/services/titan_storage_scheduler.c').read_text()
lib=(r/'src/services/titan_library_scan.c').read_text()
scene=(r/'src/ui/titan_shell_scene.c').read_text()
pkg=(r/'src/services/titan_package_installer.c').read_text()
net=(r/'src/services/titan_network.c').read_text()
diag=(r/'src/services/titan_diagnostics.c').read_text()
pika=(r/'src/graphics/titan_pikavr.c').read_text()
neu=(r/'src/services/titan_neutrino.c').read_text()
apps=(r/'src/services/titan_applications.c').read_text()
checks={
 'all-canonical-capabilities': all(x in vlh for x in (
   'TITAN_VLOMBIS_CAP_STORAGE_QOS','TITAN_VLOMBIS_CAP_BACKGROUND_QOS','TITAN_VLOMBIS_CAP_IO_COORDINATION',
   'TITAN_VLOMBIS_CAP_RENDER_STATE_CACHE','TITAN_VLOMBIS_CAP_ASYNC_PACKAGE_IO','TITAN_VLOMBIS_CAP_LIBRARY_INCREMENTAL_SCAN',
   'TITAN_VLOMBIS_CAP_AUDIO_BUFFER_QOS','TITAN_VLOMBIS_CAP_NETWORK_BACKGROUND_QOS','TITAN_VLOMBIS_CAP_FRAME_BUDGETING','TITAN_VLOMBIS_CAP_PIKAVR')),
 'measured-work-api':'TitanVlombisCore_RecordWork' in vlh and 'bytes_avoided' in vlh and 'pressure_level' in vlh,
 'adaptive-io':'TitanVlombisCore_GetRecommendedIoQuantum' in vlc and 'TITAN_STORAGE_PACKAGE_MIN_BUDGET' in sched and 'package_budget' in sched,
 'frame-budget-producer':'TitanVlombisCore_RecordFrameBudget(us, 16667u)' in diag,
 'render-state-pika-producer':'TITAN_VLOMBIS_EVENT_RENDER_STATE_CACHE_HIT' in pika and 'TITAN_VLOMBIS_EVENT_PIKAVR_OPTIMIZATION' in pika,
 'package-producer':'TITAN_VLOMBIS_EVENT_ASYNC_PACKAGE_QUANTUM' in pkg and 'TITAN_VLOMBIS_CAP_ASYNC_PACKAGE_IO' in pkg,
 'audio-qos-producer':'TITAN_VLOMBIS_CAP_AUDIO_BUFFER_QOS' in sched and 'TitanStorageScheduler_PumpAudioRealtime' in sched,
 'network-qos-producer':'TitanNetwork_BackgroundPollFrames' in net and 'TITAN_VLOMBIS_CAP_NETWORK_BACKGROUND_QOS' in net,
 'library-metadata-reuse':'TitanLibraryScan_FindPreviousIso' in lib and 'metadata_reused' in lib and 'TitanVlombisCore_RecordLibraryMetadataReuse' in lib,
 'library-view-cache':'TitanLibraryViewCache' in scene and 'pending_hits >= 32u' in scene and 'Sort only when the view key changes' in scene,
 'catalog-generations':'TitanNeutrino_GetCatalogGeneration' in neu and 'TitanApplications_GetCatalogGeneration' in apps,
 'storage-arbitration':'TITAN_VLOMBIS_EVENT_STORAGE_ARBITRATION' in sched,
 'truthful-visibility':'active_domains=%u/10' in scene and 'TitanVlombisCore_GetActiveCapabilityCount()' in scene,
 'debug-domain-counters':'Vlombis renderer/package:' in scene and 'Vlombis library:' in scene and 'Vlombis qos:' in scene,
 'optimized-720p-preserved':'g_video.hd_path_preference = TITAN_VIDEO_720P_PATH_OPTIMIZED' in (r/'src/graphics/titan_video.c').read_text(),
 'bt-welcome-preserved':'Pair wireless controller' in scene and 'd->state.pressed|d->state.buttons' in scene.replace(' ',''),
}
failed=[k for k,v in checks.items() if not v]
for k,v in checks.items(): print(f'bf30-vlombis-full-integration: {"PASS" if v else "FAIL"} {k}')
if failed: raise SystemExit('failed: '+', '.join(failed))
print(f'bf30-vlombis-full-integration: PASS {len(checks)}/{len(checks)}')
