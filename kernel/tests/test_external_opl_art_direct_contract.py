#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
service = (ROOT / 'src/services/titan_asset_service.c').read_text()
scheduler = (ROOT / 'src/services/titan_storage_scheduler.c').read_text()
image = (ROOT / 'src/services/titan_asset_image.c').read_text()

checks = {
    'opl-art-root': '#define TITAN_ASSET_EXTERNAL_ART_DIR "mass:/ART"' in service,
    'opl-icon-naming': '"_ICO.png"' in service and '"_ICO.jpg"' in service,
    'ps2-launcher-square-fallback': 'case 2u: suffix = ".png"' in service,
    'opl-background-naming': '"_BG_00.png"' in service and '"_BG.png"' in service,
    'opl-screenshot-fallback': '"_SCR_00.png"' in service and '"_SCR.png"' in service,
    'no-cover-repurposed-as-icon': '_COV.png' not in service and '_COV.jpg' not in service,
    'cache-first': service.find('TITAN_ASSET_STORAGE_LOAD_ICON') < service.find('TitanAssetService_FindExternalArt(job->serial'),
    'bounded-source-size': 'size > TITAN_ASSET_SOURCE_MAX_SIZE' in service,
    'serialized-filexio': 'TitanAssetService_ReadExternalStorageChunk' in service and 'TitanStorageIo_ReadExact' in service,
    'eight-kib-quantum': 'TITAN_ASSET_IO_QUANTUM (8u * 1024u)' in service,
    'storage-scheduler-owner': 'TitanAssetService_StorageWorkerPump' in scheduler,
    'low-priority-decode-worker': 'TitanAssetService_ProcessExternalDecode()' in service and 'TITAN_ASSET_NET_PRIORITY 110' in service,
    'cooperative-decode': 'options.yield = TitanAssetService_ImageYield' in service and 'options.yield_every_rows = 8u' in service,
    'same-png-jpeg-decoder': 'TitanAssetImage_ConvertToTimgEx(source, source_size, context.kind' in service and 'png_read_row' in image and 'jpeg_read_scanlines' in image,
    'ram-only-stage': 'g_assets.storage_stage_ready = 1' in service and 'external_decode_path' in service,
    'external-decode-no-persistent-publish': 'TitanAssetService_ProcessExternalDecode' in service,
    'preempt-selection-background': 'context->kind == TITAN_ASSET_KIND_BACKGROUND' in service and 'g_assets.selected_game_index != context->game_index' in service,
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
