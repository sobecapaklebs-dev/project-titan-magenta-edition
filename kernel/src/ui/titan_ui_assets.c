#include "titan_ui_assets.h"

#include "titan_video.h"
#include "generated/titan_ui_alert_data.h"
#include "generated/titan_ui_boot_ps2logo_data.h"
#include "generated/titan_ui_game_placeholder_data.h"
#include "generated/titan_ui_icons_data.h"
#include "generated/titan_ui_app_home_data.h"
#include "generated/titan_ui_ftp_home_data.h"

#include <string.h>

#define TITAN_UI_GAME_SIZE 128u
#define TITAN_UI_ALERT_SIZE 128u
#define TITAN_UI_BOOT_WIDTH 256u
#define TITAN_UI_BOOT_HEIGHT 96u
#define TITAN_UI_APP_HOME_SIZE 128u
#define TITAN_UI_FTP_HOME_SIZE 128u
#define TITAN_UI_SYSTEM_ICON_SOURCE_SIZE 128u
#define TITAN_UI_SYSTEM_ICON_SOURCE_COLUMNS 5u
#define TITAN_UI_SYSTEM_ICON_SOURCE_WIDTH 640u
#define TITAN_UI_SYSTEM_ICON_SOURCE_HEIGHT 384u
#define TITAN_UI_SYSTEM_ICON_COUNT 12u
#define TITAN_UI_SYSTEM_ICON_SIZE 42u
#define TITAN_UI_SYSTEM_ICON_COLUMNS 6u

#define TITAN_UI_GAME_X 0u
#define TITAN_UI_GAME_Y 0u
#define TITAN_UI_ALERT_X 128u
#define TITAN_UI_ALERT_Y 0u
#define TITAN_UI_BOOT_X 256u
#define TITAN_UI_BOOT_Y 0u
#define TITAN_UI_APP_HOME_X 0u
#define TITAN_UI_APP_HOME_Y 128u
#define TITAN_UI_FTP_HOME_X 128u
#define TITAN_UI_FTP_HOME_Y 128u
#define TITAN_UI_SYSTEM_ICONS_X 256u
#define TITAN_UI_SYSTEM_ICONS_Y 128u

static unsigned int g_ui_atlas_pixels[
    TITAN_VIDEO_UI_ATLAS_WIDTH * TITAN_VIDEO_UI_ATLAS_HEIGHT
] __attribute__((aligned(64)));
static int g_ui_assets_ready;

static void TitanUiAssets_CopyA8(
    const unsigned char *source,
    unsigned int source_size,
    unsigned int source_width,
    unsigned int source_height,
    unsigned int destination_x,
    unsigned int destination_y
)
{
    unsigned int x;
    unsigned int y;

    if (
        source == 0 ||
        source_size != source_width * source_height ||
        destination_x + source_width > TITAN_VIDEO_UI_ATLAS_WIDTH ||
        destination_y + source_height > TITAN_VIDEO_UI_ATLAS_HEIGHT
    )
    {
        return;
    }

    for (y = 0u; y < source_height; y++)
    {
        for (x = 0u; x < source_width; x++)
        {
            unsigned int coverage;
            unsigned int gs_alpha;

            coverage = source[(y * source_width) + x];
            gs_alpha = ((coverage * 128u) + 127u) / 255u;
            g_ui_atlas_pixels[
                ((destination_y + y) * TITAN_VIDEO_UI_ATLAS_WIDTH) +
                destination_x + x
            ] = 0x00808080u | (gs_alpha << 24u);
        }
    }
}

static unsigned char TitanUiAssets_SampleA8Bilinear(
    const unsigned char *source,
    unsigned int source_width,
    unsigned int source_height,
    unsigned int x_fixed,
    unsigned int y_fixed
)
{
    unsigned int x0;
    unsigned int y0;
    unsigned int x1;
    unsigned int y1;
    unsigned int fx;
    unsigned int fy;
    unsigned int a0;
    unsigned int a1;

    x0 = x_fixed >> 8;
    y0 = y_fixed >> 8;
    fx = x_fixed & 255u;
    fy = y_fixed & 255u;

    if (x0 >= source_width)
    {
        x0 = source_width - 1u;
        fx = 0u;
    }
    if (y0 >= source_height)
    {
        y0 = source_height - 1u;
        fy = 0u;
    }

    x1 = x0 + 1u < source_width ? x0 + 1u : x0;
    y1 = y0 + 1u < source_height ? y0 + 1u : y0;

    a0 =
        ((unsigned int)source[y0 * source_width + x0] * (256u - fx) +
         (unsigned int)source[y0 * source_width + x1] * fx) >> 8;
    a1 =
        ((unsigned int)source[y1 * source_width + x0] * (256u - fx) +
         (unsigned int)source[y1 * source_width + x1] * fx) >> 8;

    return (unsigned char)((a0 * (256u - fy) + a1 * fy) >> 8);
}

static void TitanUiAssets_CopySystemIcon(
    unsigned int icon_index
)
{
    unsigned int source_origin_x;
    unsigned int source_origin_y;
    unsigned int destination_origin_x;
    unsigned int destination_origin_y;
    unsigned int x;
    unsigned int y;

    source_origin_x =
        (icon_index % TITAN_UI_SYSTEM_ICON_SOURCE_COLUMNS) *
        TITAN_UI_SYSTEM_ICON_SOURCE_SIZE;
    source_origin_y =
        (icon_index / TITAN_UI_SYSTEM_ICON_SOURCE_COLUMNS) *
        TITAN_UI_SYSTEM_ICON_SOURCE_SIZE;
    destination_origin_x = TITAN_UI_SYSTEM_ICONS_X +
        (icon_index % TITAN_UI_SYSTEM_ICON_COLUMNS) * TITAN_UI_SYSTEM_ICON_SIZE;
    destination_origin_y = TITAN_UI_SYSTEM_ICONS_Y +
        (icon_index / TITAN_UI_SYSTEM_ICON_COLUMNS) * TITAN_UI_SYSTEM_ICON_SIZE;

    for (y = 0u; y < TITAN_UI_SYSTEM_ICON_SIZE; y++)
    {
        unsigned int source_y_fixed;

        source_y_fixed = TITAN_UI_SYSTEM_ICON_SIZE > 1u
            ? (y * ((TITAN_UI_SYSTEM_ICON_SOURCE_SIZE - 1u) << 8)) /
                (TITAN_UI_SYSTEM_ICON_SIZE - 1u)
            : 0u;

        for (x = 0u; x < TITAN_UI_SYSTEM_ICON_SIZE; x++)
        {
            unsigned int source_x_fixed;
            unsigned int coverage;
            unsigned int gs_alpha;
            unsigned int source_index;

            source_x_fixed = TITAN_UI_SYSTEM_ICON_SIZE > 1u
                ? (x * ((TITAN_UI_SYSTEM_ICON_SOURCE_SIZE - 1u) << 8)) /
                    (TITAN_UI_SYSTEM_ICON_SIZE - 1u)
                : 0u;
            source_index =
                source_origin_y * TITAN_UI_SYSTEM_ICON_SOURCE_WIDTH +
                source_origin_x;
            coverage = TitanUiAssets_SampleA8Bilinear(
                &titan_ui_icons[source_index],
                TITAN_UI_SYSTEM_ICON_SOURCE_WIDTH,
                TITAN_UI_SYSTEM_ICON_SOURCE_HEIGHT - source_origin_y,
                source_x_fixed,
                source_y_fixed
            );
            gs_alpha = ((coverage * 128u) + 127u) / 255u;
            g_ui_atlas_pixels[
                ((destination_origin_y + y) * TITAN_VIDEO_UI_ATLAS_WIDTH) +
                destination_origin_x + x
            ] = 0x00808080u | (gs_alpha << 24u);
        }
    }
}

TitanResult TitanUiAssets_Initialize(void)
{
    memset(g_ui_atlas_pixels, 0, sizeof(g_ui_atlas_pixels));
    g_ui_assets_ready = 0;

    if (
        size_titan_ui_game_placeholder != TITAN_UI_GAME_SIZE * TITAN_UI_GAME_SIZE ||
        size_titan_ui_alert != TITAN_UI_ALERT_SIZE * TITAN_UI_ALERT_SIZE ||
        size_titan_ui_boot_ps2logo != TITAN_UI_BOOT_WIDTH * TITAN_UI_BOOT_HEIGHT ||
        size_titan_ui_app_home != TITAN_UI_APP_HOME_SIZE * TITAN_UI_APP_HOME_SIZE ||
        size_titan_ui_ftp_home != TITAN_UI_FTP_HOME_SIZE * TITAN_UI_FTP_HOME_SIZE ||
        size_titan_ui_icons !=
            TITAN_UI_SYSTEM_ICON_SOURCE_WIDTH * TITAN_UI_SYSTEM_ICON_SOURCE_HEIGHT
    )
    {
        return TITAN_ERROR_INVALID_ARGUMENT;
    }

    TitanUiAssets_CopyA8(
        titan_ui_game_placeholder,
        size_titan_ui_game_placeholder,
        TITAN_UI_GAME_SIZE,
        TITAN_UI_GAME_SIZE,
        TITAN_UI_GAME_X,
        TITAN_UI_GAME_Y
    );
    TitanUiAssets_CopyA8(
        titan_ui_alert,
        size_titan_ui_alert,
        TITAN_UI_ALERT_SIZE,
        TITAN_UI_ALERT_SIZE,
        TITAN_UI_ALERT_X,
        TITAN_UI_ALERT_Y
    );
    TitanUiAssets_CopyA8(
        titan_ui_boot_ps2logo,
        size_titan_ui_boot_ps2logo,
        TITAN_UI_BOOT_WIDTH,
        TITAN_UI_BOOT_HEIGHT,
        TITAN_UI_BOOT_X,
        TITAN_UI_BOOT_Y
    );
    TitanUiAssets_CopyA8(
        titan_ui_app_home,
        size_titan_ui_app_home,
        TITAN_UI_APP_HOME_SIZE,
        TITAN_UI_APP_HOME_SIZE,
        TITAN_UI_APP_HOME_X,
        TITAN_UI_APP_HOME_Y
    );
    TitanUiAssets_CopyA8(
        titan_ui_ftp_home,
        size_titan_ui_ftp_home,
        TITAN_UI_FTP_HOME_SIZE,
        TITAN_UI_FTP_HOME_SIZE,
        TITAN_UI_FTP_HOME_X,
        TITAN_UI_FTP_HOME_Y
    );

    {
        unsigned int icon_index;
        for (icon_index = 0u; icon_index < TITAN_UI_SYSTEM_ICON_COUNT; icon_index++)
        {
            TitanUiAssets_CopySystemIcon(icon_index);
        }
    }

    if (
        TitanVideo_UploadUiAtlas32(
            g_ui_atlas_pixels,
            TITAN_VIDEO_UI_ATLAS_WIDTH,
            TITAN_VIDEO_UI_ATLAS_HEIGHT,
            TITAN_VIDEO_UI_ATLAS_WIDTH
        ) != TITAN_RESULT_OK
    )
    {
        return TITAN_ERROR_VIDEO_INITIALIZATION;
    }

    g_ui_assets_ready = 1;
    return TITAN_RESULT_OK;
}

static void TitanUiAssets_Draw(
    float center_x,
    float center_y,
    float width,
    float height,
    unsigned int source_x,
    unsigned int source_y,
    unsigned int source_width,
    unsigned int source_height,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    if (!g_ui_assets_ready)
    {
        return;
    }

    TitanVideo_DrawUiAtlasRegion32(
        center_x - (width * 0.5f),
        center_y - (height * 0.5f),
        width,
        height,
        source_x,
        source_y,
        source_width,
        source_height,
        red,
        green,
        blue,
        alpha
    );
}

void TitanUiAssets_DrawGamePlaceholder(
    float center_x, float center_y, float width, float height,
    unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha
)
{
    TitanUiAssets_Draw(
        center_x, center_y, width, height,
        TITAN_UI_GAME_X, TITAN_UI_GAME_Y, TITAN_UI_GAME_SIZE, TITAN_UI_GAME_SIZE,
        red, green, blue, alpha
    );
}

void TitanUiAssets_DrawAlert(
    float center_x, float center_y, float width, float height,
    unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha
)
{
    TitanUiAssets_Draw(
        center_x, center_y, width, height,
        TITAN_UI_ALERT_X, TITAN_UI_ALERT_Y, TITAN_UI_ALERT_SIZE, TITAN_UI_ALERT_SIZE,
        red, green, blue, alpha
    );
}

void TitanUiAssets_DrawBootLogo(
    float center_x, float center_y, float width, float height,
    unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha
)
{
    TitanUiAssets_Draw(
        center_x, center_y, width, height,
        TITAN_UI_BOOT_X, TITAN_UI_BOOT_Y, TITAN_UI_BOOT_WIDTH, TITAN_UI_BOOT_HEIGHT,
        red, green, blue, alpha
    );
}

void TitanUiAssets_DrawAppHome(
    float center_x, float center_y, float width, float height,
    unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha
)
{
    TitanUiAssets_Draw(
        center_x, center_y, width, height,
        TITAN_UI_APP_HOME_X, TITAN_UI_APP_HOME_Y,
        TITAN_UI_APP_HOME_SIZE, TITAN_UI_APP_HOME_SIZE,
        red, green, blue, alpha
    );
}

void TitanUiAssets_DrawFtpHome(
    float center_x, float center_y, float width, float height,
    unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha
)
{
    TitanUiAssets_Draw(
        center_x, center_y, width, height,
        TITAN_UI_FTP_HOME_X, TITAN_UI_FTP_HOME_Y,
        TITAN_UI_FTP_HOME_SIZE, TITAN_UI_FTP_HOME_SIZE,
        red, green, blue, alpha
    );
}


void TitanUiAssets_DrawSystemIcon(
    unsigned int icon_index,
    float center_x,
    float center_y,
    float width,
    float height,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    unsigned int source_x;
    unsigned int source_y;

    if (icon_index >= TITAN_UI_SYSTEM_ICON_COUNT)
    {
        return;
    }

    source_x = TITAN_UI_SYSTEM_ICONS_X +
        (icon_index % TITAN_UI_SYSTEM_ICON_COLUMNS) * TITAN_UI_SYSTEM_ICON_SIZE;
    source_y = TITAN_UI_SYSTEM_ICONS_Y +
        (icon_index / TITAN_UI_SYSTEM_ICON_COLUMNS) * TITAN_UI_SYSTEM_ICON_SIZE;

    TitanUiAssets_Draw(
        center_x,
        center_y,
        width,
        height,
        source_x,
        source_y,
        TITAN_UI_SYSTEM_ICON_SIZE,
        TITAN_UI_SYSTEM_ICON_SIZE,
        red,
        green,
        blue,
        alpha
    );
}
