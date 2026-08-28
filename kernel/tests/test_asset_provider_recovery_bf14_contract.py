#!/usr/bin/env python3
from pathlib import Path
import hashlib
ROOT=Path(__file__).resolve().parents[1]
read=lambda p:(ROOT/p).read_text(errors='replace')
service=read('src/services/titan_asset_service.c')
header=read('src/services/titan_asset_service.h')
ui=read('src/ui/titan_shell_scene.c')
video=read('src/graphics/titan_video.c')
kernel=read('src/core/titan_kernel.c')
network=(ROOT/'src/services/titan_network.c').read_bytes()
checks={
 'only-icon-background':'TITAN_ASSET_KIND_ICON' in header and 'TITAN_ASSET_KIND_BACKGROUND' in header and '_COV.png' not in service and '_COV2.png' not in service,
 'psbbn-square-icon':'CosmicScale/psbbn-art-database' in service and 'PSBBN square' in service,
 'osdxmb-icon-fallback':'HiroTex/OSD-XMB-ARTDB' in service and 'ICON0.PNG' in service,
 'background-providers':'%s/%s_BG_00.png' in service and '%s/%s_SCR_00.png' in service and 'PIC2.PNG' in service,
 'unavailable-not-error':'session_unavailable_assets' in header and 'if (permanent) g_assets.session_unavailable_assets++' in service,
 'round-robin-tail':'manual_icon_download_cursor' in service and 'manual_background_download_cursor' in service and '*cursor = ordinal + 1u' in service,
 'stage-aware-network-deadline':'TITAN_ASSET_PIPELINE_STALL_FRAMES' not in service and 'options.deadline_milliseconds = 60000u' in service,
 'round-robin-requeue':'result == -2' in service and 'state->icon_download_pending = 1u' in service,
 'http-heartbeat':'g_assets.pipeline_last_progress_frame = g_assets.frame_index;' in service[service.index('static void TitanAssetService_HttpProgress'):service.index('typedef struct TitanAssetProviderCandidate')],
 'queued-start-safe':'Aguardando finalizacao segura do I/O anterior' in service,
 'full-720-map':'g_video.logical_scale_x = scale_x;' in video and 'g_video.logical_scale_y = scale_y;' in video and 'g_video.logical_offset_y = 0.0f;' in video,
 'notification-inset-bars':'row_base + 60.0f' in ui and 'overall_track = 344.0f' in ui and 'transfer_track = 124.0f' in ui,
 'separate-unavailable-ui':'Sem arte' in ui and 'Erros' in ui,
 'placeholder-preserved':'icon_size = (46.0f + (clamped_focus * 48.0f)) * layout_scale;' in ui,
 'dex-ip-preserved':'TitanDex_SetIpAddress(network->ip)' in kernel,
 'network-core-unchanged':hashlib.sha256(network).hexdigest() == 'b9d3c3e54f65bc6e7fa1adef976076def92182a8ad99f3730d6a5571286716a8',
}
failed=[]
for n,v in checks.items():
 print(f'asset-bf14: {"PASS" if v else "FAIL"} {n}')
 if not v: failed.append(n)
if failed: raise SystemExit('failed: '+', '.join(failed))
print(f'asset-bf14: PASS {len(checks)}/{len(checks)}')
