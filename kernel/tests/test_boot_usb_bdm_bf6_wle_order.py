#!/usr/bin/env python3
from pathlib import Path
root=Path(__file__).resolve().parents[2]
boot=(root/'bootloader/src/main.c').read_text()
mk=(root/'bootloader/Makefile').read_text()
profile=(root/'kernel/src/services/titan_profile_storage.c').read_text()

def req(v,m):
    if not v: raise SystemExit('FAIL: '+m)

req('mcman-old.irx' in mk and 'mcserv-old.irx' in mk, 'wLaunchELF-compatible old MC module pair not pinned')
for token in ('SifLoadModule("rom0:SIO2MAN"', 'SifLoadModule("rom0:PADMAN"', 'fileXioInit()', 'fileXioSetRWBufferSize(128 * 1024)', 'mcInit(MC_TYPE_MC)', 'titan_usbd_irx'):
    req(token in boot, 'missing boot token '+token)
pos_mcserv=boot.index('titan_mcserv_irx')
pos_pad=boot.index('SifLoadModule("rom0:PADMAN"')
pos_fx=boot.index('g_usb_diag.filexio_init_result = fileXioInit()')
pos_buf=boot.index('fileXioSetRWBufferSize(128 * 1024)')
pos_mcinit=boot.index('g_usb_diag.mcinit_result = mcInit(MC_TYPE_MC)')
pos_usbd=boot.index('TitanBoot_ExecEmbeddedModuleDiag(titan_usbd_irx')
req(pos_mcserv < pos_pad < pos_fx < pos_buf < pos_mcinit < pos_usbd, 'boot pre-USBD order does not match wLaunchELF essentials')
req(boot.count('g_usb_diag.filexio_init_result = fileXioInit()') == 1, 'FileXio client should bind once before USBD')
pos_k_file=profile.index('titan_filexio_irx')
pos_k_init=profile.index('(void)TitanStorageIo_InitializeClient();', pos_k_file)
pos_k_usbd=profile.index('titan_usbd_irx', pos_k_init)
req(pos_k_file < pos_k_init < pos_k_usbd, 'kernel FileXio client is not bound before USBD')
print('boot-usb-bdm-bf6-wle-order: OK')
