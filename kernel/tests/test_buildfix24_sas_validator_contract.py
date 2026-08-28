import json,subprocess,sys,tempfile
from pathlib import Path
r=Path(sys.argv[1]); tool=r/'tools/titan_sas_validate.py'
with tempfile.TemporaryDirectory() as td:
 d=Path(td)/'APP_TEST'; d.mkdir(); (d/'title.cfg').write_text('title=Test\nboot=TEST.ELF\n'); (d/'TEST.ELF').write_bytes(b'')
 p=subprocess.run([sys.executable,str(tool),'--project-root',str(r),str(d)],capture_output=True,text=True); assert p.returncode==0; o=json.loads(p.stdout); assert o['compatible'] and o['compliant'] and o['state_valid']; assert any(x['rule']=='install_pbt' and not x['required_for_compliance'] for x in o['checks'])
print('titan-sas-validator-contract: OK explanatory host validator follows centralized prefix source')
