#!/usr/bin/env python3
from pathlib import Path
import subprocess,sys
root=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
runtime=(root/'kernel/src/services/titan_app_runtime.c').read_text(errors='replace')
pn=root/'apps/project-nes/build/source/project_nes.tmod'; wle=root/'apps/wlaunchelf/build/source/wlaunchelf.tmod'
def req(c,m):
    if not c: print('b30-p107-n32-rela: FAIL '+m,file=sys.stderr); raise SystemExit(1)
for t in ['#define TITAN_TMOD_SHT_RELA 4u','typedef struct TitanTmodRela','int32_t addend;','TITAN_TMOD_SHT_RELA','TitanTmod_ResolveN32RuntimeBuiltin','__extendsfdf2','_ctype_','Import N32 nao resolvido']:
    req(t in runtime,'missing '+t)
for mod in (pn,wle):
    req(mod.is_file(),'missing '+str(mod)); hdr=subprocess.check_output(['readelf','-h',str(mod)],text=True); sec=subprocess.check_output(['readelf','-S',str(mod)],text=True); req('abi2' in hdr.lower() and '5900' in hdr.lower(),mod.name+' not N32/R5900'); req('.rela.' in sec,mod.name+' not RELA')
ws=subprocess.check_output(['readelf','-Ws',str(pn)],text=True); undefined=set()
for line in ws.splitlines():
    p=line.split()
    if len(p)>=8 and p[6]=='UND' and p[7]: undefined.add(p[7])
allowed={'memcpy','memset','memcmp','memmove','strlen','strcmp','strncmp','strstr','strtok','atoi','atof','strtoul','snprintf','vsnprintf','sprintf','sscanf','sin','cos','atan','sqrt','__adddf3','__subdf3','__muldf3','__divdf3','__divdi3','__udivdi3','__muldi3','__extendsfdf2','__fixdfdi','__fixdfsi','__fixunsdfsi','__floatsidf','__floatunsidf','__truncdfsf2','_ctype_'}
extra=sorted(undefined-allowed); req(not extra,'unapproved Project NES imports: '+', '.join(extra))
print('b30-p107-n32-rela: PASS real N32 modules are RELA; explicit-addend loader + bounded imports present')
