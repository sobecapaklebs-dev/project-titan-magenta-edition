#!/usr/bin/env python3
from pathlib import Path
import hashlib
ROOT=Path(__file__).resolve().parents[1]
service=(ROOT/'src/services/titan_asset_service.c').read_text(errors='replace')
header=(ROOT/'src/services/titan_asset_service.h').read_text(errors='replace')
ui=(ROOT/'src/ui/titan_shell_scene.c').read_text(errors='replace')
net=(ROOT/'src/services/titan_network.c').read_bytes()
EXPECTED_NETWORK_SHA256='b9d3c3e54f65bc6e7fa1adef976076def92182a8ad99f3730d6a5571286716a8'
checks={
 'manual-api':'TitanAssetService_StartManualDownload' in header and 'TitanAssetService_CancelManualOperation' in header,
 'auto-default-off':'g_assets.automatic_download_enabled = 0;' in service,
 'lazy-network-worker':'Internet worker is lazy' in service and 'TitanAssetService_EnsureNetworkWorker' in service,
 'network-worker-gated':'!g_assets.automatic_download_enabled && !g_assets.manual_session_active' in service,
 'manual-needs-online':'network->state != TITAN_NETWORK_ONLINE' in service,
 'icons-first-phases':'TITAN_ASSET_MANUAL_PROBE_ICONS' in service and 'TITAN_ASSET_MANUAL_DOWNLOAD_ICONS' in service and 'TITAN_ASSET_MANUAL_PROBE_BACKGROUNDS' in service,
 'single-download-worker':'TitanAssetService_DownloadOne(game, kind)' in service and 'for (;;)' in service,
 'foreground-pause':'TitanStorageScheduler_IsForegroundBusy()' in service,
 '8k-storage-quantum':'TITAN_ASSET_IO_QUANTUM (8u * 1024u)' in service,
 'atomic-cache':'TitanStorageIo_AtomicReplace' in service and '"%s.part"' in service,
 'async-cache-clear':'TITAN_ASSET_MANUAL_CLEAR_CACHE' in service and 'TitanAssetService_StartClearCache' in service and 'TitanAssetService_StorageWorkerPump' in service,
 'diagnostics':'0x310' in service and '0x316' in service and 'TITAN_DIAG_ASSET' in service,
 'network-category-six-rows':'case TITAN_SETTINGS_NETWORK:\n            return 6;' in ui,
 'game-data-menu':'"Dados de jogos"' in ui and '"Baixar assets dos jogos"' in ui and '"Atualizar assets existentes"' in ui and '"Limpar cache de assets"' in ui and '"Download automático"' in ui,
 'progress-page':'TITAN_NETWORK_PAGE_ASSET_PROGRESS' in ui and '"Executar em segundo plano"' in ui and '"Cancelar"' in ui,
 'no-auto-on-connect':'Downloads de arte são separados da conexão' in ui,
 'network-core-unchanged': hashlib.sha256(net).hexdigest()==EXPECTED_NETWORK_SHA256,
}
failed=[]
for name,ok in checks.items():
    print(f'asset-manual-bf9: {"PASS" if ok else "FAIL"} {name}')
    if not ok: failed.append(name)
if failed: raise SystemExit('failed: '+', '.join(failed))
print('asset-manual-bf9: PASS')
