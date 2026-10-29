#!/usr/bin/env python3
from pathlib import Path
import hashlib
ROOT=Path(__file__).resolve().parents[1]
service=(ROOT/'src/services/titan_asset_service.c').read_text(errors='replace')
http=(ROOT/'src/services/titan_http_client.c').read_text(errors='replace')
h=(ROOT/'src/services/titan_http_client.h').read_text(errors='replace')
image=(ROOT/'src/services/titan_asset_image.c').read_text(errors='replace')
network=(ROOT/'src/services/titan_network.c').read_bytes()
providers=service[service.index('static unsigned int TitanAssetService_BuildProviders'):service.index('static int TitanAssetService_SelectDownload')]
checks={
 'no-stage-blind-network-watchdog':'if (g_assets.net_busy) return;' in service and 'TITAN_ASSET_PIPELINE_STALL_FRAMES' not in service,
 'storage-only-stall-recovery':'TITAN_ASSET_STORAGE_STALL_FRAMES 900u' in service and 'storage_abort_requested' in service,
 'storage-owner-cleans-up':'Main thread must not memset/remove a job' in service and 'if (g_assets.storage_abort_requested)' in service,
 '60s-http-deadline':'TITAN_HTTP_DEFAULT_DEADLINE_MS 60000u' in http and 'options.deadline_milliseconds = 60000u' in service,
 'structured-http-result':'TitanHttpFailureReason' in h and 'TitanHttpTransferStage' in h and 'TitanHttpResultInfo' in h and 'TitanHttpClient_GetEx' in h,
 'cancel-vs-timeout':'TITAN_HTTP_FAILURE_CANCELLED' in http and 'TITAN_HTTP_FAILURE_TIMEOUT' in http,
 'cancel-does-not-fake-tls':'failure_reason != TITAN_HTTP_FAILURE_CANCELLED' in http and 'failure_reason != TITAN_HTTP_FAILURE_TIMEOUT' in http,
 'body-completeness':'http_info.content_length != 0u && http_info.bytes_received != http_info.content_length' in service,
 'source-magic-validation':'source_magic_ok' in service and 'memcmp(source, "\\x89PNG\\r\\n\\x1a\\n", 8u)' in service,
 'bad-provider-retryable':'saw_transient_failure = 1' in service and 'keep the item retryable' in service,
 'adam7-bounded-target-rows':'png_set_interlace_handling' in image and 'display_rows' in image and 'target.height * (size_t)rowbytes' in image,
 'manual-inflight-kind':'g_assets.net_busy && g_assets.current_kind == TITAN_ASSET_KIND_ICON' in service and 'g_assets.net_busy && g_assets.current_kind == TITAN_ASSET_KIND_BACKGROUND' in service,
 'session-end-drained':'TitanAssetService_ManualPipelineDrainedLocked' in service and '!TitanAssetService_ManualBackgroundWorkRemainingLocked() &&' in service,
 'semantic-icon-no-cover-in-download-providers':'_COV.png' not in providers and '_COV2.png' not in providers and 'ICON0.PNG' in providers and 'psbbn-art-database' in providers,
 'local-opl-cover-support':'"_COV.png"' in service and '"_COV2.png"' in service,
 'semantic-background-no-cover':'_BG_00.png' in service and '_SCR_00.png' in service and 'PIC2.PNG' in service,
 'cdn-raw-mirrors':'cdn.jsdelivr.net/gh/CosmicScale/psbbn-art-database' in service and 'raw.githubusercontent.com/CosmicScale/psbbn-art-database' in service,
 'round-robin-four-attempts':'TITAN_ASSET_MANUAL_MAX_ATTEMPTS 4u' in service and '*cursor = ordinal + 1u' in service,
 'network-core-unchanged':hashlib.sha256(network).hexdigest() == 'b9d3c3e54f65bc6e7fa1adef976076def92182a8ad99f3730d6a5571286716a8',
}
failed=[]
for n,v in checks.items():
 print(f'asset-bf15: {"PASS" if v else "FAIL"} {n}')
 if not v: failed.append(n)
if failed: raise SystemExit('failed: '+', '.join(failed))
print(f'asset-bf15: PASS {len(checks)}/{len(checks)}')
