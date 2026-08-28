#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
installer = (root / 'src/installer/titan_installer.c').read_text(encoding='utf-8', errors='replace')

def req(cond, msg):
    if not cond:
        raise SystemExit('FAIL: ' + msg)

req(('#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf1"' in installer or
     '#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf2"' in installer or
     '#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf3"' in installer or
     '#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf4"' in installer or
     '#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf5"' in installer or
     '#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf6"' in installer),
    'BuildFix1-or-later version missing')
req('#include <io_common.h>' in installer, 'io_common.h must be included explicitly for FIO_O_* macros')
for token in ('FIO_O_RDONLY', 'FIO_O_WRONLY', 'FIO_O_RDWR', 'FIO_O_APPEND', 'FIO_O_CREAT', 'FIO_O_TRUNC', 'FIO_O_EXCL'):
    req(token in installer, 'missing FileXio mode constant: ' + token)

# fileXioOpen is an IOP RPC and must receive translated FIO_O_* flags.
req('fileXioOpen(path, iop_flags, mode)' in installer, 'FileXio open no longer receives translated flags')

# BuildFix3 supersedes the original libmc flag-domain assumption after real-hardware
# O_RDONLY=0 produced sceMcResDeniedPermit on the first mcRead().
if ('#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf3"' in installer or
    '#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf4"' in installer or
    '#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf5"' in installer or
     '#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf6"' in installer):
    req(installer.count('mcOpen(port, 0, name, iop_flags)') >= 2, 'BuildFix3 libmc IOP translation missing')
else:
    req(installer.count('mcOpen(port, 0, name, flags)') >= 2, 'libmc BuildFix1/2 behavior changed unexpectedly')
req('backend != 0 && strncmp(backend, "libmc", 5u) == 0' in installer, 'libmc telemetry gate missing')

print('installer-v2.0.14-buildfix1-headers: OK')
