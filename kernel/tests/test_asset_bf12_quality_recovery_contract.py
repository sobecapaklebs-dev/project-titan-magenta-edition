#!/usr/bin/env python3
from pathlib import Path
import hashlib
ROOT=Path(__file__).resolve().parents[1]
read=lambda p:(ROOT/p).read_text(errors='replace')
service=read('src/services/titan_asset_service.c')
header=read('src/services/titan_asset_service.h')
image=read('src/services/titan_asset_image.c')
image_h=read('src/services/titan_asset_image.h')
ui=read('src/ui/titan_shell_scene.c')
notif=read('src/ui/titan_notifications.c')
notif_h=read('src/ui/titan_notifications.h')
kernel=read('src/core/titan_kernel.c')
network=(ROOT/'src/services/titan_network.c').read_bytes()
checks={
 'icon-cache-128': 'TITAN_ASSET_ICON_SOURCE_WIDTH 128u' in header and 'TITAN_ASSET_ICON_RUNTIME_SIZE 128u' in header,
 'fast-thumb-32': 'TITAN_ASSET_ICON_THUMBNAIL_SIZE 32u' in header and 'TitanAssetService_DownsampleIconPixel' in service,
 'hq-home-tiles': 'TitanAssetService_DrawIconHighQuality' in header and 'TitanVideo_DrawAdaptiveImagePsm16' in service and 'int art_drawn = TitanAssetService_DrawIconHighQuality' in ui,
 'full-bleed-game-art': 'float art_x = x + 2.0f;' in ui and 'float art_h = content_height - 4.0f;' in ui,
 'title-strip-z-order': ui.index('TitanAssetService_DrawIconHighQuality', ui.index('static void TitanShellScene_DrawTile')) < ui.index('TitanShellScene_DrawDexMetadataStrip', ui.index('static void TitanShellScene_DrawTile')),
 'remote-quality-gate': 'minimum_source_width' in image_h and 'minimum_source_height' in image_h and 'width < options->minimum_source_width' in image,
 'icon-quality-min-128': 'minimum_source_width = kind == TITAN_ASSET_KIND_ICON ? 128u : 256u' in service,
 'catalog-rebind-not-cancel': 'TitanAssetService_PrepareManualCatalogLocked(count, 1);' in service and 'remember identities that already completed' in service,
 'session-completion-identity': 'manual_completed_icon_hash' in service and 'TitanAssetService_RecordManualCompletionLocked' in service,
 'queued-start-recovery': 'manual_start_queued' in service and 'TITAN_ASSET_MANUAL_RECOVERING' in header and 'Aguardando finalizacao segura do I/O anterior' in service,
 'queued-cache-clear': 'manual_start_clear_cache' in service and 'TitanAssetService_BeginClearCacheLocked' in service,
 'worker-restart': 'g_assets.net_faulted = 0;' in service and 'manual_recovery_count++' in service,
 'bounded-manual-retry': 'TITAN_ASSET_MANUAL_MAX_ATTEMPTS 4u' in service and 'TITAN_ASSET_MANUAL_RETRY_DELAY_FRAMES 120u' in service,
 'storage-retry-manual': 'attempts < TITAN_ASSET_MANUAL_MAX_ATTEMPTS' in service,
 'icons-first-auto': service.index('state->icon_download_pending', service.index('else if (g_assets.automatic_download_enabled)')) < service.index('state->background_download_pending', service.index('else if (g_assets.automatic_download_enabled)')),
 'download-notification-api': 'TitanNotifications_BeginDownloadActivity' in notif_h and 'TitanNotifications_UpdateDownloadActivity' in notif_h and 'TitanNotifications_GetDownloadActivity' in notif_h,
 'top-header-download-progress': 'Assets %u/%u -' in notif and 'download_activity.state == TITAN_BACKGROUND_ACTIVITY_RUNNING' in notif,
 'notifications-download-row': 'downloads = TitanNotifications_GetDownloadActivity();' in ui and 'download_counts' in ui,
 'kernel-asset-notification-bridge': 'TitanKernel_UpdateNetworkAndAssetUi' in kernel and 'TitanNotifications_UpdateDownloadActivity' in kernel,
 'dex-ip-sync': 'TitanDex_SetIpAddress(network->ip)' in kernel and 'TitanDex_SetIpAddress(0)' in kernel,
 'network-core-unchanged': hashlib.sha256(network).hexdigest() == 'b9d3c3e54f65bc6e7fa1adef976076def92182a8ad99f3730d6a5571286716a8',
}
failed=[]
for name,ok in checks.items():
 print(f'asset-bf12: {"PASS" if ok else "FAIL"} {name}')
 if not ok: failed.append(name)
if failed: raise SystemExit('failed: '+', '.join(failed))
print(f'asset-bf12: PASS {len(checks)}/{len(checks)}')
