#include "titan_ge_platform.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned short g_uploaded[4];
static unsigned char g_uploaded_rgba[16];
static unsigned int g_draw_handle;
static unsigned int g_rgba_stride;
static void *fake_alloc(uint32_t bytes,uint32_t alignment)
{
    void *pointer=0;
    if(posix_memalign(&pointer,alignment,bytes)!=0)return 0;
    return pointer;
}
static void fake_release(void *pointer){free(pointer);}
static int fake_upload(const uint16_t *pixels,uint32_t width,uint32_t height,uint32_t stride,uint32_t *handle)
{
    assert(width==2u&&height==2u&&stride==2u);
    memcpy(g_uploaded,pixels,sizeof(g_uploaded)); *handle=9u; return 1;
}
static int fake_upload_rgba32(const uint8_t *pixels,uint32_t width,uint32_t height,uint32_t stride,uint32_t *handle)
{
    assert(width==2u&&height==2u&&stride==8u);
    memcpy(g_uploaded_rgba,pixels,sizeof(g_uploaded_rgba));
    g_rgba_stride=stride; *handle=10u; return 1;
}
static int fake_draw(const TitanGameVertex3D *vertices,uint32_t vertex_count,const uint16_t *indices,uint32_t index_count,uint32_t handle)
{
    assert(vertices!=0&&vertex_count==3u&&indices!=0&&index_count==3u);
    g_draw_handle=handle; return 1;
}
static int fake_release_texture(uint32_t handle){return handle==9u||handle==10u;}

int main(void)
{
    TitanGameApiV1 api;
    TitanGePlatform platform;
    TitanGameVertex3D vertices[3];
    unsigned short indices[3]={0u,1u,2u};
    const unsigned char rgba[16]={
        255u,0u,0u,255u, 0u,255u,0u,127u,
        0u,0u,255u,128u, 255u,255u,255u,0u
    };
    unsigned int handle=0u;
    memset(&api,0,sizeof(api));
    api.abi_version=TITAN_GAME_API_VERSION;
    /* An older v2 prefix remains attachable and takes the PSM16 fallback. */
    api.struct_size=offsetof(TitanGameApiV1,video_upload_texture_rgba32);
    api.alloc=fake_alloc; api.release=fake_release;
    api.video_upload_texture_psm16=fake_upload;
    api.video_draw_indexed_triangles=fake_draw;
    api.video_release_texture=fake_release_texture;
    memset(vertices,0,sizeof(vertices));
    assert(TitanGePlatform_Attach(&platform,&api));
    assert(TitanGePlatform_UploadTextureRgba32(&platform,rgba,2u,2u,&handle));
    assert(handle==9u);
    assert(g_uploaded[0]==0x801fu);
    assert(g_uploaded[1]==0x03e0u);
    assert(g_uploaded[2]==0xfc00u);
    assert(g_uploaded[3]==0x7fffu);
    assert(TitanGePlatform_DrawTexturedTriangles(&platform,vertices,3u,indices,3u,handle)==1);
    assert(g_draw_handle==9u);
    assert(TitanGePlatform_ReleaseTexture(&platform,handle));
    api.struct_size=sizeof(api);
    api.feature_flags|=TITAN_GAME_FEATURE_TEXTURE_UPLOAD_RGBA32;
    api.video_upload_texture_rgba32=fake_upload_rgba32;
    assert(TitanGePlatform_UploadTextureRgba32(&platform,rgba,2u,2u,&handle));
    assert(handle==10u&&g_rgba_stride==8u);
    assert(memcmp(g_uploaded_rgba,rgba,sizeof(rgba))==0);
    assert(TitanGePlatform_ReleaseTexture(&platform,handle));
    puts("titan-ge-platform: OK RGBA32 preservation with PSM16 fallback");
    return 0;
}
