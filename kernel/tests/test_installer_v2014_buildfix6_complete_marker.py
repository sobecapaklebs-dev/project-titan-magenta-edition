from pathlib import Path

root = Path(__file__).resolve().parents[1]
installer = (root / 'src/installer/titan_installer.c').read_text()

def req(cond, msg):
    if not cond:
        raise SystemExit(msg)

req('#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf6"' in installer, 'BF6 version missing')
req('OPEN_COMPLETE_MARKER_POSIX' in installer, 'BF6 POSIX marker open missing')
req('WRITE_COMPLETE_MARKER_POSIX' in installer, 'BF6 POSIX marker write missing')
req('CLOSE_COMPLETE_MARKER_POSIX' in installer, 'BF6 POSIX marker close telemetry missing')
req('COMPLETE_MARKER_COMMITTED' in installer, 'BF6 marker commit telemetry missing')
req('ASYNC_OPEN_COMPLETE_MARKER' not in installer, 'BF6 still starts async marker OPEN')
req('ASYNC_WRITE_COMPLETE_MARKER' not in installer, 'BF6 still starts async marker WRITE')
req('ASYNC_CLOSE_COMPLETE_MARKER' not in installer, 'BF6 still starts async marker CLOSE')
req('s->progress = 98u;' in installer, 'post-marker progress transition missing')
req('job->phase = TITAN_INSTALL_JOB_CLEAN_BACKUPS;' in installer, 'post-marker cleanup transition missing')
# Payload and MC transport fixes must remain present.
req('ASYNC_WRITE_PUBLISH' in installer, 'cooperative payload publish path regressed')
req('.MC_STAGE_%03u.TITAN.NEW' in installer, 'BF5 USB staging for MC payload regressed')
req('install_iop_open_flags_from_posix' in installer, 'BF3 libmc/FileXio flag translation regressed')
print('installer-v2.0.14-buildfix6-complete-marker: OK')
