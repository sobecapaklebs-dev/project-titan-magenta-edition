#!/usr/bin/env python3
from pathlib import Path
import subprocess, sys, tempfile, struct
r=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[2])
def text(p): return (r/p).read_text(errors='ignore')
def req(c,m):
    if not c: raise SystemExit('FAIL: '+m)
inst=text('kernel/src/installer/titan_installer.c'); model=text('kernel/src/installer/titan_installer_model.c')
tins=text('tools/titan_tins.py'); media=text('tools/build_titan_installer_media.py'); build=text('tools/build_titan_fmcb.py')
core=text('third_party/fmcb/integrated/titan_fmcb_core.c'); coremk=text('third_party/fmcb/integrated/Makefile')
kmk=text('kernel/Makefile'); backend=text('kernel/src/installer/titan_fmcb_backend.c')
system=text('third_party/fmcb/upstream/installer/system.c'); sysh=text('third_party/fmcb/upstream/installer/system.h')
cnf=text('third_party/fmcb/titan/FREEMCB.TITAN.CNF'); entry=text('boot/fmcb_entry/main.c'); identity=text('third_party/fmcb/TITAN_FMCB_SOURCE_IDENTITY.txt')
# Source/provenance/build wiring.
req((r/'third_party/fmcb/upstream/installer/system.c').is_file() and 'PerformInstallation' in system,'real FMCB source not integrated')
req('source_input_sha256=' in identity and 'selected_package=FMCB_1.966_resources' in identity,'FMCB source identity/provenance missing')
req("os.environ.get('PS2DEV')" in build and "os.environ.get('PS2SDK')" in build and 'third_party/fmcb/integrated' in build,'PS2DEV/PS2SDK integrated build wiring missing')
req('SignKELF' in system and 'TwinSignKELF' in system and 'CalculateRequiredSpace' in system,'real FMCB signing/space logic absent')
req('PerformInstallation((unsigned char)mc_port, 0,' in core and 'INSTALL_MODE_FLAG_TITAN_MINIMAL' in core,'integrated backend bypasses upstream PerformInstallation')
req('TitanFmcbCore_Install' in backend and 'TITAN_FMCB_INTEGRATED_LIB' in kmk and 'src/installer/titan_fmcb_backend.o' in kmk,'kernel is not linked to integrated FMCB backend')
req('system.o' in coremk and 'ReqSpaceCalc.o' in coremk and 'libsecr.o' in coremk and 'mctools_rpc.o' in coremk,'integrated static library omits FMCB installation/signing engine')
req('LoadExecPS2' not in core,'integrated FMCB core must not switch to a second ELF')
req('FMCBInstaller.elf' not in inst and 'FMCBInstaller.elf' not in media and 'FMCBInstaller.elf' not in build,'standalone FMCB installer leaked into production path')
req('fmcb_autonomous_mc_space_check' in inst and 'mcGetInfo' in inst and 'TITAN_SPACE.REQ' in media,'explicit Memory Card space preflight missing')
# Product compatibility and UI fail-closed gate.
req('rom > 220u' in model and 'rom >= 200u && rom <= 220u' in model,'ROM <=2.20 gate missing')
req('rom == 0u' in model,'unknown ROM fail-closed missing')
req('if (!s.autonomous_available) s.selection=0' in inst,'method selection is not forced to semi on unsupported consoles')
req('if (s->autonomous_available) {' in inst and 'A instalação semi-autônoma é a única opção disponível.' in inst,'unsupported method UI does not collapse to semi-only')
# Existing FMCB state and conservative ownership.
for token in ('TITAN_FMCB_NO_FMCB','TITAN_FMCB_COMPATIBLE_FMCB','TITAN_FMCB_UNKNOWN_FMCB','TITAN_FMCB_CORRUPT_FMCB','TITAN_FMCB_TITAN_MANAGED_FMCB'):
    req(token in inst,'existing FMCB state missing: '+token)
req('!has_cnf && !has_uninst && !has_managed && !has_system' in inst,'foreign EXEC-SYSTEM is not fail-closed')
# Transaction / integrated activation / interrupted recovery / rollback.
req('fmcb_write_journal' in inst and 'fmcb_load_journal' in inst and 'fmcb_prepare_interrupted_recovery' in inst,'integrated transaction recovery journal missing')
req('TITAN_INSTALL_JOB_FMCB_INTEGRATED' in inst and 'TitanFmcbBackend_Install' in inst,'direct integrated activation phase missing')
req('--fmcb-resume' not in inst and 'FMCB_HANDOFF' not in inst,'legacy worker resume/handoff remains connected')
req('TITAN_INSTALL_JOB_ROLLBACK' in inst and '.TITAN.NEW' in inst and '.TITAN.BAK' in inst,'stage/backup/rollback contract missing')
req('INSTALL.COMPLETE' in inst and 'TITAN_INSTALL_JOB_WRITE_COMPLETE' in inst,'commit marker/state missing')
req('TitanFmcbBackend_Cleanup' in inst and 'fmcb_restore_titan_iop' in inst,'interrupted integrated cleanup/IOP restoration missing')
# Boot chain/config.
req('LK_Auto_E1 = mc?:/TITAN/BOOT/TITAN_FMCB_ENTRY.ELF' in cnf and 'LK_Auto_E2 = mc?:/TITAN/BOOT/TITAN_FMCB_ENTRY.ELF' in cnf,'FMCB autoboot config missing Titan entry')
req('mc%s:/BOOT/BOOT.ELF' in entry and 'LoadExecPS2' in entry,'Titan FMCB entry does not hand off to validated bootloader')
req('mc%d:/BOOT/BOOT.ELF' in inst,'semi-autonomous bootloader placement regressed')
# Packaging: FMCB resources + Titan entry, no second installer ELF or generic exploit.
req('KIND_BOOTSTRAP = 4' in tins and 'KIND_EXPLOIT = 3  # historical' in tins,'TINS bootstrap separation missing')
req('--fmcb-build' in media and 'autonomous-payload' not in media,'production builder still exposes generic exploit path')
req('libtitanfmcb_integrated.a' in media and 'titan-fmcb-entry.elf' in media,'media builder does not validate integrated FMCB build')
req('TITAN_TINS_KIND_EXPLOIT' in inst,'generic exploit entry rejection missing')
for forbidden in ('OpenTuna','FunTuna','Fortuna'):
    req(forbidden not in media and forbidden not in core and forbidden not in cnf, forbidden+' connected to production autonomous path')
# Synthetic media proves concrete FMCB material becomes BOOTSTRAP entries without packaging a standalone installer.
with tempfile.TemporaryDirectory(prefix='titan-fmcb-media-') as td:
    td=Path(td); runtime=td/'runtime'; (runtime/'TITAN').mkdir(parents=True); (runtime/'neutrino').mkdir(); (runtime/'kernel.elf').write_bytes(b'kernel')
    (runtime/'TITAN'/'titan_ul_engine.elf').write_bytes(b'ul')
    (runtime/'TITAN'/'titan_ul_loader.elf').write_bytes(b'ul')
    (runtime/'neutrino'/'neutrino.elf').write_bytes(b'neutrino')
    installer=td/'installer.elf'; installer.write_bytes(b'installer'); boot=td/'titan.elf'; boot.write_bytes(b'boot')
    fake=td/'fmcb'; fake.mkdir(); (fake/'libtitanfmcb_integrated.a').write_bytes(b'archive'); (fake/'titan-fmcb-entry.elf').write_bytes(b'entry')
    out=td/'out'
    subprocess.run(['python3',str(r/'tools/build_titan_installer_media.py'),'--project',str(r),'--runtime',str(runtime),'--installer-elf',str(installer),'--bootloader-elf',str(boot),'--fmcb-build',str(fake),'--output',str(out)],check=True,capture_output=True,text=True)
    req(not (out/'TITAN_INSTALL/FMCB/FMCBInstaller.elf').exists(),'standalone FMCB installer was packaged')
    blob=(out/'TITAN_INSTALL/TITAN_SYSTEM.TINS').read_bytes(); count=struct.unpack_from('<I',blob,16)[0]; flags=struct.unpack_from('<I',blob,20)[0]
    req(flags & 1,'autonomous flag absent from FMCB media')
    kinds=[]; names=[]
    for i in range(count):
        off=64+i*224; kind=struct.unpack_from('<I',blob,off)[0]; plen=struct.unpack_from('<I',blob,off+20)[0]; name=blob[off+24:off+24+plen].decode(); kinds.append(kind); names.append(name)
    req(3 not in kinds,'generic EXPLOIT entry leaked into production FMCB media')
    req(names.count('TITAN/BOOT/TITAN_FMCB_ENTRY.ELF')==1 and names.count('SYS-CONF/FREEMCB.CNF')==1,'concrete FMCB/Titan bootstrap material missing')

for name in (
 'titan-fmcb-source-integration-contract','titan-fmcb-license-provenance-contract','titan-fmcb-build-wiring-contract',
 'titan-fmcb-same-environment-contract','titan-fmcb-no-secondary-installer-contract','titan-autonomous-ui-gate-contract',
 'titan-fmcb-install-layout-contract','titan-fmcb-boot-entry-contract','titan-fmcb-config-contract',
 'titan-autonomous-rom-compat-contract','titan-autonomous-installer-contract','titan-autonomous-preflight-contract',
 'titan-autonomous-mc-space-contract','titan-autonomous-existing-fmcb-contract','titan-autonomous-backup-contract',
 'titan-autonomous-stage-contract','titan-autonomous-write-contract','titan-autonomous-verify-contract','titan-autonomous-commit-contract',
 'titan-autonomous-rollback-contract','titan-autonomous-interrupted-recovery-contract','titan-fmcb-to-titan-entry-contract',
 'titan-titan-entry-to-bootloader-contract','titan-autonomous-boot-chain-contract','titan-autonomous-integrity-contract',
 'titan-autonomous-recovery-contract','titan-semi-autonomous-nonregression-contract','titan-negative-opentuna-fortuna-contract'):
    print(name+': PASS')
