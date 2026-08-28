#!/usr/bin/env python3
from pathlib import Path
import zlib

root = Path(__file__).resolve().parents[1]
installer = (root / 'src/installer/titan_installer.c').read_text(encoding='utf-8', errors='replace')
diag = (root / 'src/installer/titan_installer_diag.c').read_text(encoding='utf-8', errors='replace')
readme = root.parent / 'runtime/TITAN/audio/README.md'

def req(cond, msg):
    if not cond:
        raise SystemExit('FAIL: ' + msg)

req('#define TITAN_INSTALLER_VERSION "0.2.14-poc' in installer, 'v2.0.14 family version missing')

# INSTALLER(3).LOG: FileXio open completed with fd=3, but WRITE completed with
# 0 bytes. Direct FileXio uses FIO_O_*; do not pass libc O_* unchanged.
req('install_iop_open_flags_from_posix' in installer, 'POSIX -> IOP open flag translator missing')
for token in ('FIO_O_RDONLY', 'FIO_O_WRONLY', 'FIO_O_RDWR', 'FIO_O_CREAT', 'FIO_O_TRUNC'):
    req(token in installer, 'missing IOP open flag: ' + token)
req('fileXioOpen(path, iop_flags, mode)' in installer, 'FileXio still receives raw POSIX flags')
req('fileXioOpen(path, flags, mode)' not in installer, 'raw POSIX FileXio open survived')
if ('#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf3"' in installer or
    '#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf4"' in installer or
    '#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf5"' in installer or
    '#define TITAN_INSTALLER_VERSION "0.2.14-poc-bf6"' in installer):
    req(installer.count('mcOpen(port, 0, name, iop_flags)') >= 2, 'libmc must receive translated IOP mode on BuildFix3')
else:
    req('mcOpen(port, 0, name, flags)' in installer, 'pre-BuildFix3 libmc behavior changed unexpectedly')
req('OPEN_FLAG_TRANSLATION' in installer, 'one-shot open flag telemetry missing')

# The v2.0.11 input regression is now explained by the same raw-open-flag bug.
# Keep the forward-only state machine but run data RPCs asynchronously so USB
# reads do not consume the renderer frame budget.
req('fileXioRead(fd, buffer, (int)cap)' in installer, 'mass input is not FileXio NOWAIT')
req('fileXioSetBlockMode(FXIO_NOWAIT)' in installer, 'FileXio NOWAIT missing')
req('fileXioWaitAsync(FXIO_NOWAIT, &result)' in installer, 'async completion poll missing')
req('installer_io_worker' not in installer, 'second EE filesystem worker regressed')
req('install_job_pump_main_thread' in installer, 'single-owner main-thread pump missing')

# INSTALLER(3).LOG: the 3283 ms gap happened immediately at install-start with
# install_pump_ms=0. Checkpoints must never persist the full ring mid-install.
checkpoint = diag[diag.index('int TitanInstallerDiag_FlushCheckpoint('):diag.index('int TitanInstallerDiag_FlushFinal(')]
req('diag_write_snapshot_to' not in checkpoint, 'checkpoint still writes the RAM ring to mass:')
req('persistence=ram-only' in checkpoint, 'RAM-only checkpoint telemetry missing')
req('diag_write_snapshot_to' in diag[diag.index('int TitanInstallerDiag_FlushFinal('):], 'final persistent diagnostic flush removed')

# Async OPEN returns a file descriptor. It must not be counted as transferred
# bytes merely because the operation name contains WRITE.
req('requested_bytes > 0u && result > 0' in diag, 'async open fd can still be counted as write bytes')
req('ASYNC_ZERO_TRANSFER' in diag, 'zero-byte completed async transfer is not preserved explicitly')

# First-install absence and transactional existence probes are not I/O errors.
req('TITAN_INSTALL/INSTALLER.CONSUMED' in diag, 'missing INSTALLER.CONSUMED expected-absence suppression')
req('During transactional phases STAT is an existence probe' in diag, 'transactional STAT suppression missing')

# Keep v2.0.13 CRC correction intact. INSTALLER(3).LOG no longer showed the CRC mismatch.
req(readme.is_file() and readme.stat().st_size == 1018, 'README fixture changed')
req((zlib.crc32(readme.read_bytes()) & 0xffffffff) == 0x0412C4D9, 'README canonical CRC changed')
for token in ('job->source_crc = 0u;', 'job->verify_crc = 0u;', 'job->existing_crc = 0u;'):
    req(token in installer, 'CRC zero seed regressed: ' + token)

# Keep metadata work below one 60 Hz interval as much as possible; long payload
# transfers are now async and do not need a large synchronous pump budget.
req('#define TITAN_INSTALLER_IO_FRAME_BUDGET_MS 4u' in installer, 'metadata time budget not tightened')
req('#define TITAN_INSTALLER_META_STEPS_PER_FRAME 6u' in installer, 'metadata step budget not tightened')

print('installer-rpc-flags-checkpoint-v2.0.14: OK')
