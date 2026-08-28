from pathlib import Path
import subprocess, sys, tempfile
root=Path(sys.argv[1]).resolve()
s=(root/'scripts/run_buildfix27_target_validation.py').read_text()
for token in ['validate_ps2dev_bundle.py','validate_pfs_target_prereqs.py','apps/wlaunchelf','apps/project-nes','kernel/kernel.elf','--pcsx2-cmd','target-validation.json','MISSING_BUILD_ARTIFACTS']:
    assert token in s, token
assert 'mips64r5900el-ps2-elf-gcc' in s
assert 'shell=True' in s and 'args.pcsx2_cmd' in s, 'shell use must be restricted to explicit caller PCSX2 command'
# Empty/incomplete bundle must fail closed without touching a target build.
with tempfile.TemporaryDirectory(prefix='bf27_cp47_') as td:
    td=Path(td); ev=td/'evidence'; bundle=td/'bundle'; bundle.mkdir()
    cp=subprocess.run([sys.executable,str(root/'scripts/run_buildfix27_target_validation.py'),
                       '--root',str(root),'--bundle-root',str(bundle),'--evidence-dir',str(ev),'--require-complete'],
                      text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    assert cp.returncode==2, (cp.returncode,cp.stdout)
    assert 'BUILDFIX27_TARGET_STATUS=BLOCKED' in cp.stdout
    m=(ev/'target-validation.json').read_text()
    assert 'PS2DEV_BUNDLE_NOT_READY' in m and '"target_build": "BLOCKED"' in m
# Structurally complete synthetic bundle must pass CP45 intake and then fail at a real target step,
# never report TARGET_BUILD_STATUS=PASS from file presence alone.
with tempfile.TemporaryDirectory(prefix='bf27_cp47_full_') as td:
    td=Path(td); bundle=td/'ps2dev'; sdk=bundle/'ps2sdk'; ev=td/'evidence'
    for d in ['ee/bin','iop/bin','dvp/bin','bin']:
        (bundle/d).mkdir(parents=True,exist_ok=True)
    def tool(rel, machine=None):
        p=bundle/rel
        body='#!/bin/sh\n'
        if machine:
            body += 'if [ "$1" = "-dumpmachine" ]; then echo '+machine+'; exit 0; fi\n'
        body += 'if [ "$1" = "--version" ]; then echo synthetic; exit 0; fi\nexit 9\n'
        p.write_text(body); p.chmod(0o755)
    tool('ee/bin/mips64r5900el-ps2-elf-gcc','mips64r5900el-ps2-elf')
    tool('ee/bin/mips64r5900el-ps2-elf-ld')
    tool('iop/bin/mipsel-none-elf-gcc')
    tool('dvp/bin/dvp-as')
    for rel in ['iop/irx/ps2atad.irx','iop/irx/ps2hdd.irx','iop/irx/ps2fs.irx']:
        p=sdk/rel; p.parent.mkdir(parents=True,exist_ok=True); p.write_bytes(b'IRX')
    for rel in ['iop/dev9/atad/Makefile','iop/hdd/apa/Makefile','iop/hdd/pfs/Makefile']:
        p=sdk/rel; p.parent.mkdir(parents=True,exist_ok=True); p.write_text('# synthetic\n')
    cp=subprocess.run([sys.executable,str(root/'scripts/run_buildfix27_target_validation.py'),
                       '--root',str(root),'--bundle-root',str(bundle),'--evidence-dir',str(ev)],
                      text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    assert cp.returncode != 0, cp.stdout
    m=(ev/'target-validation.json').read_text()
    assert '"target_build": "BLOCKED"' in m
    assert 'STEP_FAILED:' in m, m
print('buildfix27-target-orchestrator: OK deterministic target build/evidence fail-closed gate')
