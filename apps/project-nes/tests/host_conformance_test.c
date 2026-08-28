#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../src/project_nes.c"

static void make_header(uint8_t *h)
{
    memset(h,0,16u);
    h[0]='N'; h[1]='E'; h[2]='S'; h[3]=0x1a;
    h[4]=1u;
}

int main(void)
{
    uint8_t h[16];
    ProjectNesCoreRomInfo info;
    uint8_t *rom;
    uint32_t pattern[6];
    uint32_t total=0u;
    int16_t audio[534u*2u];
    PnNes nes;
    unsigned int i;

    make_header(h);
    h[6]=0x07u; /* battery + trainer + vertical, mapper 0 */
    h[9]=1u;    /* PAL hint in classic iNES */
    if(!ProjectNesCore_ProbeRom(h,sizeof(h),&info)) return 2;
    if(info.format!=PROJECT_NES_CORE_ROM_INES1 || info.mapper!=0u) return 3;
    if(!info.battery_backed || !info.has_trainer || !info.vertical_mirror) return 4;
    if(info.region_hint!=PROJECT_NES_CORE_REGION_PAL) return 5;
    if(info.prg_rom_bytes!=16384u || info.chr_rom_bytes!=0u) return 6;

    make_header(h);
    h[6]=0x10u; /* mapper 1 */
    if(!ProjectNesCore_ProbeRom(h,sizeof(h),&info) || info.mapper!=1u) return 7;

    make_header(h);
    h[6]=0x30u;
    h[7]=0x28u; /* NES 2 marker + mapper lower bits 0x23 */
    h[8]=0xa1u; /* submapper A + mapper upper nibble -> 0x123 */
    h[12]=1u;   /* NES 2.0 PAL timing hint */
    if(!ProjectNesCore_ProbeRom(h,sizeof(h),&info)) return 8;
    if(info.format!=PROJECT_NES_CORE_ROM_NES2 || info.mapper!=0x123u || info.submapper!=0x0au) return 9;
    if(info.region_hint!=PROJECT_NES_CORE_REGION_PAL) return 19;
    if(info.prg_rom_bytes!=0u || info.chr_rom_bytes!=0u) return 20;

    rom=(uint8_t*)calloc(1u,16u+16384u);
    if(!rom) return 10;
    make_header(rom);
    rom[6]=0x10u;
    memset(&nes,0,sizeof(nes));
    if(pn_legacy_load_rom(&nes,rom,16u+16384u)!=PROJECT_NES_CORE_LOAD_UNSUPPORTED_MAPPER) return 11;
    make_header(rom);
    rom[7]=0x08u;
    if(pn_legacy_load_rom(&nes,rom,16u+16384u)!=PROJECT_NES_CORE_LOAD_UNSUPPORTED_FORMAT) return 12;
    make_header(rom);
    rom[6]=0x02u;
    if(pn_legacy_load_rom(&nes,rom,16u+16384u)!=PROJECT_NES_CORE_LOAD_OK) return 13;
    if(pn_legacy_save_ram_size(&nes)!=8192u) return 14;
    free(rom);

    memset(&nes,0,sizeof(nes));
    if(pn_legacy_frame_rate_millihz(&nes)!=60000u) return 15;
    for(i=0u;i<6u;i++) {
        pattern[i]=pn_legacy_next_audio_frames(&nes);
        if(pn_legacy_render_audio(&nes,audio,534u)!=pattern[i]) return 16;
        total+=pattern[i];
    }
    if(pattern[0]!=533u || pattern[1]!=533u || pattern[2]!=534u ||
       pattern[3]!=533u || pattern[4]!=533u || pattern[5]!=534u) return 17;
    if(total!=3200u) return 18; /* 6 frames at 60 Hz = 0.1s = 3200 samples at 32 kHz */

    puts("project-nes-conformance: OK mapper/format/region/SRAM/audio timing metadata");
    return 0;
}
