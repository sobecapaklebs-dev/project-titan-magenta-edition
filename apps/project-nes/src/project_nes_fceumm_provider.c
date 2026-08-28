#include "project_nes_fceumm_provider.h"

#include <stddef.h>
#include <stdint.h>

#ifndef PROJECT_NES_FCEUMM_SOURCE_COMMIT
#define PROJECT_NES_FCEUMM_SOURCE_COMMIT "b5e3566515c27dc66c9c20572171673126532e06"
#endif

#ifndef PROJECT_NES_FCEUMM_LICENSE_SPDX
#define PROJECT_NES_FCEUMM_LICENSE_SPDX "GPL-2.0-only"
#endif

#if defined(PROJECT_NES_FCEUMM_LINKED)
/* Minimal stable libretro ABI surface used by the Titan adapter. The frozen
 * FCEUmm snapshot supplies the real definitions/functions at target link time;
 * keeping this shim local avoids exposing libretro ownership to Titan apps. */
typedef int pn_lr_bool;
typedef pn_lr_bool (*pn_lr_environment_t)(unsigned, void *);
typedef void (*pn_lr_video_t)(const void *, unsigned, unsigned, size_t);
typedef void (*pn_lr_audio_t)(int16_t, int16_t);
typedef size_t (*pn_lr_audio_batch_t)(const int16_t *, size_t);
typedef void (*pn_lr_input_poll_t)(void);
typedef int16_t (*pn_lr_input_state_t)(unsigned, unsigned, unsigned, unsigned);

struct pn_lr_game_info { const char *path; const void *data; size_t size; const char *meta; };
struct pn_lr_game_info_ext {
    const char *full_path; const char *archive_path; const char *archive_file;
    const char *dir; const char *name; const char *ext; const char *meta;
    const void *data; size_t size; pn_lr_bool file_in_archive; pn_lr_bool persistent_data;
};

enum {
    PN_LR_ENV_GET_SYSTEM_DIRECTORY = 9,
    PN_LR_ENV_GET_GAME_INFO_EXT = 66,
    PN_LR_ENV_SET_PIXEL_FORMAT = 10,
    PN_LR_ENV_GET_TARGET_SAMPLE_RATE = (81 | 0x10000),
    PN_LR_DEVICE_JOYPAD = 1,
    PN_LR_PIX_0RGB1555 = 0,
    PN_LR_PIX_XRGB8888 = 1,
    PN_LR_PIX_RGB565 = 2,
    PN_LR_MEMORY_SAVE_RAM = 0,
    PN_LR_REGION_NTSC = 0,
    PN_LR_JOY_B = 0,
    PN_LR_JOY_SELECT = 2,
    PN_LR_JOY_START = 3,
    PN_LR_JOY_UP = 4,
    PN_LR_JOY_DOWN = 5,
    PN_LR_JOY_LEFT = 6,
    PN_LR_JOY_RIGHT = 7,
    PN_LR_JOY_A = 8
};

extern void retro_set_environment(pn_lr_environment_t cb);
extern void retro_set_video_refresh(pn_lr_video_t cb);
extern void retro_set_audio_sample(pn_lr_audio_t cb);
extern void retro_set_audio_sample_batch(pn_lr_audio_batch_t cb);
extern void retro_set_input_poll(pn_lr_input_poll_t cb);
extern void retro_set_input_state(pn_lr_input_state_t cb);
extern void retro_init(void);
extern void retro_deinit(void);
extern void retro_reset(void);
extern pn_lr_bool retro_load_game(const struct pn_lr_game_info *info);
extern void retro_unload_game(void);
extern void retro_run(void);
extern unsigned retro_get_region(void);
extern void *retro_get_memory_data(unsigned id);
extern size_t retro_get_memory_size(unsigned id);

#define PN_FCEUMM_AUDIO_CAP 2048u

typedef struct ProjectNesFceummContext
{
    ProjectNesCoreInput input;
    ProjectNesCoreVideoTarget video;
    int16_t audio[PN_FCEUMM_AUDIO_CAP * 2u];
    uint32_t audio_frames;
    uint32_t save_baseline;
    uint8_t initialized;
    uint8_t loaded;
    uint8_t video_seen;
    uint8_t pixel_format;
} ProjectNesFceummContext;

static ProjectNesFceummContext g_fceumm;
static ProjectNesFceummContext *g_active;
static const void *g_load_data;
static size_t g_load_size;

static void pn_memzero(void *ptr, uint32_t bytes)
{
    uint8_t *p=(uint8_t*)ptr; uint32_t i;
    for(i=0u;i<bytes;i++) p[i]=0u;
}

static uint32_t pn_hash(const uint8_t *data, uint32_t size)
{
    uint32_t h=2166136261u,i;
    if(data==0) return 0u;
    for(i=0u;i<size;i++){ h^=data[i]; h*=16777619u; }
    return h;
}

static pn_lr_bool pn_environment(unsigned cmd, void *data)
{
    ProjectNesFceummContext *c=g_active;
    static const char system_dir[] = "data/system";
    static struct pn_lr_game_info_ext game_ext;
    if(cmd==PN_LR_ENV_GET_GAME_INFO_EXT && data!=0 && g_load_data!=0)
    {
        game_ext.full_path="content.nes"; game_ext.archive_path=0; game_ext.archive_file=0;
        game_ext.dir="."; game_ext.name="content"; game_ext.ext="nes"; game_ext.meta=0;
        game_ext.data=g_load_data; game_ext.size=g_load_size; game_ext.file_in_archive=0; game_ext.persistent_data=1;
        *(const struct pn_lr_game_info_ext **)data=&game_ext; return 1;
    }
    if(cmd==PN_LR_ENV_GET_SYSTEM_DIRECTORY && data!=0)
    {
        *(const char **)data = system_dir;
        return 1;
    }
    if(cmd==PN_LR_ENV_GET_TARGET_SAMPLE_RATE && data!=0)
    {
        *(double *)data = 32000.0;
        return 1;
    }
    if(cmd==PN_LR_ENV_SET_PIXEL_FORMAT && data!=0 && c!=0)
    {
        unsigned fmt=*(const unsigned*)data;
        if(fmt==PN_LR_PIX_RGB565 || fmt==PN_LR_PIX_0RGB1555 || fmt==PN_LR_PIX_XRGB8888)
        { c->pixel_format=(uint8_t)fmt; return 1; }
        return 0;
    }
    /* FCEUmm has safe defaults when optional frontend environment features are
     * unavailable. Titan deliberately does not expose filesystem/log/UI hooks. */
    return 0;
}

static uint16_t pn_1555_to_565(uint16_t v)
{
    uint16_t r=(uint16_t)((v>>10)&31u), g=(uint16_t)((v>>5)&31u), b=(uint16_t)(v&31u);
    g=(uint16_t)((g<<1)|(g>>4));
    return (uint16_t)((r<<11)|(g<<5)|b);
}

static uint16_t pn_8888_to_565(uint32_t v)
{
    return (uint16_t)((((v>>16)&0xf8u)<<8)|(((v>>8)&0xfcu)<<3)|((v>>3)&0x1fu));
}

static void pn_video(const void *data, unsigned width, unsigned height, size_t pitch)
{
    ProjectNesFceummContext *c=g_active;
    uint32_t y, copy_w, copy_h, dst_x, dst_y;
    if(c==0 || c->video.pixels==0) return;
    if(data==0){ c->video_seen=1u; return; }
    copy_w=width<c->video.width?width:c->video.width;
    copy_h=height<c->video.height?height:c->video.height;
    dst_x=(c->video.width-copy_w)/2u; dst_y=(c->video.height-copy_h)/2u;
    for(y=0u;y<c->video.height;y++)
    {
        uint32_t x; uint16_t *d=c->video.pixels+y*c->video.stride;
        for(x=0u;x<c->video.width;x++) d[x]=0u;
    }
    for(y=0u;y<copy_h;y++)
    {
        uint32_t x; uint16_t *d=c->video.pixels+(dst_y+y)*c->video.stride+dst_x;
        const uint8_t *row=(const uint8_t*)data+y*pitch;
        if(c->pixel_format==PN_LR_PIX_XRGB8888)
        {
            const uint32_t *s=(const uint32_t*)row;
            for(x=0u;x<copy_w;x++) d[x]=pn_8888_to_565(s[x]);
        }
        else if(c->pixel_format==PN_LR_PIX_0RGB1555)
        {
            const uint16_t *s=(const uint16_t*)row;
            for(x=0u;x<copy_w;x++) d[x]=pn_1555_to_565(s[x]);
        }
        else
        {
            const uint16_t *s=(const uint16_t*)row;
            for(x=0u;x<copy_w;x++) d[x]=s[x];
        }
    }
    c->video_seen=1u;
}

static void pn_audio_one(int16_t left, int16_t right)
{
    ProjectNesFceummContext *c=g_active;
    if(c!=0 && c->audio_frames<PN_FCEUMM_AUDIO_CAP)
    { c->audio[c->audio_frames*2u]=left; c->audio[c->audio_frames*2u+1u]=right; c->audio_frames++; }
}

static size_t pn_audio_batch(const int16_t *data, size_t frames)
{
    ProjectNesFceummContext *c=g_active; size_t i, room, take;
    if(c==0 || data==0) return frames;
    room=PN_FCEUMM_AUDIO_CAP-c->audio_frames; take=frames<room?frames:room;
    for(i=0;i<take;i++)
    { c->audio[(c->audio_frames+(uint32_t)i)*2u]=data[i*2u]; c->audio[(c->audio_frames+(uint32_t)i)*2u+1u]=data[i*2u+1u]; }
    c->audio_frames+=(uint32_t)take;
    return frames;
}

static void pn_input_poll(void) { }

static int16_t pn_input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    uint8_t b; (void)index;
    if(g_active==0 || port!=0u || device!=PN_LR_DEVICE_JOYPAD) return 0;
    b=g_active->input.buttons;
    switch(id)
    {
        case PN_LR_JOY_A: return (b&PROJECT_NES_CORE_INPUT_A)?1:0;
        case PN_LR_JOY_B: return (b&PROJECT_NES_CORE_INPUT_B)?1:0;
        case PN_LR_JOY_SELECT: return (b&PROJECT_NES_CORE_INPUT_SELECT)?1:0;
        case PN_LR_JOY_START: return (b&PROJECT_NES_CORE_INPUT_START)?1:0;
        case PN_LR_JOY_UP: return (b&PROJECT_NES_CORE_INPUT_UP)?1:0;
        case PN_LR_JOY_DOWN: return (b&PROJECT_NES_CORE_INPUT_DOWN)?1:0;
        case PN_LR_JOY_LEFT: return (b&PROJECT_NES_CORE_INPUT_LEFT)?1:0;
        case PN_LR_JOY_RIGHT: return (b&PROJECT_NES_CORE_INPUT_RIGHT)?1:0;
        default: return 0;
    }
}

static int pn_init(ProjectNesFceummContext *c)
{
    if(c->initialized) return 1;
    g_active=c;
    c->pixel_format=PN_LR_PIX_0RGB1555;
    retro_set_environment(pn_environment);
    retro_set_video_refresh(pn_video);
    retro_set_audio_sample(pn_audio_one);
    retro_set_audio_sample_batch(pn_audio_batch);
    retro_set_input_poll(pn_input_poll);
    retro_set_input_state(pn_input_state);
    retro_init();
    c->initialized=1u;
    return 1;
}

static int pn_load(void *context, uint8_t *data, uint32_t size)
{
    ProjectNesFceummContext *c=(ProjectNesFceummContext*)context;
    struct pn_lr_game_info info;
    if(c==0 || data==0 || size<16u || !pn_init(c)) return 0;
    g_active=c;
    if(c->loaded){ retro_unload_game(); c->loaded=0u; }
    info.path="content.nes"; info.data=data; info.size=size; info.meta=0;
    g_load_data=data; g_load_size=size;
    if(!retro_load_game(&info)){ g_load_data=0; g_load_size=0u; return 0; }
    c->loaded=1u; c->audio_frames=0u; c->video_seen=0u;
    g_load_data=0; g_load_size=0u;
    c->save_baseline=pn_hash((const uint8_t*)retro_get_memory_data(PN_LR_MEMORY_SAVE_RAM),
                             (uint32_t)retro_get_memory_size(PN_LR_MEMORY_SAVE_RAM));
    return 1;
}

static void pn_reset(void *context)
{ ProjectNesFceummContext *c=(ProjectNesFceummContext*)context; if(c&&c->loaded){g_active=c;retro_reset();} }
static void pn_set_input(void *context,const ProjectNesCoreInput *input)
{ ProjectNesFceummContext *c=(ProjectNesFceummContext*)context; if(c) c->input=input?*input:(ProjectNesCoreInput){0u}; }

static int pn_run(void *context,const ProjectNesCoreVideoTarget *video)
{
    ProjectNesFceummContext *c=(ProjectNesFceummContext*)context;
    if(c==0||!c->loaded||video==0||video->pixels==0||video->stride<video->width) return 0;
    c->video=*video; c->video_seen=0u; c->audio_frames=0u; g_active=c; retro_run();
    return c->video_seen?1:0;
}

static uint32_t pn_rate(void *context)
{ ProjectNesFceummContext *c=(ProjectNesFceummContext*)context; if(c==0||!c->loaded) return 60000u; g_active=c; return retro_get_region()==PN_LR_REGION_NTSC?60098u:50007u; }
static uint32_t pn_next_audio(void *context)
{ ProjectNesFceummContext *c=(ProjectNesFceummContext*)context; return c?c->audio_frames:0u; }
static uint32_t pn_render_audio(void *context,int16_t *samples,uint32_t capacity)
{
    ProjectNesFceummContext *c=(ProjectNesFceummContext*)context; uint32_t i,n;
    if (c == 0 || samples == 0) return 0u;
    n = c->audio_frames;
    if (n > capacity) return 0u;
    for (i = 0u; i < n * 2u; i++) samples[i] = c->audio[i];
    c->audio_frames = 0u;
    return n;
}
static uint32_t pn_save_size(void *context)
{ ProjectNesFceummContext *c=(ProjectNesFceummContext*)context; if(c==0||!c->loaded)return 0u;g_active=c;return (uint32_t)retro_get_memory_size(PN_LR_MEMORY_SAVE_RAM); }
static int pn_save_export(void *context,uint8_t *data,uint32_t capacity)
{
    ProjectNesFceummContext *c=(ProjectNesFceummContext*)context; uint32_t i,n=pn_save_size(context); const uint8_t *src;
    if (n == 0u || data == 0 || capacity < n) return 0;
    g_active = c;
    src = (const uint8_t *)retro_get_memory_data(PN_LR_MEMORY_SAVE_RAM);
    if (src == 0) return 0;
    for (i = 0u; i < n; i++) data[i] = src[i];
    return 1;
}
static int pn_save_import(void *context,const uint8_t *data,uint32_t size)
{
    ProjectNesFceummContext *c=(ProjectNesFceummContext*)context; uint32_t i,n=pn_save_size(context); uint8_t *dst;
    if (n == 0u || data == 0 || size != n) return 0;
    g_active = c;
    dst = (uint8_t *)retro_get_memory_data(PN_LR_MEMORY_SAVE_RAM);
    if (dst == 0) return 0;
    for (i = 0u; i < n; i++) dst[i] = data[i];
    c->save_baseline = pn_hash(dst, n);
    return 1;
}
static int pn_save_dirty(void *context)
{
    ProjectNesFceummContext *c=(ProjectNesFceummContext*)context; uint32_t n=pn_save_size(context); const uint8_t *p;
    if (c == 0 || n == 0u) return 0;
    g_active = c;
    p = (const uint8_t *)retro_get_memory_data(PN_LR_MEMORY_SAVE_RAM);
    return p != 0 && pn_hash(p, n) != c->save_baseline;
}
static void pn_save_clear(void *context)
{
    ProjectNesFceummContext *c=(ProjectNesFceummContext*)context; uint32_t n=pn_save_size(context); const uint8_t *p;
    if (c == 0) return;
    g_active = c;
    p = (const uint8_t *)retro_get_memory_data(PN_LR_MEMORY_SAVE_RAM);
    c->save_baseline = pn_hash(p, n);
}
static int pn_probe(void *context,const uint8_t *data,uint32_t size,ProjectNesCoreRomInfo *info)
{ (void)context; return ProjectNesCore_ProbeRom(data,size,info); }
static void pn_shutdown(void *context)
{
    ProjectNesFceummContext *c = (ProjectNesFceummContext *)context;
    if (c == 0) return;
    g_active = c;
    if (c->loaded) retro_unload_game();
    if (c->initialized) retro_deinit();
    pn_memzero(c, (uint32_t)sizeof(*c));
    g_active = 0;
}

static const ProjectNesCoreBackend g_backend={
    "FCEUmm/libretro",pn_load,pn_reset,pn_set_input,pn_run,pn_rate,pn_next_audio,pn_render_audio,
    pn_save_size,pn_save_export,pn_save_import,pn_save_dirty,pn_save_clear,pn_probe,pn_shutdown
};
static const ProjectNesProviderDescriptor g_descriptor={
    PROJECT_NES_PROVIDER_ABI_VERSION,sizeof(ProjectNesProviderDescriptor),"FCEUmm/libretro",
    PROJECT_NES_FCEUMM_LICENSE_SPDX,PROJECT_NES_FCEUMM_SOURCE_COMMIT,&g_backend,&g_fceumm
};
const ProjectNesProviderDescriptor *ProjectNesFceumm_QueryProvider(void){return &g_descriptor;}
#else
const ProjectNesProviderDescriptor *ProjectNesFceumm_QueryProvider(void){return 0;}
#endif
