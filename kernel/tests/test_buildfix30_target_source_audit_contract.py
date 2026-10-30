#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(sys.argv[1] if len(sys.argv) > 1 else '.').resolve()

def text(rel):
    return (root / rel).read_text(errors='replace')

def need(cond, msg):
    if not cond:
        raise SystemExit('target-source-audit-contract: FAIL: ' + msg)

km = text('kernel/Makefile')
need('iop/irx/ps2kbd.irx' in km and 'iop/irx/ps2mouse.irx' in km, 'PS2SDK HID names')
need('iop/irx/usbkbd.irx' not in km and 'iop/irx/usbmouse.irx' not in km, 'stale HID names')

pm = text('modules/titan-ul-engine/modules/pademu/Makefile')
usb_block = pm.split('ifeq ($(USE_USB),1)',1)[1].split('endif',1)[0]
bt_block = pm.split('ifeq ($(USE_BT),1)',1)[1].split('endif',1)[0]
need('titan_ble_hogp.o' not in usb_block and 'titan_ble_crypto.o' not in usb_block, 'BLE objects linked into USB PADEMU')
need('titan_ble_hogp.o' in bt_block and 'titan_ble_crypto.o' in bt_block, 'BLE objects absent from BT PADEMU')
need('xbox_protocol.o' in bt_block, 'Xbox Bluetooth parser implementation absent from BT PADEMU link')
need('titan_iop_mem_compat.o' in bt_block, 'PADEMU compiler-libcall compatibility object absent from BT link')
compat = text('modules/titan-ul-engine/modules/pademu/titan_iop_mem_compat.c')
need('void *memcpy(' in compat and 'mips_memcpy(dst, src, n)' in compat, 'PADEMU compiler-generated memcpy shim missing')
need('void *memset(' in compat and 'mips_memset(dst, value, n)' in compat, 'PADEMU compiler-generated memset shim missing')
for rel in ('modules/titan-ul-engine/modules/pademu/titan_ble_hogp.c',
            'modules/titan-ul-engine/modules/pademu/titan_ble_crypto.c'):
    src=text(rel)
    need('#undef memcpy' in src and '#undef memset' in src, rel+' PS2SDK sysclib macro cleanup')
    need('#define memcpy mips_memcpy' in src and '#define memset mips_memset' in src, rel+' IOP memory imports')

for rel in ('modules/titan-ul-engine/modules/pademu/ds34usb.c',
            'modules/titan-ul-engine/modules/ds34usb/iop/ds34usb.c'):
    src=text(rel)
    need('ds34pad[pad].outEndp = -1;' in src, rel+' out endpoint initialization')
    need('if (ds34pad[pad].controlEndp >= 0)' in src and 'UsbCloseEndpoint(ds34pad[pad].controlEndp)' in src, rel+' control endpoint close')
    connect=src.split('int usb_connect(int devId)\n{',1)[1].split('int usb_disconnect(int devId)\n{',1)[0]
    need(connect.find('device = (UsbDeviceDescriptor *)UsbGetDeviceStaticDescriptor') < connect.find('device->idVendor') if 'device->idVendor' in connect else True,
         rel+' device dereference before descriptor fetch')
    need('usb_release(pad);' not in connect, rel+' recursive semaphore cleanup')
    need('config_end' in connect and 'descriptor_length < 2u' in connect, rel+' bounded USB descriptor walk')

pademu_bt = text('modules/titan-ul-engine/modules/pademu/ds34bt.c')
pademu_imports = text('modules/titan-ul-engine/modules/pademu/imports.lst')
pademu_bridge = text('kernel/src/services/titan_pademu_input_bridge.c')
pademu_abi = text('modules/titan-ul-engine/include/titan_pademu_telemetry.h')
# P10.5: PADEMU owns radio/protocol only. Persistent bond storage moved to the
# EE single-owner storage worker to avoid USBD/HCI vs mass: re-entrancy.
for token in ('iomanX_open(', 'iomanX_read(', 'iomanX_write(', 'iomanX_close('):
    need(token not in pademu_bt, 'BT PADEMU still performs filesystem I/O: '+token.rstrip('('))
for token in ('iomanX_IMPORTS_start', 'I_open', 'I_read', 'I_write', 'I_close'):
    need(token not in pademu_imports, 'obsolete BT PADEMU IOMANX import remains: '+token)
need('TITAN_PADEMU_RPC_BOND_SYNC' in pademu_abi and 'ds34bt_bond_sync' in pademu_bt,
     'BT PADEMU bond persistence RPC migration missing')
need('mass:/TITAN/BTBONDS.BIN' in pademu_bridge and 'mass:/TITAN/XBOXBT.BIN' in pademu_bridge and
     'TitanStorageIo_AtomicReplace' in pademu_bridge,
     'EE-owned Bluetooth bond persistence missing')

for rel in ('modules/titan-ul-engine/modules/pademu/ds34bt.c',
            'modules/titan-ul-engine/modules/ds34bt/iop/ds34bt.c'):
    src=text(rel)
    connect=src.split('static int bt_connect(int devId)\n{',1)[1].split('static int bt_disconnect(int devId)\n{',1)[0]
    need('config_end' in connect and 'descriptor_length < 2u' in connect, rel+' bounded BT descriptor walk')
    need('UsbCloseEndpoint(bt_dev.controlEndp)' in src, rel+' control endpoint leak')
    need('chrg_end = -1' in src and 'UsbCloseEndpoint(chrg_end)' in src, rel+' charge endpoint leak')


ar=text('kernel/iop/titan_ar9271/ar9271_scanner.c')
imports=text('kernel/iop/titan_ar9271/imports.lst')
need('titan_startup_cleanup_threads' in ar and 'titan_startup_cleanup_semas' in ar, 'AR9271 startup lacks transactional cleanup')
need('if(StartThread(tids[0],NULL)<0)goto fail;' in ar and 'if(StartThread(tids[5],NULL)<0)goto fail;' in ar, 'AR9271 StartThread failures ignored')
need('UsbUnregisterDriver(&g_usb_driver)' in ar and 'NetManUnregisterNetIF("WFI")' in ar, 'AR9271 registration rollback incomplete')
for token in ('I_DeleteSema','I_TerminateThread','I_DeleteThread','I_sceUsbdUnregisterLdd'):
    need(token in imports, 'AR9271 cleanup import missing '+token)
rpc_tail=ar.split('/* RPC is deliberately the final fallible startup step.',1)[1].split('return MODULE_RESIDENT_END;',1)[0]
need('UsbRegisterDriver' not in rpc_tail and 'titan_netman_register' not in rpc_tail, 'fallible registration remains after RPC startup')

asset=text('kernel/src/services/titan_asset_service.c')
need('static int TitanAssetService_EnsureLock(void)' in asset, 'Asset Service lock allocation is not recoverable')
worker=asset.split('static int TitanAssetService_EnsureNetworkWorker(void)\n{',1)[1].split('#endif',1)[0]
need(worker.find('TitanAssetService_EnsureLock()') < worker.find('g_assets.net_faulted = 0'), 'Asset worker clears fault before synchronization is restored')
need('if (!TitanAssetService_EnsureLock())' in worker and 'return 0;' in worker, 'Asset worker can start without lock')

build=text('scripts/build_all.sh')
need('titan_target_preflight.py' in build, 'target preflight not wired')
makefile=text('kernel/Makefile')
need('TITAN_AR9271_FROZEN_IRX ?=' in makefile and 'cp "$(TITAN_AR9271_FROZEN_IRX)" "$@"' in makefile, 'validated frozen AR9271 IRX is not wired into the clean build')
need('6bc9275efbc1de6e643575867a0e14dac02a3a6cf0d5d6ea12906ac7ef73aa17' in makefile, 'frozen AR9271 IRX copy lacks integrity validation')
pre=text('tools/titan_target_preflight.py')
for token in ('mips64r5900el-ps2-elf-strip','mipsel-none-elf-ar','mipsel-none-elf-strip','iopfixup','ps2-packer','ps2kbd.irx','ps2mouse.irx','udnl-t300.irx','eesync-nano.irx','IEEE1394_bd.irx','ps2hdd-bdm.irx','libvorbisfile.a','libfreetype.a','resolve_gskit','TARGET_PREFLIGHT_GSKIT','TITAN_AR9271_FW'):
    need(token in pre, 'preflight missing '+token)

fmcb=text('third_party/fmcb/integrated/titan_fmcb_core.c')
need('TITAN_FMCB_IOP_SYNC_RETRIES' in fmcb and 'return -ETIMEDOUT' in fmcb, 'unbounded integrated IOP sync')
need('fmcb_fileio_stack_error' in fmcb and 'fmcb_usb_stack_error' in fmcb and 'fmcb_secr_error' in fmcb, 'critical FMCB module failures not fail-closed')
need('got < 6u' in fmcb and 'rom0:ROMVER' in fmcb, 'integrated ROMVER physical gate')

upstream=text('third_party/fmcb/upstream/installer/system.c')
need(upstream.count('if (InitSemaID < 0) return InitSemaID;') >= 8, 'upstream integrated IOP error propagation')

installer=text('kernel/src/installer/titan_installer.c')
fail_block=installer.split('fmcb_result = fmcb_run_integrated(s);',1)[1].split('job->phase = TITAN_INSTALL_JOB_WRITE_COMPLETE',1)[0]
need('fmcb_restore_titan_iop(s)' in fail_block, 'rollback starts before Titan IOP restoration')
need('FMCB_ROLLBACK_IOP_RESTORE' in fail_block, 'rollback IOP restore failure lacks diagnostics')

kernel=text('kernel/src/core/titan_kernel.c')
need('Recuperando estado anterior - %.81s' in kernel, 'target format-truncation warning not bounded')

# PS2SDK kernel.h owns the EE global-pointer declaration as `extern void *_gp`.
# A stale `extern char _gp` conflicts with current PS2SDK before link time.
for rel in ('kernel/src/services/titan_datetime.c',
            'kernel/src/services/titan_early_input.c'):
    src = text(rel)
    need('extern char _gp;' not in src, rel+' conflicts with PS2SDK kernel.h _gp type')
    need('extern void *_gp;' in src, rel+' does not match PS2SDK kernel.h _gp declaration')

# R4 final static invariants — issues found only after whole-tree lifetime/streaming audit.
flight=text('kernel/iop/titan_ar9271/titan_flightrec.c')
flight_h=text('kernel/iop/titan_ar9271/titan_flightrec.h')
need('int titan_fr_lock_ready(void)' in flight and 'int titan_fr_lock_ready(void);' in flight_h,
     'flight recorder lock readiness is not externally testable')
start=ar.split('int _start(int argc, char *argv[])',1)[1]
need(start.find('titan_fr_reset();') < start.find('if (!titan_fr_lock_ready()) return MODULE_NO_RESIDENT_END;'),
     'AR9271 can continue startup with an unsynchronized flight recorder')
need(start.find('if (!titan_fr_lock_ready()) return MODULE_NO_RESIDENT_END;') < start.find('CreateThread'),
     'AR9271 creates workers before flight-recorder synchronization is proven')

http=text('kernel/src/services/titan_http_client.c')
body=http.split('static int TitanHttp_ReadBody(',1)[1].split('static int TitanHttp_GetOnce(',1)[0]
need('destination != 0 ? destination + used : 0' in body,
     'chunked streaming still performs pointer arithmetic on NULL destination')
need('chunk > (unsigned long)UINT_MAX - (unsigned long)used' in body,
     'chunked body accumulated-size overflow is not rejected')
need('length > (unsigned long)UINT_MAX' in body,
     'Content-Length wider than runtime counters is not rejected')
need('(unsigned int)n > UINT_MAX - used' in body,
     'unframed streaming accumulated-size overflow is not rejected')
need('(unsigned int)n > UINT_MAX - transport->result_info->bytes_received' in body,
     'HTTP result byte counter can wrap')

appnet=text('kernel/src/services/titan_app_network.c')
worker=appnet.split('static void TitanAppNetwork_Worker(void *argument)',1)[1].split('static int TitanAppNetwork_StartWorker',1)[0]
need('g_app_network.thread_id = -1;' in worker and 'ExitDeleteThread();' in worker,
     'native-app HTTP worker leaks its completed EE thread object')
need('ExitThread();' not in worker, 'native-app HTTP worker regressed to non-deleting exit')
start_worker=appnet.split('static int TitanAppNetwork_StartWorker(uint32_t generation)',1)[1].split('#endif',1)[0]
need('if (StartThread(id, (void *)(uintptr_t)generation) < 0)' in start_worker and 'DeleteThread(id);' in start_worker,
     'native-app HTTP worker StartThread failure leaks the EE thread object')

boot_video=text('bootloader/src/boot_video.c')
show=boot_video.split('int TitanBootVideo_Show(void)',1)[1].split('int TitanBootVideo_SetProgress(',1)[0]
need(show.find('if (titan_boot_composite != 0)') < show.find('titan_boot_composite = (uint16_t *)memalign'),
     'boot video retry overwrites an existing composite allocation')
need(show.count('free(titan_boot_composite);') >= 3,
     'boot video does not free composite backing on retry/upload/fade failures')
upload_fail=show.split('if (result < 0)',1)[1].split('titan_boot_video_ready = 1;',1)[0]
need('free(titan_boot_composite);' in upload_fail and 'titan_boot_composite = 0;' in upload_fail,
     'boot video upload failure leaks composite backing')
fade_region=show.split('result = TitanBootVideo_Fade(',1)[1]
need('if (result < 0)' in fade_region and 'free(titan_boot_composite);' in fade_region and 'titan_boot_composite = 0;' in fade_region,
     'boot video fade failure leaks composite backing')

# TARGETFIX6: invariants harvested from the first full diagnostic target sweep.
game_runtime=text('kernel/src/services/titan_game_runtime.c')
download=text('kernel/src/services/titan_download_service.c')
need('#include <delaythread.h>' in game_runtime, 'Game Runtime uses DelayThread without current PS2SDK header')
need('#include <delaythread.h>' in download and '#include <malloc.h>' in download,
     'Download Service lacks DelayThread/memalign declarations on EE target')

hid=text('kernel/src/services/titan_hid_input.c')
need('#include <fileXio_rpc.h>' in hid and 'NEWLIB_PORT_AWARE' in hid,
     'HID keyboard lacks explicit current PS2SDK FileXio API')
need('#include <io_common.h>' in hid,
     'HID keyboard lacks explicit current PS2SDK O_* flag declarations')
for token in ('fileXioOpen(PS2KBD_DEVFILE,O_RDONLY,0)',
              'fileXioIoctl(g_kbd_fd,PS2KBD_IOCTL_SETREADMODE',
              'fileXioIoctl(g_kbd_fd,PS2KBD_IOCTL_SETBLOCKMODE',
              'fileXioRead(g_kbd_fd', 'fileXioClose(g_kbd_fd)'):
    need(token in hid, 'HID keyboard target I/O missing '+token)
need('FIO_O_RDONLY' not in hid, 'HID keyboard uses a non-existent FileXio open flag')
need('ioctl(g_kbd_fd' not in hid, 'HID keyboard regressed to undeclared newlib ioctl')

net_client=text('kernel/src/services/titan_network_client.c')
need('CpuSuspendIntr' not in net_client and 'CpuResumeIntr' not in net_client,
     'EE network client still uses IOP intrman APIs')
need(net_client.count('int state = DIntr();') >= 2 and net_client.count('if (state != 0) EIntr();') >= 2,
     'EE network client does not preserve interrupt state with DIntr/EIntr')

video_h=text('kernel/src/graphics/titan_video.h')
video_c=text('kernel/src/graphics/titan_video.c')
video_sw=text('kernel/src/graphics/backends/titan_video_software.inc')
need('uint32_t *texture_handle' in video_h and 'unsigned int *texture_handle' not in video_h,
     'Game texture handle pointer ABI is not fixed-width on R5900')
# P160.5: the original TargetFix6 invariant is fixed-width uint32_t storage.
# P158 intentionally changed the CLUT staging rank from [256] to [2][256] so
# the EE can fill slot N^1 while GIF DMA consumes slot N.  Preserve the ABI
# invariant without falsely requiring the old single staging buffer.
video_c_flat = ' '.join(video_c.split())
need(
    'uint32_t *allocated_handle' in video_c_flat and
    'unsigned int *allocated_handle' not in video_c_flat and
    (
        'uint32_t g_video_game_texture_clut[256]' in video_c_flat or
        'uint32_t g_video_game_texture_clut[2][256]' in video_c_flat
    ) and
    'unsigned int g_video_game_texture_clut' not in video_c_flat,
    'GS texture allocator/CLUT lost fixed-width uint32_t target storage'
)
need('#include <packet2_utils.h>' in video_c,
     'GS renderer uses VU_GS_GIFTAG without packet2_utils declaration')
need('uint32_t *texture_handle' in video_sw,
     'software renderer no longer matches fixed-width game texture ABI')

pn_make=text('apps/project-nes/Makefile')
fceumm_flags=pn_make.split('FCEUMM_CFLAGS :=',1)[1].split('\n',1)[0]
need('$(filter-out -Werror,$(CFLAGS))' in fceumm_flags,
     'frozen FCEUmm vendor build still inherits first-party -Werror and is GCC-version fragile')
need('-Werror' not in fceumm_flags.replace('$(filter-out -Werror,$(CFLAGS))',''),
     'FCEUmm vendor flags explicitly re-enable -Werror after filtering it out')
need('-Werror' in pn_make.split('CFLAGS :=',1)[1].split('\n',1)[0],
     'Project NES first-party target warning policy was weakened')
need('-mabi=32' not in pn_make and '-mabi=n32' in pn_make,
     'Project NES target objects still force MIPS O32 instead of the PS2 EE N32 ABI')
need('$(EE_CC) -march=r5900 -mabi=n32 -nostdlib -r -o $@ $(BUILD)/target-obj/*.o' in pn_make,
     'Project NES relocatable link bypasses the EE compiler driver ABI/emulation selection')
need('EE_LD ?=' not in pn_make,
     'Project NES still exposes the raw EE linker path that caused the O32/N32 emulation mismatch')
pn_compat=text('apps/project-nes/src/project_nes_fceumm_compat.h')
need('#include <inttypes.h>' in pn_compat,
     'Project NES PS2 compatibility shim does not include inttypes before libretro-common')
need('#define PRId64 "lld"' in pn_compat and '#define PRIu64 "llu"' in pn_compat and '#define PRIuPTR "u"' in pn_compat,
     'Project NES PS2 compatibility shim lacks R5900 C99 printf format fallbacks')
need('#if defined(_EE)' in pn_compat,
     'Project NES inttypes fallback is not scoped to the EE target')

ul_pad=text('modules/titan-ul-engine/ee_core/src/padhook.c')
ul_link=text('modules/titan-ul-engine/ee_core/linkfile')
ul_io=text('modules/titan-ul-engine/include/iosupport.h')
need('section(".titan_igr_stack")' in ul_pad,
     'Titan UL IGR stack still consumes ee_core .bss')
need('igrstack\t: ORIGIN = 0x00097000, LENGTH = 4K' in ul_link and
     'modules\t: ORIGIN = 0x00098000, LENGTH = 224K' in ul_link and
     'KEEP(*(.titan_igr_stack))' in ul_link,
     'Titan UL linker does not reserve the dedicated IGR stack before module storage')
need('#define OPL_MOD_STORAGE 0x00098000' in ul_io,
     'Titan UL module storage does not match the linker IGR reservation')

fmcb_entry_make=text('boot/fmcb_entry/Makefile')
need('.PHONY: all clean' in fmcb_entry_make and 'all: $(EE_BIN)' in fmcb_entry_make and
     'include $(PS2SDK)/samples/Makefile.eeglobal' in fmcb_entry_make,
     'Titan FMCB entry Makefile lacks executable all/clean target rules')

neu_util=text('modules/titan-neutrino-engine/ee/ee_core/include/util.h')
neu_pad=text('modules/titan-neutrino-engine/ee/ee_core/src/padhook.c')
need('TITAN_NEUTRINO_EE_CORE_UTIL_H' in neu_util and '#ifndef UTIL_H' not in neu_util,
     'Neutrino util header still has collision-prone generic guard')
need('#include <delaythread.h>' in neu_pad,
     'Neutrino EE pad hook uses DelayThread without current PS2SDK header')
for src in ('main.c','patches.c','iopmgr.c','util.c','gsm_api.c','padhook.c'):
    body=text('modules/titan-neutrino-engine/ee/ee_core/src/'+src)
    need('#include "../include/util.h"' in body and '#include "util.h"' not in body,
         'Neutrino source can still resolve the UL util.h by include-path order: '+src)

need('VU_GS_GIFTAG' not in video_c and 'GIF_SET_TAG' in video_c,
     'GS renderer still uses removed/non-PS2SDK VU_GS_GIFTAG macro')
wave_vu=text('kernel/src/graphics/titan_wave_vu1.c')
need('VU_GS_GIFTAG' not in wave_vu and 'GIF_SET_TAG' in wave_vu,
     'VU1 wave renderer still uses removed/non-PS2SDK VU_GS_GIFTAG macro')

pfs_platform=text('kernel/src/platform/ps2/titan_platform_pfs.c')
need('FIO_MT_RDWR' not in pfs_platform and 'FIO_MT_RDONLY' not in pfs_platform and
     'FILEXIO_MOUNTFLAG_NORMAL' in pfs_platform and 'FILEXIO_MOUNTFLAG_READONLY' in pfs_platform,
     'PFS platform still uses removed legacy FileXio mount flags')
ul_support=text('modules/titan-ul-engine/src/supportbase.c')
ul_hdd=text('modules/titan-ul-engine/src/hddsupport.c')
need('FIO_MT_RDONLY' not in ul_support and 'FILEXIO_MOUNTFLAG_READONLY' in ul_support,
     'Titan UL supportbase still uses removed legacy FileXio read-only mount flag')
need('FIO_MT_RDWR' not in ul_hdd and 'FILEXIO_MOUNTFLAG_NORMAL' in ul_hdd,
     'Titan UL HDD support still uses removed legacy FileXio read-write mount flag')

ul_padhook=text('modules/titan-ul-engine/ee_core/src/padhook.c')
need('#include <delaythread.h>' in ul_padhook,
     'Titan UL EE padhook uses DelayThread without current PS2SDK header')

ul_build=text('scripts/build_titan_ul_engine.sh')
for token in ('sandbox=', 'work="$sandbox/modules/titan-ul-engine"',
              'overlay="$sandbox/modules/titan-engine-overlay"',
              'tar -C "$ROOT/modules/titan-engine-overlay"'):
    need(token in ul_build, 'UL disposable build does not preserve sibling overlay geometry: '+token)

fmcb_make=text('third_party/fmcb/integrated/Makefile')
fmcb_core=text('third_party/fmcb/integrated/titan_fmcb_core.c')
need('-D_EE' in fmcb_make and '-DNEWLIB_PORT_AWARE' in fmcb_make,
     'integrated FMCB is not compiled as EE/newlib-aware source')
need('-DFIO_MT_RDWR=FILEXIO_MOUNTFLAG_NORMAL' in fmcb_make and
     '-DFIO_MT_RDONLY=FILEXIO_MOUNTFLAG_READONLY' in fmcb_make,
     'integrated FMCB lacks current FileXio mount-flag compatibility mapping')
need('#include <delaythread.h>' in fmcb_core and '#include <iopcontrol_special.h>' in fmcb_core,
     'integrated FMCB lacks modern PS2SDK DelayThread/IOP reboot declarations')
need('struct McData;' in fmcb_core,
     'integrated FMCB includes system.h without forward-declaring standalone McData ABI')

# TARGETFIX7: second-sweep root causes.
ul_make=text('modules/titan-ul-engine/Makefile')
ul_core_make=text('modules/titan-ul-engine/ee_core/Makefile')
need('$(MAKE) $(IGS_FLAGS) $(PADEMU_FLAGS) $(EECORE_EXTRA_FLAGS) -C $< all' in ul_make,
     'UL wrapper invokes ee_core without its all target and can falsely succeed without ee_core.elf')
need('@test -s $@' in ul_make and 'EE core build completed without producing $@' in ul_make,
     'UL ee_core generation lacks artifact postcondition')
need('.DEFAULT_GOAL := all' in ul_core_make,
     'UL ee_core Makefile default goal is an object instead of the complete EE core')

fmcb_compat=text('third_party/fmcb/integrated/titan_fmcb_upstream_compat.h')
need('#include <unistd.h>' in fmcb_compat and '#include <stdlib.h>' in fmcb_compat,
     'FMCB upstream compatibility boundary lacks getcwd/strtoul declarations')
need('#define sio_printf printf' in fmcb_compat,
     'FMCB integrated diagnostics still depend on removed legacy sio_printf declaration')
need('void InitProgressScreen(int label);' in fmcb_compat,
     'FMCB system.c cannot see Titan-owned progress shim prototype')
need('-include titan_fmcb_upstream_compat.h' in fmcb_make and '-I.' in fmcb_make,
     'FMCB upstream system.c is not compiled through the integration compatibility boundary')

diag=text('scripts/build_diagnostic_sweep.sh')
for token in ('FAILED_STAGES=', 'FAILED_STAGE_LIST=', 'TOTAL_ERRORS=', 'UNIQUE_ERRORS=', 'UNDEFINED_SYMBOLS='):
    need(token in diag, 'diagnostic sweep terminal tail lacks convergence metric '+token)
need('DIAG_BUNDLE="${LOG_ROOT}.tar.gz"' in diag and "printf 'Bundle: %s\\n'" in diag,
     'diagnostic sweep does not automatically preserve the complete per-stage log bundle')


# TARGETFIX7 UL header/ABI hardening from the second sweep.
menusys_h=text('modules/titan-ul-engine/include/menusys.h')
coreconfig_h=text('modules/titan-ul-engine/ee_core/include/coreconfig.h')
need('struct UIItem;' in menusys_h and '#include "include/dia.h"' not in menusys_h,
     'UL menusys.h still forms a dia.h include cycle instead of forward-declaring UIItem')
need('const u32 *gCheatList;' in coreconfig_h,
     'UL EECoreConfig_t cheat-list pointer discards read-only ownership from GetCheatsList')

# TARGETFIX8: closure of the 20260821T222314Z real PS2DEV target telemetry.
ul_core_make=text('modules/titan-ul-engine/ee_core/Makefile')
ul_inc_line=next(line for line in ul_core_make.splitlines() if line.startswith('EE_INCS :='))
need(ul_inc_line.index('-Iinclude') < ul_inc_line.index('-I../include'),
     'Titan UL ee_core resolves parent util.h before its local util API')

fmcb_compat=text('third_party/fmcb/integrated/titan_fmcb_upstream_compat.h')
fmcb_make=text('third_party/fmcb/integrated/Makefile')
need('#include <iox_stat.h>' in fmcb_compat,
     'FMCB compatibility boundary lacks current FIO_S_* stat/mode definitions')
for obj in ('ReqSpaceCalc.o', 'libsecr.o', 'system.o'):
    rule=fmcb_make.split('$(BUILD)/'+obj+':',1)[1].split('\n\n',1)[0]
    need('titan_fmcb_upstream_compat.h' in rule and '-include titan_fmcb_upstream_compat.h' in rule,
         'FMCB upstream '+obj+' bypasses the integration compatibility boundary')

neu_link=text('modules/titan-neutrino-engine/ee/ee_core/linkfile')
neu_pad=text('modules/titan-neutrino-engine/ee/ee_core/src/padhook.c')
neu_cfg=text('modules/titan-neutrino-engine/ee/loader/config/compat.toml')
neu_common=text('modules/titan-neutrino-engine/common/include/eecore_config.h')
need('igr_stack84 : ORIGIN = 0x00095000, LENGTH =   4K' in neu_link,
     'Neutrino lacks a dedicated 4 KiB Titan IGR worker-stack region')
need('modules84   : ORIGIN = 0x00096000, LENGTH = 424K' in neu_link,
     'Neutrino module storage still overlaps the dedicated Titan IGR stack')
need('section(".titan_igr_stack")' in neu_pad and '.titan_igr_stack (NOLOAD)' in neu_link,
     'Neutrino Titan IGR stack still consumes ee_core84 .bss')
need('eecore.mod_base = 0x96000' in neu_cfg,
     'Neutrino loader default module base does not match the relocated module region')
need('0x96000 - 0xb6000 = 128KiB' in neu_common,
     'Neutrino module checksum range documentation is stale after relocation')

kernel_make = text('kernel/Makefile')
kernel_make_flat=' '.join(kernel_make.split())
need('TITAN_REPRO_SOURCE_ROOT ?= $(abspath ..)' in kernel_make_flat and
     'TITAN_REPRO_CANONICAL_ROOT ?= /project-titan' in kernel_make_flat and
     kernel_make_flat.count('-ffile-prefix-map="$(TITAN_REPRO_SOURCE_ROOT)=$(TITAN_REPRO_CANONICAL_ROOT)"') >= 2 and
     kernel_make_flat.count('-fdebug-prefix-map="$(TITAN_REPRO_SOURCE_ROOT)=$(TITAN_REPRO_CANONICAL_ROOT)"') >= 2 and
     'EE_LDFLAGS += \\' in kernel_make,
     'Kernel release ELF can depend on mktemp DWARF/source or LTO link paths and break reproducible builds')
need('src/services/titan_repository_protocol.o \\' in kernel_make,
     'Kernel target omits titan_repository_protocol.o required by Titan Store repository symbols')

print('target-source-audit-contract: PASS')
