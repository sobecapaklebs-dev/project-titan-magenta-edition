#include "project_nes_core.h"
#include "project_nes_fceumm_provider.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int (*env_cb_t)(unsigned,void*);
typedef void (*video_cb_t)(const void*,unsigned,unsigned,size_t);
typedef void (*audio_cb_t)(int16_t,int16_t);
typedef size_t (*audio_batch_cb_t)(const int16_t*,size_t);
typedef void (*poll_cb_t)(void);
typedef int16_t (*input_cb_t)(unsigned,unsigned,unsigned,unsigned);
struct game_info { const char *path; const void *data; size_t size; const char *meta; };
static env_cb_t ecb; static video_cb_t vcb; static audio_cb_t acb; static audio_batch_cb_t abcb; static poll_cb_t pcb; static input_cb_t icb;
static uint8_t save_ram[8192]; static uint16_t frame[256*240]; static int16_t audio[800*2];
static int init_count,deinit_count,unload_count,load_count,run_count,reset_count;
static unsigned seen_fmt=99;
void retro_set_environment(env_cb_t cb){ecb=cb;}
void retro_set_video_refresh(video_cb_t cb){vcb=cb;}
void retro_set_audio_sample(audio_cb_t cb){acb=cb; (void)acb;}
void retro_set_audio_sample_batch(audio_batch_cb_t cb){abcb=cb;}
void retro_set_input_poll(poll_cb_t cb){pcb=cb;}
void retro_set_input_state(input_cb_t cb){icb=cb;}
void retro_init(void){init_count++;}
void retro_deinit(void){deinit_count++;}
void retro_reset(void){reset_count++;}
int retro_load_game(const struct game_info *info){ unsigned fmt=2; load_count++; if(!info||!info->data||info->size<16)return 0; if(!ecb||!ecb(10,&fmt))return 0; seen_fmt=fmt; return 1; }
void retro_unload_game(void){unload_count++;}
unsigned retro_get_region(void){return 0;}
void *retro_get_memory_data(unsigned id){return id==0?save_ram:0;}
size_t retro_get_memory_size(unsigned id){return id==0?sizeof(save_ram):0;}
void retro_run(void){
    unsigned i; run_count++; if(pcb)pcb();
    if(!icb || !icb(0,1,0,8) || !icb(0,1,0,4) || icb(0,1,0,0)) return;
    for(i=0;i<256u*240u;i++)frame[i]=(uint16_t)(0x1000u+(i&0x07ffu));
    for(i=0;i<800u;i++){audio[i*2]=(int16_t)i;audio[i*2+1]=(int16_t)(-((int)i));}
    vcb(frame,256,240,256u*2u); abcb(audio,800u);
}
int ProjectNesCore_ProbeRom(const uint8_t *d,uint32_t n,ProjectNesCoreRomInfo *o){(void)d;(void)n;(void)o;return 1;}

int main(void){
    const ProjectNesProviderDescriptor *pd=ProjectNesFceumm_QueryProvider();
    const ProjectNesCoreBackend *b; ProjectNesCoreInput in={PROJECT_NES_CORE_INPUT_A|PROJECT_NES_CORE_INPUT_UP};
    uint8_t rom[16]={0x4e,0x45,0x53,0x1a}; uint16_t out[512*256]; ProjectNesCoreVideoTarget vt={out,512,256,512}; int16_t pcm[1024*2]; uint8_t save[8192]; unsigned i;
    if(!pd||pd->abi_version!=PROJECT_NES_PROVIDER_ABI_VERSION||!pd->backend||!pd->context)return 1;
    if(strcmp(pd->license_spdx,"GPL-2.0-only")||strcmp(pd->source_commit,"b5e3566515c27dc66c9c20572171673126532e06"))return 2;
    b=pd->backend; if(!b->shutdown)return 3;
    if(!b->load_rom(pd->context,rom,sizeof(rom))||init_count!=1||load_count!=1||seen_fmt!=2)return 4;
    b->set_input(pd->context,&in); if(!b->run_frame(pd->context,&vt)||run_count!=1)return 5;
    if(out[8u*512u+128u]!=frame[0])return 6; /* centered 256x240 inside 512x256 */
    if(b->next_audio_frames(pd->context)!=800u)return 7;
    if(b->render_audio_s16_stereo(pd->context,pcm,1024u)!=800u||pcm[200]!=100||pcm[201]!=-100)return 8;
    if(b->frame_rate_millihz(pd->context)!=60098u)return 9;
    if(b->save_ram_size(pd->context)!=8192u)return 10;
    for (i = 0; i < 8192u; i++) save_ram[i] = (uint8_t)i;
    b->save_ram_clear_dirty(pd->context);
    if (b->save_ram_dirty(pd->context)) return 11;
    save_ram[17] ^= 0x55;
    if (!b->save_ram_dirty(pd->context)) return 12;
    if(!b->save_ram_export(pd->context,save,sizeof(save))||save[17]!=save_ram[17])return 13;
    memset(save,0x3c,sizeof(save)); if(!b->save_ram_import(pd->context,save,sizeof(save))||save_ram[0]!=0x3c||b->save_ram_dirty(pd->context))return 14;
    b->reset(pd->context); if(reset_count!=1)return 15;
    b->shutdown(pd->context); if(unload_count!=1||deinit_count!=1)return 16;
    puts("project-nes-fceumm-adapter-host: OK libretro ownership/video/audio/input/save/lifecycle"); return 0;
}
