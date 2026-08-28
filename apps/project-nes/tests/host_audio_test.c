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


static uint32_t submitted_frames;
static int nonzero_sample;
static TitanAppApiV1 test_api;
static uint32_t fake_free(void){ return 4096u; }
static uint32_t fake_no_free(void){ return 0u; }
static uint32_t fake_submit(const int16_t *samples, uint32_t frames){
    uint32_t i;
    submitted_frames += frames;
    for(i=0;i<frames*2u;i++) if(samples[i]!=0) nonzero_sample=1;
    return frames;
}

int main(int argc,char **argv){
    FILE *f; long len; uint8_t *buf; unsigned int i;
    if(argc!=2) return 2;
    f=fopen(argv[1],"rb"); if(!f)return 3; fseek(f,0,SEEK_END);len=ftell(f);fseek(f,0,SEEK_SET);
    buf=malloc((size_t)len); if(!buf)return 4; if(fread(buf,1,(size_t)len,f)!=(size_t)len)return 5; fclose(f);
    pn_zero(&test_api,sizeof(test_api));
    if(!pn_test_alloc_surfaces()) return 9;
    test_api.media_audio_free_frames=fake_free; test_api.media_submit_pcm=fake_submit; g_pn.api=&test_api; g_pn.audio_started=1u;
    if(g_pn.backend->load_rom(&g_pn.nes,buf,(uint32_t)len)!=1) return 6;
    g_pn.backend->reset(&g_pn.nes);
    for(i=0;i<4u;i++) if(!pn_core_run_frame()) return 7;
    if((g_pn.nes.apu[0x15]&1u)==0u){fprintf(stderr,"pulse not enabled\n");return 7;}
    {
        uint8_t slot_before = g_pn.nes.audio_slot;
        uint32_t (*saved_free)(void) = test_api.media_audio_free_frames;
        test_api.media_audio_free_frames = fake_no_free;
        if (pn_submit_audio(&g_pn.nes) != 0 || g_pn.nes.audio_slot != slot_before) return 10;
        test_api.media_audio_free_frames = saved_free;
    }
    pn_submit_audio(&g_pn.nes);
    if (pn_core_rate_millihz() != 60000u) return 11;
    printf("submitted=%u nonzero=%d reg4000=%02x timer=%02x/%02x\n",submitted_frames,nonzero_sample,g_pn.nes.apu[0],g_pn.nes.apu[2],g_pn.nes.apu[3]);
    pn_test_free_surfaces(); free(buf); return submitted_frames>0u && nonzero_sample ? 0 : 8;
}
