#include "titan_avatars.h"

#include "titan_video.h"
#include "generated/titan_ui_avatars_data.h"

#include <string.h>

static TitanAssetState g_avatar_atlas_state;
static TitanResult g_avatar_atlas_result = TITAN_RESULT_OK;

TitanResult TitanAvatars_Initialize(void)
{
    TitanResult result;

    if (g_avatar_atlas_state == TITAN_ASSET_READY)
    {
        return TITAN_RESULT_OK;
    }

    g_avatar_atlas_state = TITAN_ASSET_LOADING;

    if (
        size_titan_ui_avatars !=
        TITAN_VIDEO_AVATAR_ATLAS_WIDTH *
        TITAN_VIDEO_AVATAR_ATLAS_HEIGHT *
        sizeof(unsigned short)
    )
    {
        g_avatar_atlas_state = TITAN_ASSET_FAILED;
        g_avatar_atlas_result = TITAN_ERROR_INVALID_ARGUMENT;
        return g_avatar_atlas_result;
    }

    result = TitanVideo_UploadAvatarAtlas16(
            (const unsigned short *)titan_ui_avatars,
            TITAN_VIDEO_AVATAR_ATLAS_WIDTH,
            TITAN_VIDEO_AVATAR_ATLAS_HEIGHT,
            TITAN_VIDEO_AVATAR_ATLAS_WIDTH
        );

    g_avatar_atlas_result = result;
    g_avatar_atlas_state = result == TITAN_RESULT_OK
        ? TITAN_ASSET_READY
        : TITAN_ASSET_FAILED;
    return result;
}

TitanResult TitanAvatars_Retry(void)
{
    if (g_avatar_atlas_state == TITAN_ASSET_READY)
    {
        return TITAN_RESULT_OK;
    }

    g_avatar_atlas_state = TITAN_ASSET_UNLOADED;
    return TitanAvatars_Initialize();
}

int TitanAvatars_IsReady(void)
{
    return g_avatar_atlas_state == TITAN_ASSET_READY;
}

TitanAssetState TitanAvatars_GetState(void)
{
    return g_avatar_atlas_state;
}

TitanResult TitanAvatars_GetLastResult(void)
{
    return g_avatar_atlas_result;
}

int TitanAvatars_CopyBaseAtlas(
    unsigned short *destination,
    unsigned int pixel_capacity
)
{
    const unsigned int pixel_count =
        TITAN_VIDEO_AVATAR_ATLAS_WIDTH * TITAN_VIDEO_AVATAR_ATLAS_HEIGHT;
    const unsigned int expected_bytes =
        pixel_count * sizeof(unsigned short);

    if (
        destination == 0 ||
        pixel_capacity < pixel_count ||
        size_titan_ui_avatars != expected_bytes
    )
    {
        return 0;
    }

    memcpy(destination, titan_ui_avatars, expected_bytes);
    return 1;
}

void TitanAvatars_Draw(
    unsigned int avatar_id,
    float x,
    float y,
    float size,
    unsigned char alpha
)
{
    unsigned int source_x;
    unsigned int source_y;

    if (
        g_avatar_atlas_state != TITAN_ASSET_READY ||
        avatar_id >= TITAN_AVATAR_COUNT ||
        size <= 0.0f ||
        alpha == 0u
    )
    {
        return;
    }

    source_x =
        (avatar_id % TITAN_AVATAR_ATLAS_COLUMNS) *
        TITAN_AVATAR_CELL_SIZE;
    source_y =
        (avatar_id / TITAN_AVATAR_ATLAS_COLUMNS) *
        TITAN_AVATAR_CELL_SIZE;

    TitanVideo_DrawAvatarAtlasRegion16(
        x,
        y,
        size,
        size,
        source_x,
        source_y,
        TITAN_AVATAR_CELL_SIZE,
        TITAN_AVATAR_CELL_SIZE,
        alpha
    );
}
