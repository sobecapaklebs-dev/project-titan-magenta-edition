#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]

def text(rel): return (root/rel).read_text(encoding='utf-8', errors='replace')
def req(ok,msg):
    if not ok: raise SystemExit('buildfix27-app-network-api: FAIL: '+msg)

sdk=text('kernel/include/titan_app_sdk.h')
runtime=text('kernel/src/services/titan_app_runtime.c')
svc=text('kernel/src/services/titan_app_network.c')
hdr=text('kernel/src/services/titan_app_network.h')
client=text('kernel/src/services/titan_network_client.c')
make=text('kernel/Makefile')
wle=text('apps/wlaunchelf/include/titan_app_sdk.h')
pnes=text('apps/project-nes/include/titan_app_sdk.h')

req(wle == sdk and pnes == sdk, 'app SDK copies diverged from canonical kernel header')
req('network_get_begin' in sdk and 'network_get_status' in sdk and 'network_get_read' in sdk and 'network_cancel' in sdk,
    'append-only app network API missing')
req(sdk.index('text_input_poll') < sdk.index('network_get_begin') < sdk.index('network_cancel'),
    'network API was not appended after the existing ABI tail')
req('TitanAppNetworkResult' in sdk and 'content_length' in sdk and 'TITAN_APP_NETWORK_PENDING' in sdk,
    'public async result/status contract incomplete')
req('src/services/titan_app_network.o' in make, 'app network service is not linked')
req('TITAN_APP_NETWORK_INTERNAL_CAPACITY (64u * 1024u)' in svc, 'bounded response cap missing')
req('malloc(maximum_bytes)' in svc and 'free(g_app_network.body)' in svc,
    'network response memory is not transient/bounded')
req('worker_active' in svc and 'generation' in svc and 'TitanAppNetwork_AbandonSession' in svc,
    'session generation/reuse containment missing')
req('TITAN_NETWORK_CLIENT_OWNER_NATIVE_APP' in svc and 'TitanNetworkClient_GetEx' in svc,
    'native apps bypass shared Titan Network Client')
req('options.should_cancel = TitanAppNetwork_ShouldCancel' in svc and 'deadline_milliseconds' in svc,
    'app network cancellation/deadline not routed to shared transport')
reset = runtime[runtime.index('static void TitanAppRuntime_ResetSessionMemory(void)'):runtime.index('static void TitanAppRuntime_AbortLaunch', runtime.index('static void TitanAppRuntime_ResetSessionMemory(void)'))]
req('TitanAppNetwork_AbandonSession();' in reset and
    reset.index('TitanAppNetwork_AbandonSession();') < reset.index('TitanAudio_EndExternalStream();'),
    'network request is not abandoned before runtime resource reclaim')
req('TitanAppApi_NetworkGetBegin' in runtime and 'TitanAppApi_NetworkCancel' in runtime,
    'runtime ABI bridge missing')
for forbidden in ('gethostbyname','socket(','connect(','send(','recv(','lwip_close'):
    req(forbidden not in svc, 'app network service owns raw PS2IP primitive: '+forbidden)
req('TitanHttpClient_GetEx' in client, 'Network Client lower bounded HTTP transport disappeared')

print('buildfix27-app-network-api: OK append-only async ABI, bounded memory, lifecycle cancellation')
