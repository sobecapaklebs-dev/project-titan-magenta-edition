#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "../src/project_nes.c"

int main(int argc, char **argv)
{
    FILE *f; long len; uint8_t *buf; uint8_t save[PN_SAVE_CAPACITY]; uint32_t i;
    if (argc != 2) return 2;
    f=fopen(argv[1],"rb"); if(!f) return 3;
    fseek(f,0,SEEK_END); len=ftell(f); fseek(f,0,SEEK_SET);
    buf=(uint8_t*)malloc((size_t)len); if(!buf) return 4;
    if(fread(buf,1,(size_t)len,f)!=(size_t)len){fclose(f);return 5;} fclose(f);
    /* Turn the generated ROM into a battery-backed iNES fixture; no copyrighted bytes. */
    buf[6] |= 0x02u;
    pn_zero(&g_pn,sizeof(g_pn));
    g_pn.backend=&g_pn_legacy_backend;
    if(g_pn.backend->load_rom(&g_pn.nes,buf,(uint32_t)len)!=1) return 6;
    if(g_pn.backend->save_ram_size(&g_pn.nes)!=8192u) return 7;
    for(i=0;i<8192u;i++) save[i]=(uint8_t)(i*13u+7u);
    if(!g_pn.backend->save_ram_import(&g_pn.nes,save,8192u)) return 8;
    if(g_pn.backend->save_ram_dirty(&g_pn.nes)) return 9;
    pn_cpu_write(&g_pn.nes,0x6123u,0x5au);
    if(!g_pn.backend->save_ram_dirty(&g_pn.nes)) return 10;
    pn_zero(save,sizeof(save));
    if(!g_pn.backend->save_ram_export(&g_pn.nes,save,sizeof(save))) return 11;
    if(save[0x123u]!=0x5au) return 12;
    g_pn.backend->save_ram_clear_dirty(&g_pn.nes);
    if(g_pn.backend->save_ram_dirty(&g_pn.nes)) return 13;
    free(buf);
    puts("project-nes-save-core: OK battery SRAM import/export/dirty");
    return 0;
}
