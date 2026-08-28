#include "titan_shell_background.h"

#include "titan_video.h"
#include "titan_xmb_spline.h"
#include "titan_theme.h"
#include "titan_vram_boot_guard.h"
#include "titan_boot_recovery.h"
#include "titan_storage_io.h"
#include "titan_time.h"
#include "titan_theme_v4_runtime.h"
#include "titan_theme_lifecycle.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TITAN_WAVE_PARTICLE_COUNT 28u
#define TITAN_THEME_SCENE_WIDTH 512u
#define TITAN_THEME_SCENE_HEIGHT 256u
#define TITAN_THEME_SCENE_PIXELS (TITAN_THEME_SCENE_WIDTH * TITAN_THEME_SCENE_HEIGHT)
#define TITAN_THEME_SCENE_BYTES (TITAN_THEME_SCENE_PIXELS * 2u)

static int g_background_initialized;
static float g_idle_blend;
static unsigned int g_theme_generation;
static unsigned short *g_scene_home;
static unsigned short *g_scene_top;
static unsigned short *g_scene_transition[TITAN_THEME_SCENE_TRANSITION_MAX];
static unsigned short *g_scene_home_idle[TITAN_THEME_SCENE_IDLE_MAX];
static unsigned short *g_scene_top_idle[TITAN_THEME_SCENE_IDLE_MAX];
static unsigned int g_scene_transition_count;
static unsigned int g_scene_home_idle_count;
static unsigned int g_scene_top_idle_count;
static unsigned int g_scene_home_idle_frame_ms;
static unsigned int g_scene_top_idle_frame_ms;
static unsigned int g_scene_home_idle_mode;
static unsigned int g_scene_top_idle_mode;
static unsigned int g_scene_idle_state;
static uint64_t g_scene_idle_started_ms;
static unsigned int g_scene_generation;
static volatile unsigned int g_dynamic_load_requested;
static volatile unsigned int g_dynamic_load_active;
static TitanThemeV4Runtime g_scene_v4_runtime;
static unsigned short *g_scene_v4_pixels[TITAN_THEME_V4_MAX_LAYERS];
static unsigned short g_scene_v4_rotated[256u * 128u];
static unsigned short g_scene_v4_upload[256u * 64u];
static uint64_t g_scene_v4_last_ms;
static unsigned int g_scene_v4_ready;
static unsigned int g_scene_v4_upload_generation = 1u;
static uint32_t g_scene_v4_frame_uploads;
static uint32_t g_scene_v4_frame_upload_bytes;
static uint32_t g_scene_v4_update_us;
static uint32_t g_scene_v4_render_us;
static uint32_t g_scene_v4_pika_hits;
static uint32_t g_scene_v4_pika_misses;
static uint32_t g_scene_v4_evictions;
static float TitanShellBackground_Clamp01(float value);
static float TitanShellBackground_Wave(unsigned int phase);

#define TITAN_THEME_SCENE_STATE_TRANSITION 0u
#define TITAN_THEME_SCENE_STATE_HOME 1u
#define TITAN_THEME_SCENE_STATE_TOP 2u

static const signed char g_wave_lut[128] =
{
       0,   6,  12,  19,  25,  31,  37,  43,
      49,  55,  60,  66,  71,  76,  81,  86,
      90,  94,  98, 102, 106, 109, 112, 115,
     117, 120, 122, 123, 125, 126, 127, 127,
     127, 127, 127, 126, 125, 123, 122, 120,
     117, 115, 112, 109, 106, 102,  98,  94,
      90,  86,  81,  76,  71,  66,  60,  55,
      49,  43,  37,  31,  25,  19,  12,   6,
       0,  -6, -12, -19, -25, -31, -37, -43,
     -49, -55, -60, -66, -71, -76, -81, -86,
     -90, -94, -98,-102,-106,-109,-112,-115,
    -117,-120,-122,-123,-125,-126,-127,-127,
    -127,-127,-127,-126,-125,-123,-122,-120,
    -117,-115,-112,-109,-106,-102, -98, -94,
     -90, -86, -81, -76, -71, -66, -60, -55,
     -49, -43, -37, -31, -25, -19, -12,  -6
};

static void TitanShellBackground_FreeV4Assets(void)
{
    unsigned int index;
    for (index = 0u; index < TITAN_THEME_V4_MAX_LAYERS; index++)
    {
        free(g_scene_v4_pixels[index]);
        g_scene_v4_pixels[index] = 0;
    }
    TitanThemeV4_Init(&g_scene_v4_runtime);
    g_scene_v4_ready = 0u;
    g_scene_v4_last_ms = 0u;
}

static char *TitanShellBackground_LoadTextAsset(const char *relative, unsigned int max_bytes)
{
    char path[TITAN_THEME_PATH_CAPACITY];
    TitanStorageIoSize size;
    char *data;
    int descriptor;
    if (!relative || !relative[0] ||
        !TitanTheme_BuildAssetPath(relative, path, sizeof(path)) ||
        !TitanStorageIo_PathInfo64(path, 0, &size) || size == 0u ||
        size > (TitanStorageIoSize)max_bytes)
        return 0;
    data = (char *)malloc((size_t)size + 1u);
    if (!data) return 0;
    descriptor = TitanStorageIo_Open(path, TITAN_STORAGE_IO_READ);
    if (descriptor < 0 || !TitanStorageIo_ReadExact(descriptor, data, (unsigned int)size))
    {
        if (descriptor >= 0) TitanStorageIo_Close(descriptor);
        free(data);
        return 0;
    }
    if (TitanStorageIo_Close(descriptor) < 0)
    {
        free(data);
        return 0;
    }
    data[(unsigned int)size] = '\0';
    return data;
}

static unsigned short *TitanShellBackground_LoadV4Layer(const TitanThemeV4Layer *layer)
{
    char path[TITAN_THEME_PATH_CAPACITY];
    TitanStorageIoSize size;
    unsigned short *pixels;
    int descriptor;
    if (!layer || !TitanTheme_BuildAssetPath(layer->asset, path, sizeof(path)) ||
        !TitanStorageIo_PathInfo64(path, 0, &size) ||
        size != (TitanStorageIoSize)layer->declared_bytes)
        return 0;
    pixels = (unsigned short *)malloc(layer->declared_bytes);
    if (!pixels) return 0;
    descriptor = TitanStorageIo_Open(path, TITAN_STORAGE_IO_READ);
    if (descriptor < 0 ||
        !TitanStorageIo_ReadExact(descriptor, pixels, layer->declared_bytes))
    {
        if (descriptor >= 0) TitanStorageIo_Close(descriptor);
        free(pixels);
        return 0;
    }
    if (TitanStorageIo_Close(descriptor) < 0)
    {
        free(pixels);
        return 0;
    }
    return pixels;
}

static void TitanShellBackground_LoadV4(void)
{
    const TitanThemeInfo *theme = TitanTheme_GetActive();
    char *manifest;
    unsigned int index;
    if (!theme || theme->background_engine != TITAN_THEME_BACKGROUND_SCENE_V4)
        return;
    manifest = TitanShellBackground_LoadTextAsset(theme->scene_v4.manifest, 64u * 1024u);
    if (!manifest) return;
    if (!TitanThemeV4_ParseManifest(&g_scene_v4_runtime, manifest))
    {
        free(manifest);
        return;
    }
    free(manifest);
    if (g_scene_v4_runtime.vram_budget != theme->scene_v4.vram_budget ||
        g_scene_v4_runtime.ee_ram_budget != theme->scene_v4.ee_ram_budget ||
        g_scene_v4_runtime.reserved_headroom != theme->scene_v4.reserved_headroom)
        return;
    for (index = 0u; index < g_scene_v4_runtime.layer_count; index++)
    {
        g_scene_v4_pixels[index] = TitanShellBackground_LoadV4Layer(
            &g_scene_v4_runtime.layers[index]
        );
        if (!g_scene_v4_pixels[index])
        {
            TitanShellBackground_FreeV4Assets();
            return;
        }
    }
    g_scene_v4_last_ms = TitanTime_NowMilliseconds();
    g_scene_v4_ready = 1u;
}

static const unsigned short *TitanShellBackground_RotateV4(
    const unsigned short *source, unsigned int width, unsigned int height,
    float degrees)
{
    unsigned int x, y;
    float radians, cosine, sine, cx, cy;
    if (!source || width > 256u || height > 128u) return 0;
    if (degrees > -0.01f && degrees < 0.01f) return source;
    radians = degrees * 0.01745329251994329577f;
    cosine = cosf(-radians);
    sine = sinf(-radians);
    cx = ((float)width - 1.0f) * 0.5f;
    cy = ((float)height - 1.0f) * 0.5f;
    memset(g_scene_v4_rotated, 0, sizeof(g_scene_v4_rotated));
    for (y = 0u; y < height; y++)
    {
        for (x = 0u; x < width; x++)
        {
            float dx = (float)x - cx;
            float dy = (float)y - cy;
            int sx = (int)(cosine * dx - sine * dy + cx + 0.5f);
            int sy = (int)(sine * dx + cosine * dy + cy + 0.5f);
            if (sx >= 0 && sy >= 0 && sx < (int)width && sy < (int)height)
                g_scene_v4_rotated[y * width + x] = source[(unsigned int)sy * width + (unsigned int)sx];
        }
    }
    return g_scene_v4_rotated;
}

static void TitanShellBackground_DrawV4Layer(
    const TitanThemeV4ResolvedLayer *layer, const unsigned short *source, float opacity)
{
    unsigned int y0;
    const TitanThemeV4Layer *decl;
    const unsigned short *pixels;
    if (!layer || !layer->visible || !source || !layer->source) return;
    decl = layer->source;
    pixels = TitanShellBackground_RotateV4(source, decl->width, decl->height,
                                            layer->rotation_degrees);
    if (!pixels) return;
    for (y0 = 0u; y0 < decl->height; y0 += 64u)
    {
        unsigned int rows = decl->height - y0;
        unsigned int row;
        if (rows > 64u) rows = 64u;
        memset(g_scene_v4_upload, 0, sizeof(g_scene_v4_upload));
        for (row = 0u; row < rows; row++)
            memcpy(&g_scene_v4_upload[row * 256u],
                   &pixels[(y0 + row) * decl->width],
                   decl->width * sizeof(unsigned short));
        g_scene_v4_upload_generation++;
        if (g_scene_v4_upload_generation == 0u) g_scene_v4_upload_generation = 1u;
        if (TitanVideo_UploadTransientAtlasPsm16(
                g_scene_v4_upload, 256u, 64u, 256u,
                g_scene_v4_upload_generation) != TITAN_RESULT_OK)
            return;
        g_scene_v4_frame_uploads++;
        g_scene_v4_frame_upload_bytes += 256u * 64u * 2u;
        TitanVideo_DrawTransientAtlasRegionPsm16(
            layer->x,
            layer->y + (float)y0 * layer->scale_y,
            (float)decl->width * layer->scale_x,
            (float)rows * layer->scale_y,
            0u, 0u, decl->width, rows, layer->alpha * TitanShellBackground_Clamp01(opacity)
        );
    }
}

static void TitanShellBackground_DrawV4(float top_transition, float opacity)
{
    TitanThemeV4FrameInput input;
    TitanPikaVrTelemetry pika_before;
    TitanPikaVrTelemetry pika_after;
    uint64_t now;
    uint64_t started_us;
    unsigned int index;
    TitanVramPressureState pressure;
    if (!g_scene_v4_ready || opacity <= 0.001f) return;
    g_scene_v4_frame_uploads = 0u;
    g_scene_v4_frame_upload_bytes = 0u;
    TitanVideo_GetPikaVrTelemetry(&pika_before);
    now = TitanTime_NowMilliseconds();
    memset(&input, 0, sizeof(input));
    input.delta_ms = g_scene_v4_last_ms && now >= g_scene_v4_last_ms
        ? (uint32_t)(now - g_scene_v4_last_ms) : 0u;
    if (input.delta_ms > 100u) input.delta_ms = 100u;
    g_scene_v4_last_ms = now;
    input.context = TITAN_THEME_V4_CONTEXT_BOTH;
    input.top_transition = TitanShellBackground_Clamp01(top_transition);
    input.idle_drift = TitanShellBackground_Wave((unsigned int)(now >> 4u));
    input.viewport_width = (float)TITAN_UI_WIDTH;
    input.viewport_height = (float)TITAN_UI_HEIGHT;
    pressure = TitanVramBootGuard_GetPressureState();
    input.pressure_percent = pressure == TITAN_VRAM_PRESSURE_CRITICAL ? 95u :
        (pressure == TITAN_VRAM_PRESSURE_WARNING ? 72u : 0u);
    started_us = TitanTime_NowMicroseconds();
    TitanThemeV4_Update(&g_scene_v4_runtime, &input);
    g_scene_v4_update_us = (uint32_t)(TitanTime_NowMicroseconds() - started_us);
    started_us = TitanTime_NowMicroseconds();
    for (index = 0u; index < g_scene_v4_runtime.layer_count; index++)
        TitanShellBackground_DrawV4Layer(
            TitanThemeV4_GetResolved(&g_scene_v4_runtime, index),
            g_scene_v4_pixels[index], opacity
        );
    g_scene_v4_render_us = (uint32_t)(TitanTime_NowMicroseconds() - started_us);
    TitanVideo_GetPikaVrTelemetry(&pika_after);
    g_scene_v4_pika_hits = pika_after.cache_hits >= pika_before.cache_hits ? pika_after.cache_hits - pika_before.cache_hits : 0u;
    g_scene_v4_pika_misses = pika_after.cache_misses >= pika_before.cache_misses ? pika_after.cache_misses - pika_before.cache_misses : 0u;
    g_scene_v4_evictions = pika_after.textures_evicted >= pika_before.textures_evicted ? pika_after.textures_evicted - pika_before.textures_evicted : 0u;
}

void TitanShellBackground_GetThemeV4Telemetry(TitanThemeV4Telemetry *telemetry)
{
    if (!telemetry) return;
    TitanThemeV4_GetTelemetry(&g_scene_v4_runtime, telemetry);
    telemetry->texture_uploads = g_scene_v4_frame_uploads;
    telemetry->upload_bytes = g_scene_v4_frame_upload_bytes;
    telemetry->theme_vram = g_scene_v4_ready ? 256u * 64u * 2u : 0u;
    telemetry->theme_vram_peak = telemetry->theme_vram;
    telemetry->clut_usage = 0u;
    telemetry->pikavr_hits = g_scene_v4_pika_hits;
    telemetry->pikavr_misses = g_scene_v4_pika_misses;
    telemetry->evictions = g_scene_v4_evictions;
    telemetry->animation_update_us = g_scene_v4_update_us;
    telemetry->theme_render_us = g_scene_v4_render_us;
}

static void TitanShellBackground_FreeSceneAssets(void)
{
    unsigned int index;
    free(g_scene_home);
    free(g_scene_top);
    g_scene_home = 0;
    g_scene_top = 0;
    for (index = 0u; index < TITAN_THEME_SCENE_TRANSITION_MAX; index++)
    {
        free(g_scene_transition[index]);
        g_scene_transition[index] = 0;
    }
    for (index = 0u; index < TITAN_THEME_SCENE_IDLE_MAX; index++)
    {
        free(g_scene_home_idle[index]);
        free(g_scene_top_idle[index]);
        g_scene_home_idle[index] = 0;
        g_scene_top_idle[index] = 0;
    }
    g_scene_transition_count = 0u;
    g_scene_home_idle_count = 0u;
    g_scene_top_idle_count = 0u;
    g_scene_home_idle_frame_ms = 120u;
    g_scene_top_idle_frame_ms = 120u;
    g_scene_home_idle_mode = TITAN_THEME_SCENE_IDLE_LOOP;
    g_scene_top_idle_mode = TITAN_THEME_SCENE_IDLE_LOOP;
    g_scene_idle_state = TITAN_THEME_SCENE_STATE_TRANSITION;
    g_scene_idle_started_ms = 0u;
}

static unsigned short *TitanShellBackground_LoadSceneFrame(const char *relative)
{
    char path[TITAN_THEME_PATH_CAPACITY];
    TitanStorageIoSize size;
    unsigned short *pixels;
    int descriptor;

    if (!TitanTheme_BuildAssetPath(relative, path, sizeof(path)) ||
        !TitanStorageIo_PathInfo64(path, 0, &size) ||
        size != (TitanStorageIoSize)TITAN_THEME_SCENE_BYTES)
        return 0;

    pixels = (unsigned short *)malloc(TITAN_THEME_SCENE_BYTES);
    if (pixels == 0)
        return 0;
    descriptor = TitanStorageIo_Open(path, TITAN_STORAGE_IO_READ);
    if (descriptor < 0 || !TitanStorageIo_ReadExact(descriptor, pixels, TITAN_THEME_SCENE_BYTES))
    {
        if (descriptor >= 0)
            TitanStorageIo_Close(descriptor);
        free(pixels);
        return 0;
    }
    if (TitanStorageIo_Close(descriptor) < 0)
    {
        free(pixels);
        return 0;
    }
    return pixels;
}

void TitanShellBackground_RefreshThemeAssets(void)
{
    /* BF30: invalidate readiness only. Dynamic filesystem work is authorized
     * by HOME_ENTER/RETURN_HOME and executed by the serialized storage worker. */
    g_scene_generation = TitanTheme_GetGeneration();
    g_dynamic_load_requested = 0u;
    if (!g_dynamic_load_active)
    {
        TitanShellBackground_FreeSceneAssets();
        TitanShellBackground_FreeV4Assets();
    }
}

void TitanShellBackground_RequestDynamicActivation(void)
{
    const TitanThemeInfo *theme = TitanTheme_GetActive();
    if (!theme || !TitanThemeLifecycle_DynamicSceneAllowed()) return;
    if (theme->background_engine != TITAN_THEME_BACKGROUND_SCENE_V2 &&
        theme->background_engine != TITAN_THEME_BACKGROUND_SCENE_V4) return;
    if ((theme->background_engine == TITAN_THEME_BACKGROUND_SCENE_V4 && g_scene_v4_ready) ||
        (theme->background_engine == TITAN_THEME_BACKGROUND_SCENE_V2 && g_scene_home && g_scene_top))
        return;
    if (!g_dynamic_load_active) g_dynamic_load_requested = 1u;
}

int TitanShellBackground_HasStorageWork(void)
{
    return g_dynamic_load_requested != 0u && g_dynamic_load_active == 0u;
}

int TitanShellBackground_StorageWorkerPump(void)
{
    const TitanThemeInfo *theme;
    unsigned int index;
    if (!g_dynamic_load_requested || g_dynamic_load_active) return 0;
    g_dynamic_load_requested = 0u;
    g_dynamic_load_active = 1u;
    TitanShellBackground_FreeSceneAssets();
    TitanShellBackground_FreeV4Assets();
    theme = TitanTheme_GetActive();
    if (!theme || TitanTheme_GetGeneration() != g_scene_generation)
        goto done;
    if (theme->background_engine == TITAN_THEME_BACKGROUND_SCENE_V4)
    {
        TitanShellBackground_LoadV4();
        goto done;
    }
    if (theme->background_engine != TITAN_THEME_BACKGROUND_SCENE_V2)
        goto done;
    g_scene_home = TitanShellBackground_LoadSceneFrame(theme->scene.home_frame);
    g_scene_top = TitanShellBackground_LoadSceneFrame(theme->scene.top_frame);
    if (!g_scene_home || !g_scene_top) goto fail;
    for (index = 0u; index < theme->scene.transition_frame_count; index++)
    {
        g_scene_transition[index] = TitanShellBackground_LoadSceneFrame(theme->scene.transition_frames[index]);
        if (!g_scene_transition[index]) goto fail;
        g_scene_transition_count++;
    }
    for (index = 0u; index < theme->scene.home_idle_frame_count; index++)
    {
        g_scene_home_idle[index] = TitanShellBackground_LoadSceneFrame(theme->scene.home_idle_frames[index]);
        if (!g_scene_home_idle[index]) goto fail;
        g_scene_home_idle_count++;
    }
    for (index = 0u; index < theme->scene.top_idle_frame_count; index++)
    {
        g_scene_top_idle[index] = TitanShellBackground_LoadSceneFrame(theme->scene.top_idle_frames[index]);
        if (!g_scene_top_idle[index]) goto fail;
        g_scene_top_idle_count++;
    }
    g_scene_home_idle_frame_ms = theme->scene.home_idle_frame_ms;
    g_scene_top_idle_frame_ms = theme->scene.top_idle_frame_ms;
    g_scene_home_idle_mode = theme->scene.home_idle_mode;
    g_scene_top_idle_mode = theme->scene.top_idle_mode;
    g_scene_idle_state = TITAN_THEME_SCENE_STATE_TRANSITION;
    g_scene_idle_started_ms = TitanTime_NowMilliseconds();
    goto done;
fail:
    TitanShellBackground_FreeSceneAssets();
done:
    g_dynamic_load_active = 0u;
    return 1;
}

int TitanShellBackground_IsDynamicReady(void)
{
    const TitanThemeInfo *theme = TitanTheme_GetActive();
    if (!theme) return 0;
    if (theme->background_engine == TITAN_THEME_BACKGROUND_SCENE_V4) return g_scene_v4_ready != 0u;
    if (theme->background_engine == TITAN_THEME_BACKGROUND_SCENE_V2) return g_scene_home != 0 && g_scene_top != 0;
    return 1;
}

static const unsigned short *TitanShellBackground_SelectIdleFrame(
    const unsigned short *base,
    unsigned short *const *idle_frames,
    unsigned int idle_count,
    unsigned int frame_ms,
    unsigned int mode,
    uint64_t now_ms,
    unsigned int state
)
{
    uint64_t elapsed;
    uint64_t tick;
    unsigned int sequence_count;
    unsigned int slot;

    if (base == 0 || idle_count == 0u)
        return base;
    if (frame_ms < TITAN_THEME_SCENE_IDLE_FRAME_MS_MIN)
        frame_ms = TITAN_THEME_SCENE_IDLE_FRAME_MS_MIN;

    if (g_scene_idle_state != state)
    {
        g_scene_idle_state = state;
        g_scene_idle_started_ms = now_ms;
        return base;
    }

    elapsed = now_ms >= g_scene_idle_started_ms
        ? now_ms - g_scene_idle_started_ms
        : 0u;
    tick = elapsed / (uint64_t)frame_ms;
    sequence_count = idle_count + 1u; /* base + declared additional frames */

    if (mode == TITAN_THEME_SCENE_IDLE_PINGPONG && sequence_count > 1u)
    {
        unsigned int cycle = (sequence_count * 2u) - 2u;
        slot = (unsigned int)(tick % (uint64_t)cycle);
        if (slot >= sequence_count)
            slot = cycle - slot;
    }
    else
    {
        slot = (unsigned int)(tick % (uint64_t)sequence_count);
    }

    if (slot == 0u)
        return base;
    return idle_frames[slot - 1u] != 0 ? idle_frames[slot - 1u] : base;
}

static void TitanShellBackground_DrawScene(float top_transition, unsigned char alpha)
{
    const unsigned short *frame;
    float amount;
    uint64_t now_ms;

    if (alpha == 0u || g_scene_home == 0 || g_scene_top == 0)
        return;
    amount = top_transition;
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    now_ms = TitanTime_NowMilliseconds();

    /* Stable HOME/TOP states may run independent bounded idle loops. Entering
     * either state resets its clock so authors always start from the declared
     * base keyframe. During movement between states, idle animation is paused
     * and the reversible shell-synchronized transition keyframes own the scene. */
    if (amount <= 0.001f)
    {
        frame = TitanShellBackground_SelectIdleFrame(
            g_scene_home,
            g_scene_home_idle,
            g_scene_home_idle_count,
            g_scene_home_idle_frame_ms,
            g_scene_home_idle_mode,
            now_ms,
            TITAN_THEME_SCENE_STATE_HOME
        );
    }
    else if (amount >= 0.999f)
    {
        frame = TitanShellBackground_SelectIdleFrame(
            g_scene_top,
            g_scene_top_idle,
            g_scene_top_idle_count,
            g_scene_top_idle_frame_ms,
            g_scene_top_idle_mode,
            now_ms,
            TITAN_THEME_SCENE_STATE_TOP
        );
    }
    else
    {
        g_scene_idle_state = TITAN_THEME_SCENE_STATE_TRANSITION;
        g_scene_idle_started_ms = now_ms;
        if (g_scene_transition_count > 0u)
        {
            /* Treat HOME + intermediates + TOP as evenly spaced keyframes and
             * select the nearest one. This remains deterministic/reversible
             * because amount is the shell's own top-menu transition value. */
            unsigned int slot = (unsigned int)(
                amount * (float)(g_scene_transition_count + 1u) + 0.5f
            );
            if (slot == 0u)
                frame = g_scene_home;
            else if (slot > g_scene_transition_count)
                frame = g_scene_top;
            else
                frame = g_scene_transition[slot - 1u];
        }
        else
            frame = amount < 0.5f ? g_scene_home : g_scene_top;
    }

    (void)TitanVideo_DrawMediaImage16(
        frame,
        TITAN_THEME_SCENE_WIDTH,
        TITAN_THEME_SCENE_HEIGHT,
        TITAN_THEME_SCENE_WIDTH,
        0.0f, 0.0f,
        (float)TITAN_UI_WIDTH,
        (float)TITAN_UI_HEIGHT,
        alpha
    );
}

static float TitanShellBackground_Clamp01(float value)
{
    if (value < 0.0f)
    {
        return 0.0f;
    }
    if (value > 1.0f)
    {
        return 1.0f;
    }
    return value;
}

static float TitanShellBackground_Lerp(float from, float to, float amount)
{
    return from + ((to - from) * amount);
}

static float TitanShellBackground_Wave(unsigned int phase)
{
    return (float)g_wave_lut[phase & 127u] / 127.0f;
}

static unsigned char TitanShellBackground_ModulateAlpha(
    unsigned char alpha,
    float amount
)
{
    float value;

    value = (float)alpha * TitanShellBackground_Clamp01(amount);
    if (value <= 0.0f)
    {
        return 0u;
    }
    if (value >= 255.0f)
    {
        return 255u;
    }
    return (unsigned char)value;
}

static void TitanShellBackground_DrawParticles(
    unsigned int frame,
    float page_transition,
    float idle_intensity,
    float horizontal_motion,
    float opacity
)
{
    const TitanThemeFlowStyle *flow;
    unsigned int index;
    unsigned int flow_frame;
    float transition;
    float boost;

    flow = TitanTheme_GetFlowStyle();
    if (flow == 0 || flow->particles.alpha == 0u)
        return;
    transition = TitanShellBackground_Clamp01(page_transition);
    idle_intensity = TitanShellBackground_Clamp01(idle_intensity);
    horizontal_motion = TitanShellBackground_Clamp01(horizontal_motion);
    opacity = TitanShellBackground_Clamp01(opacity);
    boost = 1.0f + horizontal_motion * (flow->horizontal_boost - 1.0f);
    flow_frame = (unsigned int)((float)frame * flow->speed * boost);

    for (index = 0u; index < TITAN_WAVE_PARTICLE_COUNT; index++)
    {
        unsigned int seed;
        unsigned int twinkle;
        unsigned int span;
        float depth;
        float x;
        float y;
        float drift;
        float current;
        float size;
        unsigned char alpha;

        seed = index * 97u + 41u;
        twinkle = (flow_frame + index * 13u) & 127u;
        if (twinkle > 116u && (index & 3u) != 0u)
            continue;

        depth = (float)((seed >> 2u) & 15u) / 15.0f;
        current = TitanShellBackground_Wave(
            (flow_frame >> 4u) + seed + index * 7u
        );
        drift = current * (0.8f + depth * 3.2f) * flow->amplitude;
        span = TITAN_UI_WIDTH + 40u;
        /* Flow themes move right -> left. The modulo wrap is hidden outside
         * the authored canvas, so particles appear to enter from the right. */
        x = (float)(span - 1u -
            ((seed * 37u + (flow_frame >> (4u + (index & 1u)))) % span)) - 20.0f;
        y = TitanShellBackground_Lerp(72.0f, 80.0f, transition) +
            (float)((seed * 23u) % 170u) + drift - idle_intensity * 3.0f;
        size = 0.45f + depth * 1.35f;
        if (index == 6u || index == 21u)
            size += 0.85f;
        alpha = (unsigned char)(
            ((unsigned int)flow->particles.alpha * (38u + (unsigned int)(depth * 112.0f))) /
            150u
        );
        alpha = TitanShellBackground_ModulateAlpha(
            alpha,
            (0.78f + idle_intensity * 0.22f) * opacity
        );

        TitanVideo_DrawRectAlpha(
            x - size * 0.5f,
            y - size * 0.5f,
            size,
            size,
            flow->particles.red,
            flow->particles.green,
            flow->particles.blue,
            alpha
        );

        if ((index == 6u || index == 21u) && twinkle < 84u)
        {
            TitanVideo_DrawLineAlpha(
                x - size * 2.8f, y, x + size * 2.8f, y, 0.60f,
                flow->wave_highlight.red,
                flow->wave_highlight.green,
                flow->wave_highlight.blue,
                (unsigned char)((unsigned int)alpha / 2u)
            );
            TitanVideo_DrawLineAlpha(
                x, y - size * 2.8f, x, y + size * 2.8f, 0.60f,
                flow->wave_highlight.red,
                flow->wave_highlight.green,
                flow->wave_highlight.blue,
                (unsigned char)((unsigned int)alpha / 2u)
            );
        }
    }
}

static void TitanShellBackground_DrawAtmosphere(void)
{
    const TitanThemeFlowStyle *flow;
    unsigned int band;

    flow = TitanTheme_GetFlowStyle();
    if (flow->atmosphere_alpha == 0u)
        return;

    for (band = 0u; band < 6u; band++)
    {
        TitanVideo_DrawCircleAlpha(
            470.0f, 156.0f, 310.0f - (float)band * 38.0f,
            flow->wave_base.red,
            flow->wave_base.green,
            flow->wave_base.blue,
            flow->atmosphere_alpha
        );
    }

    for (band = 0u; band < 7u; band++)
    {
        TitanVideo_DrawCircleAlpha(
            330.0f, 430.0f, 360.0f - (float)band * 42.0f,
            flow->wave_highlight.red,
            flow->wave_highlight.green,
            flow->wave_highlight.blue,
            flow->atmosphere_alpha
        );
    }
}

static void TitanShellBackground_ApplyTheme(void)
{
    const TitanThemeFlowStyle *flow;

    flow = TitanTheme_GetFlowStyle();
    TitanVideo_BuildBackgroundGradient(
        flow->background_top.red,
        flow->background_top.green,
        flow->background_top.blue,
        flow->background_bottom.red,
        flow->background_bottom.green,
        flow->background_bottom.blue
    );
    TitanXmbSpline_InvalidateTheme();
    g_theme_generation = TitanTheme_GetGeneration();
}

void TitanShellBackground_Initialize(void)
{
    g_background_initialized = 1;
    g_idle_blend = 0.0f;

    g_theme_generation = 0u;
    TitanXmbSpline_Initialize();
    TitanShellBackground_ApplyTheme();
    TitanShellBackground_RefreshThemeAssets();
}

void TitanShellBackground_Shutdown(void)
{
    /* Release all theme-owned EE allocations before audio/input/IOP teardown. */
    TitanShellBackground_FreeSceneAssets();
    TitanShellBackground_FreeV4Assets();
    g_background_initialized = 0;
    g_idle_blend = 0.0f;
    g_theme_generation = 0u;
    g_scene_generation = 0u;
    g_scene_v4_frame_uploads = 0u;
    g_scene_v4_frame_upload_bytes = 0u;
    g_scene_v4_update_us = 0u;
    g_scene_v4_render_us = 0u;
    g_scene_v4_pika_hits = 0u;
    g_scene_v4_pika_misses = 0u;
    g_scene_v4_evictions = 0u;
    g_dynamic_load_requested = 0u;
    g_dynamic_load_active = 0u;
}

int TitanShellBackground_IsVuActive(void)
{
    return TitanXmbSpline_IsVuActive();
}

void TitanShellBackground_Draw(
    unsigned int frame,
    float page_transition,
    float idle_intensity,
    float horizontal_motion,
    float vertical_transition,
    float system_view_transition
)
{
    float transition;
    float idle_target;

    if (!g_background_initialized)
    {
        TitanShellBackground_Initialize();
    }

    if (g_theme_generation != TitanTheme_GetGeneration())
        TitanShellBackground_ApplyTheme();
    if (g_scene_generation != TitanTheme_GetGeneration())
        TitanShellBackground_RefreshThemeAssets();

    TitanVideo_RestoreBackground();

    /* BuildFix25 boot-safety: a critical VRAM state never blocks the shell on
     * decorative scene artwork. Reuse the existing atmosphere + Ribbon path
     * as the minimum visual identity while preserving core UI/font/video. */
    if (TitanBootRecovery_IsMinimum() ||
        TitanVramBootGuard_GetPressureState() == TITAN_VRAM_PRESSURE_CRITICAL)
    {
        TitanShellBackground_DrawAtmosphere();
        TitanXmbSpline_DrawWithOpacity(
            frame, TitanShellBackground_Clamp01(page_transition), 0.0f,
            TitanShellBackground_Clamp01(horizontal_motion), 0.0f, 0.35f
        );
        return;
    }

    if (TitanTheme_GetActive()->background_engine == TITAN_THEME_BACKGROUND_SCENE_V4)
    {
        float system_amount = TitanShellBackground_Clamp01(system_view_transition);
        float scene_opacity = TitanThemeLifecycle_GetSceneOpacity() * (1.0f - system_amount);
        float ribbon_opacity = 1.0f - scene_opacity;
        if (ribbon_opacity > 0.001f || !g_scene_v4_ready)
        {
            TitanShellBackground_DrawAtmosphere();
            TitanXmbSpline_DrawWithOpacity(
                frame, TitanShellBackground_Clamp01(page_transition), 0.0f,
                TitanShellBackground_Clamp01(horizontal_motion), 0.0f, ribbon_opacity
            );
            TitanShellBackground_DrawParticles(
                frame, TitanShellBackground_Clamp01(page_transition), 0.0f,
                TitanShellBackground_Clamp01(horizontal_motion), ribbon_opacity
            );
        }
        if (g_scene_v4_ready && scene_opacity > 0.001f)
            TitanShellBackground_DrawV4(vertical_transition, scene_opacity);
        return;
    }

    if (TitanTheme_GetActive()->background_engine == TITAN_THEME_BACKGROUND_SCENE_V2 &&
        g_scene_home != 0 && g_scene_top != 0)
    {
        float system_amount = TitanShellBackground_Clamp01(system_view_transition);
        float lifecycle_scene = TitanThemeLifecycle_GetSceneOpacity();
        unsigned char scene_alpha;
        if (1.0f - lifecycle_scene > system_amount) system_amount = 1.0f - lifecycle_scene;

        /* Functional views intentionally reduce scene themes to their Flow
         * palette/ribbon. HOME and the top menu never render a ribbon. */
        if (system_amount > 0.001f && TitanTheme_GetSceneStyle()->system_flow_enabled)
        {
            TitanShellBackground_DrawAtmosphere();
            TitanXmbSpline_DrawWithOpacity(
                frame, TitanShellBackground_Clamp01(page_transition), 0.0f,
                TitanShellBackground_Clamp01(horizontal_motion), 0.0f,
                system_amount
            );
            TitanShellBackground_DrawParticles(
                frame, TitanShellBackground_Clamp01(page_transition), 0.0f,
                TitanShellBackground_Clamp01(horizontal_motion), system_amount
            );
        }
        scene_alpha = (unsigned char)(255.0f * (1.0f - system_amount));
        TitanShellBackground_DrawScene(vertical_transition, scene_alpha);
        return;
    }

    TitanShellBackground_DrawAtmosphere();

    transition = TitanShellBackground_Clamp01(page_transition);
    idle_target = TitanShellBackground_Clamp01(idle_intensity) *
        (1.0f - transition);
    if (g_idle_blend < idle_target)
    {
        g_idle_blend += 0.012f;
        if (g_idle_blend > idle_target)
        {
            g_idle_blend = idle_target;
        }
    }
    else if (g_idle_blend > idle_target)
    {
        g_idle_blend -= 0.045f;
        if (g_idle_blend < idle_target)
        {
            g_idle_blend = idle_target;
        }
    }

    TitanXmbSpline_Draw(
        frame, transition, g_idle_blend,
        TitanShellBackground_Clamp01(horizontal_motion),
        TitanShellBackground_Clamp01(vertical_transition)
    );
    TitanShellBackground_DrawParticles(
        frame, transition, g_idle_blend,
        TitanShellBackground_Clamp01(horizontal_motion), 1.0f
    );
}
