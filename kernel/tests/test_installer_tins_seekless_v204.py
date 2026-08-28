#!/usr/bin/env python3
from pathlib import Path
import subprocess
import sys
import tempfile

kernel = Path(__file__).resolve().parents[1]
project = kernel.parent
installer = (kernel / 'src/installer/titan_installer.c').read_text(encoding='utf-8')
container_c = (kernel / 'src/installer/titan_install_container.c').read_text(encoding='utf-8')

def req(cond, msg):
    if not cond:
        raise SystemExit('FAIL: ' + msg)

req('#define TITAN_INSTALLER_VERSION "0.2.' in installer, 'installer version marker missing')
req('read_exact_stream' in installer and 'TITAN_INSTALL_JOB_SKIP_PREFIX' in installer,
    'forward-only TINS stream path missing')
req('TITAN_INSTALL_JOB_READ_SOURCE' in installer and 'TITAN_INSTALL_JOB_WRITE_STAGE' in installer,
    'forward-only cooperative staging missing')
req('file_end = lseek' not in installer, 'SEEK_END still used by TINS preflight')
req('if (lseek(' not in installer, 'installer runtime still seeks the TINS stream')
req('TitanInstallContainer_ParseEntryRaw' in installer and 'table_crc = crc32_update' in installer,
    'table is not parsed/CRC checked sequentially')
req('job->remaining = s->container_payload_base;' in installer and 'TITAN_INSTALL_JOB_SKIP_PREFIX' in installer,
    'installation does not advance sequentially to payload')
req('TitanInstallContainer_ReadHeader' in installer, 'header reader missing')
read_header = container_c[container_c.index('int TitanInstallContainer_ReadHeader'):container_c.index('int TitanInstallContainer_ParseEntryRaw')]
req('if (lseek(' not in read_header, 'header reader still seeks before reading')

# Host-side format sanity: build and verify a synthetic multi-entry container.
with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    a = td/'a.bin'; b = td/'b.bin'; out = td/'test.TINS'
    a.write_bytes(bytes(range(256))*17)
    b.write_bytes(b'Project Titan seekless TINS\n'*311)
    tool = project/'tools/titan_tins.py'
    subprocess.run([sys.executable, str(tool), 'build', '--output', str(out),
                    '--file', f'SYSTEM:kernel.elf={a}', '--file', f'BOOT:titan.elf={b}'], check=True,
                   stdout=subprocess.DEVNULL)
    verify = subprocess.run([sys.executable, str(tool), 'verify', str(out)], check=True,
                            capture_output=True, text=True).stdout
    req('valid TINS v1' in verify and 'SYSTEM kernel.elf' in verify and 'BOOT titan.elf' in verify,
        'synthetic TINS format verification failed')

print('installer-tins-seekless-v2.0.4: OK')
