#!/usr/bin/env python3
from pathlib import Path
import re
import zlib

root = Path(__file__).resolve().parents[1]
installer = (root / 'src/installer/titan_installer.c').read_text(encoding='utf-8', errors='replace')
diag = (root / 'src/installer/titan_installer_diag.c').read_text(encoding='utf-8', errors='replace')
readme = root.parent / 'runtime/TITAN/audio/README.md'

def req(cond, msg):
    if not cond:
        raise SystemExit('FAIL: ' + msg)

req(('#define TITAN_INSTALLER_VERSION "0.2.13-poc"' in installer or '#define TITAN_INSTALLER_VERSION "0.2.14-poc' in installer), 'v2.0.13-or-later version missing')

# INSTALLER(2).LOG proved the CRC implementation itself was correct, but its
# incremental state was incorrectly seeded with 0xffffffff. The exact 1018-byte
# README reproduces expected=0x0412C4D9 vs bad=0x7D7F0893.
req(readme.is_file() and readme.stat().st_size == 1018, 'hardware-log README fixture missing')
data = readme.read_bytes()
req((zlib.crc32(data) & 0xffffffff) == 0x0412C4D9, 'README canonical CRC changed unexpectedly')
req('job->source_crc = 0u;' in installer, 'source CRC is not seeded with standard incremental zero')
req('job->verify_crc = 0u;' in installer, 'staging CRC is not seeded with standard incremental zero')
req('job->existing_crc = 0u;' in installer, 'existing-file CRC is not seeded with standard incremental zero')
prepare = installer[installer.index('case TITAN_INSTALL_JOB_PREPARE_STAGE:'):installer.index('case TITAN_INSTALL_JOB_CHECK_EXISTING_READ:')]
req('0xffffffffu' not in prepare, 'bad CRC seed survived in PREPARE_STAGE')

# TINS input must remain seekless POSIX/newlib: v2.0.11 direct FileXio read
# regressed the stream. Only OUTPUT on mass: moves to FileXio NOWAIT.
req('result = read(fd, buffer, cap);' in installer, 'POSIX TINS input path missing')
req('fileXioRead(' not in installer, 'direct FileXio TINS read regressed')
for token in ('install_output_open_step', 'install_output_write_step', 'install_output_close_step'):
    req(token in installer, 'async output helper missing: ' + token)
req('fileXioWrite(fd, buffer, (int)cap)' in installer, 'mass output is not FileXio NOWAIT')
req('fileXioOpen(path, flags, mode)' in installer, 'mass output async open missing')
req('fileXioClose(*fd)' in installer, 'mass output async close missing')
req('fileXioSetBlockMode(FXIO_NOWAIT)' in installer, 'FileXio output is not NOWAIT')
req('OUTPUT_BACKEND_FALLBACK' in installer and 'OPEN_OUTPUT_POSIX_FALLBACK' in installer,
    'safe output-open fallback missing')

# v2.0.12 spent the beginning of installation scanning the whole manifest for
# stale state, producing metadata stutters. New runs recover stale staging when
# each entry is actually processed instead of global sweep.
begin = installer[installer.index('static int install_job_begin('):installer.index('static int install_job_step(')]
req('job->phase = TITAN_INSTALL_JOB_OPEN_CONTAINER;' in begin, 'full-manifest recovery still runs before payload I/O')
req('RECOVERY_POLICY' in installer and 'full_manifest_sweep=0' in installer, 'recovery policy telemetry missing')
req('job->current_entry_touched = 1;' in installer, 'entry touch tracking missing')
req('job->rollback_index = job->entry_index + (job->current_entry_touched ? 1u : 0u);' in installer,
    'rollback still scans untouched future entries')
req('!install_entry_enabled(s, e) || e->unchanged' in installer, 'rollback does not skip unchanged entries')

# Stale staging cleanup is best-effort. A libmc -4 from an unrelated old BOOT
# staging must not abort entry 1 or trigger irrelevant BOOT rollback work.
req('STALE_STAGE_REMOVE_NONFATAL' in installer, 'nonfatal stale staging policy missing')
req('O_TRUNC below is the authoritative test' in installer, 'stale-stage fallback rationale missing')

# Parent mkdir failures such as errno=8 seen on hardware are not authoritative;
# output-open is. Keep them in metrics without reporting a false fatal error.
req('MKDIR_OPTIONAL' in installer, 'optional mkdir telemetry missing')
req('strcmp(op, "MKDIR_OPTIONAL") == 0' in diag, 'optional mkdir failures still flood error log')
req('OPEN_EXISTING' in diag and 'REMOVE_STALE_STAGE' in diag, 'expected missing-entry probes still flood diagnostics')

# v2.0.13 moved the final marker to async FileXio. BuildFix6 supersedes only
# this tiny final operation after real hardware showed an unbounded NOWAIT
# completion at 97%; payload I/O remains cooperative/async.
marker_async = 'ASYNC_OPEN_COMPLETE_MARKER' in installer and 'ASYNC_WRITE_COMPLETE_MARKER' in installer
marker_bf6 = 'OPEN_COMPLETE_MARKER_POSIX' in installer and 'WRITE_COMPLETE_MARKER_POSIX' in installer
req(marker_async or marker_bf6, 'INSTALL.COMPLETE commit path missing')

# Single EE owner remains mandatory; async means IOP RPC completion polling,
# not a second EE filesystem worker.
req('installer_io_worker' not in installer, 'second EE filesystem worker regressed')
req('install_job_pump_main_thread' in installer, 'main-thread state-machine pump missing')

print('installer-hardware-log-fixes-v2.0.13: OK')
