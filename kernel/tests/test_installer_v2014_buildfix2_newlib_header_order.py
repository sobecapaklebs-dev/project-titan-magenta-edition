#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
installer = (root / 'src/installer/titan_installer.c').read_text(encoding='utf-8', errors='replace')

def req(cond, msg):
    if not cond:
        raise SystemExit('FAIL: ' + msg)

req(('#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf2"' in installer or
     '#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf3"' in installer or
     '#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf4"' in installer or
     '#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf5"' in installer or
     '#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf6"' in installer),
    'BuildFix2-or-later version missing')
macro = installer.find('#define NEWLIB_PORT_AWARE')
io_common = installer.find('#include <io_common.h>')
filexio = installer.find('#include <fileXio_rpc.h>')
undef = installer.find('#undef NEWLIB_PORT_AWARE')
req(min(macro, io_common, filexio, undef) >= 0, 'NEWLIB/FileXio header guard sequence incomplete')
req(macro < io_common < filexio < undef,
    'io_common.h/fileXio_rpc.h must stay inside NEWLIB_PORT_AWARE opt-in window')
req(installer.count('#include <io_common.h>') == 1,
    'io_common.h must be included exactly once')

# Retain BuildFix1 flag-domain correction.
for token in ('FIO_O_RDONLY', 'FIO_O_WRONLY', 'FIO_O_RDWR', 'FIO_O_APPEND',
              'FIO_O_CREAT', 'FIO_O_TRUNC', 'FIO_O_EXCL'):
    req(token in installer, 'missing IOP open flag: ' + token)
req('fileXioOpen(path, iop_flags, mode)' in installer,
    'FileXio open no longer receives translated FIO_O_* flags')
if ('#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf3"' in installer or
    '#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf4"' in installer or
    '#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf5"' in installer or
     '#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf6"' in installer):
    req(installer.count('mcOpen(port, 0, name, iop_flags)') >= 2,
        'BuildFix3 libmc IOP translation missing')
else:
    req(installer.count('mcOpen(port, 0, name, flags)') >= 2,
        'BuildFix2 libmc behavior changed unexpectedly')

print('installer-v2.0.14-buildfix2-newlib-header-order: OK')
