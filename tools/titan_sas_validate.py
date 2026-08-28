#!/usr/bin/env python3
import argparse,json,re
from pathlib import Path

def prefixes(project):
    src=(project/'kernel/src/services/titan_sas.c').read_text(errors='replace')
    return set(re.findall(r'\{\s*"([A-Z0-9]{3})"\s*,\s*TITAN_SAS_PREFIX_CATEGORY_',src))
def parse_cfg(p):
    d={}
    if not p.is_file(): return d
    for raw in p.read_text(errors='replace').splitlines():
        if '=' not in raw: continue
        k,v=raw.split('=',1); d[k.strip().lower()]=v.strip()
    return d
def main():
    ap=argparse.ArgumentParser(); ap.add_argument('directory'); ap.add_argument('--project-root',default=str(Path(__file__).resolve().parents[1])); a=ap.parse_args()
    project=Path(a.project_root).resolve(); d=Path(a.directory).resolve()
    name=d.name; pref=name.split('_',1)[0].upper() if '_' in name else ''
    known=pref in prefixes(project); hint=bool(re.fullmatch(r'[A-Z0-9]{3}',pref))
    title=parse_cfg(d/'title.cfg'); app=parse_cfg(d/'app.cfg')
    sub=any(x.is_dir() for x in d.iterdir()) if d.is_dir() else False
    canonical=bool(title.get('boot'))
    compatible=canonical or bool(app) or known or hint
    compliant=known and canonical and not sub
    if compliant: compatible=True
    checks=[
      {'rule':'known_prefix','passed':known,'required_for_compliance':True},
      {'rule':'title_cfg_boot','passed':canonical,'required_for_compliance':True},
      {'rule':'flat_layout','passed':not sub,'required_for_compliance':True},
      {'rule':'install_pbt','passed':(d/'INSTALL.PBT').exists(),'required_for_compliance':False,'classification':'BootManager companion / non-SAS evidence'},
      {'rule':'app_cfg','passed':bool(app),'required_for_compliance':False,'classification':'Titan compatibility fallback'},
    ]
    profile='CLASSIC' if compliant else ('TITAN_FALLBACK' if app and not title else ('EXTENDED_COMPAT' if compatible else 'NONE'))
    out={'compatible':compatible,'compliant':compliant,'state_valid':not compliant or compatible,'prefix':pref,'prefix_known':known,'has_subfolders':sub,'osdsys_manageable':compliant and name.startswith('APP_'),'copyability':'OSDSYS_COPY_UNSAFE' if sub else ('OSDSYS_COPY_SAFE' if compliant else 'OSDSYS_COPY_UNKNOWN'),'profile':profile,'checks':checks,'metadata':{'canonical':'title.cfg' if title else None,'compatibility':'app.cfg' if app else None}}
    print(json.dumps(out,indent=2,sort_keys=True)); return 0
if __name__=='__main__': raise SystemExit(main())
