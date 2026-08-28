#!/usr/bin/env python3
from pathlib import Path

root=Path(__file__).resolve().parents[1]
installer=(root/'src/installer/titan_installer.c').read_text(errors='replace')

def req(cond, msg):
    if not cond:
        raise SystemExit('FAIL: ' + msg)

if '#define TITAN_INSTALLER_VERSION "0.2.8-poc"' in installer:
    req('install_job_pump_main_thread' in installer, 'v2.0.8 single-owner successor missing')
    req('installer_io_worker' not in installer, 'v2.0.8 must retire the v2.0.7 second-thread worker')
    req('fileXioRename' in installer, 'rename compatibility fallback missing')
    req('TITAN_INSTALL_JOB_CHECK_EXISTING_READ' in installer, 'existing-file fast path missing')
    print('installer-async-transaction-v2.0.7: SUPERSEDED by v2.0.8 single-owner path')
    raise SystemExit(0)

req('#define TITAN_INSTALLER_VERSION "0.2.7-poc"' in installer, 'v2.0.7 version missing')
req('#define TITAN_INSTALLER_COPY_BUFFER (128u * 1024u)' in installer, '128 KiB transfer buffer missing')
req('#define TITAN_INSTALLER_IO_SLICE TITAN_INSTALLER_COPY_BUFFER' in installer, 'full transfer slice missing')
req('installer_io_worker' in installer and 'CreateThread(&thread)' in installer, 'EE async I/O worker missing')
req('TITAN_INSTALLER_IO_WORKER_PRIORITY 96' in installer, 'low-priority worker policy missing')
req('install_worker_poll(&s)' in installer, 'main UI loop does not poll worker')
req('render(&s);' in installer[installer.index('for (;;)'):], 'renderer loop missing while worker runs')
req('install_entry_requires_full_verify' in installer, 'tiered verify policy missing')
req('TITAN_INSTALLER_FULL_VERIFY_LIMIT' in installer, 'large-file verify threshold missing')
req('O staging não corresponde ao tamanho esperado' in installer, 'large-file size check missing')
req('TITAN_INSTALL_JOB_CHECK_EXISTING_READ' in installer, 'existing-file fast path missing')
req('e->unchanged = 1' in installer, 'unchanged file skip missing')
req('install_rename_compat' in installer and 'fileXioRename' in installer, 'rename compatibility fallback missing')
req('install_preserve_existing' in installer and 'install_copy_file_worker' in installer, 'copy backup fallback missing')
req('install_publish_staging' in installer, 'publish compatibility layer missing')
req('errno=%d, fileXio=%d' in installer, 'hardware publish diagnostics missing')
req('if (next > s->progress) s->progress = next;' in installer, 'monotonic progress missing')
print('installer-async-transaction-v2.0.7: OK')
