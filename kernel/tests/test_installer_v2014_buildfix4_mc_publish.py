#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[1]
s = (root/'src/installer/titan_installer.c').read_text(errors='replace')

def req(c,m):
    if not c: raise SystemExit('FAIL: '+m)
req(('#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf4"' in s or
     '#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf5"' in s or
     '#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf6"' in s), 'BF4-or-later version missing')
req('MC_PUBLISH_STREAM' in s and 'reason=mcserv-no-rename' in s, 'MC streaming publish telemetry missing')
req('if (install_path_is_mc(job->destination)) {' in s, 'MC publish must bypass rename-first path')
req('if (!install_path_is_mc(job->destination) &&' in s, 'MC publish must bypass delete-before-stream')
req('O_WRONLY | O_CREAT | O_TRUNC' in s and 'ASYNC_OPEN_PUBLISH_DEST' in s, 'streaming publish must truncate/create destination')
req('install_iop_open_flags_from_posix(flags)' in s, 'BF3 libmc flag translation must remain')
print('installer-v2.0.14-buildfix4-mc-publish: OK')
