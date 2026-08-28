#!/usr/bin/env python3
from pathlib import Path
import hashlib
ROOT=Path(__file__).resolve().parents[1]
read=lambda p:(ROOT/p).read_text(errors='replace')
service=read('src/services/titan_asset_service.c')
header=read('src/services/titan_asset_service.h')
image=read('src/services/titan_asset_image.c')
http=read('src/services/titan_http_client.c')
http_h=read('src/services/titan_http_client.h')
ui=read('src/ui/titan_shell_scene.c')
notif=read('src/ui/titan_notifications.c')
notif_h=read('src/ui/titan_notifications.h')
kernel=read('src/core/titan_kernel.c')
network=(ROOT/'src/services/titan_network.c').read_bytes()

def panel_base_body():
    a=ui.index('static void TitanShellScene_DrawPanelBase(')
    b=ui.index('static void TitanShellScene_DrawValueRow(',a)
    return ui[a:b]

def tile_body():
    a=ui.index('static void TitanShellScene_DrawTile(')
    b=ui.index('static void TitanShellScene_DrawHome',a)
    return ui[a:b]

checks={
 'icon-provider-is-menu-art':('PSBBN square game art' in service or 'OSD-XMB ICON0' in service) and '_COV.png' not in service,
 'covers-not-assets':'_COV.png' not in service and '_COV2.png' not in service and 'xlenore/ps2-covers' not in service,
 'background-only-bg-scr':'%s/%s_BG_00.png' in service and '%s/%s_SCR_00.png' in service,
 'timg-v2-cache-reset':'#define TITAN_TIMG_VERSION 2u' in image and 'ReadLe16(&data[4]) != 2u' in service,
 'round-robin-icon':'manual_icon_download_cursor' in service and '*cursor = ordinal + 1u' in service,
 'round-robin-background':'manual_background_download_cursor' in service,
 'bounded-socket-waits':'TitanHttp_WaitSocket' in http and 'select(transport->fd + 1' in http,
 'bounded-payload-socket':'TitanHttp_SocketRecv' in http and 'TitanHttp_SocketSend' in http,
 'dns-cache':('cached_dns_host' in http and 'cached_dns_valid' in http) or ('TitanHttp_DnsCacheLookup' in http and 'dns_cache' in http),
 'http-progress-api':'TitanHttpProgressCallback' in http_h and 'TitanHttp_Progress(options' in http,
 'asset-transfer-telemetry':all(x in header for x in ('transfer_bytes_received','transfer_bytes_total','transfer_bytes_per_second','transfer_progress_percent')),
 'notification-speed':'transfer_bytes_per_second' in notif_h and 'KiB/s' in notif,
 'notification-current-transfer':'transfer_progress_percent' in notif_h and 'atual %u%%' in ui,
 'download-bars':'row_base + 60.0f' in ui and 'overall_track' in ui and 'transfer_track' in ui,
 'library-bar-no-overlap':'TITAN_SETTINGS_ROW_TOP + 60.0f' in ui and 'TITAN_SETTINGS_ROW_TOP + 54.0f' not in ui[ui.index('static void TitanShellScene_DrawNotificationsPage('):ui.index('static void TitanShellScene_DrawUserProfilePage(')],
 'shell-header-chrome-removed':'TitanVideo_DrawRectAlpha' not in panel_base_body() and 'Header chrome/separators are theme-owned' in panel_base_body(),
 'real-icon-full-tile':'TitanAssetService_DrawIconHighQuality' in tile_body() and 'float art_w = width - 4.0f;' in tile_body(),
 'placeholder-original-size':'icon_size = (46.0f + (clamped_focus * 48.0f)) * layout_scale;' in tile_body() and 'TitanAssetService_DrawIcon(' not in tile_body(),
 'dex-ip-preserved':'TitanDex_SetIpAddress(network->ip)' in kernel,
 'network-core-unchanged':hashlib.sha256(network).hexdigest() == 'b9d3c3e54f65bc6e7fa1adef976076def92182a8ad99f3730d6a5571286716a8',
}
failed=[]
for n,v in checks.items():
 print(f'asset-bf13: {"PASS" if v else "FAIL"} {n}')
 if not v: failed.append(n)
if failed: raise SystemExit('failed: '+', '.join(failed))
print(f'asset-bf13: PASS {len(checks)}/{len(checks)}')
