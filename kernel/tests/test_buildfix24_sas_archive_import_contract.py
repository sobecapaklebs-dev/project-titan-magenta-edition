import io,json,subprocess,sys,tarfile,tempfile
from pathlib import Path
root=Path(sys.argv[1]).resolve(); tool=root/'tools/titan_sas_archive_validate.py'
with tempfile.TemporaryDirectory() as td:
 p=Path(td); good=p/'good.tar'; bad=p/'bad.tar'
 with tarfile.open(good,'w') as tf:
  d=b'x'; i=tarfile.TarInfo('APP_TEST/title.cfg'); i.size=len(d); tf.addfile(i,io.BytesIO(d))
 with tarfile.open(bad,'w') as tf:
  d=b'x'; i=tarfile.TarInfo('../escape'); i.size=len(d); tf.addfile(i,io.BytesIO(d))
 g=subprocess.run([sys.executable,str(tool),str(good)],capture_output=True,text=True); b=subprocess.run([sys.executable,str(tool),str(bad)],capture_output=True,text=True)
 assert g.returncode==0 and json.loads(g.stdout)['valid']; assert b.returncode!=0 and not json.loads(b.stdout)['valid']
print('titan-sas-archive-import-contract: OK traversal and malformed destination paths gated before extraction')
