#!/usr/bin/env python3
from pathlib import Path
import struct, subprocess, tempfile, zlib
from PIL import Image
root=Path(__file__).resolve().parents[1]
image_source=(root/'src/services/titan_asset_image.c').read_text()
assert 'jpeg_calc_output_dimensions(&cinfo)' in image_source
assert 'cinfo.scale_denom = scale_denom' in image_source
with tempfile.TemporaryDirectory(prefix='titan_img_prereq_') as prereq_td:
 prereq=Path(prereq_td)/'probe'
 check=subprocess.run(['cc','-x','c','-','-lpng','-ljpeg','-o',str(prereq)],input='#include <jpeglib.h>\n#include <png.h>\nint main(void){return 0;}\n',text=True,capture_output=True)
 if check.returncode:
  print('Titan asset PNG/JPEG -> PSM16 TIMG conversion: SKIP missing system JPEG/PNG development headers or libraries')
  raise SystemExit(0)
with tempfile.TemporaryDirectory(prefix='titan_image_conv_') as td0:
 td=Path(td0)
 Image.new('RGBA',(96,128),(220,30,80,255)).save(td/'icon.png')
 Image.new('RGB',(640,360),(20,100,210)).save(td/'bg.jpg',quality=90)
 (td/'harness.c').write_text(r'''
#include "titan_asset_image.h"
#include <stdio.h>
#include <stdlib.h>
static int run(const char *in,const char *out,int kind){FILE*f=fopen(in,"rb");long n;unsigned char*src,*dst;unsigned int os=0;if(!f)return 2;fseek(f,0,SEEK_END);n=ftell(f);fseek(f,0,SEEK_SET);src=malloc(n);dst=malloc(70000);if(!src||!dst)return 3;if(fread(src,1,n,f)!=(size_t)n)return 4;fclose(f);if(!TitanAssetImage_ConvertToTimg(src,(unsigned)n,(TitanAssetKind)kind,dst,70000,&os))return 5;f=fopen(out,"wb");if(!f)return 6;fwrite(dst,1,os,f);fclose(f);free(src);free(dst);return 0;}int main(int ac,char**av){if(ac!=5)return 9;int a=run(av[1],av[2],1);if(a)return a;return run(av[3],av[4],2);}
''')
 exe=td/'conv'
 subprocess.run(['cc','-std=gnu99',f'-I{root/"include"}',f'-I{root/"src/services"}',str(td/'harness.c'),str(root/'src/services/titan_asset_image.c'),'-lpng','-ljpeg','-o',str(exe)],check=True)
 subprocess.run([str(exe),str(td/'icon.png'),str(td/'icon.timg'),str(td/'bg.jpg'),str(td/'bg.timg')],check=True)
 for path,w,h,k in [(td/'icon.timg',128,128,1),(td/'bg.timg',256,128,2)]:
  b=path.read_bytes(); assert b[:4]==b'TIMG'; ver,kind,width,height=struct.unpack_from('<HHHH',b,4); flags,size,crc=struct.unpack_from('<III',b,12); assert (ver,kind,width,height,flags)==(2,k,w,h,1); assert size==w*h*2; assert len(b)==32+size; assert zlib.crc32(b[32:])&0xffffffff==crc
print('Titan asset PNG/JPEG -> PSM16 TIMG conversion: PASS')
