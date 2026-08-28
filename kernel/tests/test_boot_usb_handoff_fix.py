#!/usr/bin/env python3
from pathlib import Path
root=Path(__file__).resolve().parents[2]
boot=(root/'bootloader/src/main.c').read_text()
path=(root/'bootloader/src/boot_path.c').read_text()
mk=(root/'bootloader/Makefile').read_text()
storage=(root/'kernel/src/services/titan_storage_io.c').read_text()
profile=(root/'kernel/src/services/titan_profile_storage.c').read_text()
def req(v,m):
    if not v: raise SystemExit('FAIL: '+m)
for mod in ('titan_iomanx_irx','titan_filexio_irx','titan_usbd_irx','titan_bdm_irx','titan_bdmfs_fatfs_irx','titan_usbmass_bd_irx'):
    req(mod in boot, 'bootloader missing embedded '+mod)
req('SifIopReset("", 0)' in boot and 'SifIopSync' in boot, 'bootloader does not match wLaunchELF-style IOP bootstrap')
req('TITAN_BOOT_USB_PROBE_ATTEMPTS 120' in boot, 'bootloader 12s USB polling missing')
for i in range(10):
    req(f'"mass{i}:/kernel.elf"' in path, f'mass{i} root kernel fallback missing')
    req(f'"mass{i}:/TITAN/kernel.elf"' in path, f'mass{i} TITAN kernel fallback missing')
req('TITAN_STORAGE_MOUNT_ATTEMPTS 240' in profile, 'kernel 12s mount wait missing')
req('TITAN_STORAGE_IO_ALIAS_COUNT 10u' in storage, 'kernel mass0..mass9 alias policy missing')
req('TitanStorageIo_BuildAliasPath' in storage, 'kernel generalized alias translator missing')
req('-lfileXio' in mk and '-lpatches' in mk, 'bootloader USB link libs missing')
req('TITAN_BDM_IRX' in mk and 'TITAN_BDMFS_FATFS_IRX' in mk and 'TITAN_USBMASS_BD_IRX' in mk, 'bootloader BDM IRX packaging missing')
req('titan_bdm_irx' in profile and 'titan_usbmass_bd_irx' in profile, 'kernel runtime BDM chain missing')
print('boot-usb-handoff-fix: OK')
