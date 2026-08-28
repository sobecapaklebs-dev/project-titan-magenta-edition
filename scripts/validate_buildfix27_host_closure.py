#!/usr/bin/env python3
import argparse, os, pathlib, shutil, subprocess, sys

def fail(msg):
    print('buildfix27-final-host-closure: FAIL:',msg,file=sys.stderr); raise SystemExit(2)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--root',default='.')
    ns=ap.parse_args(); root=pathlib.Path(ns.root).resolve(); kernel=root/'kernel'
    runner=kernel/'tests/run_host_tests.sh'
    if not runner.exists(): fail('runner missing')
    text=runner.read_text(errors='replace')
    marker='# BuildFix27 recovered material boundary through CP10.'
    if marker not in text: fail('BuildFix27 runner marker missing')
    segment=text.split(marker,1)[1]
    pre='set -euo pipefail\nPROJECT_ROOT='+shq(str(root))+'\nkernel_root='+shq(str(kernel))+'\n'
    env=os.environ.copy(); env.setdefault('TERM','xterm')
    cp=subprocess.run(['bash','-c',pre+segment],cwd=root,env=env)
    if cp.returncode: fail('official BuildFix27 runner segment failed')
    progress=(root/'docs/history/buildfix/BUILDFIX27_PROGRESS.md').read_text(errors='replace')
    if 'BLOCKED_EXTERNAL_MATERIALIZATION' not in progress: fail('FCEUmm blocker missing')
    if 'BLOCKED_EXTERNAL_BINARY' not in progress: fail('PFS IRX blocker missing')
    if 'R5900_TARGET: BLOCKED/NOT_EXECUTED' not in progress: fail('R5900 blocker missing')
    toolchain=all(shutil.which(x) for x in ('mips64r5900el-ps2-elf-gcc','mips64r5900el-ps2-elf-ld','dvp-as'))
    print('buildfix27-final-host-closure: HOST_READY=1')
    print('buildfix27-final-host-closure: TARGET_TOOLCHAIN_READY='+('1' if toolchain else '0'))
    print('buildfix27-final-host-closure: FCEUMM_MATERIALIZED=0')
    print('buildfix27-final-host-closure: PFS_TARGET_BINARIES_READY=0')

def shq(v):
    return "'"+v.replace("'","'\\''")+"'"
if __name__=='__main__': main()
