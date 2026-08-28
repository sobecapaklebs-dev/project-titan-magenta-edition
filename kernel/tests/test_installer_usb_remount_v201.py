#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
installer = (root / 'kernel/src/installer/titan_installer.c').read_text(encoding='utf-8')

def req(cond, msg):
    if not cond:
        raise AssertionError(msg)

req('#define TITAN_INSTALLER_VERSION "0.2.' in installer, 'installer version marker missing')
req('TITAN_INSTALLER_USB_PROBE_ATTEMPTS 120u' in installer, 'bounded USB remount polling missing')
req('TITAN_INSTALLER_USB_PROBE_DELAY_US 100000u' in installer, 'USB polling cadence missing')
req('probe_usb_mount' in installer and 'probe_usb_prefix' in installer, 'USB remount probe missing')
for alias in ('"mass:"', '"mass0:"', '"mass1:"', '"mass9:"'):
    req(alias in installer, f'USB alias fallback missing: {alias}')
req('extract_mass_prefix(s->executable_path' in installer, 'launcher mass alias is not preferred after IOP reset')
req('DelayThread(350000)' not in installer, 'legacy one-shot 350ms USB delay must be removed')
req('open(s->container_path, O_RDONLY)' in installer, 'TINS open is still hardcoded to mass:/')
req('snprintf(destination, destination_cap, "%s/%s", s->usb_prefix, e->path);' in installer,
    'system destination does not follow resolved USB alias')
req('open(s->consumed_path' in installer and ('open(s->complete_path' in installer or 'install_output_open_step(job, s->complete_path' in installer),
    'transaction markers do not follow resolved USB alias')
req('s.storage_ready = initialize_storage(&s, TitanInput_UsesExtendedModules());' in installer,
    'storage initialization does not retain resolved device in installer state')
pos_storage = installer.index('s.storage_ready = initialize_storage(&s, TitanInput_UsesExtendedModules());')
pos_consumed = installer.index('path_exists(s.consumed_path)', pos_storage)
req(pos_consumed > pos_storage, 'consumed marker is checked before USB remount is restored')
req('Re-enumeration is asynchronous' in installer or 'single 350 ms delay is not sufficient' in installer, 'hardware regression rationale missing')
print('installer-usb-remount-v2.0.1: OK')
