#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
installer = (root / 'src/installer/titan_installer.c').read_text(errors='replace')
scheduler = (root / 'src/services/titan_storage_scheduler.c').read_text(errors='replace')

def req(cond, msg):
    if not cond:
        raise SystemExit('FAIL: ' + msg)

req('#define TITAN_INSTALLER_VERSION "0.2.8-poc"' in installer, 'v2.0.8 version missing')
req('#define TITAN_INSTALLER_COPY_BUFFER (32u * 1024u)' in installer, '32 KiB I/O quantum missing')
req('#define TITAN_INSTALLER_IO_FRAME_BUDGET (64u * 1024u)' in installer, '64 KiB frame budget missing')
req('#define TITAN_INSTALLER_IO_FRAME_BUDGET_MS 10u' in installer, 'time budget missing')
req('install_job_pump_main_thread' in installer, 'main-thread bounded I/O pump missing')
req('TitanTime_NowMilliseconds()' in installer, 'adaptive time budget missing')
req('installer_io_worker' not in installer, 'second EE FileXio worker must be removed')
req('CreateThread(&thread)' not in installer, 'installer must not create a filesystem worker thread')
req('install_worker_poll' not in installer and 'install_worker_start' not in installer, 'worker polling/start path remains')
req('install_result = install_job_pump_main_thread(&s);' in installer, 'installing page does not pump main-thread I/O')
req('TITAN_INSTALL_JOB_PUBLISH_ENTRY' in installer, 'per-entry publish phase missing')
req('job->phase = TITAN_INSTALL_JOB_PUBLISH_ENTRY;' in installer, 'validated file does not transition to immediate publish')
req('job->entry_index++;\n            job->phase = TITAN_INSTALL_JOB_PREPARE_ENTRY;' in installer, 'publish does not advance entry immediately')
req('Publicando %.112s' in installer, 'per-entry publish status missing')
req('fileXioRename' in installer, 'fileXio rename hardware fallback missing')
req('install_copy_file_worker' not in installer, 'hidden whole-file synchronous copy fallback remains')
for phase in ('TITAN_INSTALL_JOB_BACKUP_COPY_READ','TITAN_INSTALL_JOB_BACKUP_COPY_WRITE','TITAN_INSTALL_JOB_PUBLISH_COPY_READ','TITAN_INSTALL_JOB_PUBLISH_COPY_WRITE'):
    req(phase in installer, 'bounded rename-copy fallback phase missing: ' + phase)
req('real hardware regressed to 0%%' in scheduler, 'canonical single-owner hardware regression note missing')
req('main thread becomes the sole' in scheduler, 'canonical FileXio ownership policy missing')
print('installer-single-owner-fast-stream-v2.0.8: OK')
