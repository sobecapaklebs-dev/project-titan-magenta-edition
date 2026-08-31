.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

EE_BIN = kernel.elf

BIN2C ?= $(PS2SDK)/bin/bin2c
PYTHON ?= python3
DVP_AS ?= $(PS2DEV)/dvp/bin/dvp-as
DVP_ASFLAGS ?= -32 -no-abicalls
DVP_OBJCOPY ?= $(PS2DEV)/dvp/bin/dvp-objcopy
DVP_OBJCOPYFLAGS ?= -O binary
TITAN_FONT_SOURCE ?=
TITAN_AR9271_FW ?=

# BF30 integrated FMCB: upstream PerformInstallation/SignKELF is linked into
# titan-installer/kernel.elf. No standalone FMCBInstaller.elf is launched.
TITAN_FMCB_INTEGRATED_DIR = ../third_party/fmcb/integrated
TITAN_FMCB_INTEGRATED_LIB = $(TITAN_FMCB_INTEGRATED_DIR)/build/libtitanfmcb_integrated.a

TITAN_TLS_ROOT = third_party/.titan-tls
TITAN_BEARSSL_DIR = $(TITAN_TLS_ROOT)/bearssl-0.6
TITAN_BEARSSL_LIB = $(TITAN_TLS_ROOT)/lib/libbearssl.a
TITAN_TLS_CA = src/services/generated/titan_tls_ca_bundle.inc
TITAN_TLS_MARKER = $(TITAN_TLS_ROOT)/ready
TITAN_TLS_TOOL = tools/titan_tls_deps.py
TITAN_IMAGE_DEPS_MARKER = $(TITAN_TLS_ROOT)/image-deps-ready
TITAN_PORTS_INCLUDE ?= $(PS2SDK)/ports/include
TITAN_PORTS_LIB ?= $(PS2SDK)/ports/lib

TITAN_WAVE_VU1_SOURCE = src/graphics/vu1/titan_wave_vu1.vsm
TITAN_WAVE_VU1_DVP_OBJECT = src/graphics/vu1/titan_wave_vu1_micro.dvp.o
TITAN_WAVE_VU1_BINARY = src/graphics/vu1/titan_wave_vu1_micro.bin
TITAN_WAVE_VU1_HEADER = src/graphics/generated/titan_wave_vu1_micro.h
TITAN_WAVE_VU1_EMBED_TOOL = tools/embed_aligned_binary.py

# P162 native Titan Game geometry: VU1 owns perspective/viewport/STQ/XGKICK.
TITAN_GAME_VU1_SOURCE = src/graphics/vu1/titan_game_vu1.vsm
TITAN_GAME_VU1_DVP_OBJECT = src/graphics/vu1/titan_game_vu1_micro.dvp.o
TITAN_GAME_VU1_BINARY = src/graphics/vu1/titan_game_vu1_micro.bin
TITAN_GAME_VU1_HEADER = src/graphics/generated/titan_game_vu1_micro.h
TITAN_GAME_VU1_EXTRACT_TOOL = tools/extract_vu_micro.py

TITAN_WAVE_TEXTURE_RAW = assets/ui/titan_wave_silk.rgba32
TITAN_WAVE_TEXTURE_HEADER = src/graphics/generated/titan_wave_silk_texture.h

TITAN_LAUNCH_STAGE2_ELF = \
../bootloader/stage2/stage2.elf

TITAN_LAUNCH_STAGE2_HEADER = \
src/services/generated/titan_launch_stage2_elf.h

TITAN_UI_FONT_RAW = \
assets/ui/titan_ui_font.a8

TITAN_UI_FONT_HEADER = \
src/graphics/generated/titan_ui_font_data.h

TITAN_UI_ICON_RAW = \
assets/ui/titan_ui_icons.a8

TITAN_UI_ICON_HEADER = \
src/graphics/generated/titan_ui_icons_data.h

TITAN_UI_GAME_PLACEHOLDER_RAW = \
assets/ui/titan_ui_game_placeholder.a8

TITAN_UI_GAME_PLACEHOLDER_HEADER = \
src/graphics/generated/titan_ui_game_placeholder_data.h

TITAN_UI_ALERT_RAW = \
assets/ui/titan_ui_alert.a8

TITAN_UI_ALERT_HEADER = \
src/graphics/generated/titan_ui_alert_data.h

TITAN_UI_APP_HOME_RAW = \
assets/ui/titan_ui_app_home.a8

TITAN_UI_APP_HOME_HEADER = \
src/graphics/generated/titan_ui_app_home_data.h

TITAN_UI_FTP_HOME_RAW = \
assets/ui/titan_ui_ftp_home.a8

TITAN_UI_FTP_HOME_HEADER = \
src/graphics/generated/titan_ui_ftp_home_data.h

TITAN_UI_POWER_PS2SLIM_RAW = \
assets/ui/titan_ui_power_ps2slim.a8

TITAN_UI_POWER_PS2SLIM_HEADER = \
src/graphics/generated/titan_ui_power_ps2slim_data.h

TITAN_UI_BOOT_PS2LOGO_RAW = \
assets/ui/titan_ui_boot_ps2logo.a8

TITAN_UI_BOOT_PS2LOGO_HEADER = \
src/graphics/generated/titan_ui_boot_ps2logo_data.h

TITAN_INSTALLER_FAT_RAW = \
assets/ui/installer/titan_installer_fat.a8

TITAN_INSTALLER_FAT_HEADER = \
src/graphics/generated/titan_installer_fat_data.h

TITAN_INSTALLER_SLIM70_RAW = \
assets/ui/installer/titan_installer_slim70.a8

TITAN_INSTALLER_SLIM70_HEADER = \
src/graphics/generated/titan_installer_slim70_data.h

TITAN_INSTALLER_SLIM90_RAW = \
assets/ui/installer/titan_installer_slim90.a8

TITAN_INSTALLER_SLIM90_HEADER = \
src/graphics/generated/titan_installer_slim90_data.h

TITAN_UI_AVATAR_RAW = \
assets/ui/titan_ui_avatars.psm16

TITAN_UI_AVATAR_HEADER = \
src/graphics/generated/titan_ui_avatars_data.h

TITAN_USBD_IRX = \
$(PS2SDK)/iop/irx/usbd.irx

TITAN_USBD_HEADER = \
src/services/generated/titan_usbd_irx.h

# BF28 Early Input: build the PADEMU transports from the same frozen tree and
# embed them in the kernel; no executor-specific paths.
TITAN_SMSUTILS_IRX = ../modules/titan-ul-engine/modules/network/SMSUTILS/SMSUTILS.irx
TITAN_SMSUTILS_HEADER = src/services/generated/titan_smsutils_irx.h
TITAN_USB_PADEMU_IRX = ../modules/titan-ul-engine/modules/pademu/usb_pademu.irx
TITAN_USB_PADEMU_HEADER = src/services/generated/titan_usb_pademu_irx.h
TITAN_BT_PADEMU_IRX = ../modules/titan-ul-engine/modules/pademu/bt_pademu.irx
TITAN_BT_PADEMU_HEADER = src/services/generated/titan_bt_pademu_irx.h
TITAN_USBKBD_IRX = $(PS2SDK)/iop/irx/ps2kbd.irx
TITAN_USBKBD_HEADER = src/services/generated/titan_usbkbd_irx.h
TITAN_USBMOUSE_IRX = $(PS2SDK)/iop/irx/ps2mouse.irx
TITAN_USBMOUSE_HEADER = src/services/generated/titan_usbmouse_irx.h

TITAN_USBHDFSD_IRX = \
$(PS2SDK)/iop/irx/usbhdfsd.irx

TITAN_USBHDFSD_HEADER = \
src/services/generated/titan_usbhdfsd_irx.h

TITAN_BDM_IRX = \
$(PS2SDK)/iop/irx/bdm.irx

TITAN_BDM_HEADER = \
src/services/generated/titan_bdm_irx.h

TITAN_BDMFS_FATFS_IRX = \
$(PS2SDK)/iop/irx/bdmfs_fatfs.irx

TITAN_BDMFS_FATFS_HEADER = \
src/services/generated/titan_bdmfs_fatfs_irx.h

TITAN_USBMASS_BD_IRX = \
$(PS2SDK)/iop/irx/usbmass_bd.irx

TITAN_USBMASS_BD_HEADER = \
src/services/generated/titan_usbmass_bd_irx.h

TITAN_IOMANX_IRX = \
$(PS2SDK)/iop/irx/iomanX.irx

TITAN_IOMANX_HEADER = \
src/services/generated/titan_iomanx_irx.h

TITAN_FILEXIO_IRX = \
$(PS2SDK)/iop/irx/fileXio.irx

TITAN_FILEXIO_HEADER = \
src/services/generated/titan_filexio_irx.h

TITAN_AUDSRV_IRX = \
$(PS2SDK)/iop/irx/audsrv.irx

TITAN_AUDSRV_HEADER = \
src/services/generated/titan_audsrv_irx.h

TITAN_POWEROFF_IRX = \
$(PS2SDK)/iop/irx/poweroff.irx

TITAN_POWEROFF_HEADER = \
src/services/generated/titan_poweroff_irx.h

TITAN_PS2DEV9_IRX = \
$(PS2SDK)/iop/irx/ps2dev9.irx
TITAN_PS2DEV9_HEADER = \
src/services/generated/titan_ps2dev9_irx.h

TITAN_PS2ATAD_IRX = \
$(PS2SDK)/iop/irx/ps2atad.irx
TITAN_PS2ATAD_HEADER = \
src/services/generated/titan_ps2atad_irx.h
TITAN_PS2HDD_IRX = \
$(PS2SDK)/iop/irx/ps2hdd.irx
TITAN_PS2HDD_HEADER = \
src/services/generated/titan_ps2hdd_irx.h
TITAN_PS2FS_IRX = \
$(PS2SDK)/iop/irx/ps2fs.irx
TITAN_PS2FS_HEADER = \
src/services/generated/titan_ps2fs_irx.h

TITAN_PFS_IRX_READY := $(and $(wildcard $(TITAN_PS2ATAD_IRX)),$(wildcard $(TITAN_PS2HDD_IRX)),$(wildcard $(TITAN_PS2FS_IRX)))
ifneq ($(strip $(TITAN_PFS_IRX_READY)),)
EE_CFLAGS += -DTITAN_HAVE_PFS_EMBEDDED=1
TITAN_PFS_EMBED_HEADERS = $(TITAN_PS2ATAD_HEADER) $(TITAN_PS2HDD_HEADER) $(TITAN_PS2FS_HEADER)
else
TITAN_PFS_EMBED_HEADERS =
endif
TITAN_NETMAN_IRX = \
$(PS2SDK)/iop/irx/netman.irx
TITAN_NETMAN_HEADER = \
src/services/generated/titan_netman_irx.h
TITAN_SMAP_SOURCE_IRX = \
$(PS2SDK)/iop/irx/smap.irx
TITAN_SMAP_PATCH_TOOL = \
tools/titan_patch_smap_netif_name.py
TITAN_SMAP_IRX = \
src/services/generated/titan_smap_netif.irx
TITAN_SMAP_HEADER = \
src/services/generated/titan_smap_irx.h
TITAN_AR9271_IRX = iop/titan_ar9271/titan_ar9271.irx
TITAN_AR9271_HEADER = src/services/generated/titan_ar9271_irx.h

EE_OBJS = \
main.o \
src/core/titan_context.o \
src/core/titan_kernel.o \
src/services/titan_user_profile.o \
src/services/titan_profile_snapshot.o \
src/services/titan_profile_storage.o \
src/services/titan_iop_modules.o \
src/services/titan_storage_io.o \
src/services/titan_storage_mount.o \
src/services/titan_storage_pfs_iop.o \
src/services/titan_storage_pfs_provider_ps2.o \
src/services/titan_storage_scheduler.o \
src/services/titan_storage_inventory.o \
src/services/titan_vlombiscore.o \
src/services/titan_audio.o \
src/services/titan_iop_service_manager.o \
src/services/titan_time.o \
src/services/titan_datetime.o \
src/services/titan_localization.o \
src/services/titan_platform_overhead.o \
src/services/titan_utf8.o \
src/services/titan_shell_busy.o \
src/services/titan_vmc.o \
src/services/titan_library_scan.o \
src/services/titan_library_cache.o \
src/services/titan_package_installer.o \
src/services/titan_network.o \
src/services/titan_diagnostics.o \
src/services/titan_health_trace.o \
src/services/titan_target_memory.o \
src/services/titan_system_update.o \
src/services/titan_home_folders.o \
src/services/titan_shell_layout.o \
src/services/titan_applications.o \
src/services/titan_sas.o \
src/services/titan_app_runtime.o \
src/services/titan_game_runtime.o \
src/services/titan_session_service.o \
src/services/titan_igr_compatibility.o \
src/services/titan_ingame_overlay.o \
src/services/titan_unified_input.o \
src/services/titan_controller_assignment.o \
src/services/titan_pademu_input_bridge.o \
src/services/titan_bluetooth_runtime.o \
src/services/titan_wireless_accessories.o \
src/services/titan_controller_ux.o \
src/services/titan_early_input.o \
src/services/titan_hid_input.o \
src/services/titan_modern_gamepad_protocol.o \
src/services/titan_app_assets.o \
src/services/titan_http_client.o \
src/services/titan_network_client.o \
src/services/titan_sha256.o \
src/services/titan_download_service.o \
src/services/titan_repository_protocol.o \
src/services/titan_store_service.o \
src/services/titan_disc_service.o \
	src/services/titan_disc_engine.o \
src/services/titan_network_stream.o \
src/services/titan_ftp_service.o \
src/services/titan_app_network.o \
src/services/titan_asset_image.o \
src/services/titan_asset_service.o \
src/services/titan_browser.o \
src/services/titan_browser_net.o \
src/services/titan_elf_validate.o \
src/services/titan_neutrino.o \
src/services/titan_game_patches.o \
src/services/titan_game_gsm.o \
src/services/titan_exec_handoff.o \
src/services/titan_video_output.o \
src/services/titan_theme_scene_graph.o \
src/services/titan_theme_v4_runtime.o \
	src/services/titan_theme_lifecycle.o \
	src/services/titan_theme.o \
src/services/titan_system_info.o \
src/services/titan_dex.o \
src/installer/titan_install_container.o \
src/installer/titan_installer_model.o \
src/installer/titan_installer_diag.o \
src/installer/titan_fmcb_backend.o \
src/installer/titan_installer.o \
src/platform/ps2/titan_platform.o \
src/platform/ps2/titan_platform_pfs.o \
src/platform/ps2/titan_input.o \
src/platform/ps2/titan_system_clock.o \
src/graphics/titan_video.o \
src/graphics/titan_video_game_texture.o \
src/graphics/titan_pikavr.o \
src/graphics/titan_video_mode.o \
src/graphics/titan_video_memory.o \
src/graphics/titan_boot_recovery.o \
src/graphics/titan_vram_boot_guard.o \
src/graphics/titan_video_720p_plan.o \
src/graphics/titan_video_720p_transaction.o \
src/graphics/titan_wave_vu1.o \
src/graphics/titan_game_vu1.o \
src/graphics/titan_font.o \
src/ui/titan_animation.o \
src/ui/titan_xmb_spline.o \
src/ui/titan_icons.o \
src/ui/titan_ui_assets.o \
src/ui/titan_shell_catalog.o \
src/ui/titan_shell_background.o \
src/ui/titan_avatars.o \
src/ui/titan_avatar_picker.o \
src/ui/titan_text_input.o \
src/ui/titan_notifications.o \
src/ui/titan_system_error.o \
src/ui/titan_ui_layout_guard.o \
	src/ui/titan_ui_transition.o \
	src/ui/titan_shell_scene.o

EE_INCS += \
-Iinclude \
-I../modules/titan-ul-engine/include \
-Isrc/core \
-Isrc/services \
-Isrc/platform/ps2 \
-Isrc/graphics \
-Isrc/ui \
-Isrc/installer \
-I$(TITAN_FMCB_INTEGRATED_DIR) \
-I$(TITAN_PORTS_INCLUDE) \
-I$(TITAN_BEARSSL_DIR)/inc

EE_CFLAGS += \
-std=gnu99 \
-Wall \
-Wextra \
-Werror=implicit-function-declaration

EE_CFLAGS += -DPROJECT_TITAN_WIDESCREEN=1

# Reproducible release builds run from independent mktemp source roots.
# PS2SDK enables DWARF on EE builds, so normalize source/debug paths instead
# of letting DW_AT_comp_dir make kernel.elf depend on the temporary directory.
TITAN_REPRO_SOURCE_ROOT ?= $(abspath ..)
TITAN_REPRO_CANONICAL_ROOT ?= /project-titan
EE_CFLAGS += \
-ffile-prefix-map="$(TITAN_REPRO_SOURCE_ROOT)=$(TITAN_REPRO_CANONICAL_ROOT)" \
-fdebug-prefix-map="$(TITAN_REPRO_SOURCE_ROOT)=$(TITAN_REPRO_CANONICAL_ROOT)"

# GCC LTO creates an artificial DWARF compilation unit during the final link
# (producer: GNU GIMPLE ... -fltrans).  EE_CFLAGS normalize first-party CUs,
# but the LTRANS CU is generated by the link driver and otherwise records the
# mktemp kernel cwd in DW_AT_comp_dir.  Feed the same maps to the link/LTO
# stage so the final debug-bearing kernel.elf is reproducible without stripping.
EE_LDFLAGS += \
-ffile-prefix-map="$(TITAN_REPRO_SOURCE_ROOT)=$(TITAN_REPRO_CANONICAL_ROOT)" \
-fdebug-prefix-map="$(TITAN_REPRO_SOURCE_ROOT)=$(TITAN_REPRO_CANONICAL_ROOT)"

TITAN_VIDEO_BACKEND ?= gs

ifeq ($(TITAN_VIDEO_BACKEND),software)
EE_CFLAGS += -DTITAN_VIDEO_RENDERER_SOFTWARE=1
else
EE_CFLAGS += -DTITAN_VIDEO_RENDERER_GS=1
endif

EE_LDFLAGS += -L$(TITAN_PORTS_LIB) -L$(TITAN_TLS_ROOT)/lib -Wl,--gc-sections

EE_LIBS += \
$(TITAN_FMCB_INTEGRATED_LIB) \
-ldebug \
-lcdvd \
-lpad \
-ldraw \
-lgraph \
-lpacket2 \
-lfileXio \
-lpng \
-lz \
-ljpeg \
-lbearssl \
-lps2ip \
-lnetman \
-lmc \
-lpatches \
-liopreboot \
-ldma \
-lpwroff \
-laudsrv \
-lm

$(TITAN_TLS_CA): $(TITAN_TLS_TOOL)
	@mkdir -p $(dir $@) $(TITAN_TLS_ROOT)
	$(PYTHON) $(TITAN_TLS_TOOL) \
		--root "$(TITAN_TLS_ROOT)" \
		--ca-output "$@" \
		--marker "$(TITAN_TLS_MARKER)"
	@test -s "$@" || (echo "ERRO: bundle TLS Titan nao foi gerado" && exit 1)

$(TITAN_TLS_MARKER): $(TITAN_TLS_CA)
	@test -s "$@" || (echo "ERRO: marcador TLS Titan ausente" && exit 1)

$(TITAN_FMCB_INTEGRATED_LIB):
	$(MAKE) -C $(TITAN_FMCB_INTEGRATED_DIR)
	@test -s "$@" || (echo "ERRO: backend FMCB integrado nao foi gerado" && exit 1)

$(TITAN_BEARSSL_LIB): $(TITAN_TLS_CA) third_party/bearssl_ps2/Makefile
	$(MAKE) -C third_party/bearssl_ps2 \
		SRC_ROOT="$(abspath $(TITAN_BEARSSL_DIR))" \
		OUT="$(abspath $@)" \
		EE_CC="$(EE_CC)" \
		EE_AR="$(EE_AR)"
	@test -s "$@" || (echo "ERRO: BearSSL PS2 nao foi gerado" && exit 1)

$(TITAN_IMAGE_DEPS_MARKER):
	@test -f "$(TITAN_PORTS_INCLUDE)/png.h" || \
		(echo "ERRO: libpng PS2 ausente em $(TITAN_PORTS_INCLUDE). Instale ps2sdk-ports." && exit 1)
	@test -f "$(TITAN_PORTS_INCLUDE)/jpeglib.h" || \
		(echo "ERRO: libjpeg PS2 ausente em $(TITAN_PORTS_INCLUDE). Instale ps2sdk-ports." && exit 1)
	@test -f "$(TITAN_PORTS_LIB)/libpng.a" || \
		(echo "ERRO: libpng.a PS2 ausente em $(TITAN_PORTS_LIB). Instale ps2sdk-ports." && exit 1)
	@test -f "$(TITAN_PORTS_LIB)/libjpeg.a" || \
		(echo "ERRO: libjpeg.a PS2 ausente em $(TITAN_PORTS_LIB). Instale ps2sdk-ports." && exit 1)
	@test -f "$(TITAN_PORTS_LIB)/libz.a" || \
		(echo "ERRO: libz.a PS2 ausente em $(TITAN_PORTS_LIB). Instale ps2sdk-ports." && exit 1)
	@mkdir -p $(dir $@)
	@printf 'Project Titan image dependencies ready\n' > "$@"

src/services/titan_http_client.o: $(TITAN_TLS_CA) $(TITAN_BEARSSL_LIB)
src/services/titan_asset_image.o: $(TITAN_IMAGE_DEPS_MARKER)
$(EE_BIN): $(TITAN_BEARSSL_LIB) $(TITAN_IMAGE_DEPS_MARKER) $(TITAN_FMCB_INTEGRATED_LIB)

$(TITAN_WAVE_VU1_DVP_OBJECT): $(TITAN_WAVE_VU1_SOURCE)
	@mkdir -p $(dir $@)
	@test -x "$(DVP_AS)" || \
		(echo "ERRO: dvp-as nao encontrado em $(DVP_AS)" && exit 1)
	@rm -f $@
	$(DVP_AS) $(DVP_ASFLAGS) $< -o $@
	@test -s "$@" || (echo "ERRO: dvp-as nao gerou um objeto VU1 valido" && exit 1)

$(TITAN_WAVE_VU1_BINARY): $(TITAN_WAVE_VU1_DVP_OBJECT)
	@test -x "$(DVP_OBJCOPY)" || \
		(echo "ERRO: dvp-objcopy nao encontrado em $(DVP_OBJCOPY)" && exit 1)
	@rm -f $@
	$(DVP_OBJCOPY) $(DVP_OBJCOPYFLAGS) $< $@
	@test -s "$@" || (echo "ERRO: dvp-objcopy nao gerou o binario VU1" && exit 1)

$(TITAN_WAVE_VU1_HEADER): $(TITAN_WAVE_VU1_BINARY) $(TITAN_WAVE_VU1_EMBED_TOOL)
	$(PYTHON) $(TITAN_WAVE_VU1_EMBED_TOOL) \
		$(TITAN_WAVE_VU1_BINARY) $@ titan_wave_vu1_micro

$(TITAN_GAME_VU1_DVP_OBJECT): $(TITAN_GAME_VU1_SOURCE)
	@mkdir -p $(dir $@)
	@test -x "$(DVP_AS)" || \
		(echo "ERRO: dvp-as nao encontrado em $(DVP_AS)" && exit 1)
	@rm -f $@
	$(DVP_AS) $(DVP_ASFLAGS) $< -o $@
	@test -s "$@" || (echo "ERRO: dvp-as nao gerou o Titan Game VU1" && exit 1)

$(TITAN_GAME_VU1_BINARY): $(TITAN_GAME_VU1_DVP_OBJECT) $(TITAN_GAME_VU1_EXTRACT_TOOL)
	@rm -f $@
	$(PYTHON) $(TITAN_GAME_VU1_EXTRACT_TOOL) \
		$< $@ titan_game_vu1_micro_start titan_game_vu1_micro_end \
		--max-size 14336
	@test -s "$@" || (echo "ERRO: extrator nao gerou binario Titan Game VU1" && exit 1)

$(TITAN_GAME_VU1_HEADER): $(TITAN_GAME_VU1_BINARY) $(TITAN_WAVE_VU1_EMBED_TOOL)
	$(PYTHON) $(TITAN_WAVE_VU1_EMBED_TOOL) \
		$(TITAN_GAME_VU1_BINARY) $@ titan_game_vu1_micro

$(TITAN_WAVE_TEXTURE_HEADER): $(TITAN_WAVE_TEXTURE_RAW) $(TITAN_WAVE_VU1_EMBED_TOOL)
	$(PYTHON) $(TITAN_WAVE_VU1_EMBED_TOOL) \
		$(TITAN_WAVE_TEXTURE_RAW) $@ titan_wave_silk_texture

.PHONY: \
all \
clean \
clean-generated \
font-atlas \
system-assets \
fallback-system-assets \
deploy \
run \
run-direct \
installer

all: $(EE_BIN)

deploy: $(EE_BIN)
	cp -f $(EE_BIN) ../kernel.elf
	@echo "Kernel implantado em ../kernel.elf"

run: deploy
	ps2client execee host:../titan.elf

run-direct: $(EE_BIN)
	ps2client execee host:$(EE_BIN)

installer: $(EE_BIN)
installer: $(TITAN_FMCB_INTEGRATED_LIB)
	cp -f $(EE_BIN) ../titan-installer.elf
	@echo "Titan Installer gerado em ../titan-installer.elf"

$(TITAN_LAUNCH_STAGE2_ELF):
	$(MAKE) -C ../bootloader/stage2

$(TITAN_LAUNCH_STAGE2_HEADER): $(TITAN_LAUNCH_STAGE2_ELF)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || \
		(echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	$(BIN2C) $< $@ titan_launch_stage2_elf

$(TITAN_UI_FONT_HEADER): $(TITAN_UI_FONT_RAW)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || \
		(echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	$(BIN2C) $< $@ titan_ui_font

$(TITAN_UI_ICON_HEADER): $(TITAN_UI_ICON_RAW)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || \
		(echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	$(BIN2C) $< $@ titan_ui_icons

$(TITAN_UI_GAME_PLACEHOLDER_HEADER): $(TITAN_UI_GAME_PLACEHOLDER_RAW)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || \
		(echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	$(BIN2C) $< $@ titan_ui_game_placeholder

$(TITAN_UI_ALERT_HEADER): $(TITAN_UI_ALERT_RAW)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || \
		(echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	$(BIN2C) $< $@ titan_ui_alert

$(TITAN_UI_APP_HOME_HEADER): $(TITAN_UI_APP_HOME_RAW)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || \
		(echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	$(BIN2C) $< $@ titan_ui_app_home

$(TITAN_UI_FTP_HOME_HEADER): $(TITAN_UI_FTP_HOME_RAW)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || \
		(echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	$(BIN2C) $< $@ titan_ui_ftp_home

$(TITAN_UI_POWER_PS2SLIM_HEADER): $(TITAN_UI_POWER_PS2SLIM_RAW)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || \
		(echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	$(BIN2C) $< $@ titan_ui_power_ps2slim

$(TITAN_UI_BOOT_PS2LOGO_HEADER): $(TITAN_UI_BOOT_PS2LOGO_RAW)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || \
		(echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	$(BIN2C) $< $@ titan_ui_boot_ps2logo

$(TITAN_INSTALLER_FAT_HEADER): $(TITAN_INSTALLER_FAT_RAW)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || \
		(echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	@test -f "$<" || \
		(echo "ERRO: asset do instalador nao encontrado: $<" && exit 1)
	$(BIN2C) $< $@ titan_installer_fat

$(TITAN_INSTALLER_SLIM70_HEADER): $(TITAN_INSTALLER_SLIM70_RAW)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || \
		(echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	@test -f "$<" || \
		(echo "ERRO: asset do instalador nao encontrado: $<" && exit 1)
	$(BIN2C) $< $@ titan_installer_slim70

$(TITAN_INSTALLER_SLIM90_HEADER): $(TITAN_INSTALLER_SLIM90_RAW)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || \
		(echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	@test -f "$<" || \
		(echo "ERRO: asset do instalador nao encontrado: $<" && exit 1)
	$(BIN2C) $< $@ titan_installer_slim90

$(TITAN_UI_AVATAR_HEADER): $(TITAN_UI_AVATAR_RAW)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || \
		(echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	$(BIN2C) $< $@ titan_ui_avatars

$(TITAN_SMSUTILS_IRX):
	$(MAKE) -C ../modules/titan-ul-engine/modules/network/SMSUTILS

$(TITAN_USB_PADEMU_IRX):
	$(MAKE) -C ../modules/titan-ul-engine/modules/pademu USE_USB=1

$(TITAN_BT_PADEMU_IRX):
	$(MAKE) -C ../modules/titan-ul-engine/modules/pademu USE_BT=1

$(TITAN_SMSUTILS_HEADER): $(TITAN_SMSUTILS_IRX)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || (echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	$(BIN2C) $< $@ titan_smsutils_irx

$(TITAN_USB_PADEMU_HEADER): $(TITAN_USB_PADEMU_IRX)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || (echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	$(BIN2C) $< $@ titan_usb_pademu_irx

$(TITAN_BT_PADEMU_HEADER): $(TITAN_BT_PADEMU_IRX)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || (echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	$(BIN2C) $< $@ titan_bt_pademu_irx

$(TITAN_USBKBD_HEADER): $(TITAN_USBKBD_IRX)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || (echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	@test -f "$<" || (echo "ERRO: ps2kbd.irx nao encontrado em $<" && exit 1)
	$(BIN2C) $< $@ titan_usbkbd_irx

$(TITAN_USBMOUSE_HEADER): $(TITAN_USBMOUSE_IRX)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || (echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	@test -f "$<" || (echo "ERRO: ps2mouse.irx nao encontrado em $<" && exit 1)
	$(BIN2C) $< $@ titan_usbmouse_irx

$(TITAN_USBD_HEADER): $(TITAN_USBD_IRX)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || \
		(echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	@test -f "$<" || \
		(echo "ERRO: usbd.irx nao encontrado em $<" && exit 1)
	$(BIN2C) $< $@ titan_usbd_irx

$(TITAN_USBHDFSD_HEADER): $(TITAN_USBHDFSD_IRX)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || \
		(echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	@test -f "$<" || \
		(echo "ERRO: usbhdfsd.irx nao encontrado em $<" && exit 1)
	$(BIN2C) $< $@ titan_usbhdfsd_irx

$(TITAN_BDM_HEADER): $(TITAN_BDM_IRX)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || (echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	@test -f "$<" || (echo "ERRO: bdm.irx nao encontrado em $<" && exit 1)
	$(BIN2C) $< $@ titan_bdm_irx

$(TITAN_BDMFS_FATFS_HEADER): $(TITAN_BDMFS_FATFS_IRX)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || (echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	@test -f "$<" || (echo "ERRO: bdmfs_fatfs.irx nao encontrado em $<" && exit 1)
	$(BIN2C) $< $@ titan_bdmfs_fatfs_irx

$(TITAN_USBMASS_BD_HEADER): $(TITAN_USBMASS_BD_IRX)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || (echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	@test -f "$<" || (echo "ERRO: usbmass_bd.irx nao encontrado em $<" && exit 1)
	$(BIN2C) $< $@ titan_usbmass_bd_irx

$(TITAN_IOMANX_HEADER): $(TITAN_IOMANX_IRX)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || \
		(echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	@test -f "$<" || \
		(echo "ERRO: iomanX.irx nao encontrado em $<" && exit 1)
	$(BIN2C) $< $@ titan_iomanx_irx

$(TITAN_FILEXIO_HEADER): $(TITAN_FILEXIO_IRX)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || \
		(echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	@test -f "$<" || \
		(echo "ERRO: fileXio.irx nao encontrado em $<" && exit 1)
	$(BIN2C) $< $@ titan_filexio_irx

$(TITAN_AUDSRV_HEADER): $(TITAN_AUDSRV_IRX)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || \
		(echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	@test -f "$<" || \
		(echo "ERRO: audsrv.irx nao encontrado em $<" && exit 1)
	$(BIN2C) $< $@ titan_audsrv_irx

$(TITAN_POWEROFF_HEADER): $(TITAN_POWEROFF_IRX)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || \
		(echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	@test -f "$<" || \
		(echo "ERRO: poweroff.irx nao encontrado em $<" && exit 1)
	$(BIN2C) $< $@ titan_poweroff_irx

$(TITAN_PS2DEV9_HEADER): $(TITAN_PS2DEV9_IRX)
	@mkdir -p $(dir $@)
	@test -f "$<" || (echo "ERRO: ps2dev9.irx nao encontrado em $<" && exit 1)
	$(BIN2C) $< $@ titan_ps2dev9_irx


$(TITAN_PS2ATAD_HEADER): $(TITAN_PS2ATAD_IRX)
	@mkdir -p $(dir $@)
	@test -f "$<" || (echo "ERRO: ps2atad.irx nao encontrado em $<" && exit 1)
	$(BIN2C) $< $@ titan_ps2atad_irx

$(TITAN_PS2HDD_HEADER): $(TITAN_PS2HDD_IRX)
	@mkdir -p $(dir $@)
	@test -f "$<" || (echo "ERRO: ps2hdd.irx nao encontrado em $<" && exit 1)
	$(BIN2C) $< $@ titan_ps2hdd_irx

$(TITAN_PS2FS_HEADER): $(TITAN_PS2FS_IRX)
	@mkdir -p $(dir $@)
	@test -f "$<" || (echo "ERRO: ps2fs.irx nao encontrado em $<" && exit 1)
	$(BIN2C) $< $@ titan_ps2fs_irx

$(TITAN_NETMAN_HEADER): $(TITAN_NETMAN_IRX)
	@mkdir -p $(dir $@)
	@test -f "$<" || (echo "ERRO: netman.irx nao encontrado em $<" && exit 1)
	$(BIN2C) $< $@ titan_netman_irx

$(TITAN_SMAP_IRX): $(TITAN_SMAP_SOURCE_IRX) $(TITAN_SMAP_PATCH_TOOL)
	@mkdir -p $(dir $@)
	@test -f "$(TITAN_SMAP_SOURCE_IRX)" || (echo "ERRO: smap.irx nao encontrado em $(TITAN_SMAP_SOURCE_IRX)" && exit 1)
	$(PYTHON) $(TITAN_SMAP_PATCH_TOOL) "$(TITAN_SMAP_SOURCE_IRX)" "$@"

$(TITAN_SMAP_HEADER): $(TITAN_SMAP_IRX)
	@mkdir -p $(dir $@)
	@test -f "$<" || (echo "ERRO: Titan SMAP alias IRX nao encontrado em $<" && exit 1)
	$(BIN2C) $< $@ titan_smap_irx

$(TITAN_AR9271_IRX):
	$(MAKE) -C iop/titan_ar9271 all TITAN_AR9271_FW="$(TITAN_AR9271_FW)"

$(TITAN_AR9271_HEADER): $(TITAN_AR9271_IRX)
	@mkdir -p $(dir $@)
	$(BIN2C) $< $@ titan_ar9271_irx

src/graphics/titan_font.o: $(TITAN_UI_FONT_HEADER)
src/graphics/titan_wave_vu1.o: $(TITAN_WAVE_VU1_HEADER) $(TITAN_WAVE_TEXTURE_HEADER)
src/graphics/titan_game_vu1.o: $(TITAN_GAME_VU1_HEADER)
src/ui/titan_icons.o: $(TITAN_UI_ICON_HEADER)
src/ui/titan_avatars.o: $(TITAN_UI_AVATAR_HEADER)
src/ui/titan_ui_assets.o: \
$(TITAN_UI_ALERT_HEADER) \
$(TITAN_UI_BOOT_PS2LOGO_HEADER) \
$(TITAN_UI_GAME_PLACEHOLDER_HEADER) \
$(TITAN_UI_APP_HOME_HEADER) \
$(TITAN_UI_FTP_HOME_HEADER)
src/ui/titan_shell_scene.o: \
$(TITAN_UI_POWER_PS2SLIM_HEADER) \
$(TITAN_POWEROFF_HEADER)
src/services/titan_exec_handoff.o: \
$(TITAN_LAUNCH_STAGE2_HEADER)

src/services/titan_audio.o: \
$(TITAN_AUDSRV_HEADER)


src/services/titan_iop_modules.o: \
$(TITAN_USBKBD_HEADER) \
$(TITAN_USBMOUSE_HEADER) \
$(TITAN_SMSUTILS_HEADER) \
$(TITAN_USB_PADEMU_HEADER) \
$(TITAN_BT_PADEMU_HEADER) \
$(TITAN_IOMANX_HEADER) \
$(TITAN_FILEXIO_HEADER) \
$(TITAN_USBD_HEADER) \
$(TITAN_USBHDFSD_HEADER) \
$(TITAN_BDM_HEADER) \
$(TITAN_BDMFS_FATFS_HEADER) \
$(TITAN_USBMASS_BD_HEADER) \
$(TITAN_PS2DEV9_HEADER) \
$(TITAN_PFS_EMBED_HEADERS) \
$(TITAN_NETMAN_HEADER) \
$(TITAN_SMAP_HEADER) \
$(TITAN_AR9271_HEADER)

src/services/titan_network.o: $(TITAN_AR9271_HEADER) $(TITAN_PS2DEV9_HEADER) $(TITAN_NETMAN_HEADER) $(TITAN_SMAP_HEADER)

src/installer/titan_installer_diag.o: \
src/installer/titan_installer_diag.h

src/installer/titan_installer.o: \
src/installer/titan_installer_diag.h \
$(TITAN_INSTALLER_FAT_HEADER) \
$(TITAN_INSTALLER_SLIM70_HEADER) \
$(TITAN_INSTALLER_SLIM90_HEADER)

font-atlas:
	@test -n "$(TITAN_FONT_SOURCE)" || \
		(echo "ERRO: informe TITAN_FONT_SOURCE=/caminho/para/fonte.otf" && exit 1)
	$(PYTHON) tools/generate_font_atlas.py --font "$(TITAN_FONT_SOURCE)"

system-assets:
	$(PYTHON) tools/verify_curated_system_assets.py --root assets/ui

# Procedural assets are a fallback/test set and deliberately go outside the
# curated runtime tree.
fallback-system-assets:
	$(PYTHON) tools/generate_system_assets.py --output ../build/fallback-system-assets

# Build 36 deliberately preserves the exact seven-entry TPUP envelope proven
# during the BuildFix34 -> Build35 hardware update work. Real-PS2 telemetry showed
# that changing the envelope to either ten entries or a kernel-only one-entry
# package can make the sequential USB verifier hash the first payload incorrectly.
# Keeping the proven 64 + 7*144 = 1072-byte payload offset avoids changing that
# geometry. Entries 2..7 are the existing wLaunchELF/Scarlet Flow assets; Theme
# API v3 itself is entirely contained in kernel.elf.
PUP_OUTPUT ?= ../Project-Titan-Magenta-Edition-0.3.90-build36-complex-theme-api-v3-compat7.pup
WLAUNCHELF_PKG ?= ../apps/wlaunchelf/build/WLAUNCHELF.pkg

.PHONY: pup
pup: $(EE_BIN)
	$(MAKE) -C ../apps/wlaunchelf all EE_CC="$(EE_CC)"
	$(PYTHON) tools/titan_pup.py build \
		--output "$(PUP_OUTPUT)" \
		--build 36 \
		--version 0.3.90-build36 \
		--file kernel.elf=$(EE_BIN) \
		--file TITAN/packages/WLAUNCHELF.pkg=$(WLAUNCHELF_PKG) \
		--file TITAN/apps/TITNWLE01/wlaunchelf.tmod=../apps/wlaunchelf/build/source/wlaunchelf.tmod \
		--file TITAN/apps/TITNWLE01/icon.rgb565=../apps/wlaunchelf/build/source/icon.rgb565 \
		--file TITAN/apps/TITNWLE01/titan_app.ini=../apps/wlaunchelf/build/installed/titan_app.ini \
		--file TITAN/apps/TITNWLE01/UPSTREAM_NOTICE.txt=../apps/wlaunchelf/build/source/UPSTREAM_NOTICE.txt \
		--file TITAN/themes/SCARLETFLOW/theme.cfg=../themes/ribbon-scarlet/theme.cfg
	$(PYTHON) tools/titan_pup.py verify "$(PUP_OUTPUT)"

PUP_SELFTEST_OUTPUT ?= ../Project-Titan-PUP-SelfTest-build37.pup
PUP_SELFTEST_PAYLOAD ?= tools/pup_selftest_build37.txt

.PHONY: pup-selftest
pup-selftest:
	$(PYTHON) tools/titan_pup.py build \
		--output "$(PUP_SELFTEST_OUTPUT)" \
		--build 37 \
		--version 0.3.90-pup-selftest-build37 \
		--file TITAN/update-tests/build37.ok=$(PUP_SELFTEST_PAYLOAD)
	$(PYTHON) tools/titan_pup.py verify "$(PUP_SELFTEST_OUTPUT)"

clean:
	$(MAKE) -C iop/titan_ar9271 clean || true
	$(MAKE) -C $(TITAN_FMCB_INTEGRATED_DIR) clean || true
	rm -f $(EE_OBJS) $(EE_BIN) \
		$(TITAN_WAVE_VU1_DVP_OBJECT) \
		$(TITAN_WAVE_VU1_BINARY) \
		$(TITAN_WAVE_VU1_HEADER) \
		$(TITAN_GAME_VU1_DVP_OBJECT) \
		$(TITAN_GAME_VU1_BINARY) \
		$(TITAN_GAME_VU1_HEADER) \
		$(TITAN_WAVE_TEXTURE_HEADER) \
		$(TITAN_INSTALLER_FAT_HEADER) \
		$(TITAN_INSTALLER_SLIM70_HEADER) \
		$(TITAN_INSTALLER_SLIM90_HEADER)

clean-generated:
	rm -rf src/graphics/generated src/services/generated

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
