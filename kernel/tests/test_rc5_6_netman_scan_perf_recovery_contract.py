#!/usr/bin/env python3
from pathlib import Path
import sys

root=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[2])

def text(rel): return (root/rel).read_text(encoding='utf-8')
def req(ok,msg):
    if not ok: raise SystemExit(msg)

net=text('kernel/src/services/titan_network.c')
sched=text('kernel/src/services/titan_storage_scheduler.c')
dex=text('kernel/src/services/titan_dex.c')
scene=text('kernel/src/ui/titan_shell_scene.c')
version=text('kernel/include/titan_version.h')
syntax=text('kernel/tests/test_network_service_syntax.py')

req('TITAN_RC5_6_NETMAN_SCAN_PERF_RECOVERY_R1' in net,'rc5.6 marker missing')
req('#include <netman_rpc.h>' in net,'public netman RPC id header missing')
req('TITAN_NETWORK_NETMAN_RPC_PROBE_STEPS 150u' in net,'bounded NETMAN RPC probe missing')
req('TITAN_NETWORK_NETMAN_RPC_PROBE_DELAY_US 10000u' in net,'NETMAN RPC probe delay missing')
req('TITAN_NETWORK_BOOTSTRAP_MAX_RETRIES 3u' in net,'bounded NETMAN retry budget missing')
req('TITAN_NETWORK_BOOTSTRAP_NETMAN_RPC' in net,'NETMAN RPC bootstrap stage missing')
req('TitanNetwork_WaitForNetmanRpcServer' in net,'NETMAN RPC readiness probe missing')
probe=net[net.index('static int TitanNetwork_WaitForNetmanRpcServer'):net.index('static void TitanNetwork_InitInterfaces')]
req('SifBindRpc(&g_netman_probe_rpc,NETMAN_RPC_NUMBER,0)' in probe,'probe does not bind official NETMAN RPC number')
req('for(attempt=1u;attempt<=TITAN_NETWORK_NETMAN_RPC_PROBE_STEPS;attempt++)' in probe,'NETMAN probe is not bounded')
init=net[net.index('static void TitanNetwork_InitInterfaces'):net.index('static void TitanNetwork_BootstrapWorker')]
req(init.index('TitanNetwork_WaitForNetmanRpcServer()') < init.index('netman_init_result=NetManInit()'),'unbounded NetManInit can run before RPC readiness proof')
req('NETMAN IOP RPC server unavailable; bounded retry scheduled' in init,'named NETMAN RPC failure missing')
update=net[net.index('void TitanNetwork_Update(void)'):net.index('void TitanNetwork_SetEnabled')]
req("if(!TitanBootRecovery_GetState()->home_ready)" in update,'network bootstrap is not gated by real HOME readiness')
req('TITAN_NETWORK_HOME_STABILIZE_FRAMES' in update,'post-HOME network stabilization window missing')
req('g_network_bootstrap_retry_count<TITAN_NETWORK_BOOTSTRAP_MAX_RETRIES' in update,'NETMAN auto-retry budget not enforced')
req('0x25a' in update,'NETMAN retry telemetry missing')
req("td/'netman_rpc.h'" in syntax and 'NETMAN_RPC_NUMBER' in syntax,'host syntax stub lacks netman_rpc contract')

worker=sched[sched.index('static void TitanStorageScheduler_Worker'):sched.index('int TitanStorageScheduler_Initialize')]
req(worker.index('TitanStorageScheduler_StartOrStepScan()') < worker.index('TitanStorageScheduler_PumpAudioDeferred()'),'deferred audio can still starve requested Library scan')
fallback=sched[sched.index('static void TitanStorageScheduler_RunFallbackWork'):sched.index('void TitanStorageScheduler_UpdateMainThread')]
req(fallback.index('TitanStorageScheduler_StartOrStepScan()') < fallback.index('TitanStorageScheduler_PumpAudioDeferred()'),'fallback scheduler can still starve Library scan')
req('0x612' in sched and '0x613' in sched,'scan worker start/finish telemetry not preserved')

req('TITAN_DEX_OVERLAY_SCHEMA_PATH' in dex,'one-time DEX overlay migration marker missing')
req('dex_overlay_schema2.cfg' in dex,'overlay migration schema path missing')
req('overlay_saved = TitanDex_SaveOverlaySetting();' in dex,'legacy overlay state is not forced OFF transactionally')
req('g_dex.overlay_enabled = 0;' in dex,'overlay migration does not fail safe OFF')
req(('TITAN_RECOVERY_BUILD_STRING "RC5.6-netman-scan-perf-r3"' in version) or ('TITAN_RECOVERY_BUILD_STRING "RC5.6-netman-rpc-authority-r4"' in version) or ('TITAN_RECOVERY_BUILD_STRING "RC5.6-smap-mainif-r5"' in version) or ('TITAN_RECOVERY_BUILD_STRING "RC5.6-iop-search-zero-r6"' in version) or ('TITAN_RECOVERY_BUILD_STRING "RC5.6-exec-authority-smap-rebind-r7"' in version) or ('TITAN_RECOVERY_BUILD_STRING "RC5.6-dev9-exec-state-r8"' in version) or ('TITAN_RECOVERY_BUILD_STRING "RC5.6-network-audit-final"' in version) or ('TITAN_RECOVERY_BUILD_STRING "RC5.6-network-hwtrace-final"' in version),'explicit recovery build identity missing')
req('"Recovery build: %s\\n"' in scene,'debug report does not expose recovery build identity')
req('"Performance Overlay: %s\\n"' in scene,'debug report does not expose overlay state')
req('"Network bootstrap: %s / complete=%u\\n"' in scene,'debug report does not expose network bootstrap state')

print('rc5.6-netman-scan-perf-recovery: PASS')
