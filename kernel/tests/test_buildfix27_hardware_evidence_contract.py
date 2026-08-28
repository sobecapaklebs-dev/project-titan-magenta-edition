from pathlib import Path
import json, subprocess, sys, tempfile
root=Path(sys.argv[1]).resolve(); script=root/'scripts/validate_buildfix27_hardware_evidence.py'
s=script.read_text()
for t in ['720p path: REFERENCE','Output resolution: 1280x720','lifecycle_10_cycles','pfs_titan_owned','ftp_transactional','vmc_transactional','TitanStorageMount']:
    assert t in s,t
with tempfile.TemporaryDirectory(prefix='bf27_cp48_') as td:
    td=Path(td); report=td/'debug.txt'; events=td/'events.jsonl'; out=td/'out.json'
    report.write_text('''Project Titan Magenta Edition Debug Report\nVideo: HDTV / 720p\nEE RAM estimate: valid=1 used=100 free=200 peak=120 min_free=180 alloc_fail=0\nIOP RAM: valid=1 used=10 free=20 min_free=18 known_modules=4\n720p path: REFERENCE\nOutput resolution: 1280x720\nBOOT VRAM HISTORY\nHOME vram_used=1 vram_free=2 reserved=1 font=1 textures=1 theme=1 fails=0 pressure=0 ee_valid=1 ee_used=1 ee_free=2 ee_peak=1 ee_min_free=2 iop_valid=1 iop_used=1 iop_free=2 iop_modules=4\n''')
    rows=[{'event':'cold_boot_home','success':True},{'event':'network_online','success':True},{'event':'storage_online','success':True},
          {'event':'pfs_mount_rw','success':True,'owner':'TitanStorageMount','mount':'pfs0:/'},
          {'event':'ftp_roundtrip','success':True,'retr':True,'stor_atomic':True},
          {'event':'vmc_export','success':True,'atomic':True,'read_only_source':True}]
    rows += [{'event':'lifecycle_cycle','cycle':i,'success':True} for i in range(1,11)]
    events.write_text('\n'.join(json.dumps(r) for r in rows)+'\n')
    cp=subprocess.run([sys.executable,str(script),'--debug-report',str(report),'--events',str(events),'--output',str(out),'--require-pass'],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    assert cp.returncode==0,cp.stdout
    assert json.loads(out.read_text())['status']=='PASS'
    # Remove cycle 10: must block.
    events.write_text('\n'.join(json.dumps(r) for r in rows if r.get('cycle')!=10)+'\n')
    cp=subprocess.run([sys.executable,str(script),'--debug-report',str(report),'--events',str(events),'--output',str(out),'--require-pass'],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    assert cp.returncode==2 and json.loads(out.read_text())['status']=='BLOCKED',cp.stdout
print('buildfix27-hardware-evidence: OK physical evidence fail-closed validator')
