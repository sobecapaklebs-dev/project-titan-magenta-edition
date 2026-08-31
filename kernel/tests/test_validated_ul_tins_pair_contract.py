#!/usr/bin/env python3
from pathlib import Path
import subprocess, tempfile, sys
root=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
builder=root/'tools/build_titan_installer_media.py'
tins_tool=root/'tools/titan_tins.py'
installer_script=root/'scripts/install_to_usb.sh'
build_all=root/'scripts/build_all.sh'
diagnostic_sweep=root/'scripts/build_diagnostic_sweep.sh'

def req(v,m):
    if not v: raise SystemExit('validated-ul-tins-pair: FAIL: '+m)

builder_text=builder.read_text(encoding='utf-8')
install_text=installer_script.read_text(encoding='utf-8')
build_all_text=build_all.read_text(encoding='utf-8')
diagnostic_text=diagnostic_sweep.read_text(encoding='utf-8')
req('rel == "TITAN/kernel.elf"' in builder_text,'builder canonical-kernel exclusion missing')
req('"TITAN/kernel.elf", "TITAN/titan_ul_loader.elf"' not in builder_text,'builder still excludes UL loader')
req('filecmp.cmp(ul_engine, ul_loader, shallow=False)' in builder_text,'builder does not enforce UL pair identity')
req('"TITAN/titan_ul_loader.elf"' in install_text,'USB installer does not require UL loader')
req('for legacy_relative in "TITAN/kernel.elf";' in install_text,'legacy journal is not kernel-only')
req('for legacy in "$TARGET/TITAN/kernel.elf";' in install_text,'legacy disable is not kernel-only')
req('\"$output/TITAN/titan_ul_loader.elf\"' in build_all_text,'build_all does not materialize validated UL loader alias')
req('\"$output/TITAN/titan_ul_loader.elf\"' in diagnostic_text,'diagnostic sweep does not materialize validated UL loader alias')

with tempfile.TemporaryDirectory(prefix='titan_ul_tins_pair_') as td0:
    td=Path(td0)
    runtime=td/'runtime'; (runtime/'TITAN').mkdir(parents=True); (runtime/'neutrino').mkdir()
    runtime.joinpath('kernel.elf').write_bytes(b'kernel')
    payload=b'validated-ul-runtime'
    runtime.joinpath('TITAN/titan_ul_engine.elf').write_bytes(payload)
    runtime.joinpath('TITAN/titan_ul_loader.elf').write_bytes(payload)
    runtime.joinpath('neutrino/neutrino.elf').write_bytes(b'neutrino')
    installer=td/'titan-installer.elf'; installer.write_bytes(b'installer')
    boot=td/'BOOT.ELF'; boot.write_bytes(b'boot')
    out=td/'media'
    r=subprocess.run(['python3',str(builder),'--project',str(root),'--runtime',str(runtime),
                      '--installer-elf',str(installer),'--bootloader-elf',str(boot),'--output',str(out)],
                     capture_output=True,text=True)
    req(r.returncode==0,'builder rejected valid UL pair: '+r.stderr)
    vr=subprocess.run(['python3',str(tins_tool),'verify',str(out/'TITAN_INSTALL/TITAN_SYSTEM.TINS')],capture_output=True,text=True)
    req(vr.returncode==0,'generated TINS failed verification: '+vr.stderr)
    req('SYSTEM TITAN/titan_ul_engine.elf' in vr.stdout,'TINS missing UL engine')
    req('SYSTEM TITAN/titan_ul_loader.elf' in vr.stdout,'TINS missing UL loader')

    # Divergent compatibility names must fail closed.
    runtime.joinpath('TITAN/titan_ul_loader.elf').write_bytes(b'wrong-loader')
    bad=subprocess.run(['python3',str(builder),'--project',str(root),'--runtime',str(runtime),
                        '--installer-elf',str(installer),'--bootloader-elf',str(boot),'--output',str(td/'bad-media')],
                       capture_output=True,text=True)
    req(bad.returncode!=0 and 'compatibility pair diverged' in (bad.stdout+bad.stderr),'builder accepted divergent UL pair')

    # Transactional direct-to-USB path must preserve the live loader alias too.
    runtime.joinpath('TITAN/titan_ul_loader.elf').write_bytes(payload)
    runtime.joinpath('titan.elf').write_bytes(b'titan')
    target=td/'usb'; target.mkdir()
    run=subprocess.run(['bash',str(installer_script),'--target',str(target),'--package',str(runtime)],capture_output=True,text=True)
    req(run.returncode==0,'USB install simulation failed: '+run.stdout+'\n'+run.stderr)
    req((target/'TITAN/titan_ul_engine.elf').read_bytes()==payload,'USB engine bytes changed')
    req((target/'TITAN/titan_ul_loader.elf').read_bytes()==payload,'USB loader missing/disabled')
    req(not list((target/'TITAN').glob('titan_ul_loader.elf.legacy-disabled-*')),'loader was renamed legacy-disabled')

print('validated-ul-tins-pair: OK engine+loader survive TINS and transactional USB install; divergence fails closed')
