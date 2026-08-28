#!/usr/bin/env python3
from pathlib import Path
root=Path(__file__).resolve().parents[2]
boot=(root/'bootloader/src/main.c').read_text()
mk=(root/'bootloader/Makefile').read_text()
def req(c,m):
    if not c: raise SystemExit('FAIL: '+m)
req('titan_mcman_irx' in boot and 'titan_mcserv_irx' in boot, 'embedded MC modules missing')
req('SifLoadModule("rom0:MCMAN"' not in boot and 'SifLoadModule("rom0:MCSERV"' not in boot, 'legacy ROM MC stack still used')
req('SifLoadModule("rom0:SIO2MAN"' in boot, 'SIO2MAN bootstrap missing')
req(('TITAN_MCMAN_IRX = $(PS2SDK)/iop/irx/mcman.irx' in mk) or ('TITAN_MCMAN_IRX = $(PS2SDK)/iop/irx/mcman-old.irx' in mk), 'mcman packaging missing')
req(('TITAN_MCSERV_IRX = $(PS2SDK)/iop/irx/mcserv.irx' in mk) or ('TITAN_MCSERV_IRX = $(PS2SDK)/iop/irx/mcserv-old.irx' in mk), 'mcserv packaging missing')
# no auto flush at tail of Logf
logf=boot[boot.index('static void TitanBoot_Logf'):boot.index('static void TitanBoot_LogInitializeMemoryCard')]
req('TitanBoot_LogFlush();' not in logf, 'logger still flushes synchronously on every message')
for p in ('16u','18u','20u','22u','24u','26u','30u','35u','40u','45u','48u','50u'):
    req(f'TitanBoot_UpdateProgress({p})' in boot, 'missing visual checkpoint '+p)
req('g_boot_log_mc_ready' in boot and 'if (g_usb_diag.mcinit_result >= 0)' in boot, 'logger readiness gate missing')
print('boot-mc-bootstrap-bf5: OK')
