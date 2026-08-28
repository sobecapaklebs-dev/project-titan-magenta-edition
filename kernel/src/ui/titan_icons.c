#include "titan_icons.h"

#include "titan_video.h"
#include "titan_ui_assets.h"
#include "../services/titan_theme.h"

#define TITAN_ICON_LOGICAL_BASE_SIZE 60.0f

typedef enum TitanAtlasIcon
{
    TITAN_ATLAS_ICON_OVERVIEW = 0,
    TITAN_ATLAS_ICON_APPLICATIONS,
    TITAN_ATLAS_ICON_CONTROLLER,
    TITAN_ATLAS_ICON_USER,
    TITAN_ATLAS_ICON_SAVED_DATA,
    TITAN_ATLAS_ICON_PROFILE_SETTINGS,
    TITAN_ATLAS_ICON_SETTINGS,
    TITAN_ATLAS_ICON_RUNE,
    TITAN_ATLAS_ICON_NOTIFICATIONS,
    TITAN_ATLAS_ICON_POWER,
    TITAN_ATLAS_ICON_VMC_UTILITY,
    TITAN_ATLAS_ICON_DEBUG_SETTINGS
} TitanAtlasIcon;

static void TitanIcons_DrawAtlasIcon(
    unsigned int atlas_icon,
    float center_x,
    float center_y,
    float scale,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    float logical_size;

    if (
        scale <= 0.0f ||
        alpha == 0u ||
        atlas_icon > TITAN_ATLAS_ICON_DEBUG_SETTINGS
    )
    {
        return;
    }

    logical_size = TITAN_ICON_LOGICAL_BASE_SIZE * scale;
    TitanUiAssets_DrawSystemIcon(
        atlas_icon,
        center_x,
        center_y,
        logical_size,
        logical_size,
        red,
        green,
        blue,
        alpha
    );
}

static void TitanIcons_DrawOutlineRect(
    float x,
    float y,
    float width,
    float height,
    float thickness,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    TitanVideo_DrawLineAlpha(
        x,
        y,
        x + width,
        y,
        thickness,
        red,
        green,
        blue,
        alpha
    );

    TitanVideo_DrawLineAlpha(
        x + width,
        y,
        x + width,
        y + height,
        thickness,
        red,
        green,
        blue,
        alpha
    );

    TitanVideo_DrawLineAlpha(
        x + width,
        y + height,
        x,
        y + height,
        thickness,
        red,
        green,
        blue,
        alpha
    );

    TitanVideo_DrawLineAlpha(
        x,
        y + height,
        x,
        y,
        thickness,
        red,
        green,
        blue,
        alpha
    );
}

static void TitanIcons_DrawStorage(
    float center_x,
    float center_y,
    float scale,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    int row;
    float y;
    float thickness;

    thickness = 2.0f * scale;

    for (row = 0; row < 3; row++)
    {
        y = center_y + ((float)(row - 1) * 12.5f * scale);

        TitanIcons_DrawOutlineRect(
            center_x - (19.0f * scale),
            y - (4.6f * scale),
            38.0f * scale,
            9.2f * scale,
            thickness,
            red,
            green,
            blue,
            alpha
        );

        TitanVideo_DrawLineAlpha(
            center_x - (15.0f * scale),
            y,
            center_x - (2.0f * scale),
            y,
            thickness,
            red,
            green,
            blue,
            alpha
        );

        TitanVideo_DrawCircleAlpha(
            center_x + (12.0f * scale),
            y,
            1.8f * scale,
            red,
            green,
            blue,
            alpha
        );
    }
}

static void TitanIcons_DrawVideo(
    float center_x,
    float center_y,
    float scale,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    float thickness;

    thickness = 2.0f * scale;

    TitanIcons_DrawOutlineRect(
        center_x - (20.0f * scale),
        center_y - (14.0f * scale),
        40.0f * scale,
        27.0f * scale,
        thickness,
        red,
        green,
        blue,
        alpha
    );

    TitanVideo_DrawLineAlpha(
        center_x,
        center_y + (13.0f * scale),
        center_x,
        center_y + (20.0f * scale),
        thickness,
        red,
        green,
        blue,
        alpha
    );

    TitanVideo_DrawLineAlpha(
        center_x - (10.0f * scale),
        center_y + (20.0f * scale),
        center_x + (10.0f * scale),
        center_y + (20.0f * scale),
        thickness,
        red,
        green,
        blue,
        alpha
    );
}

static void TitanIcons_DrawSystem(
    float center_x,
    float center_y,
    float scale,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    float thickness;

    thickness = 2.0f * scale;

    TitanVideo_DrawCircleAlpha(
        center_x,
        center_y,
        9.0f * scale,
        red,
        green,
        blue,
        alpha
    );

    TitanVideo_DrawLineAlpha(
        center_x,
        center_y - (22.0f * scale),
        center_x,
        center_y - (10.0f * scale),
        thickness,
        red,
        green,
        blue,
        alpha
    );

    TitanVideo_DrawLineAlpha(
        center_x,
        center_y + (10.0f * scale),
        center_x,
        center_y + (22.0f * scale),
        thickness,
        red,
        green,
        blue,
        alpha
    );

    TitanVideo_DrawLineAlpha(
        center_x - (22.0f * scale),
        center_y,
        center_x - (10.0f * scale),
        center_y,
        thickness,
        red,
        green,
        blue,
        alpha
    );

    TitanVideo_DrawLineAlpha(
        center_x + (10.0f * scale),
        center_y,
        center_x + (22.0f * scale),
        center_y,
        thickness,
        red,
        green,
        blue,
        alpha
    );
}


static void TitanIcons_DrawLibrary(
    float center_x,
    float center_y,
    float scale,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    /* Orbis Library glyph from the user's reference video: a two-level media
     * shelf/rack.  Keep it vector-based so theme tinting and GS scaling stay
     * crisp without replacing a curated UI asset. */
    const float w = 38.0f * scale;
    const float h = 32.0f * scale;
    const float x = center_x - (w * 0.5f);
    const float y = center_y - (h * 0.5f);
    const float th = 2.4f * scale;
    const float shelf_y = y + (16.0f * scale);
    unsigned int i;

    TitanIcons_DrawOutlineRect(x, y, w, h, th, red, green, blue, alpha);
    TitanVideo_DrawLineAlpha(
        x + (2.0f * scale), shelf_y,
        x + w - (2.0f * scale), shelf_y,
        th, red, green, blue, alpha
    );

    /* Five narrow media spines on each shelf, matching the PS4 Library
     * silhouette rather than the old four-square Applications glyph. */
    for (i = 0u; i < 5u; i++)
    {
        float bx = x + (7.0f + ((float)i * 6.0f)) * scale;
        TitanVideo_DrawLineAlpha(
            bx, y + (4.0f * scale),
            bx, shelf_y - (3.0f * scale),
            2.1f * scale, red, green, blue, alpha
        );
        TitanVideo_DrawLineAlpha(
            bx, shelf_y + (3.0f * scale),
            bx, y + h - (3.0f * scale),
            2.1f * scale, red, green, blue, alpha
        );
    }
}

static void TitanIcons_DrawAbout(
    float center_x,
    float center_y,
    float scale,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    float size;

    size = 30.0f * scale;

    TitanVideo_DrawRoundedRectAlpha(
        center_x - (size * 0.5f),
        center_y - (size * 0.5f),
        size,
        size,
        5.0f * scale,
        red,
        green,
        blue,
        alpha
    );

    TitanVideo_DrawRectAlpha(
        center_x - (2.0f * scale),
        center_y - (2.0f * scale),
        4.0f * scale,
        12.0f * scale,
        74,
        76,
        86,
        alpha
    );

    TitanVideo_DrawCircleAlpha(
        center_x,
        center_y - (9.0f * scale),
        2.0f * scale,
        74,
        76,
        86,
        alpha
    );
}

void TitanIcons_Draw(
    TitanIconId icon,
    float center_x,
    float center_y,
    float scale,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    const TitanThemeIconStyle *theme_icons;

    theme_icons = TitanTheme_GetIconStyle();
    if (theme_icons != 0 && theme_icons->tint_enabled)
    {
        red = (unsigned char)(((unsigned int)red * theme_icons->tint.red) / 255u);
        green = (unsigned char)(((unsigned int)green * theme_icons->tint.green) / 255u);
        blue = (unsigned char)(((unsigned int)blue * theme_icons->tint.blue) / 255u);
    }

    switch (icon)
    {
        case TITAN_ICON_OVERVIEW:
            TitanIcons_DrawAtlasIcon(
                TITAN_ATLAS_ICON_OVERVIEW,
                center_x,
                center_y,
                scale,
                red,
                green,
                blue,
                alpha
            );
            break;

        case TITAN_ICON_APPLICATIONS:
            TitanIcons_DrawAtlasIcon(
                TITAN_ATLAS_ICON_APPLICATIONS,
                center_x,
                center_y,
                scale,
                red,
                green,
                blue,
                alpha
            );
            break;

        case TITAN_ICON_INPUT:
            TitanIcons_DrawAtlasIcon(
                TITAN_ATLAS_ICON_CONTROLLER,
                center_x,
                center_y,
                scale,
                red,
                green,
                blue,
                alpha
            );
            break;

        case TITAN_ICON_USER:
            TitanIcons_DrawAtlasIcon(
                TITAN_ATLAS_ICON_USER,
                center_x,
                center_y,
                scale,
                red,
                green,
                blue,
                alpha
            );
            break;

        case TITAN_ICON_SAVED_DATA:
            TitanIcons_DrawAtlasIcon(
                TITAN_ATLAS_ICON_SAVED_DATA,
                center_x,
                center_y,
                scale,
                red,
                green,
                blue,
                alpha
            );
            break;

        case TITAN_ICON_PROFILE_SETTINGS:
            TitanIcons_DrawAtlasIcon(
                TITAN_ATLAS_ICON_PROFILE_SETTINGS,
                center_x,
                center_y,
                scale,
                red,
                green,
                blue,
                alpha
            );
            break;

        case TITAN_ICON_STORAGE:
            TitanIcons_DrawStorage(
                center_x,
                center_y,
                scale,
                red,
                green,
                blue,
                alpha
            );
            break;

        case TITAN_ICON_SETTINGS:
            TitanIcons_DrawAtlasIcon(
                TITAN_ATLAS_ICON_SETTINGS,
                center_x,
                center_y,
                scale,
                red,
                green,
                blue,
                alpha
            );
            break;

        case TITAN_ICON_VIDEO:
            TitanIcons_DrawVideo(
                center_x,
                center_y,
                scale,
                red,
                green,
                blue,
                alpha
            );
            break;

        case TITAN_ICON_SYSTEM:
            TitanIcons_DrawSystem(
                center_x,
                center_y,
                scale,
                red,
                green,
                blue,
                alpha
            );
            break;

        case TITAN_ICON_ABOUT:
            TitanIcons_DrawAbout(
                center_x,
                center_y,
                scale,
                red,
                green,
                blue,
                alpha
            );
            break;

        case TITAN_ICON_RUNE:
            TitanIcons_DrawAtlasIcon(
                TITAN_ATLAS_ICON_RUNE,
                center_x,
                center_y,
                scale,
                red,
                green,
                blue,
                alpha
            );
            break;

        case TITAN_ICON_NOTIFICATIONS:
            TitanIcons_DrawAtlasIcon(
                TITAN_ATLAS_ICON_NOTIFICATIONS,
                center_x,
                center_y,
                scale,
                red,
                green,
                blue,
                alpha
            );
            break;

        case TITAN_ICON_POWER:
            TitanIcons_DrawAtlasIcon(
                TITAN_ATLAS_ICON_POWER,
                center_x,
                center_y,
                scale,
                red,
                green,
                blue,
                alpha
            );
            break;

        case TITAN_ICON_VMC_UTILITY:
            TitanIcons_DrawAtlasIcon(
                TITAN_ATLAS_ICON_VMC_UTILITY,
                center_x,
                center_y,
                scale,
                red,
                green,
                blue,
                alpha
            );
            break;

        case TITAN_ICON_DEBUG_SETTINGS:
            TitanIcons_DrawAtlasIcon(
                TITAN_ATLAS_ICON_DEBUG_SETTINGS,
                center_x,
                center_y,
                scale,
                red,
                green,
                blue,
                alpha
            );
            break;

        case TITAN_ICON_APP_HOME:
            TitanUiAssets_DrawAppHome(
                center_x, center_y,
                TITAN_ICON_LOGICAL_BASE_SIZE * scale,
                TITAN_ICON_LOGICAL_BASE_SIZE * scale,
                red, green, blue, alpha
            );
            break;

        case TITAN_ICON_LIBRARY:
            TitanIcons_DrawLibrary(
                center_x, center_y, scale, red, green, blue, alpha
            );
            break;

        case TITAN_ICON_FTP_HOME:
            TitanUiAssets_DrawFtpHome(
                center_x, center_y,
                TITAN_ICON_LOGICAL_BASE_SIZE * scale,
                TITAN_ICON_LOGICAL_BASE_SIZE * scale,
                red, green, blue, alpha
            );
            break;

        default:
            TitanIcons_DrawAtlasIcon(
                TITAN_ATLAS_ICON_OVERVIEW,
                center_x,
                center_y,
                scale,
                red,
                green,
                blue,
                alpha
            );
            break;
    }

    if (theme_icons != 0 && theme_icons->decorator != TITAN_THEME_ICON_DECORATOR_NONE && alpha > 0u)
    {
        const TitanThemeColor *c = &theme_icons->decorator_color;
        unsigned char da = (unsigned char)(((unsigned int)alpha * c->alpha) / 255u);
        float radius = 22.0f * scale;
        if (theme_icons->decorator == TITAN_THEME_ICON_DECORATOR_RING)
        {
            /* Outline-only octagonal ring.  Do not fake a ring by drawing a
             * dark filled circle over a bright one: that would obscure
             * bitmap-backed icons and assumes the scene behind them is black. */
            const float d = radius * 0.70710678f;
            const float t = 1.5f * scale;
            TitanVideo_DrawLineAlpha(center_x, center_y-radius, center_x+d, center_y-d, t, c->red,c->green,c->blue,da);
            TitanVideo_DrawLineAlpha(center_x+d, center_y-d, center_x+radius, center_y, t, c->red,c->green,c->blue,da);
            TitanVideo_DrawLineAlpha(center_x+radius, center_y, center_x+d, center_y+d, t, c->red,c->green,c->blue,da);
            TitanVideo_DrawLineAlpha(center_x+d, center_y+d, center_x, center_y+radius, t, c->red,c->green,c->blue,da);
            TitanVideo_DrawLineAlpha(center_x, center_y+radius, center_x-d, center_y+d, t, c->red,c->green,c->blue,da);
            TitanVideo_DrawLineAlpha(center_x-d, center_y+d, center_x-radius, center_y, t, c->red,c->green,c->blue,da);
            TitanVideo_DrawLineAlpha(center_x-radius, center_y, center_x-d, center_y-d, t, c->red,c->green,c->blue,da);
            TitanVideo_DrawLineAlpha(center_x-d, center_y-d, center_x, center_y-radius, t, c->red,c->green,c->blue,da);
        }
        else if (theme_icons->decorator == TITAN_THEME_ICON_DECORATOR_CORNERS)
        {
            float e = 7.0f * scale;
            float r = 20.0f * scale;
            TitanVideo_DrawLineAlpha(center_x-r, center_y-r, center_x-r+e, center_y-r, 1.5f*scale, c->red,c->green,c->blue,da);
            TitanVideo_DrawLineAlpha(center_x-r, center_y-r, center_x-r, center_y-r+e, 1.5f*scale, c->red,c->green,c->blue,da);
            TitanVideo_DrawLineAlpha(center_x+r-e, center_y+r, center_x+r, center_y+r, 1.5f*scale, c->red,c->green,c->blue,da);
            TitanVideo_DrawLineAlpha(center_x+r, center_y+r-e, center_x+r, center_y+r, 1.5f*scale, c->red,c->green,c->blue,da);
        }
    }
}
