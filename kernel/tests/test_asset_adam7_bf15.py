#!/usr/bin/env python3
from pathlib import Path
import subprocess, tempfile, os
from PIL import Image
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='titan_bf15_adam7_') as td0:
    td=Path(td0); plain=td/'plain.png'; adam=td/'adam7.png'
    Image.new('RGBA',(256,256),(11,80,190,255)).save(plain)
    subprocess.run(['magick',str(plain),'-interlace','PNG',str(adam)],check=True)
    assert adam.read_bytes()[28] == 1
    h=td/'h.c'
    h.write_text(r'''
#include "titan_asset_image.h"
#include <stdio.h>
#include <stdlib.h>
int main(int ac,char**av){FILE*f;long n;unsigned char*s,*d;unsigned out=0;TitanAssetImageOptions o={0};int r;if(ac!=2)return 9;f=fopen(av[1],"rb");if(!f)return 10;fseek(f,0,SEEK_END);n=ftell(f);fseek(f,0,SEEK_SET);s=malloc((size_t)n);d=malloc(70000);if(!s||!d)return 11;fread(s,1,(size_t)n,f);fclose(f);o.minimum_source_width=128;o.minimum_source_height=128;o.yield_every_rows=8;r=TitanAssetImage_ConvertToTimgEx(s,(unsigned)n,TITAN_ASSET_KIND_ICON,d,70000,&out,&o);free(s);free(d);return (r==1 && out==32800u)?0:12;}
''')
    exe=td/'adam7'
    subprocess.run(['cc','-std=gnu99','-fsanitize=address,undefined','-fno-omit-frame-pointer',f'-I{root/"include"}',f'-I{root/"src/services"}',str(h),str(root/'src/services/titan_asset_image.c'),'-lpng','-ljpeg','-o',str(exe)],check=True)
    subprocess.run([str(exe),str(adam)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:abort_on_error=1'})
print('Titan BF15 Adam7 PNG bounded decode (ASan/UBSan): PASS')
