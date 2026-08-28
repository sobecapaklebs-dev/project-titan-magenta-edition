#!/usr/bin/env python3
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
service=(ROOT/'src/services/titan_asset_service.c').read_text()
http=(ROOT/'src/services/titan_http_client.c').read_text()
http_h=(ROOT/'src/services/titan_http_client.h').read_text()
image=(ROOT/'src/services/titan_asset_image.c').read_text()
diag=(ROOT/'src/services/titan_diagnostics.c').read_text()
checks={
 'isolated-48k-stack':'TITAN_ASSET_NET_STACK_SIZE (48u * 1024u)' in service and 'g_asset_net_stack_arena' in service and 'thread.stack = g_asset_net_stack_arena.stack' in service and 'net_stack[' not in service.split('typedef struct TitanAssetServiceState',1)[1].split('} TitanAssetServiceState',1)[0],
 'stack-canaries':'TITAN_ASSET_NET_STACK_GUARD_VALUE' in service and 'TitanAssetService_CheckNetworkStack' in service and 'TITAN_ASSET_DIAG_STACK_CANARY_FAIL' in service,
 'stack-fail-closed':'g_assets.net_faulted = 1' in service and 'if (g_assets.net_faulted) return 0;' in service,
 'http-scratch-off-stack':'static TitanHttpScratch g_http_scratch' in http and 'tls_buffer[BR_SSL_BUFSIZE_BIDI]' in http and 'TitanHttpTransport transport;' not in http.split('static int TitanHttp_GetOnce',1)[1],
 'http-single-flight':'g_http_scratch_busy' in http,
 'http-deadline':'deadline_milliseconds' in http_h and 'TitanHttp_TransportShouldCancel' in http and 'TITAN_HTTP_DEFAULT_DEADLINE_MS 60000u' in http,
 'https-no-downgrade':'base->scheme == TITAN_HTTP_SCHEME_HTTPS' in http and 'strncmp(location, "http://", 7u) == 0' in http,
 'http-non200-no-body':'if (status != 200)' in http and 'Error pages are not image assets' in http,
 'http-strict-length':'char *endptr;' in http and "*endptr != '\\0'" in http and 'TITAN_HTTP_FAILURE_TOO_LARGE' in http,
 'http-strict-url-port':'port_end == port_text' in http and "*port_end != '\\0'" in http and "c == '@'" in http and 'c == 0x5cu' in http,
 'storage-job-validation':'TitanAssetService_StorageJobIsValid' in service and 'TITAN_ASSET_DIAG_STORAGE_JOB_REJECT' in service,
 'storage-exact-timg-size':'TitanAssetService_ExpectedTimgSize' in service and 'file_size != (TitanStorageIoSize)expected' in service,
 'storage-write-failure-recovers':'TitanAssetService_FailStorageWrite' in service and 'TITAN_ASSET_DIAG_CACHE_WRITE_FAIL' in service,
 'storage-load-failure-recovers':'TitanAssetService_FailStorageLoad' in service and 'TITAN_ASSET_DIAG_CACHE_LOAD_FAIL' in service,
 'stage-always-released':service.count('g_assets.storage_stage_ready = 0;') >= 2 and 'g_assets.storage_stage_startup_hash = 0u;' in service,
 'immutable-cache-identity':'download_startup_hash' in service and 'download_serial' in service and 'BuildCachePathForSerial' in service and 'storage_stage_startup_hash' in service,
 'catalog-generation-drop':'catalog_generation' in service and 'selection_generation != g_assets.catalog_generation' in service and 'TITAN_ASSET_DIAG_CATALOG_RACE_DROP' in service,
 'session-owner-generation':'manual_session_generation' in service and 'download_session_generation' in service and 'manual_owner' in service and 'StorageJobBelongsToManualSessionLocked' in service,
 'cancel-preserves-active-write-buffer':'g_assets.storage_job.op != TITAN_ASSET_STORAGE_WRITE_DOWNLOAD' in service and 'download_serial[0]' in service,
 'network-storage-serialized':'g_assets.storage_job.op == TITAN_ASSET_STORAGE_NONE' in service and 'g_assets.net_busy && !g_assets.download_ready_for_storage' in service,
 'failed-session-reported':'TitanAssetService_LogSessionEnd(g_assets.session_failed_assets == 0u ? 0 : -1)' in service,
 'manual-no-backoff-on-cache-miss':'manual ? g_assets.frame_index : g_assets.frame_index + TITAN_ASSET_RETRY_FRAMES' in service,
 'manual-network-failure-advances':'if (g_assets.manual_session_active && result != -2)' in service and 'g_assets.session_failed_assets++' in service and 'icon_download_pending = 0u' in service,
 'explicit-unavailable-retry':'TITAN_ASSET_PRESENCE_UNAVAILABLE)' in service,
 'source-buffer-reused':'g_assets.source_buffer' in service and 'free(g_assets.source_buffer)' in service,
 'clear-cache-sweeps-orphans':'TitanStorageIo_ListDirectory(TITAN_ASSET_CACHE_DIR' in service and 'TitanStorageIo_RemoveAny(path)' in service,
 'png-interlace-bounded':'png_set_interlace_handling' in image and 'interlace_passes' in image and 'display_row' in image,
 'png-rowbytes-checked':'rowbytes != (png_size_t)width * 4u' in image,
 'image-pixel-cap':'TITAN_IMAGE_MAX_PIXELS' in image,
 'png-input-underflow-guard':'memory->offset > memory->size' in image,
 'decode-cooperative':'TitanAssetImage_ConvertToTimgEx' in image and 'TitanAssetImage_Cooperate' in image and 'image_options.yield_every_rows = 8u' in service,
 'decode-cancel-aware':'image_options.should_cancel = TitanAssetService_ShouldAbortFetch' in service,
 'malformed-200-provider-fallback':'source_magic_ok' in service and 'provider/source issue' in service,
 'flight-recorder-hardening-names':'ASSET_STACK_CANARY_FAIL' in diag and 'ASSET_STORAGE_JOB_REJECT' in diag and 'TLS_ENGINE_ERROR' in diag and 'CACHE_WRITE_FAIL' in diag and 'CACHE_LOAD_FAIL' in diag,
}
failed=[]
for name,ok in checks.items():
 print(f'asset-subsystem-bf11: {"PASS" if ok else "FAIL"} {name}')
 if not ok: failed.append(name)
if failed: raise SystemExit('failed: '+', '.join(failed))
print(f'asset-subsystem-bf11: PASS {len(checks)}/{len(checks)}')
