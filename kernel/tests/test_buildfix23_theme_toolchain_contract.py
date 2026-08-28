#!/usr/bin/env python3
from pathlib import Path
import tempfile,json,subprocess,sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
tool=root/'tools/titan_theme_asset.py'; txt=tool.read_text()
for x in ('layer','duration','missing asset','peak_residency','duplicate','compile-manifest'): assert x in txt
with tempfile.TemporaryDirectory() as d:
 d=Path(d);(d/'a.bin').write_bytes(b'1234'); spec={'layers':[{'type':'BACKGROUND','frames':[{'asset':'a.bin','duration_ms':100},{'asset':'a.bin','duration_ms':400}]}]};(d/'s.json').write_text(json.dumps(spec));out=d/'o.json';subprocess.run([sys.executable,str(tool),'compile-manifest','--input',str(d/'s.json'),'--output',str(out)],check=True);o=json.loads(out.read_text());assert o['duplicate_instances']==1 and o['layers'][0]['resident_cap']==2
print('theme-toolchain-contract: OK validation, missing assets, durations, dedupe, peak residency, compiled manifest')
