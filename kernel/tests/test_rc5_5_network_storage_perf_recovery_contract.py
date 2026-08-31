#!/usr/bin/env python3
from pathlib import Path
import sys

root=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[2])
net=(root/'kernel/src/services/titan_network.c').read_text(encoding='utf-8')
htrace=(root/'kernel/src/services/titan_health_trace.h').read_text(encoding='utf-8')
kernel=(root/'kernel/src/core/titan_kernel.c').read_text(encoding='utf-8')
sched=(root/'kernel/src/services/titan_storage_scheduler.c').read_text(encoding='utf-8')
diag=(root/'kernel/src/services/titan_diagnostics.c').read_text(encoding='utf-8')
dex=(root/'kernel/src/services/titan_dex.c').read_text(encoding='utf-8')
ui=(root/'kernel/src/ui/titan_shell_scene.c').read_text(encoding='utf-8')

init_start=net.index('static void TitanNetwork_InitInterfaces(void)')
init_end=net.index('static void TitanNetwork_BootstrapWorker', init_start)
init=net[init_start:init_end]

setpref_start=net.index('void TitanNetwork_SetPreference(')
setpref_end=net.index('void TitanNetwork_RequestPreference(', setpref_start)
setpref=net[setpref_start:setpref_end]

worker_start=sched.index('static void TitanStorageScheduler_Worker')
worker_end=sched.index('#endif',worker_start)
worker=sched[worker_start:worker_end]

settings_start=ui.index('static void TitanShellScene_DrawSettingsSubpage(')
settings_end=ui.index('static void TitanShellScene_DrawSettingsPage(', settings_start)
settings=ui[settings_start:settings_end]
system_start=settings.index('case TITAN_SETTINGS_SYSTEM:')
system_end=settings.index('case TITAN_SETTINGS_DEBUG:', system_start)
system=settings[system_start:system_end]
preamble=settings[:settings.index('switch ((TitanSettingsCategory)')]

checks={
    'rc5.5 marker':'TITAN_RC5_5_NETWORK_STORAGE_PERF_RECOVERY_R1' in net,
    'pinned ps2sdk getconfig exact success':'ps2ip_getconfig("sm0",&ipinfo)==1' in net and 'if(get_ok!=1) return 0;' in net,
    'no zero-success getconfig contract':'ps2ip_getconfig("sm0",&ipinfo)>=0' not in net and 'if(get_ok<0) return 0;' not in net,
    'cold order dev9 before netman':init.index('TitanNetwork_EnsureDev9Driver()') < init.index('SifSearchModuleByName("Network_Manager")'),
    'cold order netman before smap':init.index('SifSearchModuleByName("Network_Manager")') < init.index('TitanNetwork_EnsureEthernetDriver()'),
    'explicit netman ee init':'netman_init_result=NetManInit();' in init and 'g_netman_ee_initialized=1;' in init,
    'SMAP selected before ps2ip':'NetManSetMainIF("SMA")' in init and init.index('NetManSetMainIF("SMA")') < init.index('ps2ipInit(&ip,&nm,&gw)'),
    'no premature fatal AUTO link mode':'NetManSetLinkMode(NETMAN_NETIF_ETH_LINK_MODE_AUTO)' not in init,
    'netman ee trace boundaries':'TITAN_HT_NETWORK_NETMAN_INIT_BEFORE' in htrace and 'TITAN_HT_NETWORK_NETMAN_INIT_AFTER' in htrace,
    'link mode trace boundaries':'TITAN_HT_NETWORK_LINK_MODE_BEFORE' in htrace and 'TITAN_HT_NETWORK_LINK_MODE_AFTER' in htrace,
    'bootstrap stage visibility':all(x in net for x in ('NETMAN EE RPC','SMAP link mode','ps2ip / lwIP')) and 'g_network_bootstrap_error_stage' in net,
    'preference API remains async':'TitanNetwork_RequestPreference(p);' in setpref and 'TitanNetwork_EnsureEthernetDriver' not in setpref and 'TitanNetwork_SelectMainInterface' not in setpref,
    'boot scan request return checked':'scan_request_ok = TitanStorageScheduler_RequestLibraryScan(0);' in kernel and 'if (scan_request_ok)' in kernel,
    'failed scan request retries':'background_scan_started = 1;' in kernel and '0x611' in kernel,
    'startup periodic health dump removed':'context.frame_count % 5u' not in kernel and 'TitanHealthTrace_RequestFlush();' not in kernel[kernel.index('TitanDiagnostics_EndFrame'):kernel.index('Background storage gets only')],
    'scan outranks health persistence':worker.index('TitanStorageScheduler_StartOrStepScan()') < worker.index('TitanHealthTrace_HasStorageWork()'),
    'scan start finish telemetry':'TITAN_DIAG_LIBRARY,0x612' in sched and 'TITAN_DIAG_LIBRARY,0x613' in sched,
    'diagnostic flush relaxed':'#define TITAN_DIAG_FLUSH_FRAMES 600u' in diag,
    'dex overlay default off':'g_dex.overlay_enabled = 0;' in dex and (('TITAN_DEX_OVERLAY_BACKUP_PATH,\n        0,' in dex) or ('TITAN_DEX_OVERLAY_SCHEMA_PATH' in dex)),
    'settings preamble no technology sampling':'TitanVideo_GetPikaVrTelemetry' not in preamble and 'TitanVlombisCore_GetTelemetry' not in preamble,
    'system owns technology sampling':'TitanVideo_GetPikaVrTelemetry(&pikavr_telemetry);' in system and 'TitanVlombisCore_GetTelemetry(&vlombis_telemetry);' in system,
    'network services exposes live truth':'network->netman_rpc_ready' in ui and 'network->main_interface_ready' in ui and 'TitanNetwork_GetConnectionTestStatus()' not in ui[ui.index('if (g_scene.network_page_mode == TITAN_NETWORK_PAGE_SERVICES)'):ui.index('static void TitanShellScene_OpenSettingsCategoryAnimated')],
    'test page software status visible':'software_detail' in ui and 'Running connection test...' in ui,
}
failed=[k for k,v in checks.items() if not v]
for k,v in checks.items():
    print(f'{k}: {"PASS" if v else "FAIL"}')
if failed:
    raise SystemExit('RC5.5 recovery contract failed: '+', '.join(failed))
print('rc5.5-network-storage-perf-recovery: PASS')
