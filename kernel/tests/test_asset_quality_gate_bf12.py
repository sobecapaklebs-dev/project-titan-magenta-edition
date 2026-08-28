#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
from PIL import Image
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='titan_asset_qgate_') as td0:
 td=Path(td0)
 Image.new('RGBA',(64,64),(255,0,0,255)).save(td/'tiny.png')
 Image.new('RGBA',(256,256),(0,180,255,255)).save(td/'good.png')
 (td/'h.c').write_text(r'''
#include "titan_asset_image.h"
#include <stdio.h>
#include <stdlib.h>
static int cv(const char*p){FILE*f=fopen(p,"rb");long n;unsigned char*s,*d;unsigned int os=0;TitanAssetImageOptions o;if(!f)return -1;fseek(f,0,SEEK_END);n=ftell(f);fseek(f,0,SEEK_SET);s=malloc(n);d=malloc(70000);fread(s,1,n,f);fclose(f);o.should_cancel=0;o.yield=0;o.user_data=0;o.yield_every_rows=8;o.minimum_source_width=128;o.minimum_source_height=128;int r=TitanAssetImage_ConvertToTimgEx(s,(unsigned)n,TITAN_ASSET_KIND_ICON,d,70000,&os,&o);free(s);free(d);return r;}
int main(int ac,char**av){if(ac!=3)return 9;if(cv(av[1])!=0)return 2;if(cv(av[2])!=1)return 3;return 0;}
''')
 exe=td/'q'
 subprocess.run(['cc','-std=gnu99','-fsanitize=address,undefined','-fno-omit-frame-pointer',f'-I{root/"include"}',f'-I{root/"src/services"}',str(td/'h.c'),str(root/'src/services/titan_asset_image.c'),'-lpng','-ljpeg','-o',str(exe)],check=True)
 subprocess.run([str(exe),str(td/'tiny.png'),str(td/'good.png')],check=True)
print('Titan BF12 remote artwork quality gate (ASan/UBSan): PASS')
