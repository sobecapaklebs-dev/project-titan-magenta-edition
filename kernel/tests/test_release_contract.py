#!/usr/bin/env python3
"""Static acceptance contract for Project Titan Magenta Edition release metadata.

This is not a substitute for PS2 hardware validation. It prevents the exact
failure mode that occurred in dev1: shipping placeholder/stub code while the
package is labelled as implementing the feature.
"""

from __future__ import annotations

from pathlib import Path
import sys


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def read(root: Path, relative: str) -> str:
    return (root / relative).read_text(encoding="utf-8")


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} KERNEL_ROOT", file=sys.stderr)
        return 2

    root = Path(sys.argv[1]).resolve()
    kernel = read(root, "src/core/titan_kernel.c")
    scanner = read(root, "src/services/titan_library_scan.c")
    scheduler = read(root, "src/services/titan_storage_scheduler.c")
    video_output = read(root, "src/services/titan_video_output.c")
    cache = read(root, "src/services/titan_library_cache.c")
    notifications = read(root, "src/ui/titan_notifications.c")
    scene = read(root, "src/ui/titan_shell_scene.c")
    background = read(root, "src/ui/titan_shell_background.c")
    xmb_spline = read(root, "src/ui/titan_xmb_spline.c")
    vu_c = read(root, "src/graphics/titan_wave_vu1.c")
    video = read(root, "src/graphics/titan_video.c")
    vu_vsm = read(root, "src/graphics/vu1/titan_wave_vu1.vsm")
    makefile = read(root, "Makefile")
    catalog = read(root, "src/ui/titan_shell_catalog.c")
    busy = read(root, "src/services/titan_shell_busy.c")
    vmc = read(root, "src/services/titan_vmc.c")
    audio = read(root, "src/services/titan_audio.c")
    profile_storage = read(root, "src/services/titan_profile_storage.c")
    neutrino = read(root, "src/services/titan_neutrino.c")
    titan_ul = (root.parent / "modules/titan-ul-engine/src/opl.c").read_text(encoding="utf-8")
    titan_ul_system = (root.parent / "modules/titan-ul-engine/src/system.c").read_text(encoding="utf-8")
    package_installer = read(root, "src/services/titan_package_installer.c")
    applications = read(root, "src/services/titan_applications.c")
    font_metrics = read(root, "src/graphics/titan_font_metrics.h")
    boot_version = read(root.parent / "bootloader", "include/titan_boot_version.h")

    # Fast boot catalog: cache first; all blocking discovery runs on the
    # low-priority storage worker rather than the realtime render/audio loop.
    require("TitanLibraryScan_Initialize();" in kernel, "cache/scanner is not initialized")
    require("TitanStorageScheduler_Initialize();" in kernel, "storage scheduler is not initialized")
    require("TitanStorageScheduler_RequestLibraryScan(0)" in kernel, "background scanner is not queued")
    require("TitanLibraryScan_Update(1u);" not in kernel, "blocking scanner returned to the realtime kernel loop")
    require("TitanLibraryScan_Update(1u);" in scheduler, "storage worker does not advance scanner incrementally")
    require("TitanStorageScheduler_Worker" in scheduler and "DelayThread(TITAN_STORAGE_WORKER_IDLE_DELAY_US)" in scheduler,
            "background scanner is not isolated on a cooperative worker")
    require("TitanNeutrino_Refresh();" not in kernel, "blocking game refresh returned to boot")
    require("TitanApplications_Refresh();" not in kernel, "blocking app refresh returned to boot")
    require("TitanNeutrino_Refresh();" not in scene, "blocking game refresh remains in UI")
    require("TitanApplications_Refresh();" not in scene, "blocking app refresh remains in UI")
    require("TITAN_LIBRARY_CACHE_TEMP_PATH" in cache, "atomic cache temp path missing")
    require("TitanStorageIo_AtomicReplace" in cache, "atomic cache commit missing")
    require("previous catalog preserved" in scanner and "TitanLocalization_Compat" in scanner, "localized disconnect preservation result missing")

    # Notifications: no placeholder slogan and detailed persistent activity.
    require("Project Titan: OrbisOS experience on ps2" not in scene, "placeholder slogan remains")
    require("Project Titan - OrbisOS experience on PS2" not in scene, "placeholder slogan remains")
    require("Atualizando biblioteca em segundo plano - abra Notificacoes" in notifications,
            "Home activity ticker missing")
    require("Library Activity" in scene, "library activity category missing")
    require('"Downloads"' in scene and '"Uploads"' in scene, "transfer categories missing")
    require("activity->processed" in scene and "activity->total" in scene,
            "detailed processed/total progress missing")
    require("progress_width" in scene, "notification progress bar missing")
    require("Notification center placeholder" not in catalog,
            "notification catalog is still labelled as a placeholder")

    # Shell loading responsiveness: same PlayStation-symbol animation replaces
    # the clock, while long storage operations yield render frames.
    require("TitanShellBusy_Initialize" in kernel, "busy render pump is not initialized")
    require("TitanShellBusy_IsActive()" in scene, "header busy state is missing")
    require("TitanStorageScheduler_IsLibraryScanBusy()" in scene,
            "scheduled library work does not replace the clock")
    require("TitanShellScene_DrawShutdownSymbolCluster" in scene,
            "shutdown symbol animation is not reused in the header")
    require("g_shell_busy.callback" in busy and "g_shell_busy.depth" in busy,
            "nested busy service is incomplete")
    require("TitanShellBusy_Pulse();" in vmc, "VMC writes do not yield render frames")
    require("mcSync(1" in profile_storage, "Memory Card RPC still blocks with mcSync mode 0")
    require("TitanShellBusy_Pulse();" in profile_storage,
            "storage retries do not yield render frames")
    require("src/services/titan_shell_busy.o" in makefile,
            "busy service is not linked into the kernel")
    require("src/services/titan_storage_scheduler.o" in makefile,
            "storage scheduler is not linked into the kernel")

    # Hardware runtime I/O contract: startup animation and Draw() are not
    # allowed to synchronously walk USB/VMC data, and busy work must keep
    # realtime audio alive.
    require("!TitanShellScene_IsStartupActive()" in kernel,
            "background USB work can run during startup animation")
    busy_callback_start = kernel.index("static void TitanKernel_RenderBusyProgress")
    busy_callback_end = kernel.index("static void TitanKernel_RenderInputProgress", busy_callback_start)
    busy_callback = kernel[busy_callback_start:busy_callback_end]
    require("TitanAudio_Update();" in busy_callback and "TitanKernel_RenderFrame(context);" in busy_callback,
            "busy storage work does not pump both audio and rendering")
    require("TitanVmc_GetStatus(profile)" not in scene,
            "Draw/UI path performs synchronous VMC verification")
    require("TitanStorageScheduler_RequestProfileSave" in scene and
            "TitanStorageScheduler_RequestVmcVerify" in scene and
            "TitanStorageScheduler_RequestVmcFormat" in scene and
            "TitanStorageScheduler_RequestVmcRecreate" in scene,
            "profile/VMC storage work still bypasses the scheduler")
    require("TitanProfileStorage_Save();" not in scene and
            "TitanVmc_Verify(profile)" not in scene and
            "TitanVmc_Format(profile)" not in scene and
            "TitanVmc_Recreate(profile)" not in scene,
            "foreground storage analysis still blocks the UI thread")
    require("TitanLibraryScan_SetDeferredUi(1)" in scheduler and
            "TitanLibraryScan_CommitPendingCatalog" in scheduler,
            "worker catalog scan can mutate UI catalogs from the worker thread")
    require("TitanVmc_GetCachedStatus(profile)" in scene,
            "UI does not use cached VMC readiness")
    require("TitanVmc_Ensure(" not in profile_storage and
            "TitanVmc_RefreshStatus(" not in profile_storage,
            "profile boot/save still performs VMC filesystem I/O")
    require("FAT[0] is EOF only on an empty card" in vmc,
            "VMC validator can reject a legitimate extended root chain")
    require("TITAN_VMC_FAT_ALLOCATED_BIT" in vmc and
            "TITAN_VMC_FAT_CLUSTER_MASK" in vmc and
            "linked_cluster = value & TITAN_VMC_FAT_CLUSTER_MASK" in vmc,
            "VMC validator does not understand allocated FAT chain links")
    require("TITAN_AUDIO_MUSIC_RING_BYTES (1024u * 1024u)" in audio and
            "TITAN_AUDIO_MUSIC_RING_REFILL_LOW_WATER (768u * 1024u)" in audio and
            "TITAN_AUDIO_MUSIC_RING_REFILL_HIGH_WATER (896u * 1024u)" in audio,
            "music ring is too small for USB/HDD latency spikes")
    require("TitanAudio_StorageWorkerPumpRealtime" in scheduler and
            "TitanAudio_StorageWorkerPumpDeferred" in scheduler and
            "TitanShellBusy_SetWorkerPump" in scheduler,
            "storage worker does not own realtime/deferred audio I/O")
    update_start = audio.index("void TitanAudio_Update(void)")
    worker_start = audio.index("int TitanAudio_StorageWorkerPumpRealtime", update_start)
    realtime_audio_update = audio[update_start:worker_start]
    require("TitanStorageIo_" not in realtime_audio_update and
            "TitanAudio_FillMusicRing();" not in realtime_audio_update and
            "TitanAudio_StepClipLoad();" not in realtime_audio_update,
            "realtime audio update can still block on storage RPC")
    require("TitanAudio_StepClipLoad" in audio and
            "TITAN_AUDIO_SFX_LOAD_BUDGET (4u * 1024u)" in audio,
            "SFX still load as one blocking USB transfer")
    require("TITAN_AUDIO_STARTUP_CRITICAL_SFX_COUNT 3u" in audio and
            "TitanAudio_SetDeferredAssetIoAllowed" in audio and
            "TitanAudio_SetDeferredAssetIoAllowed(" in kernel,
            "visible startup still competes with deferred audio asset I/O")
    require('"mass%d:/"' in titan_ul and '"mass%d:"' not in titan_ul and
            '"mass0:/"' in titan_ul,
            "Titan UL still probes the hardware-invalid bare massN: root")
    for token in ("-ulname=", "-ulparts=", "-ulmedia=", "-ulcfg="):
        require(token in neutrino and token in titan_ul,
                f"Titan UL release handoff missing {token}")
    require("TITAN_UL_DEVICE_WAIT_MS" in titan_ul and
            "DelayThread(TITAN_UL_RETRY_DELAY_US)" in titan_ul,
            "Titan UL does not wait cooperatively for USB re-enumeration")
    require("sysResetWithTimeout" in titan_ul_system,
            "Titan UL IOP reset can still spin forever")
    require("detail_bottom_bleed" in scene and
            "66.0f + detail_bottom_bleed" in scene,
            "720p selected-tile backing still stops at logical y=448")
    require("TitanVideoOutput_LoadPersistentSetting();" in kernel and
            "TitanVideoOutput_SavePersistentSetting()" in scene and
            "shell_video.cfg" in video_output and
            "TitanStorageIo_AtomicReplace" in video_output,
            "confirmed 720p selection is not persisted transactionally")
    require("folder_row_alpha" in scene and
            "(1.0f - menu_transition)" in scene and
            "TitanShellScene_DrawFolderOpenRow" in scene,
            "folder preview layer is not suppressed during top-menu focus transition")

    # VU wave: real VIF1 microprogram upload/execution and PATH1 XGKICK.
    forbidden_stub = "g_wave_vu1.available = 0;\n}\n\nint TitanWaveVu1_Draw"
    require(forbidden_stub not in vu_c, "VU backend is still the dev1 stub")
    for token in (
        "DMA_CHANNEL_VIF1",
        "packet2_vif_add_micro_program",
        "packet2_utils_vu_add_unpack_data",
        "packet2_vif_mscal",
        "packet2_vif_pad128",
        "TitanVideo_FlushForPath1",
        "TitanVideo_AccountExternalQwords",
    ):
        require(token in vu_c, f"VU backend missing {token}")
    require("xgkick" in vu_vsm.lower(), "VU microprogram does not emit PATH1 packets")
    require("TitanWaveVu1_Loop" in vu_vsm and "ibne" in vu_vsm.lower(),
            "VU microprogram does not generate the strip loop")
    require("#define TITAN_XMB_SPLINE_ROW_COUNT (TITAN_WAVE_VU1_LAYER_COUNT + 1u)" in xmb_spline,
            "VU1 layer-count contract is not shared with the XMB spline")
    require("TITAN_WAVE_PARTICLE_COUNT 28u" in background,
            "particle field is not configured")
    require("TitanXmbSpline_Draw(\n        frame, transition, g_idle_blend," in background,
            "background does not submit the XMB spline")
    require("TitanWaveVu1_Draw" in xmb_spline, "XMB spline does not submit the VU wave")
    require("TitanVideo_SubmitPacket(1)" in video,
            "PATH1 handoff does not wait for a GS FINISH before VU1 XGKICK")
    require("TITAN_WAVE_VU1_MAX_TRANSIENT_FAILURES" in vu_c and
            "TitanWaveVu1_MarkTransientFailure" in vu_c,
            "VU1 backend still permanently falls back after one transient stall")
    require("TitanXmbSpline_DrawFallback" in xmb_spline, "safe renderer fallback missing")
    require("g_idle_blend" in background, "idle/Home/menu wave behaviour missing")
    require('"ACTIVE" : "FALLBACK"' in scene, "DEX VU1 runtime indicator missing")

    # Bootloader metadata must hand off to the exact RC being built.
    require('TITAN_BOOTLOADER_VERSION_STRING "0.1.18-rc15-usb-bdm-bf6"' in boot_version,
            "bootloader version metadata is stale")
    require('TITAN_BOOTLOADER_KERNEL_TARGET "0.3.93-rc17"' in boot_version,
            "bootloader kernel target is stale")
    require('TITAN_BOOTLOADER_UI_TARGET "Titan UI 0.9.35"' in boot_version,
            "bootloader UI target is stale")

    # RC8 Debug Settings/package contract: bounded transactional extraction,
    # strict validation and USB-backed Home publication must remain linked.
    require('"★debug settings"' in scene or "TITAN_STR_DEBUG_SETTINGS" in scene, "Debug Settings menu entry missing")
    require('"★ APP_HOME"' in catalog and '"★ FTP_HOME"' in catalog,
            "DEX tiles do not share the star title prefix")
    require("TitanPackageInstaller_Update(" not in kernel and
            ("TitanPackageInstaller_Update(TITAN_STORAGE_PACKAGE_BUDGET);" in scheduler or
             ("TitanPackageInstaller_Update(package_budget);" in scheduler and
              "TitanVlombisCore_GetRecommendedIoQuantum" in scheduler and
              "TITAN_STORAGE_PACKAGE_MIN_BUDGET" in scheduler and
              "TITAN_STORAGE_PACKAGE_MAX_BUDGET" in scheduler)) and
            "package_worker_parked" in scheduler and
            "TitanStorageScheduler_IsPackageWorkerParked" in scheduler,
            "package extraction lost exclusive single-owner FileXio scheduling")
    require("src/services/titan_package_installer.o" in makefile,
            "package installer service is not linked")
    for token in (
        "TITAN_PACKAGE_HEADER_CHECKSUM_OFFSET",
        "TitanPackageInstaller_RelativePathSafe",
        "TitanElf_ValidateFile",
        "TITAN_PACKAGE_PREPARE_CREATE_STAGE",
        "TitanStorageIo_Rename",
        'TITAN_STORAGE_DEVICE_ROOT "/apps"',
    ):
        require(token in package_installer, f"package installer missing {token}")
    require("TitanApplications_AddInstalledManifest" in applications,
            "installed app manifests are not catalogued")
    require("TitanApplications_GetHomeTileCount" in applications,
            "installed-app Home filtering is missing")
    require("TITAN_FONT_STAR_CODEPOINT 0x2605u" in font_metrics,
            "font atlas star mapping is missing")
    star_cell = root / "assets" / "ui" / "titan_ui_font.a8"
    font_atlas = star_cell.read_bytes()
    star_pixels = b"".join(
        font_atlas[row * 320 + 300:row * 320 + 320]
        for row in range(120, 144)
    )
    require(any(star_pixels),
            "font atlas star cell is empty")

    # Reproducible VU build: assemble, strip to raw microcode, embed aligned.
    for token in (
        "dvp-as",
        "dvp-objcopy",
        "embed_aligned_binary.py",
        "titan_wave_vu1_micro.h",
    ):
        require(token in makefile, f"VU build pipeline missing {token}")

    require("DVP_ASFLAGS ?= -32 -no-abicalls" in makefile,
            "VU assembler ABI is not pinned to O32")
    require("$(DVP_AS) $(DVP_ASFLAGS)" in makefile,
            "VU assembler flags are not applied")
    require(".DELETE_ON_ERROR:" in makefile,
            "partial VU build artifacts are not protected")

    print("Project Titan Magenta Edition release contract passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"release contract failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
