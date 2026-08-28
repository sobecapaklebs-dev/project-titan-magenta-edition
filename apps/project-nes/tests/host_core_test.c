#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "../src/project_nes.c"

static int pn_test_alloc_surfaces(void)
{
    pn_zero(&g_pn, (uint32_t)sizeof(g_pn));
    g_pn.backend = &g_pn_legacy_backend;
    pn_zero(g_pn_nes_frame, PN_NES_FRAME_BYTES);
    pn_zero(g_pn_media_frame, PN_MEDIA_FRAME_BYTES);
    pn_zero(g_pn_audio_buffer, PN_AUDIO_BUFFER_BYTES);
    g_pn.nes_frame = g_pn_nes_frame;
    g_pn.media_frame = g_pn_media_frame;
    g_pn.audio_buffer = g_pn_audio_buffer;
    return 1;
}

static void pn_test_free_surfaces(void)
{
    pn_zero(&g_pn, (uint32_t)sizeof(g_pn));
}


int main(int argc, char **argv)
{
    FILE *f;
    long len;
    uint8_t *buf;
    int parsed;
    unsigned int i;
    unsigned int nonzero = 0;
    if (argc != 2) return 2;
    f = fopen(argv[1], "rb");
    if (!f) return 3;
    fseek(f,0,SEEK_END); len=ftell(f); fseek(f,0,SEEK_SET);
    buf=(uint8_t*)malloc((size_t)len);
    if (!buf) return 4;
    if (fread(buf,1,(size_t)len,f)!=(size_t)len) return 5;
    fclose(f);
    if (!pn_test_alloc_surfaces()) return 9;
    parsed=g_pn.backend->load_rom(&g_pn.nes,buf,(uint32_t)len);
    if (parsed != 1) { fprintf(stderr,"parse=%d\n",parsed); return 6; }
    g_pn.backend->reset(&g_pn.nes);
    for (i=0;i<8;i++)
    {
        if (!pn_core_run_frame()) return 7;
        printf("frame %u pc=%04x halt=%u op=%02x ppu=%02x ctrl=%02x mask=%02x\n",i,g_pn.nes.cpu.pc,g_pn.nes.halted,(unsigned)g_pn.nes.cpu.unsupported_opcode,g_pn.nes.ppu.status,g_pn.nes.ppu.ctrl,g_pn.nes.ppu.mask);
        if (g_pn.nes.halted) return 7;
    }
    for (i=0;i<PN_NES_W*PN_NES_H;i++) if (g_pn.nes_frame[i] != 0) nonzero++;
    printf("nonzero=%u palette1=%02x vram21ca=%u\n",nonzero,g_pn.nes.ppu.palette[1],g_pn.nes.ppu.vram[pn_ppu_mirror_nametable(&g_pn.nes,0x21cau)]);
    { const char *dump=getenv("PROJECT_NES_FRAME_DUMP"); FILE *o=dump&&*dump?fopen(dump,"wb"):NULL; unsigned int x,y; if(o){ fprintf(o,"P6\n256 240\n255\n"); for(y=0;y<240;y++) for(x=0;x<256;x++){ uint16_t c=g_pn.nes_frame[y*256+x]; unsigned char rgb[3]; rgb[0]=(unsigned char)(((c>>11)&31)*255/31); rgb[1]=(unsigned char)(((c>>5)&63)*255/63); rgb[2]=(unsigned char)((c&31)*255/31); fwrite(rgb,1,3,o);} fclose(o);} }
    pn_test_free_surfaces();
    free(buf);
    return nonzero > 100 ? 0 : 8;
}
