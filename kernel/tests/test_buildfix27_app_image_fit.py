#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
from PIL import Image
root=Path(__file__).resolve().parents[1]
# This host integration test needs the system JPEG/PNG development headers and
# link libraries. Keep the rest of the host suite runnable on source-only images.
with tempfile.TemporaryDirectory(prefix='titan_img_prereq_') as prereq_td:
    probe=Path(prereq_td)/'probe'
    check=subprocess.run(
        ['cc','-x','c','-','-lpng','-ljpeg','-o',str(probe)],
        input='#include <jpeglib.h>\n#include <png.h>\nint main(void){return 0;}\n',
        text=True,capture_output=True)
    if check.returncode:
        print('buildfix27-app-image-fit: SKIP missing system JPEG/PNG development headers or libraries')
        raise SystemExit(0)
with tempfile.TemporaryDirectory(prefix='titan_app_img_') as td0:
    td=Path(td0)
    Image.new('RGB',(640,360),(220,30,80)).save(td/'wide.jpg',quality=95)
    Image.new('RGBA',(200,400),(20,160,80,255)).save(td/'tall.png')
    (td/'h.c').write_text(r'''
#include "titan_asset_image.h"
#include <stdio.h>
#include <stdlib.h>
static int run(const char*p,int tall){FILE*f=fopen(p,"rb");long n;unsigned char*s;unsigned short*d;TitanAssetImageOptions o={0};int r;if(!f)return 2;fseek(f,0,SEEK_END);n=ftell(f);fseek(f,0,SEEK_SET);s=malloc((size_t)n);d=malloc(512u*256u*2u);if(!s||!d)return 3;fread(s,1,(size_t)n,f);fclose(f);r=TitanAssetImage_DecodeMediaRgb565Fit(s,(unsigned)n,d,512,256,512,&o);if(!r)return 4;if(d[0]!=0u||d[511u]!=0u)return 5;if(tall){if(d[128u*512u+256u]==0u)return 6;if(d[128u*512u+20u]!=0u)return 7;}else{if(d[128u*512u+256u]==0u)return 8;if(d[128u*512u+10u]!=0u)return 9;}free(s);free(d);return 0;}int main(int ac,char**av){int r;if(ac!=3)return 1;r=run(av[1],0);if(r)return r;return run(av[2],1);}
''')
    exe=td/'fit'
    subprocess.run(['cc','-std=gnu99','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',f'-I{root/"include"}',f'-I{root/"src/services"}',str(td/'h.c'),str(root/'src/services/titan_asset_image.c'),'-lpng','-ljpeg','-o',str(exe)],check=True)
    subprocess.run([str(exe),str(td/'wide.jpg'),str(td/'tall.png')],check=True,env={'ASAN_OPTIONS':'detect_leaks=1'})
print('buildfix27-app-image-fit: OK JPEG/PNG full-image FIT letterbox')
