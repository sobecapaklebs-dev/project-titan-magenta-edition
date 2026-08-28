#!/usr/bin/env python3
from pathlib import Path
import hashlib
ROOT=Path(__file__).resolve().parents[1]
read=lambda p:(ROOT/p).read_text(errors='replace')
http=read('src/services/titan_http_client.c')
hh=read('src/services/titan_http_client.h')
asset=read('src/services/titan_asset_service.c')
network=(ROOT/'src/services/titan_network.c').read_bytes()
checks={
 'two-connection-pool':'TITAN_HTTP_CONNECTION_SLOTS 2u' in http and 'TitanHttpConnectionSlot connections[TITAN_HTTP_CONNECTION_SLOTS]' in http,
 'keep-alive-request':'Connection: keep-alive' in http,
 'bounded-reuse-retry':'TITAN_HTTP_REUSED_RETRY_LIMIT 1u' in http and 'reused_retries < TITAN_HTTP_REUSED_RETRY_LIMIT' in http,
 'drain-non200-for-reuse':'Error pages are not image assets' in http and 'TitanHttp_ReadBody(transport, header, destination, capacity, &discard_size, options)' in http,
 'multi-host-dns-cache':'TITAN_HTTP_DNS_CACHE_SLOTS 4u' in http and 'TitanHttp_DnsCacheLookup' in http and 'TitanHttp_DnsCacheStore' in http,
 'stale-dns-self-heal':'TitanHttp_DnsCacheConnectResult' in http and 'entry->connect_failures >= 2u' in http,
 'session-reset-api':'void TitanHttpClient_ResetSession(void)' in hh and 'TitanHttp_CloseConnectionSlot' in http,
 'socket-error-trace':'TITAN_HTTP_TRACE_SOCKET_ERROR' in hh and 'TITAN_ASSET_DIAG_SOCKET_ERROR' in asset,
 'connect-error-trace':'TITAN_HTTP_TRACE_CONNECT_ERROR' in hh and 'TITAN_ASSET_DIAG_CONNECT_ERROR' in asset,
 'reuse-trace':'TITAN_HTTP_TRACE_CONNECTION_REUSE' in hh and 'TITAN_ASSET_DIAG_CONNECTION_REUSE' in asset,
 'close-trace':'TITAN_HTTP_TRACE_CONNECTION_CLOSE' in hh and 'TITAN_ASSET_DIAG_CONNECTION_CLOSE' in asset,
 'cancel-is-drain-barrier':'Cancellation is a drain barrier' in asset and 'TitanAssetService_ManualPipelineDrainedLocked()' in asset,
 'worker-does-not-clear-cancel':'if (g_assets.manual_cancel_requested) g_assets.manual_cancel_requested = 0;' not in asset[asset.index('static void TitanAssetService_NetworkWorker'):],
 'session-reset-on-manual-start':'New manual session, drained boundary' in asset and 'TitanHttpClient_ResetSession();' in asset,
 'network-core-unchanged':hashlib.sha256(network).hexdigest() == 'b9d3c3e54f65bc6e7fa1adef976076def92182a8ad99f3730d6a5571286716a8',
}
failed=[]
for n,v in checks.items():
 print(f'asset-bf16: {"PASS" if v else "FAIL"} {n}')
 if not v: failed.append(n)
if failed: raise SystemExit('failed: '+', '.join(failed))
print(f'asset-bf16: PASS {len(checks)}/{len(checks)}')
