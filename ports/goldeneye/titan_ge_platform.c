#include "titan_ge_platform.h"
#include <stddef.h>
#include <string.h>

#define TITAN_GE_REQUIRED_API_BYTES \
    offsetof(TitanGameApiV1, video_upload_texture_rgba32)
#define TITAN_GE_RGBA32_API_BYTES \
    (offsetof(TitanGameApiV1, video_upload_texture_rgba32) + \
     sizeof(((TitanGameApiV1 *)0)->video_upload_texture_rgba32))
#define TITAN_GE_I8_API_BYTES \
    (offsetof(TitanGameApiV1, video_upload_texture_i8) + \
     sizeof(((TitanGameApiV1 *)0)->video_upload_texture_i8))

int TitanGePlatform_Attach(TitanGePlatform *platform, const TitanGameApiV1 *api)
{
    if (platform == 0 || api == 0 || api->abi_version != TITAN_GAME_API_VERSION ||
        api->struct_size < TITAN_GE_REQUIRED_API_BYTES)
        return 0;
    memset(platform, 0, sizeof(*platform));
    platform->api = api;
    return 1;
}

void TitanGePlatform_Poll(TitanGePlatform *platform)
{
    unsigned int index;
    if (platform == 0 || platform->api == 0) return;
    platform->api->poll_input();
    for (index = 0u; index < TITAN_GAME_MAX_PADS; index++)
    {
        memset(&platform->pads[index], 0, sizeof(platform->pads[index]));
        (void)platform->api->get_pad(index, &platform->pads[index]);
    }
}

const TitanGamePadState *TitanGePlatform_GetPad(const TitanGePlatform *platform, unsigned int index)
{
    if (platform == 0 || index >= TITAN_GAME_MAX_PADS) return 0;
    return &platform->pads[index];
}

int TitanGePlatform_BeginFrame(TitanGePlatform *platform, unsigned char r, unsigned char g, unsigned char b)
{
    return platform != 0 && platform->api != 0
        ? platform->api->video_begin_frame(r, g, b)
        : 0;
}

void TitanGePlatform_SetMvp(TitanGePlatform *platform, const float matrix16[16])
{
    if (platform != 0 && platform->api != 0)
        platform->api->video_set_mvp(matrix16);
}

int TitanGePlatform_DrawTriangles(
    TitanGePlatform *platform,
    const TitanGameVertex3D *vertices, unsigned int vertex_count,
    const unsigned short *indices, unsigned int index_count
)
{
    if (platform == 0 || platform->api == 0) return -1;
    return platform->api->video_draw_indexed_triangles(
        vertices, vertex_count, indices, index_count, 0u
    );
}

int TitanGePlatform_DrawTexturedTriangles(
    TitanGePlatform *platform,
    const TitanGameVertex3D *vertices, unsigned int vertex_count,
    const unsigned short *indices, unsigned int index_count,
    unsigned int texture_handle
)
{
    if (platform == 0 || platform->api == 0 || texture_handle == 0u)
        return -1;
    return platform->api->video_draw_indexed_triangles(
        vertices, vertex_count, indices, index_count, texture_handle
    );
}

int TitanGePlatform_UploadTextureRgba32(
    TitanGePlatform *platform,
    const unsigned char *pixels,
    unsigned int width,
    unsigned int height,
    unsigned int *texture_handle
)
{
    unsigned short *converted;
    unsigned int pixel_count;
    unsigned int index;
    int result;

    if (platform == 0 || platform->api == 0 || pixels == 0 ||
        texture_handle == 0 || width == 0u || height == 0u ||
        width > 512u || height > 320u ||
        width > 0xffffffffu / height)
        return 0;

    /* P10.59 direct texture bridge.
     *
     * The old Titan bridge scanned alpha, allocated a temporary PSM16 buffer
     * from the 16 MiB game heap, converted every pixel, called the PSM16 API,
     * then the kernel copied that temporary buffer into its own GS staging
     * buffer.  P153 observed hundreds of texture imports in slow frames.
     *
     * The RGBA32 ABI is already negotiated by feature flag. Pass the decoded
     * Fast3D pixels directly to the kernel; TitanVideo now chooses PSM16 for
     * binary-alpha textures, preserving the old visual/storage semantics
     * without a per-texture heap allocation or duplicate copy. */
    if ((platform->api->feature_flags &
            TITAN_GAME_FEATURE_TEXTURE_UPLOAD_RGBA32) != 0u &&
        platform->api->struct_size >= TITAN_GE_RGBA32_API_BYTES &&
        platform->api->video_upload_texture_rgba32 != 0)
    {
        return platform->api->video_upload_texture_rgba32(
            pixels,
            width,
            height,
            width * 4u,
            texture_handle
        );
    }

    /* Compatibility fallback for an older Titan Game API implementation.
     * The legacy PSM16 bridge has only the original 128x128 scratch. */
    if (width > 128u || height > 128u)
        return 0;
    pixel_count = width * height;
    converted = (unsigned short *)platform->api->alloc(
        pixel_count * (unsigned int)sizeof(*converted),
        64u
    );
    if (converted == 0)
        return 0;

    for (index = 0u; index < pixel_count; index++)
    {
        const unsigned char *source = &pixels[index * 4u];

        converted[index] = (unsigned short)(
            ((unsigned int)source[0] >> 3u) |
            (((unsigned int)source[1] >> 3u) << 5u) |
            (((unsigned int)source[2] >> 3u) << 10u) |
            (source[3] >= 128u ? 0x8000u : 0u)
        );
    }

    result = platform->api->video_upload_texture_psm16(
        converted,
        width,
        height,
        width,
        texture_handle
    );
    platform->api->release(converted);
    return result;
}

int TitanGePlatform_UploadTextureI8(
    TitanGePlatform *platform,
    const unsigned char *pixels,
    unsigned int width,
    unsigned int height,
    unsigned int stride,
    unsigned int *texture_handle
)
{
    if (platform == 0 || platform->api == 0 || pixels == 0 ||
        texture_handle == 0 || width == 0u || height == 0u ||
        stride < width || width > 512u || height > 320u ||
        (platform->api->feature_flags & TITAN_GAME_FEATURE_TEXTURE_UPLOAD_I8) == 0u ||
        platform->api->struct_size < TITAN_GE_I8_API_BYTES ||
        platform->api->video_upload_texture_i8 == 0)
        return 0;
    return platform->api->video_upload_texture_i8(
        pixels, width, height, stride, texture_handle);
}

int TitanGePlatform_ReleaseTexture(
    TitanGePlatform *platform,
    unsigned int texture_handle
)
{
    if (platform == 0 || platform->api == 0 ||
        platform->api->video_release_texture == 0)
        return 0;
    return platform->api->video_release_texture(texture_handle);
}

int TitanGePlatform_SetRenderState(
    TitanGePlatform *platform,
    const TitanGameRenderState *state
)
{
    if (platform == 0 || platform->api == 0 || state == 0 ||
        (platform->api->feature_flags & TITAN_GAME_FEATURE_RENDER_STATE) == 0u ||
        platform->api->video_set_render_state == 0)
        return 0;
    return platform->api->video_set_render_state(state);
}

int TitanGePlatform_DrawMaterialTriangles(
    TitanGePlatform *platform,
    const TitanGameMaterialVertex *vertices,
    unsigned int vertex_count,
    unsigned int texture0_handle,
    unsigned int texture1_handle
)
{
    if (platform == 0 || platform->api == 0 || vertices == 0 ||
        (platform->api->feature_flags &
            TITAN_GAME_FEATURE_TWO_TEXTURE_MATERIAL) == 0u ||
        platform->api->video_draw_material_triangles == 0)
        return -1;
    return platform->api->video_draw_material_triangles(
        vertices, vertex_count, texture0_handle, texture1_handle
    );
}

int TitanGePlatform_EndFrame(TitanGePlatform *platform)
{
    return platform != 0 && platform->api != 0
        ? platform->api->video_end_frame()
        : 0;
}

int TitanGePlatform_BeginAudio(TitanGePlatform *platform)
{
    return platform != 0 && platform->api != 0 ? platform->api->audio_begin() : 0;
}

unsigned int TitanGePlatform_SubmitPcm(
    TitanGePlatform *platform, const short *samples, unsigned int frames
)
{
    return platform != 0 && platform->api != 0
        ? platform->api->audio_submit_pcm(samples, frames)
        : 0u;
}

void TitanGePlatform_EndAudio(TitanGePlatform *platform)
{
    if (platform != 0 && platform->api != 0) platform->api->audio_end();
}
