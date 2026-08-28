#include "titan_game_sdk.h"
#include "titan_ge_platform.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned int g_calls;
static uint32_t g_w, g_h, g_stride;

static int upload_i8(const uint8_t *pixels, uint32_t width, uint32_t height,
                     uint32_t stride, uint32_t *handle)
{
    assert(pixels != 0 && handle != 0);
    g_calls++;
    g_w = width; g_h = height; g_stride = stride;
    *handle = 0x1624u;
    return 1;
}

int main(void)
{
    TitanGameApiV1 api;
    TitanGePlatform platform;
    uint8_t pixel = 0x7fu;
    uint32_t handle = 0u;

    memset(&api, 0, sizeof(api));
    memset(&platform, 0, sizeof(platform));
    api.abi_version = TITAN_GAME_API_VERSION;
    api.struct_size = sizeof(api);
    api.feature_flags = TITAN_GAME_FEATURE_TEXTURE_UPLOAD_I8;
    api.video_upload_texture_i8 = upload_i8;
    assert(TitanGePlatform_Attach(&platform, &api));
    assert(TitanGePlatform_UploadTextureI8(
        &platform, &pixel, 440u, 299u, 440u, &handle));
    assert(handle == 0x1624u && g_calls == 1u);
    assert(g_w == 440u && g_h == 299u && g_stride == 440u);

    /* Oversize and malformed stride must fail before callback. */
    assert(!TitanGePlatform_UploadTextureI8(
        &platform, &pixel, 513u, 1u, 513u, &handle));
    assert(!TitanGePlatform_UploadTextureI8(
        &platform, &pixel, 440u, 321u, 440u, &handle));
    assert(!TitanGePlatform_UploadTextureI8(
        &platform, &pixel, 440u, 299u, 439u, &handle));
    assert(g_calls == 1u);

    api.feature_flags = 0u;
    assert(!TitanGePlatform_UploadTextureI8(
        &platform, &pixel, 440u, 299u, 440u, &handle));
    api.feature_flags = TITAN_GAME_FEATURE_TEXTURE_UPLOAD_I8;

    /* Append-only ABI: a v2 struct that ends before the I8 tail must not read it. */
    api.struct_size = (uint32_t)offsetof(TitanGameApiV1, video_upload_texture_i8);
    assert(!TitanGePlatform_UploadTextureI8(
        &platform, &pixel, 440u, 299u, 440u, &handle));
    assert(g_calls == 1u);

    puts("P1624_I8_BRIDGE=PASS");
    return 0;
}
