#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
s = (root / 'src/installer/titan_installer.c').read_text(encoding='utf-8', errors='replace')

def req(cond, msg):
    if not cond:
        raise SystemExit('FAIL: ' + msg)

req(('#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf5"' in s or '#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf6"' in s), 'BF5-or-later version missing')

# BF4 froze on real hardware after entering the MC->MC publish stream. MC-bound
# staging must now live on the installer USB, never beside the MC destination.
req('static void install_build_temporary_path' in s, 'temporary-path router missing')
req('%s/.MC_STAGE_%03u.TITAN.NEW' in s, 'USB MC-stage path missing')
req(s.count('install_build_temporary_path(') >= 4,
    'MC-aware stage path is not used by prepare/recovery/rollback')

# Final MC publication is USB -> MC. Do not rename or delete the MC destination
# before the authoritative O_TRUNC open.
req('MC_PUBLISH_STREAM' in s and 'reason=mcserv-no-rename' in s,
    'MC publish telemetry missing')
req('if (install_path_is_mc(job->destination)) {' in s,
    'MC publish does not bypass rename-first path')
req('if (!install_path_is_mc(job->destination) &&' in s,
    'MC publish still deletes destination before stream')
req('ASYNC_OPEN_PUBLISH_SOURCE' in s and 'ASYNC_OPEN_PUBLISH_DEST' in s,
    'USB->MC publication open states missing')
req('if (!install_path_is_mc(job->destination))\n                size_ok = size_ok && install_stat_size(job->destination' in s,
    'MC publish finish still performs synchronous stat')

# Existing BOOT backup and rollback also use USB as the transport hop. This
# prevents any transactional state from copying MC fd -> MC fd.
req('%s/.MC_BACKUP_%03u.TMP' in s, 'USB MC-backup transport path missing')
req('backup_transport_phase = 1u' in s and 'backup_transport_phase = 2u' in s,
    'two-hop MC backup transport state missing')
req('backup_transport_ready' in s and 'e->backup_transport_ready = 1;' in s,
    'rollback can not distinguish complete vs partial USB backup transport')
for phase in ('ROLLBACK_RESTORE_OPEN', 'ROLLBACK_RESTORE_READ',
              'ROLLBACK_RESTORE_WRITE', 'ROLLBACK_RESTORE_FINISH'):
    req('TITAN_INSTALL_JOB_' + phase in s, 'missing rollback USB->MC phase: ' + phase)
req('ASYNC_OPEN_ROLLBACK_USB_BACKUP' in s and 'ASYNC_OPEN_ROLLBACK_MC_DEST' in s,
    'rollback USB->MC endpoints missing')
req('job->open_step == 1u' in s[s.index('case TITAN_INSTALL_JOB_ROLLBACK_RESTORE_OPEN:'):],
    'rollback async opens are not sequenced with open_step')

# BF3 IOP flag fix and BF2 newlib header ordering remain mandatory.
req(s.count('mcOpen(port, 0, name, iop_flags)') >= 2,
    'translated libmc flags regressed')
macro = s.find('#define NEWLIB_PORT_AWARE')
io_common = s.find('#include <io_common.h>')
filexio = s.find('#include <fileXio_rpc.h>')
undef = s.find('#undef NEWLIB_PORT_AWARE')
req(min(macro, io_common, filexio, undef) >= 0 and macro < io_common < filexio < undef,
    'NEWLIB_PORT_AWARE include window regressed')

print('installer-v2.0.14-buildfix5-mc-transport: OK')
