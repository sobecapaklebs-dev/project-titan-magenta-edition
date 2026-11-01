#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
service = (ROOT / 'src/services/titan_asset_service.c').read_text()
header = (ROOT / 'src/services/titan_asset_service.h').read_text()
scene = (ROOT / 'src/ui/titan_shell_scene.c').read_text()
scheduler = (ROOT / 'src/services/titan_storage_scheduler.c').read_text()
storage_io = (ROOT / 'src/services/titan_storage_io.c').read_text()
image = (ROOT / 'src/services/titan_asset_image.c').read_text()
probe = service[service.index('static int TitanAssetService_SelectProbeLocked'):service.index('static int TitanAssetService_EnsureCacheDirs')]
read_external = service[service.index('static int TitanAssetService_ReadExternalStorageChunk'):service.index('static int TitanAssetService_WriteStorageChunk')]
storage_pump = service[service.index('int TitanAssetService_StorageWorkerPump(void)'):service.index('#if defined(__EE__) || defined(_EE)\ntypedef struct TitanAssetFetchContext')]
worker_start = scheduler.index('static void TitanStorageScheduler_Worker(void *argument)')
storage_worker = scheduler[worker_start:scheduler.index('int TitanStorageScheduler_Initialize(void)', worker_start)]
decode = service[service.index('static int TitanAssetService_ProcessExternalDecode(void)\n{'):service.index('static void TitanAssetService_NetworkWorker')]

checks = {
    'opl-art-root': '#define TITAN_ASSET_EXTERNAL_ART_DIR "mass:/ART"' in service,
    'opl-icon-naming': '"_ICO.png"' in service and '"_ICO.jpg"' in service,
    'ps2-launcher-square-fallback': '".png", ".jpg"' in service and 'TitanAssetService_ExternalArtSuffix' in service,
    'opl-cov-last-resort': '"_COV2.png", "_COV2.jpg", "_COV.png", "_COV.jpg"' in service,
    'opl-background-naming': '"_BG_00.png"' in service and '"_BG.png"' in service,
    'opl-screenshot-fallback': '"_SCR_00.png"' in service and '"_SCR.png"' in service,
    'bounded-source-size': 'size > TITAN_ASSET_SOURCE_MAX_SIZE' in service,
    'serialized-filexio': 'TitanStorageIo_AsyncRead' in read_external and 'fileXioSetBlockMode(FXIO_NOWAIT)' in storage_io and 'fileXioWaitAsync(FXIO_NOWAIT' in storage_io,
    'async-read-retains-buffer-while-offset-zero': 'if (!job->external_buffer_allocated)' in read_external and read_external.index('if (!job->external_buffer_allocated)') < read_external.index('TitanStorageIo_AsyncRead'),
    'storage-io-iop-flags-header-order': storage_io.index('#define NEWLIB_PORT_AWARE') < storage_io.index('#include <io_common.h>') < storage_io.index('#include <fileXio_rpc.h>') < storage_io.index('#undef NEWLIB_PORT_AWARE'),
    'cache-publish-rename-async': 'TitanStorageIo_AsyncAtomicReplace(&job->io_file' in service and 'fileXioRename(source, destination)' in storage_io and 'TITAN_STORAGE_IO_ASYNC_RENAME_PENDING' in storage_io,
    'cache-publish-checks-structure-without-payload-crc': 'TitanAssetService_ValidateTimgHeader(g_assets.download_buffer, job->total' in service and 'TitanAssetService_ValidateWriteChunk(job)' not in service and 'Crc32Update' not in service,
    'startup-prewarms-persistent-cache': 'Warm the first Home viewport from persistent TIMG cache' in service and 'TitanAssetService_SetVisibleGames(startup_selected, startup_visible' in service and 'startup_visible_count < 16u' in service,
    'startup-queues-whole-catalog-cache': 'startup_preload_count * 2u' in service and 'item < g_assets.startup_preload_count' in service and 'if (g_assets.startup_preload_active) return 1;' in service,
    'catalog-icons-and-backgrounds-have-ram-thumbnails': 'icon_thumbnails[TITAN_NEUTRINO_GAME_CAPACITY]' in service and 'background_thumbnails[TITAN_NEUTRINO_GAME_CAPACITY]' in service and 'TitanAssetService_StoreIconThumbnail(game_index, source)' in service,
    'cache-load-validates-header-without-payload-crc': 'TitanAssetService_ValidateTimgHeader(g_assets.storage_buffer,\n                g_assets.storage_buffer_size, job->kind, 0)' in service and 'TitanAssetService_ValidateCacheReadChunk(job)' not in service and 'Crc32Update' not in service,
    'main-thread-keeps-structural-cache-check': 'TitanAssetService_ValidateTimg(g_assets.storage_buffer, stage_size' not in service and 'TitanAssetService_ValidateTimgHeader(g_assets.storage_buffer, stage_size' in service,
    'normal-cache-probe-does-not-open-art-search-ribbon': 'if (job->manual_owner || g_assets.artwork_wizard_active)' in service and '"PROBE", job->serial, -1' in service,
    'home-waits-for-startup-cache': 'TitanAssetService_StartupCacheReady()' in scene and 'startup_cache_done' in scene and 'Preparing cached game artwork' in scene,
    'folder-preview-queues-cached-icons-before-idle-scan': 'Folder children are drawn by this overlay' in scene and 'TitanShellScene_BuildFolderItemEntry(folder->item_keys[index], &folder_entry)' in scene and 'TitanAssetService_SetVisibleGames(asset_selected_game, asset_visible_games' in scene,
    'bounded-io-quantums': ('TITAN_ASSET_IO_QUANTUM (8u * 1024u)' in service and 'TITAN_ASSET_EXTERNAL_IO_QUANTUM (64u * 1024u)' in service and 'TITAN_ASSET_WIZARD_IO_QUANTUM (64u * 1024u)' in service and 'quantum = remaining > TITAN_ASSET_EXTERNAL_IO_QUANTUM' in service),
    'wizard-indexes-local-art-directory-once': 'TitanAssetService_BuildExternalArtIndex(art_root)' in service and 'TitanStorageIo_ListDirectory(art_root, start, entries' in service and 'external_art_sizes[TITAN_NEUTRINO_GAME_CAPACITY][2][8]' in service,
    'indexed-art-read-skips-extra-stat': 'g_assets.external_art_index_state == 1u ? 1u : 0u' in service and 'up to eight path-stat RPCs per asset into one directory pass' in service,
    'separate-prefetch-buffer': 'external_prefetch_buffer' in service and 'external_source_buffer' in service and 'external_prefetch_capacity' in service,
    'prefetch-memory-pressure-falls-back-serial': 'decoder_owns_stage' in read_external and 'if (decoder_owns_stage) return 1' in read_external,
    'io-decode-overlap': 'job->external_prefetch = g_assets.external_decode_busy ? 1u : 0u' in read_external and 'g_assets.external_prefetch_ready = 1' in storage_pump,
    'prefetch-promoted-after-decode-releases-source': decode.index('TitanAssetService_FreeExternalSource();') < decode.index('TitanAssetService_PromoteExternalPrefetchLocked();'),
    'decoded-stage-protected-from-overwrite': 'g_assets.storage_stage_ready || g_assets.external_source_buffer == 0' in decode,
    'cache-load-stays-serialized-with-decode': 'g_assets.storage_job.op != TITAN_ASSET_STORAGE_LOAD_EXTERNAL_ICON' in storage_pump and 'g_assets.storage_job.op != TITAN_ASSET_STORAGE_LOAD_EXTERNAL_BACKGROUND' in storage_pump,
    'storage-scheduler-owner': 'TitanAssetService_StorageWorkerPump' in scheduler,
    'low-priority-decode-worker': 'TitanAssetService_ProcessExternalDecode()' in service and 'TITAN_ASSET_NET_PRIORITY 110' in service,
    'cooperative-decode': 'options.yield = TitanAssetService_ImageYield' in service and 'options.yield_every_rows = 128u' in decode and 'image_options.yield_every_rows = 8u' in service,
    'same-png-jpeg-decoder': 'TitanAssetImage_ConvertToTimgEx(source, source_size, context.kind' in service and 'png_read_row' in image and 'jpeg_read_scanlines' in image,
    'preempt-selection-background': 'context->kind == TITAN_ASSET_KIND_BACKGROUND' in service and 'g_assets.selected_game_index != context->game_index' in service,
    'visible-icons-before-selected-background': probe.index('for (i = 0u; i < g_assets.visible_count; i++)') < probe.index('if (s->background_probe_pending'),
    'local-opl-small-icons-accepted': 'minimum_source_width = context.kind == TITAN_ASSET_KIND_ICON ? 64u : 256u' in service and 'minimum_source_height = context.kind == TITAN_ASSET_KIND_ICON ? 64u : 128u' in service,
    'visible-icons-protected-from-lru': 'if (!visible && g_assets.icon_slots[i].last_touch_frame < oldest)' in service and 'evicted->icon_presence = TITAN_ASSET_PRESENCE_UNKNOWN' in service,
    'library-draw-refreshes-lru': 'slot->last_touch_frame = g_assets.frame_index;' in service[service.index('int TitanAssetService_DrawIcon('):service.index('int TitanAssetService_DrawIconHighQuality(')],
    'background-reloaded-on-selection-return': 'g_assets.games[selected_game_index].background_presence = TITAN_ASSET_PRESENCE_UNKNOWN' in service and 'g_assets.active_background_game = TITAN_ASSET_INVALID_GAME;' in service,
    'bad-opl-candidate-falls-through': 'context.candidate < 7u' in service and 'state->icon_external_candidate = (unsigned char)(context.candidate + 1u)' in service and 'state->background_external_candidate = (unsigned char)(context.candidate + 1u)' in service,
    'external-candidate-resume': 'job->serial, job->kind, job->external_candidate' in service and '*candidate_out = candidate;' in service,
    'flight-recorder': 'TITAN_ASSET_DIAG_EXTERNAL_FOUND' in service and 'TITAN_ASSET_DIAG_EXTERNAL_DECODE_END' in service,
    'read-decode-duration-telemetry': 'TITAN_ASSET_DIAG_EXTERNAL_READ_TIMING' in service and 'TITAN_ASSET_DIAG_EXTERNAL_READ_IO_TIMING' in service and 'TITAN_ASSET_DIAG_EXTERNAL_PROBE_TIMING' in service and 'TITAN_ASSET_DIAG_EXTERNAL_DISPATCH_TIMING' in service and 'TITAN_ASSET_DIAG_STAGE_READY_TIMING' in service and 'DECODE WAIT %ums' in service and 'ready_stage' in service,
    'external-read-splits-io-and-scheduler-wait': 'external_io_elapsed_us' in service and '"%s %us I%u W%u"' in service,
    'external-file-stat-once-per-job': 'if (!job->external_path_checked)' in service and 'job->external_path_checked = 1u;' in service,
    'wizard-owned-conversion': 'int TitanAssetService_StartArtworkWizard' in service and 'TITAN_ASSET_MANUAL_IMPORT_ARTWORK' in service,
    'wizard-prefetches-next-source-during-decode': 'Keep one local ART read in flight during decode' in service and 'external_prefetch_ready && !g_assets.download_ready_for_storage' in service and 'g_assets.download_ready_for_storage || g_assets.storage_stage_ready' in service and 'TitanAssetService_PromoteExternalPrefetchLocked();' in service[service.index('void TitanAssetService_UpdateMainThread'):service.index('int TitanAssetService_DrawIcon(')],
    'wizard-retries-transient-external-reads': 'job->retry_count < TITAN_ASSET_WIZARD_IO_RETRIES' in service and 'job->external_path_checked = 0u;' in service,
    'wizard-retries-transient-cache-writes': 'TITAN_ASSET_DIAG_CACHE_WRITE_FAIL, -2' in service and 'job->offset = 0u;' in service,
    'persistent-atomic-timg-cache': 'g_assets.download_ready_for_storage = 1' in service and 'TitanStorageIo_AsyncAtomicReplace(&job->io_file' in service,
    'no-runtime-raw-art-fallback': 'if (wizard_import)' in service and 'FindExternalArt(job->game_index' in service,
    'asset-wizard-settings-content': 'TITAN_SETTINGS_CONTENT' in scene and '"Asset Wizard"' in scene,
    'single-and-library-modes': '"Convert selected icon"' in scene and '"Convert selected background"' in scene and '"Convert all artwork"' in scene,
    'clear-asset-cache-remains-separate': '"Clear asset cache"' in scene and 'TITAN_ASSET_MANUAL_CLEAR_CACHE' in service,
    'wizard-api-contract': 'TitanAssetService_StartArtworkWizard' in header and 'artwork_wizard_active' in header,
    'per-asset-decode-progress-api': 'void (*progress)(void *user_data, unsigned int completed, unsigned int total);' in (ROOT / 'src/services/titan_asset_image.h').read_text() and 'options->progress(options->user_data, row, total)' in image,
    'wizard-progress-tracks-persist-stage': 'current_asset_progress_percent' in header and '90u +' in service and '(job->offset * 9u) / job->total' in service and 'current_asset_progress_percent = 100u' in service,
    'wizard-filexio-runs-on-main-thread-after-worker-park': 'TitanAssetService_IsArtworkWizardSessionActive()' in scheduler and 'TitanStorageScheduler_IsAssetWizardWorkerParked()' in scheduler and 'TitanAssetService_StorageWorkerPump()' in scheduler and 'TitanAssetService_StorageIoPending()' in scheduler and 'FXIO_NOWAIT' in storage_io,
    'wizard-worker-exclusive-handoff-ack': 'TitanStorageScheduler_IsAssetWizardExclusiveMode()' in storage_worker and 'TitanStorageScheduler_SetAssetWizardWorkerParked(wizard_mode)' in storage_worker,
    'wizard-park-ack-persists-until-transition': 'changed = g_storage_scheduler.asset_wizard_exclusive_mode != value;' in scheduler and 'if (changed)\n    {\n        g_storage_scheduler.asset_wizard_exclusive_mode = value;' in scheduler,
    'progress-screen-has-no-control-rows': 'Current item progress' in scene and 'Return to Content' not in scene[scene.index('static void TitanShellScene_DrawNetworkPage'):scene.index('static void TitanShellScene_DrawSystemUpdatePage')],
    'progress-footer-contextual-actions': 'X Return    O Cancel & Return' in scene and 'X Return    O Back' in scene,
}

# External image decoding is now an explicit Asset Wizard step. The conversion
# hands TIMG to the existing atomic cache writer instead of publishing raw ART
# pixels only to RAM.
section = service.split('static int TitanAssetService_ProcessExternalDecode(void)', 1)[1]
section = section.split('static void TitanAssetService_NetworkWorker', 1)[0]
checks['wizard-publishes-timg'] = all(x in section for x in (
    'context.wizard_import', 'g_assets.download_buffer',
    'g_assets.download_ready_for_storage = 1'
)) and 'TitanStorageIo_AsyncAtomicReplace(&job->io_file' in service

failed = []
for name, ok in checks.items():
    print(f'asset-wizard: {"PASS" if ok else "FAIL"} {name}')
    if not ok:
        failed.append(name)
if failed:
    raise SystemExit('failed: ' + ', '.join(failed))
print(f'asset-wizard: PASS {len(checks)}/{len(checks)}')
