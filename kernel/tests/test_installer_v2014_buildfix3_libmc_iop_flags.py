#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
installer = (root / 'src/installer/titan_installer.c').read_text(encoding='utf-8', errors='replace')

def req(cond, msg):
    if not cond:
        raise SystemExit('FAIL: ' + msg)

req(('#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf3"' in installer or
     '#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf4"' in installer or
     '#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf5"' in installer or
     '#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf6"' in installer),
    'BuildFix3-or-later version missing')
req('install_iop_open_flags_from_posix' in installer, 'IOP flag translator missing')
for token in ('FIO_O_RDONLY', 'FIO_O_WRONLY', 'FIO_O_RDWR', 'FIO_O_CREAT', 'FIO_O_TRUNC'):
    req(token in installer, 'missing translated MC/FileXio flag: ' + token)

# INSTALLER(4).LOG: newlib O_RDONLY was 0x0 and the first mcRead on the
# successfully-opened BOOT.ELF.TITAN.NEW returned sceMcResDeniedPermit (-5).
# libmc forwards mode unchanged to MCSERV/MCMAN, so direct mcOpen must receive
# the IOP/FIO domain rather than raw newlib O_* values.
req(installer.count('mcOpen(port, 0, name, iop_flags)') >= 2,
    'direct libmc opens are not translated to IOP/FIO flags')
req('mcOpen(port, 0, name, flags)' not in installer,
    'raw newlib flags still reach direct mcOpen')
req('install_log_open_flag_translation_once("libmc-iop", path, flags, iop_flags)' in installer,
    'libmc IOP flag telemetry missing')

# Do not regress BuildFix2 header ordering while using FIO constants.
macro = installer.find('#define NEWLIB_PORT_AWARE')
io_common = installer.find('#include <io_common.h>')
filexio = installer.find('#include <fileXio_rpc.h>')
undef = installer.find('#undef NEWLIB_PORT_AWARE')
req(min(macro, io_common, filexio, undef) >= 0 and macro < io_common < filexio < undef,
    'NEWLIB_PORT_AWARE header window regressed')

print('installer-v2.0.14-buildfix3-libmc-iop-flags: OK')
