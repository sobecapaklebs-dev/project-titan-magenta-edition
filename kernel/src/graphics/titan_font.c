#include "titan_font.h"

#include "generated/titan_ui_font_data.h"
#include "titan_font_metrics.h"
#include "titan_video.h"

static int g_font_initialized;
static unsigned int g_font_atlas_pixels[
    TITAN_VIDEO_FONT_ATLAS_WIDTH *
    TITAN_VIDEO_FONT_ATLAS_HEIGHT
] __attribute__((aligned(64)));

typedef enum TitanFontAccent
{
    TITAN_FONT_ACCENT_NONE = 0,
    TITAN_FONT_ACCENT_ACUTE,
    TITAN_FONT_ACCENT_GRAVE,
    TITAN_FONT_ACCENT_CIRCUMFLEX,
    TITAN_FONT_ACCENT_TILDE,
    TITAN_FONT_ACCENT_CEDILLA,
    TITAN_FONT_ACCENT_DIAERESIS
} TitanFontAccent;

TitanResult TitanFont_Initialize(void)
{
    if (
        size_titan_ui_font !=
        TITAN_FONT_EXPECTED_BYTES
    )
    {
        return TITAN_ERROR_FONT_ASSET_INVALID;
    }

    g_font_initialized = 1;

    return TITAN_RESULT_OK;
}

static unsigned int TitanFont_DecodeCodepoint(
    const char *text,
    unsigned int *text_index
)
{
    unsigned int index;
    unsigned int first;
    unsigned int second;
    unsigned int third;
    unsigned int fourth;

    if (text == 0 || text_index == 0)
    {
        return (unsigned int)'?';
    }

    index = *text_index;
    first = (unsigned char)text[index];

    if (first < 0x80u)
    {
        *text_index = index + 1u;
        return first;
    }

    second = (unsigned char)text[index + 1u];

    if (
        first >= 0xC2u && first <= 0xDFu &&
        (second & 0xC0u) == 0x80u
    )
    {
        *text_index = index + 2u;
        return ((first & 0x1Fu) << 6u) | (second & 0x3Fu);
    }

    if (second == 0u)
    {
        *text_index = index + 1u;
        return (unsigned int)'?';
    }

    third = (unsigned char)text[index + 2u];

    if (
        first >= 0xE0u && first <= 0xEFu &&
        (second & 0xC0u) == 0x80u &&
        (third & 0xC0u) == 0x80u
    )
    {
        *text_index = index + 3u;
        return
            ((first & 0x0Fu) << 12u) |
            ((second & 0x3Fu) << 6u) |
            (third & 0x3Fu);
    }

    if (third == 0u)
    {
        *text_index = index + 1u;
        return (unsigned int)'?';
    }

    fourth = (unsigned char)text[index + 3u];

    if (
        first >= 0xF0u && first <= 0xF4u &&
        (second & 0xC0u) == 0x80u &&
        (third & 0xC0u) == 0x80u &&
        (fourth & 0xC0u) == 0x80u
    )
    {
        *text_index = index + 4u;
        return
            ((first & 0x07u) << 18u) |
            ((second & 0x3Fu) << 12u) |
            ((third & 0x3Fu) << 6u) |
            (fourth & 0x3Fu);
    }

    *text_index = index + 1u;
    return (unsigned int)'?';
}

static unsigned int TitanFont_GetBaseCharacter(
    unsigned int codepoint,
    TitanFontAccent *accent
)
{
    if (accent != 0)
    {
        *accent = TITAN_FONT_ACCENT_NONE;
    }

    switch (codepoint)
    {
        case 0x00C0u: if (accent != 0) *accent = TITAN_FONT_ACCENT_GRAVE; return 'A';
        case 0x00C1u: if (accent != 0) *accent = TITAN_FONT_ACCENT_ACUTE; return 'A';
        case 0x00C2u: if (accent != 0) *accent = TITAN_FONT_ACCENT_CIRCUMFLEX; return 'A';
        case 0x00C3u: if (accent != 0) *accent = TITAN_FONT_ACCENT_TILDE; return 'A';
        case 0x00C7u: if (accent != 0) *accent = TITAN_FONT_ACCENT_CEDILLA; return 'C';
        case 0x00C9u: if (accent != 0) *accent = TITAN_FONT_ACCENT_ACUTE; return 'E';
        case 0x00CAu: if (accent != 0) *accent = TITAN_FONT_ACCENT_CIRCUMFLEX; return 'E';
        case 0x00CDu: if (accent != 0) *accent = TITAN_FONT_ACCENT_ACUTE; return 'I';
        case 0x00D3u: if (accent != 0) *accent = TITAN_FONT_ACCENT_ACUTE; return 'O';
        case 0x00D4u: if (accent != 0) *accent = TITAN_FONT_ACCENT_CIRCUMFLEX; return 'O';
        case 0x00D5u: if (accent != 0) *accent = TITAN_FONT_ACCENT_TILDE; return 'O';
        case 0x00DAu: if (accent != 0) *accent = TITAN_FONT_ACCENT_ACUTE; return 'U';
        case 0x00DCu: if (accent != 0) *accent = TITAN_FONT_ACCENT_DIAERESIS; return 'U';
        case 0x00E0u: if (accent != 0) *accent = TITAN_FONT_ACCENT_GRAVE; return 'a';
        case 0x00E1u: if (accent != 0) *accent = TITAN_FONT_ACCENT_ACUTE; return 'a';
        case 0x00E2u: if (accent != 0) *accent = TITAN_FONT_ACCENT_CIRCUMFLEX; return 'a';
        case 0x00E3u: if (accent != 0) *accent = TITAN_FONT_ACCENT_TILDE; return 'a';
        case 0x00E7u: if (accent != 0) *accent = TITAN_FONT_ACCENT_CEDILLA; return 'c';
        case 0x00E9u: if (accent != 0) *accent = TITAN_FONT_ACCENT_ACUTE; return 'e';
        case 0x00EAu: if (accent != 0) *accent = TITAN_FONT_ACCENT_CIRCUMFLEX; return 'e';
        case 0x00EDu: if (accent != 0) *accent = TITAN_FONT_ACCENT_ACUTE; return 'i';
        case 0x00F3u: if (accent != 0) *accent = TITAN_FONT_ACCENT_ACUTE; return 'o';
        case 0x00F4u: if (accent != 0) *accent = TITAN_FONT_ACCENT_CIRCUMFLEX; return 'o';
        case 0x00F5u: if (accent != 0) *accent = TITAN_FONT_ACCENT_TILDE; return 'o';
        case 0x00FAu: if (accent != 0) *accent = TITAN_FONT_ACCENT_ACUTE; return 'u';
        case 0x00FCu: if (accent != 0) *accent = TITAN_FONT_ACCENT_DIAERESIS; return 'u';
        default: return codepoint;
    }
}

static unsigned int TitanFont_GetGlyphIndex(
    unsigned int codepoint,
    TitanFontAccent *accent
)
{
    unsigned int character;

    if (codepoint == TITAN_FONT_STAR_CODEPOINT)
    {
        if (accent != 0)
        {
            *accent = TITAN_FONT_ACCENT_NONE;
        }
        return TITAN_FONT_STAR_GLYPH_INDEX;
    }

    character = TitanFont_GetBaseCharacter(codepoint, accent);

    if (
        character < TITAN_FONT_FIRST_CHARACTER ||
        character > TITAN_FONT_LAST_CHARACTER
    )
    {
        character = (unsigned int)'?';

        if (accent != 0)
        {
            *accent = TITAN_FONT_ACCENT_NONE;
        }
    }

    return
        (unsigned int)character -
        TITAN_FONT_FIRST_CHARACTER;
}

static unsigned int TitanFont_GetAdvance(unsigned int glyph_index)
{
    return glyph_index == TITAN_FONT_STAR_GLYPH_INDEX
        ? TITAN_FONT_STAR_ADVANCE
        : (unsigned int)g_titan_font_advance[glyph_index];
}

static unsigned int TitanFont_GetDrawWidth(unsigned int glyph_index)
{
    return glyph_index == TITAN_FONT_STAR_GLYPH_INDEX
        ? TITAN_FONT_STAR_DRAW_WIDTH
        : (unsigned int)g_titan_font_draw_width[glyph_index];
}


static int TitanFont_IsProceduralSymbol(unsigned int codepoint)
{
    return
        codepoint == 0x00AEu || /* registered sign */
        codepoint == 0x2022u || /* bullet */
        codepoint == 0x2122u || /* trademark */
        codepoint == 0x2191u || /* up arrow */
        codepoint == 0x2192u || /* right arrow */
        codepoint == 0x2193u || /* down arrow */
        codepoint == 0x26A0u;   /* warning sign */
}

static float TitanFont_GetProceduralAdvance(unsigned int codepoint, float scale)
{
    float units;

    switch (codepoint)
    {
        case 0x2022u: units = 10.0f; break;
        case 0x00AEu: units = 14.0f; break;
        case 0x2122u: units = 15.0f; break;
        case 0x2191u:
        case 0x2193u: units = 12.0f; break;
        case 0x2192u: units = 14.0f; break;
        case 0x26A0u: units = 16.0f; break;
        default: units = 12.0f; break;
    }

    return units * scale;
}

static void TitanFont_DrawProceduralSymbol(
    float x,
    float y,
    float scale,
    unsigned int codepoint,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    float t = scale < 0.70f ? 0.75f : 1.0f;
    float cx;
    float cy;

    if (alpha == 0u) return;

    if (codepoint == 0x2022u)
    {
        TitanVideo_DrawCircleAlpha(
            x + (4.5f * scale),
            y + (12.0f * scale),
            2.1f * scale,
            red, green, blue, alpha
        );
        return;
    }

    if (codepoint == 0x2122u)
    {
        /* Superscript TM, matching the canonical Unicode character without
         * requiring another atlas cell. */
        TitanFont_DrawTextAlpha(
            x,
            y - (1.5f * scale),
            scale * 0.52f,
            "TM",
            red, green, blue, alpha
        );
        return;
    }

    if (codepoint == 0x00AEu)
    {
        float r = 5.1f * scale;
        cx = x + (6.0f * scale);
        cy = y + (8.0f * scale);

        /* Thin octagonal ring plus a small R. */
        TitanVideo_DrawLineAlpha(cx - r * 0.70f, cy - r * 0.70f, cx, cy - r, t, red, green, blue, alpha);
        TitanVideo_DrawLineAlpha(cx, cy - r, cx + r * 0.70f, cy - r * 0.70f, t, red, green, blue, alpha);
        TitanVideo_DrawLineAlpha(cx + r * 0.70f, cy - r * 0.70f, cx + r, cy, t, red, green, blue, alpha);
        TitanVideo_DrawLineAlpha(cx + r, cy, cx + r * 0.70f, cy + r * 0.70f, t, red, green, blue, alpha);
        TitanVideo_DrawLineAlpha(cx + r * 0.70f, cy + r * 0.70f, cx, cy + r, t, red, green, blue, alpha);
        TitanVideo_DrawLineAlpha(cx, cy + r, cx - r * 0.70f, cy + r * 0.70f, t, red, green, blue, alpha);
        TitanVideo_DrawLineAlpha(cx - r * 0.70f, cy + r * 0.70f, cx - r, cy, t, red, green, blue, alpha);
        TitanVideo_DrawLineAlpha(cx - r, cy, cx - r * 0.70f, cy - r * 0.70f, t, red, green, blue, alpha);
        TitanFont_DrawTextAlpha(
            x + (3.2f * scale),
            y + (2.0f * scale),
            scale * 0.42f,
            "R",
            red, green, blue, alpha
        );
        return;
    }

    if (codepoint == 0x26A0u)
    {
        float left = x + (1.0f * scale);
        float right = x + (14.0f * scale);
        float top = y + (2.0f * scale);
        float bottom = y + (17.0f * scale);
        cx = (left + right) * 0.5f;

        TitanVideo_DrawLineAlpha(cx, top, left, bottom, t, red, green, blue, alpha);
        TitanVideo_DrawLineAlpha(left, bottom, right, bottom, t, red, green, blue, alpha);
        TitanVideo_DrawLineAlpha(right, bottom, cx, top, t, red, green, blue, alpha);
        TitanVideo_DrawLineAlpha(cx, y + (7.0f * scale), cx, y + (12.5f * scale), t, red, green, blue, alpha);
        TitanVideo_DrawCircleAlpha(cx, y + (15.0f * scale), 0.9f * scale, red, green, blue, alpha);
        return;
    }

    cx = x + (6.0f * scale);
    cy = y + (11.0f * scale);

    if (codepoint == 0x2191u)
    {
        TitanVideo_DrawLineAlpha(cx, y + (18.0f * scale), cx, y + (4.0f * scale), t, red, green, blue, alpha);
        TitanVideo_DrawLineAlpha(cx, y + (4.0f * scale), cx - (4.0f * scale), y + (8.0f * scale), t, red, green, blue, alpha);
        TitanVideo_DrawLineAlpha(cx, y + (4.0f * scale), cx + (4.0f * scale), y + (8.0f * scale), t, red, green, blue, alpha);
    }
    else if (codepoint == 0x2193u)
    {
        TitanVideo_DrawLineAlpha(cx, y + (4.0f * scale), cx, y + (18.0f * scale), t, red, green, blue, alpha);
        TitanVideo_DrawLineAlpha(cx, y + (18.0f * scale), cx - (4.0f * scale), y + (14.0f * scale), t, red, green, blue, alpha);
        TitanVideo_DrawLineAlpha(cx, y + (18.0f * scale), cx + (4.0f * scale), y + (14.0f * scale), t, red, green, blue, alpha);
    }
    else if (codepoint == 0x2192u)
    {
        TitanVideo_DrawLineAlpha(x + (1.0f * scale), cy, x + (12.0f * scale), cy, t, red, green, blue, alpha);
        TitanVideo_DrawLineAlpha(x + (12.0f * scale), cy, x + (8.0f * scale), cy - (4.0f * scale), t, red, green, blue, alpha);
        TitanVideo_DrawLineAlpha(x + (12.0f * scale), cy, x + (8.0f * scale), cy + (4.0f * scale), t, red, green, blue, alpha);
    }
}

static void TitanFont_DrawAccent(
    float x,
    float y,
    float width,
    float scale,
    TitanFontAccent accent,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    float center;
    float top;
    float left;
    float right;
    float thickness;

    if (accent == TITAN_FONT_ACCENT_NONE || alpha == 0u)
    {
        return;
    }

    center = x + (width * 0.5f);
    top = y + (1.0f * scale);
    left = center - (3.0f * scale);
    right = center + (3.0f * scale);
    thickness = scale < 0.70f ? 0.75f : 1.0f;

    if (accent == TITAN_FONT_ACCENT_ACUTE)
    {
        TitanVideo_DrawLineAlpha(left, top + (3.0f * scale), right, top, thickness, red, green, blue, alpha);
    }
    else if (accent == TITAN_FONT_ACCENT_GRAVE)
    {
        TitanVideo_DrawLineAlpha(left, top, right, top + (3.0f * scale), thickness, red, green, blue, alpha);
    }
    else if (accent == TITAN_FONT_ACCENT_CIRCUMFLEX)
    {
        TitanVideo_DrawLineAlpha(left, top + (3.0f * scale), center, top, thickness, red, green, blue, alpha);
        TitanVideo_DrawLineAlpha(center, top, right, top + (3.0f * scale), thickness, red, green, blue, alpha);
    }
    else if (accent == TITAN_FONT_ACCENT_TILDE)
    {
        TitanVideo_DrawLineAlpha(left, top + (2.0f * scale), center - (1.0f * scale), top, thickness, red, green, blue, alpha);
        TitanVideo_DrawLineAlpha(center - (1.0f * scale), top, right, top + (2.0f * scale), thickness, red, green, blue, alpha);
    }
    else if (accent == TITAN_FONT_ACCENT_DIAERESIS)
    {
        TitanVideo_DrawLineAlpha(left, top + (1.0f * scale), left + thickness, top + (1.0f * scale), thickness, red, green, blue, alpha);
        TitanVideo_DrawLineAlpha(right - thickness, top + (1.0f * scale), right, top + (1.0f * scale), thickness, red, green, blue, alpha);
    }
    else if (accent == TITAN_FONT_ACCENT_CEDILLA)
    {
        float bottom;

        bottom = y + (20.0f * scale);
        TitanVideo_DrawLineAlpha(center, bottom, center - (1.5f * scale), bottom + (2.0f * scale), thickness, red, green, blue, alpha);
        TitanVideo_DrawLineAlpha(center - (1.5f * scale), bottom + (2.0f * scale), center + (1.0f * scale), bottom + (3.5f * scale), thickness, red, green, blue, alpha);
    }
}

static int TitanFont_GetKerning(
    unsigned int left_glyph,
    unsigned int right_glyph
)
{
    if (
        left_glyph >= TITAN_FONT_GLYPH_COUNT ||
        right_glyph >= TITAN_FONT_GLYPH_COUNT
    )
    {
        return 0;
    }

    return (int)g_titan_font_kerning[
        (left_glyph * TITAN_FONT_GLYPH_COUNT) +
        right_glyph
    ];
}

static unsigned char TitanFont_QuantizeCoverage(
    unsigned int coverage
)
{
    unsigned int boosted;

    /*
     * Keep all 8 bits of FreeType/Pillow coverage.  The previous four-level
     * quantizer caused the stair-stepped, broken-looking glyph edges visible
     * after HDTV magnification.  A small mid-tone boost keeps thin strokes
     * readable without turning the atlas into a hard bitmap.
     */
    if (coverage < 4u)
    {
        return 0u;
    }

    boosted = coverage +
        ((coverage * (255u - coverage) + 255u) >> 9u);

    return (unsigned char)(boosted > 255u ? 255u : boosted);
}

TitanResult TitanFont_UploadAtlas(void)
{
    unsigned int x;
    unsigned int y;
    unsigned int coverage;
    unsigned int gs_alpha;
    unsigned int destination_offset;

    if (!g_font_initialized)
    {
        return TITAN_ERROR_FONT_ASSET_INVALID;
    }

    for (
        destination_offset = 0u;
        destination_offset <
            TITAN_VIDEO_FONT_ATLAS_WIDTH *
            TITAN_VIDEO_FONT_ATLAS_HEIGHT;
        destination_offset++
    )
    {
        g_font_atlas_pixels[destination_offset] = 0u;
    }

    for (y = 0u; y < TITAN_FONT_ATLAS_HEIGHT; y++)
    {
        for (x = 0u; x < TITAN_FONT_ATLAS_WIDTH; x++)
        {
            coverage = TitanFont_QuantizeCoverage(
                titan_ui_font[
                    (y * TITAN_FONT_ATLAS_WIDTH) + x
                ]
            );

            gs_alpha =
                ((coverage * 128u) + 127u) /
                255u;

            g_font_atlas_pixels[
                (y * TITAN_VIDEO_FONT_ATLAS_WIDTH) + x
            ] =
                0x00808080u |
                (gs_alpha << 24u);
        }
    }

    return TitanVideo_UploadFontAtlas32(
        g_font_atlas_pixels,
        TITAN_VIDEO_FONT_ATLAS_WIDTH,
        TITAN_VIDEO_FONT_ATLAS_HEIGHT,
        TITAN_VIDEO_FONT_ATLAS_WIDTH
    );
}

static unsigned short TitanFont_RasterPackColor(
    unsigned char red,
    unsigned char green,
    unsigned char blue
)
{
    return (unsigned short)(
        ((unsigned short)(red >> 3u)) |
        ((unsigned short)(green >> 3u) << 5u) |
        ((unsigned short)(blue >> 3u) << 10u) |
        0x8000u
    );
}

static void TitanFont_RasterBlendPixel(
    unsigned short *pixels,
    unsigned int surface_width,
    unsigned int surface_height,
    unsigned int stride,
    int x,
    int y,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned int alpha
)
{
    unsigned short destination;
    unsigned int destination_red;
    unsigned int destination_green;
    unsigned int destination_blue;
    unsigned int source_red;
    unsigned int source_green;
    unsigned int source_blue;
    unsigned int inverse_alpha;
    unsigned int output_red;
    unsigned int output_green;
    unsigned int output_blue;

    if (
        pixels == 0 ||
        x < 0 ||
        y < 0 ||
        (unsigned int)x >= surface_width ||
        (unsigned int)y >= surface_height ||
        alpha == 0u
    )
    {
        return;
    }

    if (alpha >= 255u)
    {
        pixels[((unsigned int)y * stride) + (unsigned int)x] =
            TitanFont_RasterPackColor(red, green, blue);
        return;
    }

    destination = pixels[
        ((unsigned int)y * stride) + (unsigned int)x
    ];

    destination_red = destination & 0x1Fu;
    destination_green = (destination >> 5u) & 0x1Fu;
    destination_blue = (destination >> 10u) & 0x1Fu;
    source_red = red >> 3u;
    source_green = green >> 3u;
    source_blue = blue >> 3u;
    inverse_alpha = 255u - alpha;

    output_red =
        ((source_red * alpha) +
         (destination_red * inverse_alpha) + 127u) /
        255u;

    output_green =
        ((source_green * alpha) +
         (destination_green * inverse_alpha) + 127u) /
        255u;

    output_blue =
        ((source_blue * alpha) +
         (destination_blue * inverse_alpha) + 127u) /
        255u;

    pixels[((unsigned int)y * stride) + (unsigned int)x] =
        (unsigned short)(
            output_red |
            (output_green << 5u) |
            (output_blue << 10u) |
            0x8000u
        );
}

static void TitanFont_RasterLine(
    unsigned short *pixels,
    unsigned int surface_width,
    unsigned int surface_height,
    unsigned int stride,
    int x0,
    int y0,
    int x1,
    int y1,
    unsigned char red,
    unsigned char green,
    unsigned char blue
)
{
    int delta_x;
    int delta_y;
    int step_x;
    int step_y;
    int error;

    delta_x = x1 >= x0 ? x1 - x0 : x0 - x1;
    delta_y = y1 >= y0 ? y0 - y1 : y1 - y0;
    step_x = x0 < x1 ? 1 : -1;
    step_y = y0 < y1 ? 1 : -1;
    error = delta_x + delta_y;

    for (;;)
    {
        int doubled_error;

        TitanFont_RasterBlendPixel(
            pixels,
            surface_width,
            surface_height,
            stride,
            x0,
            y0,
            red,
            green,
            blue,
            255u
        );

        if (x0 == x1 && y0 == y1)
        {
            break;
        }

        doubled_error = error * 2;

        if (doubled_error >= delta_y)
        {
            error += delta_y;
            x0 += step_x;
        }

        if (doubled_error <= delta_x)
        {
            error += delta_x;
            y0 += step_y;
        }
    }
}

static void TitanFont_RasterAccent16(
    unsigned short *pixels,
    unsigned int surface_width,
    unsigned int surface_height,
    unsigned int stride,
    int x,
    int y,
    int width,
    int glyph_height,
    TitanFontAccent accent,
    unsigned char red,
    unsigned char green,
    unsigned char blue
)
{
    int center;
    int left;
    int right;

    if (accent == TITAN_FONT_ACCENT_NONE)
    {
        return;
    }

    center = x + (width / 2);
    left = center - 2;
    right = center + 2;

    if (accent == TITAN_FONT_ACCENT_ACUTE)
    {
        TitanFont_RasterLine(pixels, surface_width, surface_height, stride, left, y + 3, right, y, red, green, blue);
    }
    else if (accent == TITAN_FONT_ACCENT_GRAVE)
    {
        TitanFont_RasterLine(pixels, surface_width, surface_height, stride, left, y, right, y + 3, red, green, blue);
    }
    else if (accent == TITAN_FONT_ACCENT_CIRCUMFLEX)
    {
        TitanFont_RasterLine(pixels, surface_width, surface_height, stride, left, y + 3, center, y, red, green, blue);
        TitanFont_RasterLine(pixels, surface_width, surface_height, stride, center, y, right, y + 3, red, green, blue);
    }
    else if (accent == TITAN_FONT_ACCENT_TILDE)
    {
        TitanFont_RasterLine(pixels, surface_width, surface_height, stride, left, y + 2, center - 1, y, red, green, blue);
        TitanFont_RasterLine(pixels, surface_width, surface_height, stride, center - 1, y, right, y + 2, red, green, blue);
    }
    else if (accent == TITAN_FONT_ACCENT_DIAERESIS)
    {
        TitanFont_RasterBlendPixel(pixels, surface_width, surface_height, stride, left, y + 1, red, green, blue, 255u);
        TitanFont_RasterBlendPixel(pixels, surface_width, surface_height, stride, right, y + 1, red, green, blue, 255u);
    }
    else if (accent == TITAN_FONT_ACCENT_CEDILLA)
    {
        TitanFont_RasterLine(pixels, surface_width, surface_height, stride, center, y + glyph_height - 1, center - 1, y + glyph_height + 2, red, green, blue);
        TitanFont_RasterLine(pixels, surface_width, surface_height, stride, center - 1, y + glyph_height + 2, center + 1, y + glyph_height + 3, red, green, blue);
    }
}

float TitanFont_MeasureTextRaster(
    float horizontal_scale,
    const char *text
)
{
    return TitanFont_MeasureText(
        horizontal_scale,
        text
    );
}

void TitanFont_RasterizeText16(
    unsigned short *pixels,
    unsigned int surface_width,
    unsigned int surface_height,
    unsigned int stride,
    int x,
    int y,
    float horizontal_scale,
    float vertical_scale,
    const char *text,
    unsigned char red,
    unsigned char green,
    unsigned char blue
)
{
    unsigned int text_index;
    unsigned int codepoint;
    unsigned int glyph_index;
    unsigned int previous_glyph_index;
    unsigned int atlas_column;
    unsigned int atlas_row;
    unsigned int glyph_draw_width;
    int destination_width;
    int destination_height;
    int destination_x;
    int destination_y;
    int source_x;
    int source_y;
    unsigned int atlas_x;
    unsigned int atlas_y;
    unsigned int atlas_offset;
    float cursor_x;
    float cursor_y;
    float line_start_x;
    TitanFontAccent accent;

    if (
        !g_font_initialized ||
        pixels == 0 ||
        surface_width == 0u ||
        surface_height == 0u ||
        stride < surface_width ||
        text == 0 ||
        horizontal_scale <= 0.0f ||
        vertical_scale <= 0.0f
    )
    {
        return;
    }

    destination_height = (int)(
        ((float)TITAN_FONT_CELL_HEIGHT * vertical_scale) +
        0.5f
    );

    if (destination_height < 1)
    {
        destination_height = 1;
    }

    cursor_x = (float)x;
    cursor_y = (float)y;
    line_start_x = (float)x;
    text_index = 0u;
    previous_glyph_index = TITAN_FONT_GLYPH_COUNT;

    while (text[text_index] != '\0')
    {
        codepoint = TitanFont_DecodeCodepoint(text, &text_index);

        if (codepoint == (unsigned int)'\n')
        {
            cursor_x = line_start_x;
            cursor_y +=
                (float)TITAN_FONT_CELL_HEIGHT *
                vertical_scale;
            previous_glyph_index = TITAN_FONT_GLYPH_COUNT;
            continue;
        }

        glyph_index = TitanFont_GetGlyphIndex(codepoint, &accent);
        cursor_x +=
            (float)TitanFont_GetKerning(
                previous_glyph_index,
                glyph_index
            ) * horizontal_scale;

        atlas_column = glyph_index % TITAN_FONT_ATLAS_COLUMNS;
        atlas_row = glyph_index / TITAN_FONT_ATLAS_COLUMNS;
        glyph_draw_width = TitanFont_GetDrawWidth(glyph_index);
        destination_width = (int)(
            ((float)glyph_draw_width * horizontal_scale) +
            0.5f
        );

        if (destination_width < 1)
        {
            destination_width = 1;
        }

        for (destination_y = 0; destination_y < destination_height; destination_y++)
        {
            source_y =
                (destination_y * TITAN_FONT_CELL_HEIGHT) /
                destination_height;
            atlas_y =
                (atlas_row * TITAN_FONT_CELL_HEIGHT) +
                (unsigned int)source_y;

            for (destination_x = 0; destination_x < destination_width; destination_x++)
            {
                source_x =
                    (destination_x * (int)glyph_draw_width) /
                    destination_width;
                atlas_x =
                    (atlas_column * TITAN_FONT_CELL_WIDTH) +
                    (unsigned int)source_x;
                atlas_offset =
                    (atlas_y * TITAN_FONT_ATLAS_WIDTH) +
                    atlas_x;

                TitanFont_RasterBlendPixel(
                    pixels,
                    surface_width,
                    surface_height,
                    stride,
                    (int)cursor_x + destination_x,
                    (int)cursor_y + destination_y,
                    red,
                    green,
                    blue,
                    titan_ui_font[atlas_offset]
                );
            }
        }

        TitanFont_RasterAccent16(
            pixels,
            surface_width,
            surface_height,
            stride,
            (int)cursor_x,
            (int)cursor_y,
            destination_width,
            destination_height,
            accent,
            red,
            green,
            blue
        );

        cursor_x +=
            (float)TitanFont_GetAdvance(glyph_index) *
            horizontal_scale;
        previous_glyph_index = glyph_index;
    }
}

void TitanFont_DrawTextAlpha(
    float x,
    float y,
    float scale,
    const char *text,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    unsigned int text_index;
    unsigned int glyph_index;
    unsigned int previous_glyph_index;
    unsigned int codepoint;
    unsigned int glyph_draw_width;
    unsigned int atlas_column;
    unsigned int atlas_row;

    float cursor_x;
    float cursor_y;
    float line_start_x;
    float logical_width;
    float logical_height;
    TitanFontAccent accent;

    if (
        !g_font_initialized ||
        text == 0 ||
        scale <= 0.0f ||
        alpha == 0
    )
    {
        return;
    }

    cursor_x = x;
    cursor_y = y;
    line_start_x = x;
    text_index = 0;
    previous_glyph_index = TITAN_FONT_GLYPH_COUNT;

    while (text[text_index] != '\0')
    {
        codepoint = TitanFont_DecodeCodepoint(text, &text_index);

        if (codepoint == (unsigned int)'\n')
        {
            cursor_x = line_start_x;
            cursor_y +=
                (float)TITAN_FONT_CELL_HEIGHT *
                scale;

            previous_glyph_index =
                TITAN_FONT_GLYPH_COUNT;

            continue;
        }

        if (TitanFont_IsProceduralSymbol(codepoint))
        {
            TitanFont_DrawProceduralSymbol(
                cursor_x,
                cursor_y,
                scale,
                codepoint,
                red,
                green,
                blue,
                alpha
            );
            cursor_x += TitanFont_GetProceduralAdvance(codepoint, scale);
            previous_glyph_index = TITAN_FONT_GLYPH_COUNT;
            continue;
        }

        glyph_index = TitanFont_GetGlyphIndex(
            codepoint,
            &accent
        );

        cursor_x +=
            (float)TitanFont_GetKerning(
                previous_glyph_index,
                glyph_index
            ) * scale;

        atlas_column =
            glyph_index %
            TITAN_FONT_ATLAS_COLUMNS;

        atlas_row =
            glyph_index /
            TITAN_FONT_ATLAS_COLUMNS;

        glyph_draw_width = TitanFont_GetDrawWidth(glyph_index);

        logical_width =
            (float)glyph_draw_width *
            scale;
        logical_height =
            (float)TITAN_FONT_CELL_HEIGHT *
            scale;

        TitanVideo_DrawFontAtlasRegion32(
            cursor_x,
            cursor_y,
            logical_width,
            logical_height,
            atlas_column * TITAN_FONT_CELL_WIDTH,
            atlas_row * TITAN_FONT_CELL_HEIGHT,
            glyph_draw_width,
            TITAN_FONT_CELL_HEIGHT,
            red,
            green,
            blue,
            alpha
        );

        TitanFont_DrawAccent(
            cursor_x,
            cursor_y,
            logical_width,
            scale,
            accent,
            red,
            green,
            blue,
            alpha
        );

        cursor_x +=
            (float)TitanFont_GetAdvance(glyph_index) *
            scale;

        previous_glyph_index = glyph_index;
    }
}

void TitanFont_DrawText(
    float x,
    float y,
    float scale,
    const char *text,
    unsigned char red,
    unsigned char green,
    unsigned char blue
)
{
    TitanFont_DrawTextAlpha(
        x,
        y,
        scale,
        text,
        red,
        green,
        blue,
        255
    );
}

void TitanFont_DrawTextShadow(
    float x,
    float y,
    float scale,
    const char *text,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    unsigned char outline_alpha;

    if (TitanVideo_IsProgressiveOutput())
    {
        /*
         * A single soft drop shadow remains legible at the 720p anamorphic
         * scale. Four cardinal copies looked like a heavy outline and blurred
         * small labels.
         */
        outline_alpha = (unsigned char)((alpha * 2u) / 5u);
        TitanFont_DrawTextAlpha(
            x + 1.0f,
            y + 1.0f,
            scale,
            text,
            0,
            0,
            0,
            outline_alpha
        );
    }
    else
    {
        outline_alpha = (unsigned char)((alpha * 3u) / 5u);
        TitanFont_DrawTextAlpha(x - 1.0f, y, scale, text, 0, 0, 0, outline_alpha);
        TitanFont_DrawTextAlpha(x + 1.0f, y, scale, text, 0, 0, 0, outline_alpha);
        TitanFont_DrawTextAlpha(x, y - 1.0f, scale, text, 0, 0, 0, outline_alpha);
        TitanFont_DrawTextAlpha(x, y + 1.0f, scale, text, 0, 0, 0, outline_alpha);
    }

    TitanFont_DrawTextAlpha(
        x,
        y,
        scale,
        text,
        red,
        green,
        blue,
        alpha
    );
}

float TitanFont_MeasureText(
    float scale,
    const char *text
)
{
    unsigned int text_index;
    unsigned int glyph_index;
    unsigned int previous_glyph_index;
    unsigned int codepoint;
    TitanFontAccent accent;

    float current_width;
    float maximum_width;

    if (
        !g_font_initialized ||
        text == 0 ||
        scale <= 0.0f
    )
    {
        return 0.0f;
    }

    current_width = 0.0f;
    maximum_width = 0.0f;
    text_index = 0;
    previous_glyph_index = TITAN_FONT_GLYPH_COUNT;

    while (text[text_index] != '\0')
    {
        codepoint = TitanFont_DecodeCodepoint(text, &text_index);

        if (codepoint == (unsigned int)'\n')
        {
            if (current_width > maximum_width)
            {
                maximum_width = current_width;
            }

            current_width = 0.0f;
            previous_glyph_index =
                TITAN_FONT_GLYPH_COUNT;

            continue;
        }

        if (TitanFont_IsProceduralSymbol(codepoint))
        {
            current_width += TitanFont_GetProceduralAdvance(codepoint, scale);
            previous_glyph_index = TITAN_FONT_GLYPH_COUNT;
            continue;
        }

        glyph_index = TitanFont_GetGlyphIndex(
            codepoint,
            &accent
        );

        current_width +=
            (float)TitanFont_GetKerning(
                previous_glyph_index,
                glyph_index
            ) * scale;

        current_width +=
            (float)TitanFont_GetAdvance(glyph_index) *
            scale;

        previous_glyph_index = glyph_index;
    }

    if (current_width > maximum_width)
    {
        maximum_width = current_width;
    }

    return maximum_width;
}
