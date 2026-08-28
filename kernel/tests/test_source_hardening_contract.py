#!/usr/bin/env python3
from pathlib import Path
import re
import sys

root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]

def text(relative: str) -> str:
    return (root / relative).read_text(encoding="utf-8", errors="replace")

def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"source-hardening: FAIL: {message}")

input_c = text("kernel/src/platform/ps2/titan_input.c")
iop_c = text("kernel/src/services/titan_iop_service_manager.c")
notifications = text("kernel/src/ui/titan_notifications.c")
utf8 = text("kernel/src/services/titan_utf8.c")
boot_path = text("bootloader/src/boot_path.c")
installer = text("scripts/install_to_usb.sh")
ul_shell = text("kernel/src/services/titan_neutrino.c")
ul_opl = text("modules/titan-ul-engine/src/opl.c")
ul_system = text("modules/titan-ul-engine/src/system.c")
ul_bdm = text("modules/titan-ul-engine/src/bdmsupport.c")
ul_chain = text("modules/titan-neutrino-engine/ee/loader/src/ul_chain.c")
fhi_config = text("modules/titan-neutrino-engine/ee/loader/src/fhi_config.c")
ul_makefile = text("modules/titan-ul-engine/Makefile")
build_all = text("scripts/build_all.sh")
kernel_makefile = text("kernel/Makefile")
project_nes_makefile = text("apps/project-nes/Makefile")
project_nes_c = text("apps/project-nes/src/project_nes.c")

audio_c = text("kernel/src/services/titan_audio.c")
scheduler_c = text("kernel/src/services/titan_storage_scheduler.c")
kernel_c = text("kernel/src/core/titan_kernel.c")
package_installer_c = text("kernel/src/services/titan_package_installer.c")
app_runtime_c = text("kernel/src/services/titan_app_runtime.c")
scene_c = text("kernel/src/ui/titan_shell_scene.c")

app_assets_c = text("kernel/src/services/titan_app_assets.c")
avatars_c = text("kernel/src/ui/titan_avatars.c")
avatars_h = text("kernel/src/ui/titan_avatars.h")

require(
    "src/ui/titan_avatars.o: $(TITAN_UI_AVATAR_HEADER)" in kernel_makefile,
    "avatar renderer can compile before the generated avatar atlas header exists",
)
require(
    "generated/titan_ui_avatars_data.h" not in app_assets_c,
    "Titan App assets directly include bin2c avatar data and would duplicate its globals",
)
require(
    "TitanAvatars_CopyBaseAtlas" in app_assets_c and
    "TitanAvatars_CopyBaseAtlas" in avatars_c and
    "TitanAvatars_CopyBaseAtlas" in avatars_h,
    "Titan App atlas must reuse the single avatar-data owner through TitanAvatars",
)
require(
    len(re.findall(r"^fallback-system-assets:\s*$", kernel_makefile, re.MULTILINE)) == 1,
    "kernel Makefile defines fallback-system-assets more than once",
)

require(
    "check-ee-compiler" in project_nes_makefile and
    "mips64r5900el-ps2-elf" in project_nes_makefile and
    "-march=r5900 -mabi=n32 -mhard-float -msingle-float" in project_nes_makefile and
    "-mabi=32" not in project_nes_makefile and
    'EE_CC="$PS2DEV/ee/bin/mips64r5900el-ps2-elf-gcc"' in build_all and
    "apps/project-nes/build" in build_all,
    "Project Nes hardware package can regress away from the PS2 EE/R5900 N32 toolchain",
)
require(
    "library_probe_pending" in project_nes_c and
    "g_pn.library_probe_pending = 1u;" in project_nes_c,
    "Project Nes init lost deferred app-filesystem probing",
)
require(
    "static ProjectNesState g_pn;" in project_nes_c and
    "g_pn_state" not in project_nes_c and
    "g_pn.nes_frame = g_pn_nes_frame;" in project_nes_c and
    "g_pn.media_frame = g_pn_media_frame;" in project_nes_c and
    "g_pn.audio_buffer = g_pn_audio_buffer;" in project_nes_c,
    "Project Nes regressed to Titan heap allocation during protected init",
)
project_nes_init = project_nes_c[
    project_nes_c.index("int titan_app_init("):
    project_nes_c.index("void titan_app_update(", project_nes_c.index("int titan_app_init("))
]
require(
    "api->alloc(" not in project_nes_init and
    "api->release(" not in project_nes_init and
    "api->notify(" not in project_nes_init and
    "api->file_" not in project_nes_init and
    "api->media_" not in project_nes_init,
    "Project Nes performs blocking/resource Titan API work inside titan_app_init",
)
require(
    "TitanAppRuntime_ServiceInitRequest" not in app_runtime_c and
    "TitanAppRuntime_BeginInitRequest" not in app_runtime_c and
    "TitanAppRuntime_ShouldBridgeInitCall" not in app_runtime_c and
    "Titan App tentou alocar memoria durante titan_app_init" in app_runtime_c and
    "Titan App tentou ler storage durante titan_app_init" in app_runtime_c and
    "Titan App tentou iniciar audio durante titan_app_init" in app_runtime_c,
    "Titan App init regained a cross-thread resource bridge",
)
require(
    "TITAN_TMOD_EF_MIPS_MACH_5900" in app_runtime_c and
    "TITAN_TMOD_EF_MIPS_ABI2) == 0u" in app_runtime_c and
    ("ABI N32 compativel" in app_runtime_c or "N32/ABI2" in app_runtime_c) and
    "TitanAppRuntime_ValidateRuntimeModuleFile" in app_runtime_c and
    "TitanTmod_ValidateR5900ExecutableCode" in app_runtime_c and
    "opcode == 0x35u || opcode == 0x3du" in app_runtime_c and
    "TITAN_APP_INIT_TIMEOUT_MS 15000u" in app_runtime_c and
    "TitanAppRuntime_StartInitThread" in app_runtime_c and
    "TitanAppRuntime_OwnsShell" in scene_c and
    "TitanAppRuntime_ValidateRuntimeModuleFile(entrypoint)" in package_installer_c,
    "Titan-native runtime lost R5900 execution guard, init watchdog, or install-time relocation validation",
)

# All steady-state fileXio work must stay off the render/audio thread. PS2SDK
# serializes fileXio RPC calls, so a main-thread music read can otherwise park
# the entire shell behind a low-priority USB scan.
audio_update = audio_c[audio_c.index("void TitanAudio_Update(void)"):audio_c.index("int TitanAudio_StorageWorkerPumpRealtime")]
require("TitanStorageIo_" not in audio_update and "TitanAudio_FillMusicRing();" not in audio_update,
        "realtime TitanAudio_Update performs storage I/O")
require("TitanAudio_StorageWorkerPumpRealtime" in scheduler_c and
        "TitanShellBusy_SetWorkerPump" in scheduler_c,
        "storage worker does not service realtime audio at cooperative boundaries")
require("TitanPackageInstaller_Update(" not in kernel_c,
        "package extraction still executes on the realtime kernel loop")
package_cancel = package_installer_c[
    package_installer_c.index("void TitanPackageInstaller_Cancel(void)"):
    package_installer_c.index("int TitanPackageInstaller_IsActive",
                              package_installer_c.index("void TitanPackageInstaller_Cancel(void)"))
]
require("TitanPackageInstaller_Fail(" not in package_cancel and
        "TitanStorageIo_" not in package_cancel and
        "cancel_requested = 1u" in package_cancel,
        "package cancellation performs file I/O from the UI thread")
require("TitanAudio_StorageWorkerNeedsRealtime" in scheduler_c and
        "TITAN_STORAGE_SCAN_DELAY_US 250u" in scheduler_c and
        "TITAN_STORAGE_PACKAGE_BUDGET (4u * 1024u)" in scheduler_c,
        "storage lane lost bounded scheduling between audio, package, and scan")
worker_body = scheduler_c[
    scheduler_c.index("static void TitanStorageScheduler_Worker(void *argument)"):
    scheduler_c.index("#endif", scheduler_c.index("static void TitanStorageScheduler_Worker(void *argument)"))
]
main_update = scheduler_c[
    scheduler_c.index("void TitanStorageScheduler_UpdateMainThread(void)"):
    scheduler_c.index("int TitanStorageScheduler_RequestLibraryScan",
                      scheduler_c.index("void TitanStorageScheduler_UpdateMainThread(void)"))
]
require("TitanPackageInstaller_Update(" not in worker_body and
        ("TitanPackageInstaller_Update(TITAN_STORAGE_PACKAGE_BUDGET);" in main_update or
         ("TitanPackageInstaller_Update(package_budget);" in main_update and
          "TitanVlombisCore_GetRecommendedIoQuantum" in main_update and
          "TITAN_STORAGE_PACKAGE_MIN_BUDGET" in scheduler_c and
          "TITAN_STORAGE_PACKAGE_MAX_BUDGET" in scheduler_c)) and
        "package_worker_parked" in scheduler_c and
        "TitanStorageScheduler_IsPackageWorkerParked" in scheduler_c,
        "package installer lost deterministic single-owner FileXio handoff")
require("package_exclusive_mode" in scheduler_c and
        "TitanStorageScheduler_SetPackageExclusiveMode" in scheduler_c,
        "package storage exclusivity does not survive the completed/cancelled result screen")
require("app_exclusive_mode" in scheduler_c and
        "TitanStorageScheduler_SetTitanAppExclusiveMode" in scheduler_c and
        "TitanStorageScheduler_IsTitanAppWorkerParked" in scheduler_c and
        "TitanStorageScheduler_SetTitanAppExclusiveMode(1)" in app_runtime_c,
        "Titan-native runtime lost single-owner storage handoff")
require("TitanStorageIo_Flush" not in package_installer_c,
        "package staging still forces synchronous per-file flushes")
require("TITAN_PACKAGE_PHASE_PREPARING" in package_installer_c and
        "package_headers[TITAN_PACKAGE_CAPACITY]" in package_installer_c and
        "package_entries[TITAN_PACKAGE_CAPACITY][TITAN_PACKAGE_MAX_FILES]" in package_installer_c and
        "package_plan_valid" in package_installer_c and
        "g_package_installer.prepare_step = TITAN_PACKAGE_PREPARE_BUILD_PATHS;" in package_installer_c and
        "TITAN_PACKAGE_PREPARE_CREATE_STAGE" in package_installer_c and
        "TitanPackageInstaller_RemoveTree" not in package_installer_c,
        "package preparation lost cached plan or fresh transactional staging")
package_update = package_installer_c[
    package_installer_c.index("void TitanPackageInstaller_Update(unsigned int byte_budget)"):
    package_installer_c.index("void TitanPackageInstaller_Cancel(void)")
]
require("TitanStorageIo_ReadExact" not in package_update and
        "TitanStorageIo_WriteExact" not in package_update and
        "TitanStorageIo_ReadSome" in package_update and
        "TitanStorageIo_WriteSome" in package_update,
        "package extraction hides multiple storage transfers inside one scheduler quantum")
require("(g_audio.music_requested && TitanAudio_MusicBufferedBytes() > 0u)" in audio_c,
        "stopped package music still drains its buffered playback position")

require("SifIopReset" not in input_c and "SifIopSync" not in input_c,
        "input still owns IOP reset/sync")
require("TitanIopServiceManager_Initialize" in input_c,
        "input does not depend on the IOP service manager")
require("TITAN_IOP_RESET_MAX_ATTEMPTS" in iop_c and "TITAN_IOP_SYNC_MAX_ATTEMPTS" in iop_c,
        "IOP bootstrap is not bounded")
require("TitanIopServiceManager_Shutdown" in iop_c,
        "IOP manager has no lifecycle shutdown")

for token in ("TitanNotifications_Priority", "CardMatches", "dropped_count",
              "TITAN_NOTIFICATION_ENTER_MS", "TitanTime_NowMilliseconds"):
    require(token in notifications, f"notification hardening missing {token}")
require("TITAN_NOTIFICATION_ENTER_FRAMES" not in notifications,
        "notification lifetime still uses frames")

for token in ("0x10FFFFu", "0xD800u", "first == 0xE0u", "first == 0xF4u",
              "TITAN_UTF8_REPLACEMENT_CODEPOINT"):
    require(token in utf8, f"strict UTF-8 rule missing {token}")

canonical = boot_path.index('"mass:/kernel.elf"')
legacy = boot_path.index('"kernel/kernel.elf"')
require(canonical < legacy, "canonical root kernel is not prioritized")
require("TITAN/kernel.elf is never installed" in installer,
        "installer does not declare the canonical kernel contract")
require("legacy-disabled" in installer and "sha256sum -c" in installer,
        "installer lacks legacy disable or hash verification")
require(".install-transactions" in installer and "recover_journal" in installer,
        "installer lacks interrupted-transaction recovery")

require("-vmc0=" in ul_shell and "titan_ul_engine.elf" in ul_shell,
        "Titan UL Engine launch lacks canonical binary or VMC argument")
require("gTitanAutoVmcPath" in ul_opl and "-vmc0=" in ul_opl,
        "Titan UL Engine does not parse the VMC override")
for token in ("-ulname=", "-ulparts=", "-ulmedia=", "-ulcfg="):
    require(token in ul_shell and token in ul_opl,
            f"Titan UL metadata handoff missing {token}")
require("titanULMiniInit();" in ul_opl and
        ul_opl.index("titanULMiniInit();") < ul_opl.index("titanULParseLaunchArguments(argc, argv, &metadata)"),
        "Titan UL parses VMC/metadata before setDefaults clears the overrides")
ul_auto = ul_opl[ul_opl.index("static int autoLaunchULGame("):ul_opl.index("static void titanULRecoverFromFailure", ul_opl.index("static int autoLaunchULGame("))]
require("delay(" not in ul_auto and "DelayThread(TITAN_UL_RETRY_DELAY_US)" in ul_opl,
        "Titan UL autolaunch still uses CPU busy-spin waits")
require("sysResetWithTimeout" in ul_system and "sysIopSyncWait" in ul_system and
        "DelayThread(1000)" in ul_system,
        "Titan UL IOP reset/sync path is not bounded/cooperative")
require("continuing without VMC" in ul_bdm and
        "memset(&bdm_vmc_infos, 0, sizeof(bdm_vmc_infos))" in ul_bdm,
        "Titan UL headless VMC failure still hard-aborts game launch")
require("if (index < 0)" in ul_bdm and "titanAutoLaunch" in ul_bdm,
        "Titan UL headless BDM errors are not guarded")
require("sysCheckVMCPath" in ul_system and "gTitanAutoVmcPath" in ul_bdm,
        "Titan UL Engine does not validate/apply the explicit VMC")
require("UlChain_ReadExact" in ul_chain and "DelayThread(10000)" in ul_chain,
        "Neutrino UL chain still uses spin/single-read I/O")
require("UINT64_MAX - chain->total_size" in ul_chain,
        "Neutrino UL total size lacks overflow protection")
require("#include <delaythread.h>" in fhi_config,
        "Neutrino FHI retry lacks the PS2SDK DelayThread declaration")
require(re.search(
    r"^\$\(TRANSLATIONS_YML\):\s+%\.yml:.*\|\s+download_lng\s*$",
    ul_makefile,
    re.MULTILINE,
), "Titan UL translation YAML updates can race the language checkout")
require(re.search(
    r"^\$\(ENGLISH_TEMPLATE_YML\):.*\|\s+download_lng\s*$",
    ul_makefile,
    re.MULTILINE,
), "Titan UL English template generation can race the language checkout")

cdvdman_serial_edges = (
    "modules/iopcore/cdvdman/bdm_ata_cdvdman.irx: | modules/iopcore/cdvdman/bdm_cdvdman.irx",
    "modules/iopcore/cdvdman/smb_cdvdman.irx: | modules/iopcore/cdvdman/bdm_ata_cdvdman.irx",
    "modules/iopcore/cdvdman/hdd_cdvdman.irx: | modules/iopcore/cdvdman/smb_cdvdman.irx",
    "modules/iopcore/cdvdman/hdd_hdpro_cdvdman.irx: | modules/iopcore/cdvdman/hdd_cdvdman.irx",
)
for edge in cdvdman_serial_edges:
    require(edge in ul_makefile,
            "Titan UL cdvdman variants can race shared isofs objects")
require("make --output-sync=recurse" in text("scripts/build_titan_ul_engine.sh"),
        "Titan UL recursive build output can hide the first failing target")
require(re.search(
    r"^modules/network/SMSTCPIP/SMSTCPIP\.irx:.*?\n"
    r"(?:\t#.*\n)*"
    r"\t\$\(MAKE\) \$\(SMSTCPIP_INGAME_CFLAGS\) -C \$< all\s*$",
    ul_makefile,
    re.MULTILINE,
), "Titan UL SMSTCPIP still invokes parallel-unsafe rebuild: clean all")
require(not re.search(
    r"\$\(MAKE\).*SMSTCPIP_INGAME_CFLAGS.*\brebuild\b",
    ul_makefile,
), "Titan UL SMSTCPIP can race its clean and all prerequisites")

loader_rebuild = build_all.index(
    'make -C "$work/modules/titan-neutrino-engine/ee/loader" clean all'
)
module_collection = build_all.index(
    'make -C "$work/modules/titan-neutrino-engine" copy',
    loader_rebuild,
)
require(loader_rebuild < module_collection,
        "Neutrino modules are collected before the final loader clean")
require('compgen -G "$neutrino_loader/modules/*.irx"' in build_all,
        "Neutrino runtime packaging does not validate its IRX payload")

# Production generated blobs and backup attempts must not be tracked. Host stubs
# are test fixtures and are intentionally allowed.
tracked = []
try:
    import subprocess
    tracked = subprocess.check_output(
        ["git", "-C", str(root), "ls-files"],
        text=True,
        stderr=subprocess.DEVNULL,
    ).splitlines()
except Exception:
    pass
for path in tracked:
    if path.startswith(("bootloader/generated/", "bootloader/stage2/generated/",
                        "kernel/src/graphics/generated/", "kernel/src/services/generated/")):
        require(False, f"generated production artifact is tracked: {path}")
    if re.search(r"(?:\.bak|\.orig|\.rej|working|before-minimal|baseline-vu1)", path):
        require(False, f"backup attempt is tracked: {path}")
    if path.startswith(("reference/deployed-", "runtime/neutrino/")):
        require(False, f"duplicate deployed runtime is tracked: {path}")

asset_generator = text("kernel/tools/generate_system_assets.py")
asset_verifier = text("kernel/tools/verify_curated_system_assets.py")
require("refuses to overwrite curated" in asset_generator and
        "--force-curated-overwrite" in asset_generator,
        "fallback asset generator can overwrite curated UI assets")
require("CURATED_ASSET_SHA256SUMS" in asset_verifier and
        "hashlib.sha256" in asset_verifier,
        "curated UI asset integrity verifier is missing")
require(text("VERSION").strip() == "0.3.91-rc15",
        "release version is not synchronized")

print("source-hardening: OK")
