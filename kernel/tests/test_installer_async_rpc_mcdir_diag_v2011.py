#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
installer = (root / 'src/installer/titan_installer.c').read_text(errors='replace')
diag = (root / 'src/installer/titan_installer_diag.c').read_text(errors='replace')
fxio = (root / 'tests/host_stubs/fileXio_rpc.h').read_text(errors='replace')
libmc = (root / 'tests/host_stubs/libmc.h').read_text(errors='replace')

def req(cond, msg):
    if not cond:
        raise SystemExit('FAIL: ' + msg)

req('#define TITAN_INSTALLER_VERSION "0.2.11-poc"' in installer, 'v2.0.11 version missing')
req('TITAN_INSTALLER_MC_DIR_ENTRIES 128u' in installer, 'MC directory enumeration capacity missing')
req('mcGetDir(s->memory_card' in installer and '"/BOOT/*"' in installer, 'BOOT backup enumeration is not using libmc mcGetDir')
req('mcSync(1, 0, &dir_result)' in installer, 'MC directory enumeration is blocking instead of polled')
req('EntryName' in installer and 'install_mc_dir_contains' in installer, 'MC directory entries are not inspected by name')
req('install_allocate_backup_path_step' in installer, 'incremental backup allocator missing')
req('static int install_allocate_backup_path(' not in installer, 'old stat-loop backup allocator still active')

req('fileXioSetBlockMode(FXIO_NOWAIT)' in installer, 'FileXio NOWAIT mode missing')
req('fileXioWaitAsync(FXIO_NOWAIT' in installer, 'FileXio completion polling missing')
for token in (
    'ASYNC_READ_TINS', 'ASYNC_WRITE_STAGE', 'ASYNC_READ_STAGE_VERIFY',
    'ASYNC_RENAME_BACKUP', 'ASYNC_RENAME_PUBLISH',
    'ASYNC_ROLLBACK_REMOVE_STAGE', 'ASYNC_ROLLBACK_REMOVE_DEST',
    'ASYNC_ROLLBACK_RESTORE_RENAME', 'ASYNC_REMOVE_PUBLISHED_STAGE',
):
    req(token in installer, 'missing async transaction operation: ' + token)
req('installer_io_worker' not in installer, 'second EE filesystem worker regressed')
req('install_job_pump_main_thread' in installer, 'single-owner UI-thread pump missing')
req('job->async_io.kind != TITAN_INSTALLER_ASYNC_NONE' in installer, 'pump does not yield while RPC is outstanding')

req('STUTTER_AGGREGATE' in diag, 'stutter aggregation missing')
req('g_diag_stutter_agg_count' in diag, 'stutter aggregate counters missing')
req('strncmp(op, "ASYNC_", 6u) == 0' in diag, 'async I/O is not distinguished from frame-blocking I/O')
req('if (!async_op)' in diag, 'async service latency still contaminates frame blocking metrics')
req('ASYNC_IO_LATENCY' in diag and 'ASYNC_IO_AGGREGATE' in diag, 'async service latency diagnostics missing')
req('TITAN_INSTALLER_DIAG_SLOW_IO_LOG_MS 50u' in diag, 'diagnostic flood threshold missing')
req('frame_elapsed_ms >= 1000u' in diag, 'severe individual stutters are not preserved')

for token in ('FXIO_NOWAIT', 'FXIO_COMPLETE', 'FXIO_INCOMPLETE', 'fileXioWaitAsync', 'fileXioRead', 'fileXioWrite'):
    req(token in fxio, 'host FileXio stub missing: ' + token)
req('sceMcTblGetDir' in libmc and 'mcGetDir' in libmc, 'host libmc directory stubs missing')

print('installer-async-rpc-mcdir-diagnostics-v2.0.11: OK')
