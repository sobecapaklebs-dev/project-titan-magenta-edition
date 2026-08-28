#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
installer = (root / 'src/installer/titan_installer.c').read_text(errors='replace')

def req(cond, msg):
    if not cond:
        raise SystemExit('FAIL: ' + msg)

req('#define TITAN_INSTALLER_VERSION "0.2.9-poc"' in installer, 'v2.0.9 version missing')
req('#define TITAN_INSTALLER_BACKUP_SLOTS 64u' in installer, 'backup slot pool missing')
req('int backup_slot;' in installer, 'per-entry backup slot state missing')
req('install_allocate_backup_path' in installer, 'fresh backup allocator missing')
req('install_find_recovery_backup' in installer, 'numbered backup recovery missing')
req('.TITAN.BK%02u' in installer and '.TITAN.BK%02d' in installer, 'numbered transaction backup naming missing')
req('Não foi possível limpar um backup transacional antigo.' not in installer, 'stale backup is still fatal')
req('if (!install_allocate_backup_path(e, job->destination, job->backup' in installer, 'publication still does not allocate a fresh backup')
req('install_build_backup_path(e, job->destination, job->backup' in installer, 'rollback/cleanup do not use selected backup path')
req('if (e->backup_slot >= 0' in installer, 'numbered transaction backups are not cleanup-only')
req('if (strcmp(e->kind, "BOOT") == 0)' in installer and '%s.TITAN.BAK' in installer, 'canonical BOOT.TITAN.BAK preservation missing')
req('installer_io_worker' not in installer, 'regressed to second EE filesystem worker')
req('install_job_pump_main_thread' in installer, 'single-owner bounded I/O pump missing')
print('installer-stale-backup-slots-v2.0.9: OK')
