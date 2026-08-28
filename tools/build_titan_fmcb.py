#!/usr/bin/env python3
from __future__ import annotations
import argparse, os, shutil, subprocess
from pathlib import Path


def run(cmd, cwd, env):
    print('+', ' '.join(map(str, cmd)))
    subprocess.run(cmd, cwd=cwd, env=env, check=True)


def main() -> int:
    ap = argparse.ArgumentParser(description='Build the FMCB backend integrated into the Titan installer.')
    ap.add_argument('--project', type=Path, default=Path(__file__).resolve().parents[1])
    ap.add_argument('--output', type=Path, default=None)
    args = ap.parse_args()
    root = args.project.resolve()
    out = (args.output or root / 'build/fmcb').resolve()
    if not os.environ.get('PS2DEV') or not os.environ.get('PS2SDK'):
        raise SystemExit('PS2DEV and PS2SDK must be set')

    env = os.environ.copy()
    out.mkdir(parents=True, exist_ok=True)
    integrated = root / 'third_party/fmcb/integrated'

    # Build the upstream installation/signing engine as a static library that is
    # linked into titan-installer.elf. There is intentionally no second installer
    # process or user-visible FMCB executable in the release path.
    run(['make', 'clean'], integrated, env)
    run(['make'], integrated, env)
    library = integrated / 'build/libtitanfmcb_integrated.a'
    if not library.is_file() or library.stat().st_size == 0:
        raise SystemExit('integrated FMCB backend library was not produced')
    shutil.copy2(library, out / library.name)

    # FMCB autoboots this tiny Titan-owned entry, which then chains to the
    # validated BuildFix6 bootloader at mc?:/BOOT/BOOT.ELF.
    run(['make', 'clean'], root / 'boot/fmcb_entry', env)
    run(['make'], root / 'boot/fmcb_entry', env)
    entry = root / 'titan-fmcb-entry.elf'
    if not entry.is_file():
        raise SystemExit('titan-fmcb-entry.elf was not produced')
    shutil.copy2(entry, out / 'titan-fmcb-entry.elf')

    print('FMCB_INTEGRATED_BUILD_READY=1')
    print('FMCB_STANDALONE_INSTALLER_PACKAGED=0')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
