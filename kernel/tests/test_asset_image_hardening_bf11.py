#!/usr/bin/env python3
from pathlib import Path
import subprocess, tempfile, struct, zlib
from PIL import Image
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='titan_image_hardening_') as td0:
    td=Path(td0)
    good=td/'good.png'
    Image.new('RGBA',(8,8),(1,2,3,255)).save(good)
    b=bytearray(good.read_bytes())
    # Make IHDR advertise Adam7 and repair its CRC. Our one-row-at-a-time
    # converter deliberately rejects interlaced PNG instead of decoding it unsafely.
    assert b[:8] == b'\x89PNG\r\n\x1a\n' and b[12:16] == b'IHDR'
    b[28] = 1
    b[29:33] = struct.pack('>I', zlib.crc32(bytes(b[12:29])) & 0xffffffff)
    (td/'interlaced.png').write_bytes(b)
    (td/'truncated.png').write_bytes(b'\x89PNG\r\n\x1a\n\x00\x00')
    (td/'truncated.jpg').write_bytes(b'\xff\xd8\xff\xe0\x00\x10JFIF\x00')
    (td/'garbage.bin').write_bytes(bytes(range(1,64)))
    (td/'harness.c').write_text(r'''
#include "titan_asset_image.h"
#include <stdio.h>
#include <stdlib.h>
static int reject(const char *path, int kind) {
    FILE *f=fopen(path,"rb"); long n; unsigned char *src=NULL,*dst=NULL; unsigned int out=0xDEADBEEFu; int rc;
    if(!f) return 20; fseek(f,0,SEEK_END); n=ftell(f); fseek(f,0,SEEK_SET);
    if(n < 0) return 21; src=(unsigned char*)malloc(n ? (size_t)n : 1u); dst=(unsigned char*)malloc(70000u);
    if(!src || !dst) return 22; if(n && fread(src,1,(size_t)n,f)!=(size_t)n) return 23; fclose(f);
    rc=TitanAssetImage_ConvertToTimg(src,(unsigned int)n,(TitanAssetKind)kind,dst,70000u,&out);
    free(src); free(dst); return rc == 0 ? 0 : 24;
}
int main(int ac,char **av) {
    int i; if(ac != 5) return 30;
    for(i=1;i<5;i++) { int r=reject(av[i], i==3 ? 2 : 1); if(r) return r; }
    return 0;
}
''')
    exe=td/'hardening'
    cmd=['cc','-std=gnu99','-fsanitize=address,undefined','-fno-omit-frame-pointer',
         f'-I{root/"include"}',f'-I{root/"src/services"}',str(td/'harness.c'),
         str(root/'src/services/titan_asset_image.c'),'-lpng','-ljpeg','-o',str(exe)]
    subprocess.run(cmd,check=True)
    subprocess.run([str(exe),str(td/'interlaced.png'),str(td/'truncated.png'),str(td/'truncated.jpg'),str(td/'garbage.bin')],check=True,
                   env={**__import__('os').environ,'ASAN_OPTIONS':'detect_leaks=1:abort_on_error=1'})
print('Titan asset malformed/interlaced image hardening (ASan/UBSan): PASS')
