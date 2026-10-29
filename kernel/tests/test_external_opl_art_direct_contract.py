#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
service = (ROOT / 'src/services/titan_asset_service.c').read_text()
scheduler = (ROOT / 'src/services/titan_storage_scheduler.c').read_text()
image = (ROOT / 'src/services/titan_asset_image.c').read_text()
probe = service[service.index('static int TitanAssetService_SelectProbeLocked'):service.index('static int TitanAssetService_EnsureCacheDirs')]

checks = {
    'opl-art-root': '#define TITAN_ASSET_EXTERNAL_ART_DIR "mass:/ART"' in service,
    'opl-icon-naming': all(x in service for x in ('"_COV.png"', '"_COV.jpg"', '"_COV2.png"', '"_ICO.png"', '"_ICO.jpg"')),
    'ps2-launcher-square-fallback': 'case 6u: suffix = ".png"' in service,
    'opl-background-naming': '"_BG_00.png"' in service and '"_BG.png"' in service,
    'opl-screenshot-fallback': '"_SCR_00.png"' in service and '"_SCR.png"' in service,
    'opl-cover-and-ico-art-supported': all(x in service for x in ('"_COV.png"', '"_COV.jpg"', '"_COV2.png"', '"_ICO.png"', '"_ICO.jpg"')),
    'cache-first': service.find('TITAN_ASSET_STORAGE_LOAD_ICON') < service.find('TitanAssetService_FindExternalArt(job->serial'),
    'bounded-source-size': 'size > TITAN_ASSET_SOURCE_MAX_SIZE' in service,
    'serialized-filexio': 'TitanAssetService_ReadExternalStorageChunk' in service and 'TitanStorageIo_ReadExact' in service,
    'bounded-io-quantums': ('TITAN_ASSET_IO_QUANTUM (8u * 1024u)' in service and 'TITAN_ASSET_EXTERNAL_IO_QUANTUM (32u * 1024u)' in service and 'quantum = remaining > TITAN_ASSET_EXTERNAL_IO_QUANTUM' in service),
    'storage-scheduler-owner': 'TitanAssetService_StorageWorkerPump' in scheduler,
    'low-priority-decode-worker': 'TitanAssetService_ProcessExternalDecode()' in service and 'TITAN_ASSET_NET_PRIORITY 110' in service,
    'cooperative-decode': 'options.yield = TitanAssetService_ImageYield' in service and 'options.yield_every_rows = 8u' in service,
    'same-png-jpeg-decoder': 'TitanAssetImage_ConvertToTimgEx(source, source_size, context.kind' in service and 'png_read_row' in image and 'jpeg_read_scanlines' in image,
    'ram-only-stage': 'g_assets.storage_stage_ready = 1' in service and 'external_decode_path' in service,
    'external-decode-no-persistent-publish': 'TitanAssetService_ProcessExternalDecode' in service,
    'preempt-selection-background': 'context->kind == TITAN_ASSET_KIND_BACKGROUND' in service and 'g_assets.selected_game_index != context->game_index' in service,
    'visible-icons-before-selected-background': probe.index('for (i = 0u; i < g_assets.visible_count; i++)') < probe.index('if (s->background_probe_pending'),
    'local-opl-small-icons-accepted': 'minimum_source_width = context.kind == TITAN_ASSET_KIND_ICON ? 64u : 256u' in service and 'minimum_source_height = context.kind == TITAN_ASSET_KIND_ICON ? 64u : 128u' in service,
    'visible-icons-protected-from-lru': 'if (!visible && g_assets.icon_slots[i].last_touch_frame < oldest)' in service and 'evicted->icon_presence = TITAN_ASSET_PRESENCE_UNKNOWN' in service,
    'library-draw-refreshes-lru': 'slot->last_touch_frame = g_assets.frame_index;' in service[service.index('int TitanAssetService_DrawIcon('):service.index('int TitanAssetService_DrawIconHighQuality(')],
    'background-reloaded-on-selection-return': 'g_assets.games[selected_game_index].background_presence = TITAN_ASSET_PRESENCE_UNKNOWN' in service and 'g_assets.active_background_game = TITAN_ASSET_INVALID_GAME;' in service,
    'bad-opl-candidate-falls-through': 'context.candidate < 7u' in service and 'state->icon_external_candidate = (unsigned char)(context.candidate + 1u)' in service and 'state->background_external_candidate = (unsigned char)(context.candidate + 1u)' in service,
    'external-candidate-resume': 'job->external_candidate, external_path' in service and '*candidate_out = candidate;' in service,
    'flight-recorder': 'TITAN_ASSET_DIAG_EXTERNAL_FOUND' in service and 'TITAN_ASSET_DIAG_EXTERNAL_DECODE_END' in service,
}

# Direct external decode function must not write/rename/cache the source.
section = service.rsplit('static int TitanAssetService_ProcessExternalDecode(void)', 1)[1]
section = section.split('static void TitanAssetService_NetworkWorker', 1)[0]
checks['no-disk-write-during-external-decode'] = all(x not in section for x in (
    'TitanStorageIo_Write', 'TitanStorageIo_AtomicReplace', 'TitanStorageIo_Rename',
    'TITAN_ASSET_STORAGE_WRITE_DOWNLOAD'))

failed = []
for name, ok in checks.items():
    print(f'external-opl-art-direct: {"PASS" if ok else "FAIL"} {name}')
    if not ok:
        failed.append(name)
if failed:
    raise SystemExit('failed: ' + ', '.join(failed))
print(f'external-opl-art-direct: PASS {len(checks)}/{len(checks)}')
