#include "titan_game_runtime.h"
#include "titan_input.h"
#include "titan_result.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint64_t g_now_ms = 1000;
static int g_game_storage = 0;
static int g_game_parked = 0;
static int g_video_game = 0;
static int g_video_end_blocked = 0;
static int g_render_state_blocked = 0;
static int g_frame = 0;
static int g_triangle_count = 0;
static int g_textured_triangle_count = 0;
static int g_material_triangle_count = 0;
static float g_last_triangle_x0 = 0.0f;
static float g_last_triangle_y0 = 0.0f;
static float g_last_textured_x0 = 0.0f;
static float g_last_textured_y0 = 0.0f;
static float g_last_material_x0 = 0.0f;
static float g_last_material_y0 = 0.0f;
static int g_texture_live = 0;
static int g_audio = 0;
static int g_input_updates = 0;
static TitanInputPadState g_pad;
static char g_storage_path[768];
static void *g_alloc_stress[20000];
static void *g_frag_stress[768];

uint64_t TitanTime_NowMilliseconds(void){ return g_now_ms++; }
uint64_t TitanTime_NowMicroseconds(void){ return g_now_ms * 1000u; }
void TitanAudio_StopMusic(void){}
void TitanAudio_StartMusic(void){}
int TitanAudio_BeginExternalStream(void){ g_audio=1; return 1; }
void TitanAudio_EndExternalStream(void){ g_audio=0; }
unsigned int TitanAudio_SubmitExternalPcm(const short *p,unsigned int f){ return (g_audio&&p)?f:0u; }
unsigned int TitanAudio_GetExternalFreeFrames(void){ return g_audio?4096u:0u; }
void TitanStorageScheduler_SetTitanGameExclusiveMode(int e){ g_game_storage=e; g_game_parked=e; }
int TitanStorageScheduler_IsTitanGameWorkerParked(void){ return g_game_parked; }
TitanResult TitanVideo_BeginGameMode(unsigned int w,unsigned int h){ if(w!=640||h!=448)return TITAN_ERROR_VIDEO_INITIALIZATION; g_video_game=1; return TITAN_RESULT_OK; }
TitanResult TitanVideo_EndGameMode(void){ if(g_video_end_blocked)return TITAN_ERROR_FRAME_END; g_video_game=0; return TITAN_RESULT_OK; }
int TitanVideo_IsGameMode(void){ return g_video_game; }
unsigned int TitanVideo_GetGameZBufferAddress(void){ return g_video_game?0x1234u:0u; }
unsigned int TitanVideo_GetGameTextureBytesFree(void){ return g_video_game?123456u:0u; }
unsigned int TitanVideo_GetFramebufferCount(void){ return 2u; }
TitanResult TitanVideo_BeginFrame(void){ if(!g_video_game||g_frame)return TITAN_ERROR_FRAME_BEGIN; g_frame=1; return TITAN_RESULT_OK; }
void TitanVideo_Clear(unsigned char r,unsigned char g,unsigned char b){ (void)r;(void)g;(void)b; }
TitanResult TitanVideo_EndFrame(void){ if(!g_frame)return TITAN_ERROR_FRAME_END; g_frame=0; return TITAN_RESULT_OK; }
void TitanVideo_DrawGameTrianglePhysical(float x0,float y0,unsigned int z0,float x1,float y1,unsigned int z1,float x2,float y2,unsigned int z2,unsigned char r,unsigned char g,unsigned char b,unsigned char a){ (void)z0;(void)x1;(void)y1;(void)z1;(void)x2;(void)y2;(void)z2;(void)r;(void)g;(void)b;(void)a; g_last_triangle_x0=x0;g_last_triangle_y0=y0;g_triangle_count++; }
int TitanVideo_DrawGameTexturedTrianglePhysical(float x0,float y0,unsigned int z0,float u0,float v0,float x1,float y1,unsigned int z1,float u1,float v1,float x2,float y2,unsigned int z2,float u2,float v2,unsigned char r0,unsigned char g0,unsigned char b0,unsigned char a0,unsigned char r1,unsigned char g1,unsigned char b1,unsigned char a1,unsigned char r2,unsigned char g2,unsigned char b2,unsigned char a2,unsigned int handle){ (void)z0;(void)u0;(void)v0;(void)x1;(void)y1;(void)z1;(void)u1;(void)v1;(void)x2;(void)y2;(void)z2;(void)u2;(void)v2;(void)r0;(void)g0;(void)b0;(void)a0;(void)r1;(void)g1;(void)b1;(void)a1;(void)r2;(void)g2;(void)b2;(void)a2; if(!g_texture_live||handle!=1u)return 0; g_last_textured_x0=x0;g_last_textured_y0=y0;g_textured_triangle_count++; return 1; }
TitanResult TitanVideo_GameUploadTexturePsm16(const unsigned short *p,unsigned int w,unsigned int h,unsigned int s,unsigned int *a){ (void)h; if(!p||!a||s!=w)return TITAN_ERROR_INVALID_ARGUMENT; *a=1u; g_texture_live=1; return TITAN_RESULT_OK; }
TitanResult TitanVideo_GameUploadTextureRgba32(const unsigned char *p,unsigned int w,unsigned int h,unsigned int s,unsigned int *a){ (void)h; if(!p||!a||s!=w*4u)return TITAN_ERROR_INVALID_ARGUMENT; *a=1u; g_texture_live=1; return TITAN_RESULT_OK; }
TitanResult TitanVideo_GameUploadTextureI8(const unsigned char *p,unsigned int w,unsigned int h,unsigned int s,unsigned int *a){ (void)h; if(!p||!a||s<w)return TITAN_ERROR_INVALID_ARGUMENT; *a=1u; g_texture_live=1; return TITAN_RESULT_OK; }
int TitanVideo_GameReleaseTexture(unsigned int handle){ if(handle!=1u||!g_texture_live)return 0; g_texture_live=0; return 1; }
int TitanVideo_GameSetRenderState(const TitanGameRenderState *state){ return !g_render_state_blocked && state && state->struct_size>=offsetof(TitanGameRenderState,reserved); }
int TitanVideo_DrawGameMaterialTrianglePhysical(
    float x0,float y0,unsigned int z0,float s00,float t00,float q00,float s10,float t10,float q10,
    float x1,float y1,unsigned int z1,float s01,float t01,float q01,float s11,float t11,float q11,
    float x2,float y2,unsigned int z2,float s02,float t02,float q02,float s12,float t12,float q12,
    unsigned char r0,unsigned char g0,unsigned char b0,unsigned char a0,
    unsigned char r1,unsigned char g1,unsigned char b1,unsigned char a1,
    unsigned char r2,unsigned char g2,unsigned char b2,unsigned char a2,
    unsigned char f0,unsigned char f1,unsigned char f2,unsigned char l0,unsigned char l1,unsigned char l2,
    unsigned int h0,unsigned int h1){
    (void)x0;(void)y0;(void)z0;(void)s00;(void)t00;(void)q00;(void)s10;(void)t10;(void)q10;
    (void)x1;(void)y1;(void)z1;(void)s01;(void)t01;(void)q01;(void)s11;(void)t11;(void)q11;
    (void)x2;(void)y2;(void)z2;(void)s02;(void)t02;(void)q02;(void)s12;(void)t12;(void)q12;
    (void)r0;(void)g0;(void)b0;(void)a0;(void)r1;(void)g1;(void)b1;(void)a1;
    (void)r2;(void)g2;(void)b2;(void)a2;(void)f0;(void)f1;(void)f2;(void)l0;(void)l1;(void)l2;(void)h1;
    if(h0!=1u||!g_texture_live)return 0;
    g_last_material_x0=x0;g_last_material_y0=y0;
    g_material_triangle_count++;
    return 1;
}
void TitanVideo_GameResetTextures(void){ g_texture_live=0; }
void TitanInput_Update(void){g_input_updates++;}
const TitanInputPadState *TitanInput_GetPadState(unsigned int i){ return i==0?&g_pad:0; }
unsigned int TitanInput_GetConnectedPadCount(void){ return g_pad.connected?1u:0u; }
int TitanInput_SetRumble(unsigned int i,unsigned char s,unsigned char l){(void)s;(void)l;return i==0u;}
void TitanInput_StopAllRumble(void){}
int TitanStorageIo_Open(const char *p,int f){ (void)f; strncpy(g_storage_path,p,sizeof(g_storage_path)-1u);g_storage_path[sizeof(g_storage_path)-1u]='\0';return 3; }
int TitanStorageIo_Close(int d){ return d==3; }
int64_t TitanStorageIo_ReadSome(int d,void *b,size_t s){ (void)d;(void)b; return (int64_t)s; }
int64_t TitanStorageIo_WriteSome(int d,const void *b,size_t s){ (void)d;(void)b; return (int64_t)s; }
int64_t TitanStorageIo_Seek64(int d,int64_t o,int wh){ (void)d;(void)wh; return o; }
int TitanStorageIo_PathInfo64(const char *p,int *dir,uint64_t *size){ (void)p; if(dir)*dir=0;if(size)*size=42;return 1; }
int TitanStorageIo_ListDirectory(const char *p,unsigned int start,void *e,unsigned int cap){ (void)p;(void)start;(void)e;(void)cap;return 0; }
int TitanStorageIo_CreateDirectory(const char *p){ (void)p; return 1; }
int TitanStorageIo_RemoveAny(const char *p){ (void)p; return 1; }
int TitanStorageIo_Rename(const char *a,const char *b){ (void)a;(void)b; return 1; }

int main(void)
{
    TitanGameSessionConfig cfg;
    const TitanGameApiV1 *api=0;
    TitanGamePadState pad;
    TitanGameVideoInfo info;
    TitanGameVertex3D v[3];
    TitanGameMaterialVertex material[3];
    TitanGameClipVertex clip[3];
    TitanGameRenderState state;
    uint16_t ix[3]={0,1,2};
    uint16_t texture[4]={0xffff,0xffff,0xffff,0xffff};
    uint8_t rgba_texture[16]={255u,0u,0u,64u,0u,255u,0u,128u,0u,0u,255u,192u,255u,255u,255u,255u};
    uint8_t i8_texture[4]={0u,85u,170u,255u};
    uint32_t handle=0;
    void *p;
    short pcm[8]={0};

    memset(&cfg,0,sizeof(cfg)); cfg.struct_size=sizeof(cfg); cfg.width=640; cfg.height=448; cfg.heap_limit_bytes=16u*1024u*1024u;
    assert(TitanGameRuntime_SetStorageRoot("mass:/TITAN/apps/titaneye"));
    assert(TitanGameRuntime_Begin(&cfg,&api));
    assert(api && api->abi_version==TITAN_GAME_API_VERSION);
    assert(g_game_storage && g_video_game && TitanGameRuntime_IsActive());
    assert(api->heap_limit()==16u*1024u*1024u);
    p=api->alloc(1024,64); assert(p && ((uintptr_t)p%64u)==0u); assert(api->heap_used()==1024u); api->release(p); assert(api->heap_used()==0u);
    /* P162.2: exceed the old 8192-slot registry with tiny live allocations
     * while staying far below the byte heap limit; release in a nontrivial
     * order and prove the O(1) slot cookie/free-list path returns to zero. */
    for (unsigned int ai=0; ai<20000u; ai++) {
        g_alloc_stress[ai]=api->alloc(32u,16u);
        assert(g_alloc_stress[ai] && ((uintptr_t)g_alloc_stress[ai]%16u)==0u);
    }
    assert(api->heap_used()==20000u*32u);
    for (unsigned int ai=1u; ai<20000u; ai+=2u) api->release(g_alloc_stress[ai]);
    for (unsigned int ai=0u; ai<20000u; ai+=2u) api->release(g_alloc_stress[ai]);
    assert(api->heap_used()==0u);

    /* P162.4: emulate the hardware failure pattern that previously left the
     * logical heap around 6.5 MiB yet made newlib reject 32B/208B/0x1010 and
     * 0x6910 requests. The chunk allocator must reuse/coalesce interior free
     * blocks without issuing one libc malloc/free per game object. */
    {
        static const uint32_t sizes[] = {32u,208u,0x1010u,0x6910u,0x210u,0x4010u,0x1110u};
        for (unsigned int ai=0u; ai<768u; ai++) {
            uint32_t size=sizes[ai%(sizeof(sizes)/sizeof(sizes[0]))];
            g_frag_stress[ai]=api->alloc(size, (ai&3u)==0u?64u:16u);
            assert(g_frag_stress[ai]!=0);
        }
        for (unsigned int ai=0u; ai<768u; ai+=2u) {
            api->release(g_frag_stress[ai]);
            g_frag_stress[ai]=0;
        }
        for (unsigned int ai=0u; ai<768u; ai+=2u) {
            g_frag_stress[ai]=api->alloc((ai&4u)?0x3010u:0x1810u,32u);
            assert(g_frag_stress[ai]!=0);
        }
        for (unsigned int ai=0u; ai<768u; ai++) api->release(g_frag_stress[ai]);
        assert(api->heap_used()==0u);
    }

    /* P162.5.6: push payload beyond the normal 10 MiB partition. The carved
     * emergency bump arena must carry the tail without relying on another
     * libc allocation or the intrusive free-list. */
    for (unsigned int ai=0u; ai<2700u; ai++) {
        g_alloc_stress[ai]=api->alloc(4096u,64u);
        assert(g_alloc_stress[ai]!=0);
    }
    assert(api->heap_used()==2700u*4096u);
    for (unsigned int ai=0u; ai<2700u; ai++) api->release(g_alloc_stress[ai]);
    assert(api->heap_used()==0u);

    memset(&g_pad,0,sizeof(g_pad)); g_pad.connected=1; g_pad.port=0; g_pad.buttons=TITAN_INPUT_BUTTON_CROSS; g_pad.pressed=TITAN_INPUT_BUTTON_CROSS; g_pad.left_stick_x=130;
    api->poll_input(); assert(api->get_pad(0,&pad)); assert(pad.connected && pad.held==TITAN_GAME_BUTTON_CROSS && pad.left_stick_x==130);
    assert(g_input_updates==0);
    assert(api->connected_pad_count()==1u);
    assert((api->feature_flags&TITAN_GAME_FEATURE_HAPTICS)!=0u);
    assert(api->set_pad_rumble(0u,0u,128u));

    assert(api->storage_open("rom/GoldenEye 007.z64",TITAN_GAME_STORAGE_READ)==3);
    assert(strcmp(g_storage_path,"mass:/TITAN/apps/titaneye/rom/GoldenEye 007.z64")==0);
    assert(api->storage_open("data/save.eep",TITAN_GAME_STORAGE_WRITE|TITAN_GAME_STORAGE_CREATE)==3);
    assert(strcmp(g_storage_path,"mass:/TITAN/apps/titaneye/data/save.eep")==0);
    assert(api->storage_open("rom/GoldenEye 007.z64",TITAN_GAME_STORAGE_WRITE)<0);
    assert(api->storage_open("../outside",TITAN_GAME_STORAGE_READ)<0);
    assert(api->storage_mkdir("data/profiles")>=0);
    assert(api->storage_mkdir("rom/nope")<0);

    assert(api->video_get_info(&info)); assert(info.width==640 && info.height==448 && info.zbuffer_enabled);
    memset(v,0,sizeof(v));
    v[0].x=-0.5f;v[0].y=-0.5f;v[0].z=0;v[0].rgba=0xff0000ffu;
    v[1].x=0.5f;v[1].y=-0.5f;v[1].z=0;v[1].rgba=0xff0000ffu;
    v[2].x=0.0f;v[2].y=0.5f;v[2].z=0;v[2].rgba=0xff0000ffu;
    assert(api->video_begin_frame(0,0,0)); assert(api->video_draw_indexed_triangles(v,3,ix,3,0u)==1); assert(g_triangle_count==1); assert(api->video_end_frame());
    assert((api->feature_flags & TITAN_GAME_FEATURE_TEXTURED_TRIANGLES) != 0u);
    assert((api->feature_flags & TITAN_GAME_FEATURE_TEXTURE_UPLOAD_RGBA32) != 0u);
    assert((api->feature_flags & TITAN_GAME_FEATURE_TEXTURE_UPLOAD_I8) != 0u);
    assert(api->video_upload_texture_rgba32 != 0);
    assert(api->video_upload_texture_i8 != 0);
    assert(api->video_upload_texture_rgba32(rgba_texture,2u,2u,8u,&handle) && handle==1u);
    assert(api->video_release_texture(handle));
    assert(api->video_upload_texture_i8(i8_texture,2u,2u,2u,&handle) && handle==1u);
    assert(api->video_release_texture(handle));
    assert(api->video_upload_texture_psm16(texture,2,2,2,&handle) && handle==1u);
    assert(api->video_begin_frame(0,0,0)); assert(api->video_draw_indexed_triangles(v,3,ix,3,handle)==1); assert(g_textured_triangle_count==1); assert(api->video_end_frame());
    memset(&state,0,sizeof(state)); state.struct_size=sizeof(state); state.viewport_x=320; state.viewport_y=224; state.viewport_width=320; state.viewport_height=224; state.scissor_x=320; state.scissor_y=224; state.scissor_width=320; state.scissor_height=224; state.depth_test=1; state.depth_write=1; state.depth_compare=TITAN_GAME_DEPTH_GEQUAL; state.combine_mode=TITAN_GAME_COMBINE_TEX0_MODULATE;
    assert(api->video_set_render_state(&state));
    assert(api->video_begin_frame(0,0,0)); assert(api->video_draw_indexed_triangles(v,3,ix,3,0u)==1); assert(g_triangle_count==2); assert(g_last_triangle_x0==400.0f&&g_last_triangle_y0==392.0f); assert(api->video_end_frame());
    memset(material,0,sizeof(material));
    for(unsigned int mi=0;mi<3u;mi++){ material[mi].w=1.0f; material[mi].rgba=0xffffffffu; }
    material[0].x=-1.0f; material[0].y=1.0f; material[1].x=1.0f; material[1].y=1.0f; material[2].y=-1.0f;
    assert(api->video_begin_frame(0,0,0)); assert(api->video_draw_material_triangles(material,3,handle,0u)==1); assert(g_material_triangle_count==1); assert(g_last_material_x0==320.0f&&g_last_material_y0==224.0f); assert(api->video_end_frame());
    memset(clip,0,sizeof(clip));
    for(unsigned int ci=0;ci<3u;ci++){clip[ci].w=1.0f;clip[ci].rgba=0xffffffffu;}
    clip[0].x=-1.0f;clip[0].y=1.0f;clip[1].x=1.0f;clip[1].y=1.0f;clip[2].y=-1.0f;
    assert(api->video_begin_frame(0,0,0)); assert(api->video_draw_clip_triangles(clip,3,handle)==1); assert(g_last_textured_x0==320.0f&&g_last_textured_y0==224.0f); assert(api->video_end_frame());
    assert(api->video_release_texture(handle));
    assert(!api->video_release_texture(handle));

    assert(api->audio_begin()); assert(api->audio_submit_pcm(pcm,2)==2u); assert(api->audio_free_frames()==4096u); api->audio_end(); assert(!g_audio);
    api->request_exit(); assert(api->exit_requested());
    p=api->alloc(512u,16u); assert(p && api->heap_used()==512u);
    g_video_end_blocked=1;
    assert(!TitanGameRuntime_End());
    assert(g_game_storage && g_video_game && TitanGameRuntime_IsActive());
    assert(api->heap_used()==512u);
    g_video_end_blocked=0;
    assert(TitanGameRuntime_End());
    assert(!g_game_storage && !g_video_game && !TitanGameRuntime_IsActive());

    /* Failed Begin after video ownership crossed must also remain teardown-only
     * until EndGameMode can safely return VIF1 to the shell. */
    api=(const TitanGameApiV1 *)(uintptr_t)1u;
    assert(TitanGameRuntime_SetStorageRoot("mass:/TITAN/apps/titaneye"));
    g_render_state_blocked=1; g_video_end_blocked=1;
    assert(!TitanGameRuntime_Begin(&cfg,&api));
    assert(api==0 && g_game_storage && g_video_game && TitanGameRuntime_IsActive());
    assert(!TitanGameRuntime_SetStorageRoot("mass:/TITAN/apps/other"));
    g_render_state_blocked=0; g_video_end_blocked=0;
    assert(TitanGameRuntime_End());
    assert(!g_game_storage && !g_video_game && !TitanGameRuntime_IsActive());
    puts("titan-game-runtime: OK transactional video ownership teardown");
    return 0;
}
