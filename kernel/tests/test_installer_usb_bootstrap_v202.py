#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
installer = (root / 'kernel/src/installer/titan_installer.c').read_text(encoding='utf-8')

def req(cond, msg):
    if not cond:
        raise SystemExit('FAIL: ' + msg)

req('#define TITAN_INSTALLER_VERSION "0.2.' in installer, 'installer version marker missing')
req('return id >= 0 && result >= 0;' in installer, 'strict SifExecModuleBuffer success semantics')
req('usb_modules_ok' not in installer, 'no short-circuit USB module chain')
for mod in ('titan_iomanx_irx', 'titan_filexio_irx', 'titan_usbd_irx', 'titan_usbhdfsd_irx'):
    req(f'(void)exec_module_checked(\n        {mod},' in installer, f'{mod} always attempted')
req('s->usb_filexio_client_result = fileXioInit();' in installer, 'fileXioInit diagnostic retained')
req('if (fileXioInit() < 0) return 0;' not in installer, 'fileXioInit no longer authoritative')
req('fd = open(container_path, O_RDONLY);' in installer, 'direct TINS open probe')
req('memcmp(magic, TITAN_TINS_MAGIC' in installer, 'direct TINS magic probe')
req('if (path_exists(root_path))' in installer, 'root mount fallback preserved')
req('stat(s->container_path' not in installer, 'TINS manifest no longer rejected by stat before open')
req('file_end = lseek(fd, 0, SEEK_END);' not in installer, 'legacy SEEK_END TINS validation must stay removed')
req('s->tins_preflight_attempted = 0;' in installer, 'preflight state reset')
req('s->tins_preflight_ok = 1;' in installer, 'preflight success state')
req('"TITAN_SYSTEM.TINS ainda não verificado"' in installer, 'no false TINS error while USB unavailable')
req('s->storage_ready && s->tins_preflight_ok' in installer, 'continue gated on verified TINS')
req('[TitanInstaller] USB bootstrap:' in installer, 'hardware bootstrap diagnostics emitted')
print('installer-usb-bootstrap-v2.0.2: OK')
