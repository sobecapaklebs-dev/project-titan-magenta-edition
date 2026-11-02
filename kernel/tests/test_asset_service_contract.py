#!/usr/bin/env python3
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
service=(ROOT/'src/services/titan_asset_service.c').read_text()
header=(ROOT/'src/services/titan_asset_service.h').read_text()
http=(ROOT/'src/services/titan_http_client.c').read_text()
image=(ROOT/'src/services/titan_asset_image.c').read_text()
scheduler=(ROOT/'src/services/titan_storage_scheduler.c').read_text()
kernel=(ROOT/'src/core/titan_kernel.c').read_text()
scene=(ROOT/'src/ui/titan_shell_scene.c').read_text()
video=(ROOT/'src/graphics/titan_video.c').read_text()
makefile=(ROOT/'Makefile').read_text()
tls=(ROOT/'tools/titan_tls_deps.py').read_text()
providers=service[service.index('static unsigned int TitanAssetService_BuildProviders'):service.index('static int TitanAssetService_SelectDownload')]
checks={
 'service-object-linked':'src/services/titan_asset_service.o' in makefile,
 'native-http-object-linked':'src/services/titan_http_client.o' in makefile,
 'native-image-object-linked':'src/services/titan_asset_image.o' in makefile,
 'service-init-after-scheduler':kernel.find('TitanAssetService_Initialize();')>kernel.find('TitanStorageScheduler_Initialize();'),
 'main-thread-commit':'TitanAssetService_UpdateMainThread(context.frame_count);' in kernel,
 'renderer-no-network':'TitanAssetService_DrawIcon' in scene and 'recv(' not in scene and 'socket(' not in scene,
 'home-icons':'item->game_index' in scene and 'TitanAssetService_DrawIcon' in scene,
 'library-icons':'entry->index' in scene and scene.count('TitanAssetService_DrawIcon')>=2,
 'background-debounce':'TITAN_ASSET_BACKGROUND_DEBOUNCE_FRAMES 10u' in service,
 'compressed-source-transient':'TITAN_ASSET_SOURCE_MAX_SIZE' in service and 'malloc(TITAN_ASSET_SOURCE_MAX_SIZE)' in service and 'g_assets.source_buffer' in service and 'free(g_assets.source_buffer)' in service,
 '8k-storage-quantum':'TITAN_ASSET_IO_QUANTUM (8u * 1024u)' in service,
 'larger-bounded-opl-art-reads':('TITAN_ASSET_EXTERNAL_IO_QUANTUM (64u * 1024u)' in service and 'quantum = remaining > TITAN_ASSET_EXTERNAL_IO_QUANTUM' in service and 'TITAN_ASSET_WIZARD_IO_QUANTUM (64u * 1024u)' in service),
 'atomic-part':'"%s.part"' in service and 'TitanStorageIo_AtomicReplace' in service,
 'runtime-payload-crc-validation-removed':'TitanAssetService_Crc32' not in service and 'Crc32Update' not in service and 'ValidateTimgHeader' in service,
 'storage-scheduler-owner':'TitanAssetService_StorageWorkerPump' in scheduler,
 'scan-fairness':'asset_scan_turn' in scheduler,
 'package-pause':'TitanPackageInstaller_IsActive()' in service,
 'system-update-pause':'TitanSystemUpdate_IsActive()' in service,
 'foreground-pause':'TitanStorageScheduler_IsForegroundBusy()' in service,
 'cancel-mid-download':'TitanAssetService_ShouldAbortFetch' in service and 'should_cancel' in http,
 'cancel-stale-background':'g_assets.selected_game_index != context->game_index' in service,
 'no-new-vram':'&g_video.transient_atlas,\n            g_video.avatar_atlas.address' in video and '&g_video.transient_atlas,\n            g_video.cached_layer.address' in video,
 'direct-square-icon-provider':'CosmicScale/psbbn-art-database/main/art/%s.png' in service and 'HiroTex/OSD-XMB-ARTDB/main/PS2/%s/ICON0.PNG' in service,
 'direct-luden-background':'Luden02/psx-ps2-opl-art-database/main/PS2/%s/%s_BG_00.png' in service,
 'opl-cover-last-resort-icon':'"_COV2.png", "_COV2.jpg", "_COV.png", "_COV.jpg"' in service,
 'no-runtime-gateway':'assets.cfg' not in service and 'gateway' not in service.lower(),
 'https-native':'br_ssl_client_init_full' in http and 'https://' in service,
 'tls-hostname-verify':'br_ssl_client_reset(&transport->ssl, url->host, 0)' in http,
 'tls-time-verify':'br_x509_minimal_set_time' in http,
 'tls-pinned-ca-build-input':'ATHENA_CA_SHA256' in tls,
 'bearssl-build':'-lbearssl' in makefile,
 'png-jpeg-decode':'png_read_row' in image and 'jpeg_read_scanlines' in image,
 'streaming-image-rows':'rowbytes = png_get_rowbytes' in image and 'malloc((size_t)rowbytes)' in image and 'alloc_sarray' in image,
 'timg-icon-128':'TITAN_ASSET_ICON_SOURCE_WIDTH 128u' in header,
 'timg-psm16-ready':'TitanAssetService_ReadLe32(&data[12]) != 1u' in service and 'TitanAssetImage_PackPsm16' in image,
 'renderer-direct-psm16':'TitanVideo_UploadTransientAtlasPsm16' in video and 'TitanVideo_DrawAdaptiveImagePsm16' in video,
 'timg-bg-256x128':'TITAN_ASSET_BACKGROUND_WIDTH 256u' in header and 'TITAN_ASSET_BACKGROUND_HEIGHT 128u' in header,
 'indexed-cache-package-path':'TITAN_ASSET_CACHE_PACKAGE_PATH TITAN_ASSET_CACHE_DIR "/ASSETS.TPK"' in service,
 'package-index-mounted-once':'TitanAssetService_EnsureCachePackage' in service and 'TitanStorageIo_ReadExact(fd, g_assets.cache_package_index_bytes, index_size)' in service,
 'package-read-falls-back-to-timg':'TITAN_ASSET_STORAGE_LOAD_PACKAGE' in service and 'TitanAssetService_DisableCachePackage(1)' in service,
 'package-loader-uses-persistent-file':'TitanStorageIo_Seek64(g_assets.cache_package_fd' in service and 'TitanStorageIo_ReadExact(g_assets.cache_package_fd, g_assets.storage_buffer' in service,
 'wizard-auto-builds-package':'TITAN_ASSET_MANUAL_PACKAGING_ARTWORK' in service and 'TitanAssetService_PackageBuildPump' in service,
 'wizard-builder-atomic-publish':'TitanStorageIo_AsyncAtomicReplace(&g_assets.package_build_file' in service and 'TITAN_ASSET_CACHE_PACKAGE_TEMP_PATH' in service,
 'package-build-included-in-storage-scheduler':'TITAN_ASSET_PACKAGE_BUILD_STREAM_READY' in service and 'TitanAssetService_PackageBuildPump();' in service,
 'package-build-streams-hot-timg':'TitanAssetService_PackageBuildAppendCurrent(job)' in service and 'g_assets.download_buffer, job->total' in service,
 'package-build-avoids-cache-rescan':'TitanStorageIo_ListDirectory' not in service[service.find('static int TitanAssetService_PackageBuildPump'):service.find('static int TitanAssetService_ReadStorageChunk')],
 'package-v2-footer':'version == 2u' in service and 'TITAN_ASSET_PACKAGE_BUILD_WRITE_FOOTER' in service,
 'status-direct-internet':'direct_internet' in header and 'Internet direto' in service,
}
failed=[]
for n,v in checks.items(): print(f'asset-service-contract: {"OK" if v else "FAIL"} {n}'); failed += [] if v else [n]
if failed: raise SystemExit('failed: '+', '.join(failed))
print('asset-service-contract: PASS')
