#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
service = (ROOT / 'src/services/titan_asset_service.c').read_text(errors='replace')
header = (ROOT / 'src/services/titan_asset_service.h').read_text(errors='replace')
http_h = (ROOT / 'src/services/titan_http_client.h').read_text(errors='replace')
http_c = (ROOT / 'src/services/titan_http_client.c').read_text(errors='replace')
ui = (ROOT / 'src/ui/titan_shell_scene.c').read_text(errors='replace')
diag = (ROOT / 'src/services/titan_diagnostics.c').read_text(errors='replace')

checks = {
    'auto-default-off': 'g_assets.automatic_download_enabled = 0;' in service,
    'auto-30s-online': '#define TITAN_ASSET_NETWORK_STABLE_FRAMES 1800u' in service,
    'manual-bypasses-auto-delay': 'if (!g_assets.manual_session_active)' in service and
        'g_assets.frame_index - online_since_frame < TITAN_ASSET_NETWORK_STABLE_FRAMES' in service,
    'home-idle-api': 'TitanAssetService_SetAutomaticIdleEligible' in header and
        'TitanAssetService_SetAutomaticIdleEligible(' in ui,
    'input-pauses-auto': 'TitanAssetService_NotifyUserActivity' in header and
        'TitanAssetService_NotifyUserActivity();' in ui and
        'g_assets.last_user_activity_frame' in service,
    'selected-visible-rest-order': 'TitanAssetService_BuildManualOrderLocked' in service and
        'game = g_assets.selected_game_index;' in service and
        'for (i = 0u; i < g_assets.visible_count; i++)' in service and
        'for (game = 0u; game < count; game++)' in service,
    'manual-probe-scheduler-visible': 'g_assets.manual_probe_cursor < g_assets.session_games' in service and
        'Missing-only manual sessions drive their cache probes' in service,
    'manual-probe-uses-order': 'g_assets.manual_order[g_assets.manual_probe_cursor++]' in service,
    'manual-download-uses-order': 'ordered_game = g_assets.manual_order[ordinal]' in service,
    'icons-before-backgrounds': 'TITAN_ASSET_MANUAL_DOWNLOAD_ICONS' in service and
        'TITAN_ASSET_MANUAL_PROBE_BACKGROUNDS' in service and
        'TITAN_ASSET_MANUAL_DOWNLOAD_BACKGROUNDS' in service,
    'single-download-worker': service.count('TitanAssetService_DownloadOne(game, kind)') == 1,
    'library-scan-wins': service.count('TitanStorageScheduler_IsLibraryScanBusy()') >= 2,
    '8k-storage-quantum': 'TITAN_ASSET_IO_QUANTUM (8u * 1024u)' in service,
    'atomic-cache': 'TitanStorageIo_AtomicReplace' in service and '"%s.part"' in service,
    'http-trace-callback': 'TitanHttpTraceCallback trace;' in http_h and
        'TitanHttp_Trace(options, TITAN_HTTP_TRACE_DNS_BEGIN' in http_c and
        'TitanHttp_Trace(options, TITAN_HTTP_TRACE_TCP_BEGIN' in http_c and
        'TitanHttp_Trace(options, TITAN_HTTP_TRACE_TLS_BEGIN' in http_c,
    'download-decode-cache-markers': all(x in service for x in (
        'TITAN_ASSET_DIAG_DOWNLOAD_BEGIN', 'TITAN_ASSET_DIAG_DOWNLOAD_END',
        'TITAN_ASSET_DIAG_DECODE_BEGIN', 'TITAN_ASSET_DIAG_DECODE_END',
        'TITAN_ASSET_DIAG_CACHE_BEGIN', 'TITAN_ASSET_DIAG_CACHE_END')),
    'named-flight-recorder': all(x in diag for x in (
        'ASSET_SESSION_BEGIN', 'ASSET_SESSION_END', 'DNS_BEGIN', 'DNS_END',
        'TCP_BEGIN', 'TCP_END', 'TLS_BEGIN', 'TLS_END', 'DOWNLOAD_BEGIN',
        'DOWNLOAD_END', 'DECODE_BEGIN', 'DECODE_END', 'CACHE_BEGIN', 'CACHE_END')),
    'progress-game-ordinal': 'current_game_ordinal' in header and 'Jogo: %u / %u' in ui,
    'progress-current-title': 'assets->current_game_index' in ui and
        'TitanNeutrino_GetGame(assets->current_game_index)' in ui,
    'auto-description-30s': 'Aguarda 30 s ONLINE + Home ociosa' in ui,
}

failed = []
for name, ok in checks.items():
    print(f'asset-pipeline-bf10: {"PASS" if ok else "FAIL"} {name}')
    if not ok:
        failed.append(name)
if failed:
    raise SystemExit('failed: ' + ', '.join(failed))
print('asset-pipeline-bf10: PASS')
