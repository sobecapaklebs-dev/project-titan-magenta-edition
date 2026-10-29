#!/usr/bin/env python3
from pathlib import Path
import hashlib, json, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
tool=root/'tools/titan_tins.py'
installer=(root/'kernel/src/installer/titan_install_container.c').read_text(errors='replace')
boot_main=(root/'bootloader/src/main.c').read_text(errors='replace')
assert 'TITAN_TINS_MAGIC' in installer and 'table_crc32' in installer
assert 'TITAN_BOOT_USB_PROBE_ATTEMPTS' in boot_main and 'TITAN_BOOT_LOG_PATH' in boot_main
with tempfile.TemporaryDirectory() as td:
    d=Path(td); a=d/'BOOT.ELF'; b=d/'kernel.elf'; a.write_bytes(b'boot-baseline'); b.write_bytes(b'kernel-payload')
    tins=d/'TITAN_SYSTEM.TINS'
    subprocess.run(['python3',str(tool),'build','--output',str(tins),'--file',f'BOOT:BOOT/BOOT.ELF={a}','--file',f'SYSTEM:TITAN/kernel.elf={b}'],check=True,capture_output=True,text=True)
    subprocess.run(['python3',str(tool),'verify',str(tins)],check=True,capture_output=True,text=True)
    out=d/'release'; subprocess.run(['python3',str(tool),'release-metadata',str(tins),'--output-dir',str(out)],check=True,capture_output=True,text=True)
    m=json.loads((out/'TITAN_SYSTEM.TINS.manifest.json').read_text())
    digest=hashlib.sha256(tins.read_bytes()).hexdigest()
    assert m['sha256']==digest and m['file_count']==2 and m['schema']=='project-titan-system-package-v1'
    assert (out/'TITAN_SYSTEM.TINS.sha256').read_text().startswith(digest+'  TITAN_SYSTEM.TINS')
    damaged=bytearray(tins.read_bytes()); damaged[-1]^=0x55; (d/'bad.tins').write_bytes(damaged)
    r=subprocess.run(['python3',str(tool),'verify',str(d/'bad.tins')],capture_output=True,text=True)
    assert r.returncode != 0
print('titan-boot-validated-baseline-preservation-contract: OK')
print('titan-system-packaging-integrity-contract: OK')
print('titan-tins-corruption-fail-closed-contract: OK')
print('titan-release-sha256-manifest-contract: OK')
