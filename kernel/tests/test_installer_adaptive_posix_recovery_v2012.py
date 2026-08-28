#!/usr/bin/env python3
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
installer = (root / 'src/installer/titan_installer.c').read_text(encoding='utf-8', errors='replace')
diag = (root / 'src/installer/titan_installer_diag.c').read_text(encoding='utf-8', errors='replace')
libmc = (root / 'tests/host_stubs/libmc.h').read_text(encoding='utf-8', errors='replace')


def req(cond, msg):
    if not cond:
        raise SystemExit('FAIL: ' + msg)

req('#define TITAN_INSTALLER_VERSION "0.2.12-poc"' in installer, 'v2.0.12 version missing')

# Physical v2.0.11 regression: direct FileXio NOWAIT data reads returned a
# completed zero-byte result on the inherited launcher stack. Data plane must
# therefore be POSIX for mass:, not direct fileXioRead/fileXioWrite.
req('install_posix_adaptive_read' in installer, 'adaptive POSIX read path missing')
req('install_posix_adaptive_write' in installer, 'adaptive POSIX write path missing')
req('result = read(fd, buffer, cap);' in installer, 'mass data read is not POSIX/newlib')
req('result = write(fd, buffer, cap);' in installer, 'mass data write is not POSIX/newlib')
req('fileXioRead(' not in installer, 'direct FileXio data read regressed')
req('fileXioWrite(' not in installer, 'direct FileXio data write regressed')
req('TITAN_INSTALLER_IO_TARGET_MS 8u' in installer, 'adaptive latency target missing')
req('IO_SLICE_TUNE' in installer, 'adaptive I/O telemetry missing')

# Memory Card I/O has its own native asynchronous RPC. This keeps BOOT writes,
# verification and metadata operations off the wave-rendering frame wait while
# avoiding fileXio* on mc0:, which returned -2 in the physical v2.0.11 log.
for token in ('mcOpen(', 'mcRead(', 'mcWrite(', 'mcClose(', 'mcDelete(', 'mcRename('):
    req(token in installer, 'native libmc transaction path missing: ' + token)
req('TITAN_INSTALLER_ASYNC_BACKEND_LIBMC' in installer, 'libmc async backend missing')
req('mcSync(MC_NOWAIT' in installer, 'libmc completion is not polled between frames')
req('install_path_is_mc(path)' in installer, 'MC path routing missing')
req('fileXioRemove(path)' in installer and 'fileXioRename(source, destination)' in installer,
    'mass: metadata FileXio NOWAIT fallback missing')

# Recovery must not rescan 64 numbered stale backups for every entry. The old
# behavior generated 67 stat() calls per frame and flooded the diagnostic ring.
recover_start = installer.index('static int install_find_recovery_backup(')
recover_end = installer.index('\nstatic int install_rename_compat(', recover_start)
recover = installer[recover_start:recover_end]
req('TITAN_INSTALLER_BACKUP_SLOTS' not in recover, 'recovery still scans every numbered backup slot')
req('.TITAN.BAK' in recover and '.TITAN.BK%02' not in recover,
    'recovery is not canonical-only for ambiguous legacy backups')
req('TITAN_INSTALLER_BACKUP_PROBE_ATTEMPTS 8u' in installer, 'bounded mass backup allocator missing')
req('USB_BACKUP_NO_SLOT_BOUNDED' in installer, 'bounded backup allocation diagnostics missing')

# BOOT slot selection remains based on one real MC directory enumeration.
req('mcGetDir(s->memory_card' in installer and '"/BOOT/*"' in installer,
    'BOOT slot allocation is not using mcGetDir')
req('MC_BACKUP_SLOT_SELECTED' in installer, 'MC backup slot telemetry missing')

# POSIX descriptors must never be passed to fileXioClose after switching the
# mass data plane back to newlib.
req('install_close_sync_compat' in installer, 'backend-aware emergency close missing')
req('install_fxio_close_sync' not in installer, 'old FileXio close helper still active on POSIX descriptors')

# Expected missing transaction scratch files should not consume the ring log.
req('diag_expected_absent_probe' in diag, 'expected-missing probe filter missing')
for phase in ('RECOVER', 'ROLLBACK', 'PREPARE_STAGE', 'PUBLISH_ENTRY'):
    req('"' + phase + '"' in diag, 'diagnostic probe filter missing phase: ' + phase)
for suffix in ('.TITAN.NEW', '.TITAN.BK', '.TITAN.BAK'):
    req(suffix in diag, 'diagnostic probe filter missing suffix: ' + suffix)

# Keep single-owner architecture: no second EE filesystem worker.
req('installer_io_worker' not in installer, 'second EE filesystem worker regressed')
req('install_job_pump_main_thread' in installer, 'main-thread cooperative pump missing')

# Host libmc stubs cover native async data/metadata path for syntax validation.
for token in ('MC_WAIT', 'MC_NOWAIT', 'mcOpen', 'mcClose', 'mcRead', 'mcWrite', 'mcDelete', 'mcRename', 'mcGetDir'):
    req(token in libmc, 'host libmc stub missing: ' + token)

print('installer-adaptive-posix-recovery-v2.0.12: OK')
