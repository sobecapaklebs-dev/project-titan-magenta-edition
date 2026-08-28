#include "titan_theme.h"

#include "titan_storage_io.h"
#include "titan_storage_paths.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TITAN_THEME_ROOT TITAN_STORAGE_DEVICE_ROOT "/themes"
#define TITAN_THEME_CONFIG_NAME "theme.cfg"
#define TITAN_THEME_SETTINGS_ROOT TITAN_STORAGE_DEVICE_ROOT "/settings"
#define TITAN_THEME_SETTINGS_PATH TITAN_THEME_SETTINGS_ROOT "/active_theme.cfg"
#define TITAN_THEME_SETTINGS_TEMP TITAN_THEME_SETTINGS_ROOT "/active_theme.tmp"
#define TITAN_THEME_SETTINGS_BACKUP TITAN_THEME_SETTINGS_ROOT "/active_theme.bak"
#define TITAN_THEME_LINE_CAPACITY 192u
#define TITAN_THEME_SCENE_FRAME_BYTES (512u * 256u * 2u)

static int TitanTheme_PathJoin(
    char *destination,
    unsigned int capacity,
    const char *root,
    const char *name
);

static TitanThemeInfo g_themes[TITAN_THEME_CAPACITY];
static unsigned int g_theme_count;
static unsigned int g_active_index;
static unsigned int g_generation;

static void TitanTheme_CopyText(
    char *destination,
    unsigned int capacity,
    const char *source
)
{
    unsigned int index;

    if (destination == 0 || capacity == 0u)
        return;
    index = 0u;
    while (source != 0 && source[index] != '\0' && index + 1u < capacity)
    {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static int TitanTheme_IdSafe(const char *value)
{
    unsigned int index;
    unsigned int length;

    if (value == 0)
        return 0;
    length = (unsigned int)strlen(value);
    if (length < 3u || length >= TITAN_THEME_ID_CAPACITY)
        return 0;
    for (index = 0u; index < length; index++)
    {
        unsigned char c = (unsigned char)value[index];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return 0;
    }
    return 1;
}

static int TitanTheme_TextSafe(const char *value, unsigned int capacity)
{
    unsigned int index;

    if (value == 0 || value[0] == '\0' || strlen(value) >= capacity)
        return 0;
    for (index = 0u; value[index] != '\0'; index++)
    {
        unsigned char c = (unsigned char)value[index];
        if (c < 0x20u || c == '=')
            return 0;
    }
    return 1;
}

static int TitanTheme_AssetPathSafe(const char *value)
{
    unsigned int index;
    unsigned int length;

    if (value == 0 || value[0] == '\0')
        return 1; /* Optional assets may be empty. */
    length = (unsigned int)strlen(value);
    if (length >= TITAN_THEME_ASSET_PATH_CAPACITY || value[0] == '/' ||
        strstr(value, "..") != 0 || strchr(value, ':') != 0 || strchr(value, '\\') != 0)
        return 0;
    for (index = 0u; index < length; index++)
    {
        unsigned char c = (unsigned char)value[index];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '/'))
            return 0;
    }
    return 1;
}

static int TitanTheme_CopyAssetPath(char *destination, const char *value)
{
    if (!TitanTheme_AssetPathSafe(value))
        return 0;
    TitanTheme_CopyText(destination, TITAN_THEME_ASSET_PATH_CAPACITY, value);
    return 1;
}

static int TitanTheme_ValidateSceneFrame(const char *root, const char *relative)
{
    char path[TITAN_THEME_PATH_CAPACITY];
    TitanStorageIoSize size;
    int is_directory;

    if (relative == 0 || relative[0] == '\0' ||
        !TitanTheme_PathJoin(path, sizeof(path), root, relative) ||
        !TitanStorageIo_PathInfo64(path, &is_directory, &size) || is_directory)
        return 0;
    return size == (TitanStorageIoSize)TITAN_THEME_SCENE_FRAME_BYTES;
}

static unsigned int TitanTheme_ParseUnsigned(
    const char *value,
    unsigned int minimum,
    unsigned int maximum,
    unsigned int fallback
)
{
    char *end;
    unsigned long parsed;

    if (value == 0 || value[0] == '\0')
        return fallback;
    errno = 0;
    parsed = strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed < minimum || parsed > maximum)
        return fallback;
    return (unsigned int)parsed;
}

static int TitanTheme_ParseSigned(
    const char *value,
    int minimum,
    int maximum,
    int fallback
)
{
    char *end;
    long parsed;

    if (value == 0 || value[0] == '\0')
        return fallback;
    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed < minimum || parsed > maximum)
        return fallback;
    return (int)parsed;
}

static float TitanTheme_MilliToFloat(unsigned int value)
{
    return (float)value / 1000.0f;
}

static void TitanTheme_DefaultShell(TitanThemeShellStyle *style)
{
    memset(style, 0, sizeof(*style));
    style->accent.red = 222u;
    style->accent.green = 43u;
    style->accent.blue = 82u;
    style->accent.alpha = 255u;
    style->home_selected_scale = 1.0f;
    style->home_tile_spacing = 1.0f;
    style->title_offset_y = 0.0f;
    style->header_alpha = 1.0f;
    style->help_alpha = 1.0f;
    style->chrome_alpha = 1.0f;
    style->horizontal_motion_scale = 1.0f;
    style->vertical_motion_scale = 1.0f;
}

static void TitanTheme_DefaultFlow(TitanThemeFlowStyle *flow)
{
    memset(flow, 0, sizeof(*flow));
    flow->background_top.red = 3u;
    flow->background_top.green = 0u;
    flow->background_top.blue = 11u;
    flow->background_top.alpha = 255u;
    flow->background_bottom.red = 132u;
    flow->background_bottom.green = 0u;
    flow->background_bottom.blue = 108u;
    flow->background_bottom.alpha = 255u;
    flow->wave_base.red = 224u;
    flow->wave_base.green = 118u;
    flow->wave_base.blue = 235u;
    flow->wave_base.alpha = 32u;
    flow->wave_highlight.red = 255u;
    flow->wave_highlight.green = 202u;
    flow->wave_highlight.blue = 252u;
    flow->wave_highlight.alpha = 58u;
    flow->particles.red = 255u;
    flow->particles.green = 225u;
    flow->particles.blue = 255u;
    flow->particles.alpha = 140u;
    flow->speed = 1.0f;
    flow->frequency = 1.0f;
    flow->amplitude = 1.0f;
    flow->noise = 1.0f;
    flow->fresnel = 1.0f;
    flow->horizontal_boost = 1.7f;
    flow->vertical_shift = 18.0f;
    flow->day_frequency = 1.0f;
    flow->night_frequency = 1.0f;
    flow->day_speed = 1.0f;
    flow->night_speed = 1.0f;
    flow->vu1_deform_speed = 0.0f;
    flow->vu1_deform_frequency = 2.0f;
    flow->vu1_deform_amplitude = 0.0f;
    flow->primary_wave_count = 1u;
    flow->primary_waves[0].start_y = 252.0f;
    flow->primary_waves[0].end_y = 252.0f;
    flow->primary_waves[0].control1_y = 252.0f;
    flow->primary_waves[0].control2_y = 252.0f;
    flow->primary_waves[0].drift_y = 0.0f;
    flow->primary_waves[0].thickness_scale = 1.0f;
    flow->primary_waves[0].amplitude_scale = 1.0f;
    flow->primary_waves[0].alpha_scale = 1.0f;
    flow->primary_waves[0].phase_offset = 0u;
    flow->primary_waves[0].sweep_enabled = 0u;
    flow->primary_waves[0].sweep_speed = 0.0f;
    flow->primary_waves[0].sweep_orbit_x = 0.0f;
    flow->primary_waves[0].sweep_orbit_y = 0.0f;
    flow->primary_waves[0].sweep_span_scale = 1.0f;
    flow->primary_waves[0].sweep_phase = 0u;
    flow->primary_waves[0].wander_enabled = 0u;
    flow->primary_waves[0].wander_speed = 0.0f;
    flow->primary_waves[0].wander_extent_x = 0.0f;
    flow->primary_waves[0].wander_extent_y = 0.0f;
    flow->primary_waves[0].wander_curve = 0.0f;
    flow->primary_waves[0].wander_visibility_min = 1.0f;
    flow->primary_waves[0].wander_seed = 1979u;
    flow->primary_waves[0].snake_enabled = 0u;
    flow->primary_waves[0].snake_speed = 0.0f;
    flow->primary_waves[0].snake_extent_x = 0.0f;
    flow->primary_waves[0].snake_extent_y = 0.0f;
    flow->primary_waves[0].snake_trail_frames = 4.0f;
    flow->primary_waves[0].snake_visibility_min = 1.0f;
    flow->primary_waves[0].snake_seed = 7411u;
    flow->primary_waves[1].start_y = 132.0f;
    flow->primary_waves[1].end_y = 318.0f;
    flow->primary_waves[1].control1_y = 194.0f;
    flow->primary_waves[1].control2_y = 256.0f;
    flow->primary_waves[1].drift_y = 0.0f;
    flow->primary_waves[1].thickness_scale = 0.86f;
    flow->primary_waves[1].amplitude_scale = 0.72f;
    flow->primary_waves[1].alpha_scale = 0.72f;
    flow->primary_waves[1].phase_offset = 43u;
    flow->primary_waves[1].sweep_enabled = 0u;
    flow->primary_waves[1].sweep_speed = 0.0f;
    flow->primary_waves[1].sweep_orbit_x = 0.0f;
    flow->primary_waves[1].sweep_orbit_y = 0.0f;
    flow->primary_waves[1].sweep_span_scale = 1.0f;
    flow->primary_waves[1].sweep_phase = 0u;
    flow->primary_waves[1].wander_enabled = 0u;
    flow->primary_waves[1].wander_speed = 0.0f;
    flow->primary_waves[1].wander_extent_x = 0.0f;
    flow->primary_waves[1].wander_extent_y = 0.0f;
    flow->primary_waves[1].wander_curve = 0.0f;
    flow->primary_waves[1].wander_visibility_min = 1.0f;
    flow->primary_waves[1].wander_seed = 2956u;
    flow->primary_waves[1].snake_enabled = 0u;
    flow->primary_waves[1].snake_speed = 0.0f;
    flow->primary_waves[1].snake_extent_x = 0.0f;
    flow->primary_waves[1].snake_extent_y = 0.0f;
    flow->primary_waves[1].snake_trail_frames = 4.0f;
    flow->primary_waves[1].snake_visibility_min = 1.0f;
    flow->primary_waves[1].snake_seed = 17411u;
    flow->primary_waves[2].start_y = 338.0f;
    flow->primary_waves[2].end_y = 166.0f;
    flow->primary_waves[2].control1_y = 281.0f;
    flow->primary_waves[2].control2_y = 223.0f;
    flow->primary_waves[2].drift_y = 0.0f;
    flow->primary_waves[2].thickness_scale = 0.72f;
    flow->primary_waves[2].amplitude_scale = 0.58f;
    flow->primary_waves[2].alpha_scale = 0.58f;
    flow->primary_waves[2].phase_offset = 87u;
    flow->primary_waves[2].sweep_enabled = 0u;
    flow->primary_waves[2].sweep_speed = 0.0f;
    flow->primary_waves[2].sweep_orbit_x = 0.0f;
    flow->primary_waves[2].sweep_orbit_y = 0.0f;
    flow->primary_waves[2].sweep_span_scale = 1.0f;
    flow->primary_waves[2].sweep_phase = 0u;
    flow->primary_waves[2].wander_enabled = 0u;
    flow->primary_waves[2].wander_speed = 0.0f;
    flow->primary_waves[2].wander_extent_x = 0.0f;
    flow->primary_waves[2].wander_extent_y = 0.0f;
    flow->primary_waves[2].wander_curve = 0.0f;
    flow->primary_waves[2].wander_visibility_min = 1.0f;
    flow->primary_waves[2].wander_seed = 3933u;
    flow->primary_waves[2].snake_enabled = 0u;
    flow->primary_waves[2].snake_speed = 0.0f;
    flow->primary_waves[2].snake_extent_x = 0.0f;
    flow->primary_waves[2].snake_extent_y = 0.0f;
    flow->primary_waves[2].snake_trail_frames = 4.0f;
    flow->primary_waves[2].snake_visibility_min = 1.0f;
    flow->primary_waves[2].snake_seed = 27411u;
    {
        unsigned int ribbon_index;
        for (ribbon_index = 0u; ribbon_index < TITAN_THEME_MAX_PRIMARY_WAVES; ribbon_index++)
        {
            TitanThemePrimaryWave *ribbon = &flow->primary_waves[ribbon_index];
            ribbon->reference_ribbon_enabled = 0u;
            ribbon->ribbon_speed = 0.065f;
            ribbon->ribbon_center_y = 190.0f;
            ribbon->ribbon_thickness = 178.0f;
            ribbon->ribbon_fold_amplitude = 24.0f;
            ribbon->ribbon_fold_frequency = 92.0f;
            ribbon->ribbon_twist_amplitude = 62.0f;
            ribbon->ribbon_span_scale = 1.30f;
            ribbon->ribbon_drift_x = 28.0f;
            ribbon->ribbon_drift_y = 18.0f;
            ribbon->ribbon_seed = 7411u + ribbon_index * 10000u;
        }
    }
    flow->atmosphere_alpha = 1u;
}

static void TitanTheme_DefaultScene(TitanThemeSceneStyle *scene)
{
    memset(scene, 0, sizeof(*scene));
    scene->home_idle_frame_ms = 120u;
    scene->top_idle_frame_ms = 120u;
    scene->home_idle_mode = TITAN_THEME_SCENE_IDLE_LOOP;
    scene->top_idle_mode = TITAN_THEME_SCENE_IDLE_LOOP;
    scene->system_flow_enabled = 1u;
    scene->home_title_visible = 1u;
}

static void TitanTheme_DefaultAudio(TitanThemeAudioStyle *audio)
{
    memset(audio, 0, sizeof(*audio));
}

static void TitanTheme_DefaultIcons(TitanThemeIconStyle *icons)
{
    memset(icons, 0, sizeof(*icons));
    icons->tint.red = 255u;
    icons->tint.green = 255u;
    icons->tint.blue = 255u;
    icons->tint.alpha = 255u;
    icons->decorator_color.red = 255u;
    icons->decorator_color.green = 255u;
    icons->decorator_color.blue = 255u;
    icons->decorator_color.alpha = 160u;
}

static void TitanTheme_MakeDefault(TitanThemeInfo *theme)
{
    memset(theme, 0, sizeof(*theme));
    TitanTheme_CopyText(theme->id, sizeof(theme->id), "DEFAULT");
    TitanTheme_CopyText(theme->name, sizeof(theme->name), "Default");
    TitanTheme_CopyText(theme->author, sizeof(theme->author), "Project Titan");
    TitanTheme_CopyText(theme->version, sizeof(theme->version), "1.0");
    theme->shell_api = TITAN_THEME_SHELL_API_VERSION;
    theme->background_engine = TITAN_THEME_BACKGROUND_DEFAULT;
    theme->built_in = 1;
    TitanTheme_DefaultShell(&theme->shell);
    TitanTheme_DefaultFlow(&theme->flow);
    TitanTheme_DefaultScene(&theme->scene);
    memset(&theme->scene_v4, 0, sizeof(theme->scene_v4));
    theme->scene_v4.vram_budget = 512u * 1024u;
    theme->scene_v4.ee_ram_budget = 2u * 1024u * 1024u;
    theme->scene_v4.reserved_headroom = 256u * 1024u;
    TitanTheme_DefaultAudio(&theme->audio);
    TitanTheme_DefaultIcons(&theme->icons);
}

static void TitanTheme_MakeFlowVariant(
    TitanThemeInfo *theme,
    const char *id,
    const char *name,
    unsigned char bg_top_r, unsigned char bg_top_g, unsigned char bg_top_b,
    unsigned char bg_bottom_r, unsigned char bg_bottom_g, unsigned char bg_bottom_b,
    unsigned char wave_r, unsigned char wave_g, unsigned char wave_b, unsigned char wave_alpha,
    unsigned char hi_r, unsigned char hi_g, unsigned char hi_b, unsigned char hi_alpha,
    unsigned char particle_r, unsigned char particle_g, unsigned char particle_b, unsigned char particle_alpha,
    unsigned char accent_r, unsigned char accent_g, unsigned char accent_b,
    unsigned int snake_seed)
{
    TitanThemePrimaryWave *wave;
    memset(theme, 0, sizeof(*theme));
    TitanTheme_CopyText(theme->id, sizeof(theme->id), id);
    TitanTheme_CopyText(theme->name, sizeof(theme->name), name);
    TitanTheme_CopyText(theme->author, sizeof(theme->author), "Project Titan");
    TitanTheme_CopyText(theme->version, sizeof(theme->version), "8.0.1-hwfix2");
    theme->shell_api = 1u;
    theme->background_engine = TITAN_THEME_BACKGROUND_FLOW3D;
    theme->built_in = 1;
    TitanTheme_DefaultShell(&theme->shell);
    TitanTheme_DefaultFlow(&theme->flow);
    TitanTheme_DefaultScene(&theme->scene);
    memset(&theme->scene_v4, 0, sizeof(theme->scene_v4));
    theme->scene_v4.vram_budget = 512u * 1024u;
    theme->scene_v4.ee_ram_budget = 2u * 1024u * 1024u;
    theme->scene_v4.reserved_headroom = 256u * 1024u;
    TitanTheme_DefaultAudio(&theme->audio);
    TitanTheme_DefaultIcons(&theme->icons);

    theme->flow.background_top.red=bg_top_r; theme->flow.background_top.green=bg_top_g; theme->flow.background_top.blue=bg_top_b; theme->flow.background_top.alpha=255u;
    theme->flow.background_bottom.red=bg_bottom_r; theme->flow.background_bottom.green=bg_bottom_g; theme->flow.background_bottom.blue=bg_bottom_b; theme->flow.background_bottom.alpha=255u;
    theme->flow.wave_base.red=wave_r; theme->flow.wave_base.green=wave_g; theme->flow.wave_base.blue=wave_b; theme->flow.wave_base.alpha=wave_alpha;
    theme->flow.wave_highlight.red=hi_r; theme->flow.wave_highlight.green=hi_g; theme->flow.wave_highlight.blue=hi_b; theme->flow.wave_highlight.alpha=hi_alpha;
    theme->flow.particles.red=particle_r; theme->flow.particles.green=particle_g; theme->flow.particles.blue=particle_b; theme->flow.particles.alpha=particle_alpha;
    theme->flow.primary_wave_count=1u;
    wave=&theme->flow.primary_waves[0];
    memset(wave,0,sizeof(*wave));
    /* Restored from the hardware-proven Network + Flow Themes RT1 baseline:
     * broad head-driven Snake Flow on EE, fine living deformation on VU1. */
    wave->start_y=250.0f; wave->control1_y=180.0f; wave->control2_y=300.0f; wave->end_y=250.0f;
    wave->drift_y=4.5f; wave->thickness_scale=1.54f; wave->amplitude_scale=0.43f; wave->alpha_scale=1.0f;
    wave->sweep_enabled=0u; wave->wander_enabled=0u; wave->snake_enabled=1u;
    wave->snake_speed=0.002f; wave->snake_extent_x=520.0f; wave->snake_extent_y=320.0f;
    wave->snake_trail_frames=4.5f; wave->snake_visibility_min=0.38f; wave->snake_seed=snake_seed;
    wave->reference_ribbon_enabled=0u; wave->sweep_span_scale=1.45f;
    theme->flow.speed=0.38f; theme->flow.frequency=0.62f; theme->flow.amplitude=0.56f; theme->flow.noise=0.072f;
    theme->flow.fresnel=1.04f; theme->flow.horizontal_boost=1.08f; theme->flow.vertical_shift=6.0f;
    theme->flow.day_frequency=0.9f; theme->flow.night_frequency=0.82f; theme->flow.day_speed=0.82f; theme->flow.night_speed=0.7f;
    theme->flow.vu1_deform_speed=0.72f; theme->flow.vu1_deform_frequency=1.5f; theme->flow.vu1_deform_amplitude=7.2f;
    theme->flow.atmosphere_alpha=0u;
    theme->shell.accent.red=accent_r; theme->shell.accent.green=accent_g; theme->shell.accent.blue=accent_b; theme->shell.accent.alpha=255u;
    theme->shell.horizontal_motion_scale=1.02f; theme->shell.vertical_motion_scale=0.98f;
}

static void TitanTheme_AddBuiltIns(void)
{
    TitanTheme_MakeDefault(&g_themes[0]);
    TitanTheme_MakeFlowVariant(&g_themes[1],"SCARLETFLOW","Ribbon Scarlet",0,0,0,0,0,0,148,14,25,34,235,70,82,62,190,28,42,28,196,28,42,52391u);
    TitanTheme_MakeFlowVariant(&g_themes[2],"FLOWWINE","Ribbon Vinho",32,4,14,68,8,30,226,82,132,46,255,188,216,78,238,112,158,24,232,86,139,51391u);
    TitanTheme_MakeFlowVariant(&g_themes[3],"FLOWCYAN","Ribbon Ciano",50,0,44,95,0,72,20,188,210,46,132,246,255,78,44,208,226,24,30,202,220,62117u);
    TitanTheme_MakeFlowVariant(&g_themes[4],"FLOWTWILIGHT","Ribbon Crepusculo",14,6,28,38,10,56,190,48,220,46,238,176,255,78,212,86,236,24,202,72,226,47831u);
    g_theme_count=5u;
}

static int TitanTheme_IsBuiltInId(const char *id)
{
    unsigned int i;
    if(id==0)return 0;
    for(i=0u;i<5u;i++) if(strcmp(g_themes[i].id,id)==0)return 1;
    return 0;
}

static int TitanTheme_RemoveTree(const char *path)
{
    TitanStorageIoEntry entries[8];
    unsigned int start=0u;
    int n;
    for(;;){
        int i;
        n=TitanStorageIo_ListDirectory(path,start,entries,8u);
        if(n<0)return 0;
        if(n==0)break;
        for(i=0;i<n;i++){
            char child[TITAN_THEME_PATH_CAPACITY];
            if(!TitanTheme_PathJoin(child,sizeof(child),path,entries[i].name))return 0;
            if(entries[i].flags&TITAN_STORAGE_IO_ENTRY_DIRECTORY){ if(!TitanTheme_RemoveTree(child))return 0; }
            else if(TitanStorageIo_Remove(child)<0)return 0;
        }
        /* Removing entries shifts the logical list, so always restart at zero. */
        start=0u;
    }
    return TitanStorageIo_RemoveAny(path)==0;
}

static int TitanTheme_PathJoin(
    char *destination,
    unsigned int capacity,
    const char *root,
    const char *name
)
{
    int written;
    if (destination == 0 || capacity == 0u || root == 0 || name == 0)
        return 0;
    written = snprintf(destination, capacity, "%s/%s", root, name);
    return written >= 0 && (unsigned int)written < capacity;
}

static void TitanTheme_SetError(
    char *error,
    unsigned int capacity,
    const char *message
)
{
    TitanTheme_CopyText(error, capacity, message);
}

static int TitanTheme_ParsePrimaryWaveKey(
    TitanThemeFlowStyle *flow,
    const char *key,
    const char *value
)
{
    const char prefix[] = "primary_wave";
    const char *suffix;
    unsigned int index;
    unsigned int fallback_milli;

    if (flow == 0 || key == 0 || value == 0 ||
        strncmp(key, prefix, sizeof(prefix) - 1u) != 0)
        return 0;

    suffix = key + sizeof(prefix) - 1u;
    if (suffix[0] < '1' || suffix[0] > '3' || suffix[1] != '_')
        return 0;
    index = (unsigned int)(suffix[0] - '1');
    suffix += 2;

    if (strcmp(suffix, "start_y_milli") == 0)
    {
        fallback_milli = (unsigned int)(flow->primary_waves[index].start_y * 1000.0f);
        flow->primary_waves[index].start_y =
            (float)TitanTheme_ParseUnsigned(value, 0u, 448000u, fallback_milli) / 1000.0f;
    }
    else if (strcmp(suffix, "end_y_milli") == 0)
    {
        fallback_milli = (unsigned int)(flow->primary_waves[index].end_y * 1000.0f);
        flow->primary_waves[index].end_y =
            (float)TitanTheme_ParseUnsigned(value, 0u, 448000u, fallback_milli) / 1000.0f;
    }
    else if (strcmp(suffix, "control1_y_milli") == 0)
    {
        fallback_milli = (unsigned int)(flow->primary_waves[index].control1_y * 1000.0f);
        flow->primary_waves[index].control1_y =
            (float)TitanTheme_ParseUnsigned(value, 0u, 448000u, fallback_milli) / 1000.0f;
    }
    else if (strcmp(suffix, "control2_y_milli") == 0)
    {
        fallback_milli = (unsigned int)(flow->primary_waves[index].control2_y * 1000.0f);
        flow->primary_waves[index].control2_y =
            (float)TitanTheme_ParseUnsigned(value, 0u, 448000u, fallback_milli) / 1000.0f;
    }
    else if (strcmp(suffix, "drift_y_milli") == 0)
    {
        fallback_milli = (unsigned int)(flow->primary_waves[index].drift_y * 1000.0f);
        flow->primary_waves[index].drift_y =
            (float)TitanTheme_ParseUnsigned(value, 0u, 80000u, fallback_milli) / 1000.0f;
    }
    else if (strcmp(suffix, "thickness_milli") == 0)
    {
        fallback_milli = (unsigned int)(flow->primary_waves[index].thickness_scale * 1000.0f);
        flow->primary_waves[index].thickness_scale = TitanTheme_MilliToFloat(
            TitanTheme_ParseUnsigned(value, 350u, 1800u, fallback_milli)
        );
    }
    else if (strcmp(suffix, "amplitude_milli") == 0)
    {
        fallback_milli = (unsigned int)(flow->primary_waves[index].amplitude_scale * 1000.0f);
        flow->primary_waves[index].amplitude_scale = TitanTheme_MilliToFloat(
            TitanTheme_ParseUnsigned(value, 200u, 2000u, fallback_milli)
        );
    }
    else if (strcmp(suffix, "alpha_milli") == 0)
    {
        fallback_milli = (unsigned int)(flow->primary_waves[index].alpha_scale * 1000.0f);
        flow->primary_waves[index].alpha_scale = TitanTheme_MilliToFloat(
            TitanTheme_ParseUnsigned(value, 0u, 1500u, fallback_milli)
        );
    }
    else if (strcmp(suffix, "sweep") == 0)
    {
        flow->primary_waves[index].sweep_enabled = TitanTheme_ParseUnsigned(
            value, 0u, 1u, flow->primary_waves[index].sweep_enabled
        );
    }
    else if (strcmp(suffix, "sweep_speed_milli") == 0)
    {
        fallback_milli = (unsigned int)(flow->primary_waves[index].sweep_speed * 1000.0f);
        flow->primary_waves[index].sweep_speed = TitanTheme_MilliToFloat(
            TitanTheme_ParseUnsigned(value, 1u, 250u, fallback_milli != 0u ? fallback_milli : 22u)
        );
    }
    else if (strcmp(suffix, "sweep_orbit_x_milli") == 0)
    {
        fallback_milli = (unsigned int)(flow->primary_waves[index].sweep_orbit_x * 1000.0f);
        flow->primary_waves[index].sweep_orbit_x = (float)TitanTheme_ParseUnsigned(
            value, 0u, 600000u, fallback_milli
        ) / 1000.0f;
    }
    else if (strcmp(suffix, "sweep_orbit_y_milli") == 0)
    {
        fallback_milli = (unsigned int)(flow->primary_waves[index].sweep_orbit_y * 1000.0f);
        flow->primary_waves[index].sweep_orbit_y = (float)TitanTheme_ParseUnsigned(
            value, 0u, 400000u, fallback_milli
        ) / 1000.0f;
    }
    else if (strcmp(suffix, "sweep_span_milli") == 0)
    {
        fallback_milli = (unsigned int)(flow->primary_waves[index].sweep_span_scale * 1000.0f);
        flow->primary_waves[index].sweep_span_scale = TitanTheme_MilliToFloat(
            TitanTheme_ParseUnsigned(value, 700u, 1800u, fallback_milli != 0u ? fallback_milli : 1000u)
        );
    }
    else if (strcmp(suffix, "sweep_phase") == 0)
    {
        flow->primary_waves[index].sweep_phase = TitanTheme_ParseUnsigned(
            value, 0u, 127u, flow->primary_waves[index].sweep_phase
        );
    }
    else if (strcmp(suffix, "wander") == 0)
    {
        flow->primary_waves[index].wander_enabled = TitanTheme_ParseUnsigned(
            value, 0u, 1u, flow->primary_waves[index].wander_enabled
        );
    }
    else if (strcmp(suffix, "wander_speed_milli") == 0)
    {
        fallback_milli = (unsigned int)(flow->primary_waves[index].wander_speed * 1000.0f);
        flow->primary_waves[index].wander_speed = TitanTheme_MilliToFloat(
            TitanTheme_ParseUnsigned(value, 1u, 20u, fallback_milli != 0u ? fallback_milli : 2u)
        );
    }
    else if (strcmp(suffix, "wander_extent_x_milli") == 0)
    {
        fallback_milli = (unsigned int)(flow->primary_waves[index].wander_extent_x * 1000.0f);
        flow->primary_waves[index].wander_extent_x = (float)TitanTheme_ParseUnsigned(
            value, 0u, 800000u, fallback_milli
        ) / 1000.0f;
    }
    else if (strcmp(suffix, "wander_extent_y_milli") == 0)
    {
        fallback_milli = (unsigned int)(flow->primary_waves[index].wander_extent_y * 1000.0f);
        flow->primary_waves[index].wander_extent_y = (float)TitanTheme_ParseUnsigned(
            value, 0u, 600000u, fallback_milli
        ) / 1000.0f;
    }
    else if (strcmp(suffix, "wander_curve_milli") == 0)
    {
        fallback_milli = (unsigned int)(flow->primary_waves[index].wander_curve * 1000.0f);
        flow->primary_waves[index].wander_curve = (float)TitanTheme_ParseUnsigned(
            value, 0u, 240000u, fallback_milli
        ) / 1000.0f;
    }
    else if (strcmp(suffix, "wander_visibility_min_milli") == 0)
    {
        fallback_milli = (unsigned int)(flow->primary_waves[index].wander_visibility_min * 1000.0f);
        flow->primary_waves[index].wander_visibility_min = TitanTheme_MilliToFloat(
            TitanTheme_ParseUnsigned(value, 0u, 1000u, fallback_milli)
        );
    }
    else if (strcmp(suffix, "wander_seed") == 0)
    {
        flow->primary_waves[index].wander_seed = TitanTheme_ParseUnsigned(
            value, 0u, 65535u, flow->primary_waves[index].wander_seed
        );
    }
    else if (strcmp(suffix, "snake") == 0)
    {
        flow->primary_waves[index].snake_enabled = TitanTheme_ParseUnsigned(
            value, 0u, 1u, flow->primary_waves[index].snake_enabled
        );
    }
    else if (strcmp(suffix, "snake_speed_milli") == 0)
    {
        fallback_milli = (unsigned int)(flow->primary_waves[index].snake_speed * 1000.0f);
        flow->primary_waves[index].snake_speed = TitanTheme_MilliToFloat(
            TitanTheme_ParseUnsigned(value, 1u, 20u, fallback_milli != 0u ? fallback_milli : 2u)
        );
    }
    else if (strcmp(suffix, "snake_extent_x_milli") == 0)
    {
        fallback_milli = (unsigned int)(flow->primary_waves[index].snake_extent_x * 1000.0f);
        flow->primary_waves[index].snake_extent_x = (float)TitanTheme_ParseUnsigned(
            value, 0u, 900000u, fallback_milli
        ) / 1000.0f;
    }
    else if (strcmp(suffix, "snake_extent_y_milli") == 0)
    {
        fallback_milli = (unsigned int)(flow->primary_waves[index].snake_extent_y * 1000.0f);
        flow->primary_waves[index].snake_extent_y = (float)TitanTheme_ParseUnsigned(
            value, 0u, 700000u, fallback_milli
        ) / 1000.0f;
    }
    else if (strcmp(suffix, "snake_trail_frames_milli") == 0)
    {
        fallback_milli = (unsigned int)(flow->primary_waves[index].snake_trail_frames * 1000.0f);
        flow->primary_waves[index].snake_trail_frames = TitanTheme_MilliToFloat(
            TitanTheme_ParseUnsigned(value, 500u, 12000u, fallback_milli != 0u ? fallback_milli : 4000u)
        );
    }
    else if (strcmp(suffix, "snake_visibility_min_milli") == 0)
    {
        fallback_milli = (unsigned int)(flow->primary_waves[index].snake_visibility_min * 1000.0f);
        flow->primary_waves[index].snake_visibility_min = TitanTheme_MilliToFloat(
            TitanTheme_ParseUnsigned(value, 0u, 1000u, fallback_milli)
        );
    }
    else if (strcmp(suffix, "snake_seed") == 0)
    {
        flow->primary_waves[index].snake_seed = TitanTheme_ParseUnsigned(
            value, 0u, 65535u, flow->primary_waves[index].snake_seed
        );
    }
    else if (strcmp(suffix, "reference_ribbon") == 0)
    {
        flow->primary_waves[index].reference_ribbon_enabled = TitanTheme_ParseUnsigned(value, 0u, 1u, flow->primary_waves[index].reference_ribbon_enabled);
    }
    else if (strcmp(suffix, "ribbon_speed_milli") == 0)
    {
        fallback_milli = (unsigned int)(flow->primary_waves[index].ribbon_speed * 1000.0f);
        flow->primary_waves[index].ribbon_speed = TitanTheme_MilliToFloat(TitanTheme_ParseUnsigned(value, 5u, 500u, fallback_milli != 0u ? fallback_milli : 60u));
    }
    else if (strcmp(suffix, "ribbon_center_y_milli") == 0)
    {
        fallback_milli = (unsigned int)(flow->primary_waves[index].ribbon_center_y * 1000.0f);
        flow->primary_waves[index].ribbon_center_y = (float)TitanTheme_ParseUnsigned(value, 0u, 448000u, fallback_milli) / 1000.0f;
    }
    else if (strcmp(suffix, "ribbon_thickness_milli") == 0)
    {
        fallback_milli = (unsigned int)(flow->primary_waves[index].ribbon_thickness * 1000.0f);
        flow->primary_waves[index].ribbon_thickness = (float)TitanTheme_ParseUnsigned(value, 40000u, 420000u, fallback_milli != 0u ? fallback_milli : 330000u) / 1000.0f;
    }
    else if (strcmp(suffix, "ribbon_fold_amplitude_milli") == 0)
    {
        fallback_milli = (unsigned int)(flow->primary_waves[index].ribbon_fold_amplitude * 1000.0f);
        flow->primary_waves[index].ribbon_fold_amplitude = (float)TitanTheme_ParseUnsigned(value, 0u, 120000u, fallback_milli) / 1000.0f;
    }
    else if (strcmp(suffix, "ribbon_fold_frequency_milli") == 0)
    {
        fallback_milli = (unsigned int)(flow->primary_waves[index].ribbon_fold_frequency * 1000.0f);
        flow->primary_waves[index].ribbon_fold_frequency = TitanTheme_MilliToFloat(TitanTheme_ParseUnsigned(value, 16000u, 256000u, fallback_milli != 0u ? fallback_milli : 82000u));
    }
    else if (strcmp(suffix, "ribbon_twist_amplitude_milli") == 0)
    {
        fallback_milli = (unsigned int)(flow->primary_waves[index].ribbon_twist_amplitude * 1000.0f);
        flow->primary_waves[index].ribbon_twist_amplitude = (float)TitanTheme_ParseUnsigned(value, 0u, 300000u, fallback_milli) / 1000.0f;
    }
    else if (strcmp(suffix, "ribbon_span_milli") == 0)
    {
        fallback_milli = (unsigned int)(flow->primary_waves[index].ribbon_span_scale * 1000.0f);
        flow->primary_waves[index].ribbon_span_scale = TitanTheme_MilliToFloat(TitanTheme_ParseUnsigned(value, 1000u, 1800u, fallback_milli != 0u ? fallback_milli : 1450u));
    }
    else if (strcmp(suffix, "ribbon_drift_x_milli") == 0)
    {
        fallback_milli = (unsigned int)(flow->primary_waves[index].ribbon_drift_x * 1000.0f);
        flow->primary_waves[index].ribbon_drift_x = (float)TitanTheme_ParseUnsigned(value, 0u, 160000u, fallback_milli) / 1000.0f;
    }
    else if (strcmp(suffix, "ribbon_drift_y_milli") == 0)
    {
        fallback_milli = (unsigned int)(flow->primary_waves[index].ribbon_drift_y * 1000.0f);
        flow->primary_waves[index].ribbon_drift_y = (float)TitanTheme_ParseUnsigned(value, 0u, 120000u, fallback_milli) / 1000.0f;
    }
    else if (strcmp(suffix, "ribbon_seed") == 0)
    {
        flow->primary_waves[index].ribbon_seed = TitanTheme_ParseUnsigned(value, 0u, 65535u, flow->primary_waves[index].ribbon_seed);
    }
    else if (strcmp(suffix, "phase") == 0)
    {
        flow->primary_waves[index].phase_offset = TitanTheme_ParseUnsigned(
            value, 0u, 127u, flow->primary_waves[index].phase_offset
        );
    }
    else
    {
        return 0;
    }
    return 1;
}

static int TitanTheme_ParseConfig(
    const char *root_path,
    TitanThemeInfo *theme,
    char *error,
    unsigned int error_capacity
)
{
    char path[TITAN_THEME_PATH_CAPACITY];
    char line[TITAN_THEME_LINE_CAPACITY];
    FILE *file;
    unsigned int format = 0u;
    int saw_id = 0;
    int saw_name = 0;
    int saw_engine = 0;

    if (theme == 0 || !TitanTheme_PathJoin(path, sizeof(path), root_path, TITAN_THEME_CONFIG_NAME))
    {
        TitanTheme_SetError(error, error_capacity, "Caminho de tema invalido");
        return 0;
    }

    file = fopen(path, "rb");
    if (file == 0)
    {
        TitanTheme_SetError(error, error_capacity, "theme.cfg ausente");
        return 0;
    }

    memset(theme, 0, sizeof(*theme));
    TitanTheme_DefaultShell(&theme->shell);
    TitanTheme_DefaultFlow(&theme->flow);
    TitanTheme_DefaultScene(&theme->scene);
    memset(&theme->scene_v4, 0, sizeof(theme->scene_v4));
    theme->scene_v4.vram_budget = 512u * 1024u;
    theme->scene_v4.ee_ram_budget = 2u * 1024u * 1024u;
    theme->scene_v4.reserved_headroom = 256u * 1024u;
    TitanTheme_DefaultAudio(&theme->audio);
    TitanTheme_DefaultIcons(&theme->icons);
    theme->background_engine = TITAN_THEME_BACKGROUND_DEFAULT;
    theme->shell_api = TITAN_THEME_SHELL_API_VERSION;
    TitanTheme_CopyText(theme->path, sizeof(theme->path), root_path);

    while (fgets(line, sizeof(line), file) != 0)
    {
        char *equals;
        char *newline;
        char *key;
        char *value;

        newline = strpbrk(line, "\r\n");
        if (newline != 0)
            *newline = '\0';
        if (line[0] == '\0' || line[0] == '#')
            continue;
        equals = strchr(line, '=');
        if (equals == 0)
            continue;
        *equals = '\0';
        key = line;
        value = equals + 1;

        if (strcmp(key, "format") == 0)
            format = TitanTheme_ParseUnsigned(value, 1u, 1u, 0u);
        else if (strcmp(key, "id") == 0)
        {
            if (!TitanTheme_IdSafe(value))
            {
                fclose(file);
                TitanTheme_SetError(error, error_capacity, "ID de tema invalido");
                return 0;
            }
            TitanTheme_CopyText(theme->id, sizeof(theme->id), value);
            saw_id = 1;
        }
        else if (strcmp(key, "name") == 0)
        {
            if (!TitanTheme_TextSafe(value, TITAN_THEME_NAME_CAPACITY))
            {
                fclose(file);
                TitanTheme_SetError(error, error_capacity, "Nome de tema invalido");
                return 0;
            }
            TitanTheme_CopyText(theme->name, sizeof(theme->name), value);
            saw_name = 1;
        }
        else if (strcmp(key, "author") == 0)
        {
            if (value[0] != '\0' && !TitanTheme_TextSafe(value, TITAN_THEME_AUTHOR_CAPACITY))
            {
                fclose(file);
                TitanTheme_SetError(error, error_capacity, "Autor de tema invalido");
                return 0;
            }
            TitanTheme_CopyText(theme->author, sizeof(theme->author), value);
        }
        else if (strcmp(key, "version") == 0)
        {
            if (!TitanTheme_TextSafe(value, TITAN_THEME_VERSION_CAPACITY))
            {
                fclose(file);
                TitanTheme_SetError(error, error_capacity, "Versao de tema invalida");
                return 0;
            }
            TitanTheme_CopyText(theme->version, sizeof(theme->version), value);
        }
        else if (strcmp(key, "shell_api") == 0)
            theme->shell_api = TitanTheme_ParseUnsigned(value, 1u, TITAN_THEME_SHELL_API_VERSION, 0u);
        else if (strcmp(key, "background_engine") == 0)
        {
            if (strcmp(value, "ribbon3d-v2") == 0 ||
                strcmp(value, "ribbon3d-v1") == 0)
                theme->background_engine = TITAN_THEME_BACKGROUND_RIBBON3D;
            else if (strcmp(value, "flow3d-v1") == 0)
                theme->background_engine = TITAN_THEME_BACKGROUND_FLOW3D;
            else if (strcmp(value, "scene-v2") == 0)
                theme->background_engine = TITAN_THEME_BACKGROUND_SCENE_V2;
            else if (strcmp(value, "scene-v4") == 0)
                theme->background_engine = TITAN_THEME_BACKGROUND_SCENE_V4;
            else if (strcmp(value, "default") == 0)
                theme->background_engine = TITAN_THEME_BACKGROUND_DEFAULT;
            else
            {
                fclose(file);
                TitanTheme_SetError(error, error_capacity, "Background engine nao suportado");
                return 0;
            }
            saw_engine = 1;
        }
        else if (strcmp(key, "bg_top_r") == 0) theme->flow.background_top.red = (unsigned char)TitanTheme_ParseUnsigned(value, 0u, 255u, theme->flow.background_top.red);
        else if (strcmp(key, "bg_top_g") == 0) theme->flow.background_top.green = (unsigned char)TitanTheme_ParseUnsigned(value, 0u, 255u, theme->flow.background_top.green);
        else if (strcmp(key, "bg_top_b") == 0) theme->flow.background_top.blue = (unsigned char)TitanTheme_ParseUnsigned(value, 0u, 255u, theme->flow.background_top.blue);
        else if (strcmp(key, "bg_bottom_r") == 0) theme->flow.background_bottom.red = (unsigned char)TitanTheme_ParseUnsigned(value, 0u, 255u, theme->flow.background_bottom.red);
        else if (strcmp(key, "bg_bottom_g") == 0) theme->flow.background_bottom.green = (unsigned char)TitanTheme_ParseUnsigned(value, 0u, 255u, theme->flow.background_bottom.green);
        else if (strcmp(key, "bg_bottom_b") == 0) theme->flow.background_bottom.blue = (unsigned char)TitanTheme_ParseUnsigned(value, 0u, 255u, theme->flow.background_bottom.blue);
        else if (strcmp(key, "primary_wave_count") == 0) theme->flow.primary_wave_count = TitanTheme_ParseUnsigned(value, 1u, TITAN_THEME_MAX_PRIMARY_WAVES, theme->flow.primary_wave_count);
        else if (TitanTheme_ParsePrimaryWaveKey(&theme->flow, key, value)) { }
        else if (strcmp(key, "wave_r") == 0) theme->flow.wave_base.red = (unsigned char)TitanTheme_ParseUnsigned(value, 0u, 255u, theme->flow.wave_base.red);
        else if (strcmp(key, "wave_g") == 0) theme->flow.wave_base.green = (unsigned char)TitanTheme_ParseUnsigned(value, 0u, 255u, theme->flow.wave_base.green);
        else if (strcmp(key, "wave_b") == 0) theme->flow.wave_base.blue = (unsigned char)TitanTheme_ParseUnsigned(value, 0u, 255u, theme->flow.wave_base.blue);
        else if (strcmp(key, "wave_alpha") == 0) theme->flow.wave_base.alpha = (unsigned char)TitanTheme_ParseUnsigned(value, 0u, 255u, theme->flow.wave_base.alpha);
        else if (strcmp(key, "highlight_r") == 0) theme->flow.wave_highlight.red = (unsigned char)TitanTheme_ParseUnsigned(value, 0u, 255u, theme->flow.wave_highlight.red);
        else if (strcmp(key, "highlight_g") == 0) theme->flow.wave_highlight.green = (unsigned char)TitanTheme_ParseUnsigned(value, 0u, 255u, theme->flow.wave_highlight.green);
        else if (strcmp(key, "highlight_b") == 0) theme->flow.wave_highlight.blue = (unsigned char)TitanTheme_ParseUnsigned(value, 0u, 255u, theme->flow.wave_highlight.blue);
        else if (strcmp(key, "highlight_alpha") == 0) theme->flow.wave_highlight.alpha = (unsigned char)TitanTheme_ParseUnsigned(value, 0u, 255u, theme->flow.wave_highlight.alpha);
        else if (strcmp(key, "particle_r") == 0) theme->flow.particles.red = (unsigned char)TitanTheme_ParseUnsigned(value, 0u, 255u, theme->flow.particles.red);
        else if (strcmp(key, "particle_g") == 0) theme->flow.particles.green = (unsigned char)TitanTheme_ParseUnsigned(value, 0u, 255u, theme->flow.particles.green);
        else if (strcmp(key, "particle_b") == 0) theme->flow.particles.blue = (unsigned char)TitanTheme_ParseUnsigned(value, 0u, 255u, theme->flow.particles.blue);
        else if (strcmp(key, "particle_alpha") == 0) theme->flow.particles.alpha = (unsigned char)TitanTheme_ParseUnsigned(value, 0u, 255u, theme->flow.particles.alpha);
        else if (strcmp(key, "flow_speed_milli") == 0) theme->flow.speed = TitanTheme_MilliToFloat(TitanTheme_ParseUnsigned(value, 350u, 3000u, 1000u));
        else if (strcmp(key, "flow_frequency_milli") == 0) theme->flow.frequency = TitanTheme_MilliToFloat(TitanTheme_ParseUnsigned(value, 500u, 2000u, 1000u));
        else if (strcmp(key, "flow_amplitude_milli") == 0) theme->flow.amplitude = TitanTheme_MilliToFloat(TitanTheme_ParseUnsigned(value, 350u, 1800u, 1000u));
        else if (strcmp(key, "flow_noise_milli") == 0) theme->flow.noise = TitanTheme_MilliToFloat(TitanTheme_ParseUnsigned(value, 0u, 1500u, 1000u));
        else if (strcmp(key, "flow_fresnel_milli") == 0) theme->flow.fresnel = TitanTheme_MilliToFloat(TitanTheme_ParseUnsigned(value, 250u, 2000u, 1000u));
        else if (strcmp(key, "horizontal_boost_milli") == 0) theme->flow.horizontal_boost = TitanTheme_MilliToFloat(TitanTheme_ParseUnsigned(value, 1000u, 4000u, 1700u));
        else if (strcmp(key, "vertical_shift_milli") == 0) theme->flow.vertical_shift = (float)TitanTheme_ParseSigned(value, 0, 60000, 18000) / 1000.0f;
        else if (strcmp(key, "day_frequency_milli") == 0) theme->flow.day_frequency = TitanTheme_MilliToFloat(TitanTheme_ParseUnsigned(value, 500u, 1800u, 1000u));
        else if (strcmp(key, "night_frequency_milli") == 0) theme->flow.night_frequency = TitanTheme_MilliToFloat(TitanTheme_ParseUnsigned(value, 500u, 1800u, 1000u));
        else if (strcmp(key, "day_speed_milli") == 0) theme->flow.day_speed = TitanTheme_MilliToFloat(TitanTheme_ParseUnsigned(value, 400u, 1800u, 1000u));
        else if (strcmp(key, "night_speed_milli") == 0) theme->flow.night_speed = TitanTheme_MilliToFloat(TitanTheme_ParseUnsigned(value, 400u, 1800u, 1000u));
        else if (strcmp(key, "vu1_deform_speed_milli") == 0) theme->flow.vu1_deform_speed = TitanTheme_MilliToFloat(TitanTheme_ParseUnsigned(value, 0u, 6000u, 0u));
        else if (strcmp(key, "vu1_deform_frequency_milli") == 0) theme->flow.vu1_deform_frequency = TitanTheme_MilliToFloat(TitanTheme_ParseUnsigned(value, 250u, 8000u, 2000u));
        else if (strcmp(key, "vu1_deform_amplitude_milli") == 0) theme->flow.vu1_deform_amplitude = TitanTheme_MilliToFloat(TitanTheme_ParseUnsigned(value, 0u, 36000u, 0u));
        else if (strcmp(key, "atmosphere_alpha") == 0) theme->flow.atmosphere_alpha = (unsigned char)TitanTheme_ParseUnsigned(value, 0u, 255u, theme->flow.atmosphere_alpha);
        else if (strcmp(key, "shell_accent_r") == 0) theme->shell.accent.red = (unsigned char)TitanTheme_ParseUnsigned(value, 0u, 255u, theme->shell.accent.red);
        else if (strcmp(key, "shell_accent_g") == 0) theme->shell.accent.green = (unsigned char)TitanTheme_ParseUnsigned(value, 0u, 255u, theme->shell.accent.green);
        else if (strcmp(key, "shell_accent_b") == 0) theme->shell.accent.blue = (unsigned char)TitanTheme_ParseUnsigned(value, 0u, 255u, theme->shell.accent.blue);
        else if (strcmp(key, "scene_home") == 0)
        {
            if (!TitanTheme_CopyAssetPath(theme->scene.home_frame, value)) { fclose(file); TitanTheme_SetError(error, error_capacity, "scene_home invalido"); return 0; }
        }
        else if (strcmp(key, "scene_top") == 0)
        {
            if (!TitanTheme_CopyAssetPath(theme->scene.top_frame, value)) { fclose(file); TitanTheme_SetError(error, error_capacity, "scene_top invalido"); return 0; }
        }
        else if (strncmp(key, "scene_transition_", 17u) == 0 && key[17] >= '0' && key[17] <= '5' && key[18] == '\0')
        {
            unsigned int frame_index = (unsigned int)(key[17] - '0');
            if (!TitanTheme_CopyAssetPath(theme->scene.transition_frames[frame_index], value)) { fclose(file); TitanTheme_SetError(error, error_capacity, "Quadro de transicao invalido"); return 0; }
        }
        else if (strcmp(key, "scene_transition_count") == 0) theme->scene.transition_frame_count = TitanTheme_ParseUnsigned(value, 0u, TITAN_THEME_SCENE_TRANSITION_MAX, 0u);
        else if (strncmp(key, "scene_home_idle_", 16u) == 0 && key[16] >= '0' && key[16] <= '7' && key[17] == '\0')
        {
            unsigned int frame_index = (unsigned int)(key[16] - '0');
            if (!TitanTheme_CopyAssetPath(theme->scene.home_idle_frames[frame_index], value)) { fclose(file); TitanTheme_SetError(error, error_capacity, "Quadro idle HOME invalido"); return 0; }
        }
        else if (strncmp(key, "scene_top_idle_", 15u) == 0 && key[15] >= '0' && key[15] <= '7' && key[16] == '\0')
        {
            unsigned int frame_index = (unsigned int)(key[15] - '0');
            if (!TitanTheme_CopyAssetPath(theme->scene.top_idle_frames[frame_index], value)) { fclose(file); TitanTheme_SetError(error, error_capacity, "Quadro idle TOP invalido"); return 0; }
        }
        else if (strcmp(key, "scene_home_idle_count") == 0) theme->scene.home_idle_frame_count = TitanTheme_ParseUnsigned(value, 0u, TITAN_THEME_SCENE_IDLE_MAX, 0u);
        else if (strcmp(key, "scene_top_idle_count") == 0) theme->scene.top_idle_frame_count = TitanTheme_ParseUnsigned(value, 0u, TITAN_THEME_SCENE_IDLE_MAX, 0u);
        else if (strcmp(key, "scene_home_idle_ms") == 0) theme->scene.home_idle_frame_ms = TitanTheme_ParseUnsigned(value, TITAN_THEME_SCENE_IDLE_FRAME_MS_MIN, TITAN_THEME_SCENE_IDLE_FRAME_MS_MAX, 120u);
        else if (strcmp(key, "scene_top_idle_ms") == 0) theme->scene.top_idle_frame_ms = TitanTheme_ParseUnsigned(value, TITAN_THEME_SCENE_IDLE_FRAME_MS_MIN, TITAN_THEME_SCENE_IDLE_FRAME_MS_MAX, 120u);
        else if (strcmp(key, "scene_home_idle_mode") == 0)
        {
            if (strcmp(value, "loop") == 0) theme->scene.home_idle_mode = TITAN_THEME_SCENE_IDLE_LOOP;
            else if (strcmp(value, "pingpong") == 0) theme->scene.home_idle_mode = TITAN_THEME_SCENE_IDLE_PINGPONG;
            else { fclose(file); TitanTheme_SetError(error, error_capacity, "Modo idle HOME invalido"); return 0; }
        }
        else if (strcmp(key, "scene_top_idle_mode") == 0)
        {
            if (strcmp(value, "loop") == 0) theme->scene.top_idle_mode = TITAN_THEME_SCENE_IDLE_LOOP;
            else if (strcmp(value, "pingpong") == 0) theme->scene.top_idle_mode = TITAN_THEME_SCENE_IDLE_PINGPONG;
            else { fclose(file); TitanTheme_SetError(error, error_capacity, "Modo idle TOP invalido"); return 0; }
        }
        else if (strcmp(key, "scene_system_flow") == 0) theme->scene.system_flow_enabled = TitanTheme_ParseUnsigned(value, 0u, 1u, 1u);
        else if (strcmp(key, "scene_home_title") == 0) theme->scene.home_title_visible = TitanTheme_ParseUnsigned(value, 0u, 1u, 1u);
        else if (strcmp(key, "scene_v4_manifest") == 0) { if (!TitanTheme_CopyAssetPath(theme->scene_v4.manifest, value)) { fclose(file); TitanTheme_SetError(error, error_capacity, "scene_v4_manifest invalido"); return 0; } }
        else if (strcmp(key, "scene_v4_vram_budget") == 0) theme->scene_v4.vram_budget = TitanTheme_ParseUnsigned(value, 64u * 1024u, 1024u * 1024u, 512u * 1024u);
        else if (strcmp(key, "scene_v4_ee_ram_budget") == 0) theme->scene_v4.ee_ram_budget = TitanTheme_ParseUnsigned(value, 128u * 1024u, 8u * 1024u * 1024u, 2u * 1024u * 1024u);
        else if (strcmp(key, "scene_v4_reserved_headroom") == 0) theme->scene_v4.reserved_headroom = TitanTheme_ParseUnsigned(value, 128u * 1024u, 2u * 1024u * 1024u, 256u * 1024u);
        else if (strcmp(key, "audio_music") == 0) { if (!TitanTheme_CopyAssetPath(theme->audio.music, value)) { fclose(file); TitanTheme_SetError(error, error_capacity, "audio_music invalido"); return 0; } }
        else if (strcmp(key, "audio_music_home_only") == 0) theme->audio.music_home_only = TitanTheme_ParseUnsigned(value, 0u, 1u, 0u);
        else if (strcmp(key, "audio_notification") == 0) { if (!TitanTheme_CopyAssetPath(theme->audio.notification, value)) { fclose(file); TitanTheme_SetError(error, error_capacity, "audio_notification invalido"); return 0; } }
        else if (strcmp(key, "audio_profile_entry") == 0) { if (!TitanTheme_CopyAssetPath(theme->audio.profile_entry, value)) { fclose(file); TitanTheme_SetError(error, error_capacity, "audio_profile_entry invalido"); return 0; } }
        else if (strcmp(key, "audio_home_nav") == 0) { if (!TitanTheme_CopyAssetPath(theme->audio.home_nav, value)) { fclose(file); TitanTheme_SetError(error, error_capacity, "audio_home_nav invalido"); return 0; } }
        else if (strcmp(key, "audio_top_nav") == 0) { if (!TitanTheme_CopyAssetPath(theme->audio.top_nav, value)) { fclose(file); TitanTheme_SetError(error, error_capacity, "audio_top_nav invalido"); return 0; } }
        else if (strcmp(key, "audio_interface_nav") == 0) { if (!TitanTheme_CopyAssetPath(theme->audio.interface_nav, value)) { fclose(file); TitanTheme_SetError(error, error_capacity, "audio_interface_nav invalido"); return 0; } }
        else if (strcmp(key, "audio_top_enter") == 0) { if (!TitanTheme_CopyAssetPath(theme->audio.top_enter, value)) { fclose(file); TitanTheme_SetError(error, error_capacity, "audio_top_enter invalido"); return 0; } }
        else if (strcmp(key, "audio_shutdown") == 0) { if (!TitanTheme_CopyAssetPath(theme->audio.shutdown, value)) { fclose(file); TitanTheme_SetError(error, error_capacity, "audio_shutdown invalido"); return 0; } }
        else if (strcmp(key, "icon_tint_enabled") == 0) theme->icons.tint_enabled = TitanTheme_ParseUnsigned(value, 0u, 1u, 0u);
        else if (strcmp(key, "icon_tint_r") == 0) theme->icons.tint.red = (unsigned char)TitanTheme_ParseUnsigned(value, 0u, 255u, 255u);
        else if (strcmp(key, "icon_tint_g") == 0) theme->icons.tint.green = (unsigned char)TitanTheme_ParseUnsigned(value, 0u, 255u, 255u);
        else if (strcmp(key, "icon_tint_b") == 0) theme->icons.tint.blue = (unsigned char)TitanTheme_ParseUnsigned(value, 0u, 255u, 255u);
        else if (strcmp(key, "icon_decorator") == 0)
        {
            if (strcmp(value, "none") == 0) theme->icons.decorator = TITAN_THEME_ICON_DECORATOR_NONE;
            else if (strcmp(value, "ring") == 0) theme->icons.decorator = TITAN_THEME_ICON_DECORATOR_RING;
            else if (strcmp(value, "corners") == 0) theme->icons.decorator = TITAN_THEME_ICON_DECORATOR_CORNERS;
            else { fclose(file); TitanTheme_SetError(error, error_capacity, "Decorador de icone invalido"); return 0; }
        }
        else if (strcmp(key, "icon_decorator_r") == 0) theme->icons.decorator_color.red = (unsigned char)TitanTheme_ParseUnsigned(value, 0u, 255u, 255u);
        else if (strcmp(key, "icon_decorator_g") == 0) theme->icons.decorator_color.green = (unsigned char)TitanTheme_ParseUnsigned(value, 0u, 255u, 255u);
        else if (strcmp(key, "icon_decorator_b") == 0) theme->icons.decorator_color.blue = (unsigned char)TitanTheme_ParseUnsigned(value, 0u, 255u, 255u);
        else if (strcmp(key, "icon_decorator_alpha") == 0) theme->icons.decorator_color.alpha = (unsigned char)TitanTheme_ParseUnsigned(value, 0u, 255u, 160u);
        else if (strcmp(key, "shell_selected_scale_milli") == 0) theme->shell.home_selected_scale = TitanTheme_MilliToFloat(TitanTheme_ParseUnsigned(value, 850u, 1250u, 1000u));
        else if (strcmp(key, "shell_tile_spacing_milli") == 0) theme->shell.home_tile_spacing = TitanTheme_MilliToFloat(TitanTheme_ParseUnsigned(value, 800u, 1250u, 1000u));
        else if (strcmp(key, "shell_title_offset_y_milli") == 0) theme->shell.title_offset_y = (float)TitanTheme_ParseSigned(value, -20000, 20000, 0) / 1000.0f;
        else if (strcmp(key, "shell_header_alpha_milli") == 0) theme->shell.header_alpha = TitanTheme_MilliToFloat(TitanTheme_ParseUnsigned(value, 250u, 1000u, 1000u));
        else if (strcmp(key, "shell_help_alpha_milli") == 0) theme->shell.help_alpha = TitanTheme_MilliToFloat(TitanTheme_ParseUnsigned(value, 250u, 1000u, 1000u));
        else if (strcmp(key, "shell_chrome_alpha_milli") == 0) theme->shell.chrome_alpha = TitanTheme_MilliToFloat(TitanTheme_ParseUnsigned(value, 250u, 1000u, 1000u));
        else if (strcmp(key, "shell_motion_x_milli") == 0) theme->shell.horizontal_motion_scale = TitanTheme_MilliToFloat(TitanTheme_ParseUnsigned(value, 500u, 1600u, 1000u));
        else if (strcmp(key, "shell_motion_y_milli") == 0) theme->shell.vertical_motion_scale = TitanTheme_MilliToFloat(TitanTheme_ParseUnsigned(value, 500u, 1600u, 1000u));
    }
    fclose(file);

    if (format != 1u || !saw_id || !saw_name || !saw_engine ||
        theme->shell_api == 0u || theme->shell_api > TITAN_THEME_SHELL_API_VERSION)
    {
        TitanTheme_SetError(error, error_capacity, "Manifesto de tema incompativel");
        return 0;
    }
    if (theme->background_engine == TITAN_THEME_BACKGROUND_SCENE_V2)
    {
        unsigned int frame_index;
        unsigned int idle_total;
        if (theme->shell_api < 2u ||
            !TitanTheme_ValidateSceneFrame(root_path, theme->scene.home_frame) ||
            !TitanTheme_ValidateSceneFrame(root_path, theme->scene.top_frame))
        {
            TitanTheme_SetError(error, error_capacity, "scene-v2 requer HOME/TOP RGB565 512x256");
            return 0;
        }
        for (frame_index = 0u; frame_index < theme->scene.transition_frame_count; frame_index++)
        {
            if (!TitanTheme_ValidateSceneFrame(root_path, theme->scene.transition_frames[frame_index]))
            {
                TitanTheme_SetError(error, error_capacity, "Quadro scene-v2 ausente ou invalido");
                return 0;
            }
        }

        idle_total = theme->scene.home_idle_frame_count + theme->scene.top_idle_frame_count;
        if (idle_total > 0u && theme->shell_api < 3u)
        {
            TitanTheme_SetError(error, error_capacity, "Loops idle scene-v2 requerem shell_api=3");
            return 0;
        }
        if (idle_total > TITAN_THEME_SCENE_IDLE_TOTAL_MAX)
        {
            TitanTheme_SetError(error, error_capacity, "Orcamento idle scene-v2 excedido");
            return 0;
        }
        for (frame_index = 0u; frame_index < theme->scene.home_idle_frame_count; frame_index++)
        {
            if (!TitanTheme_ValidateSceneFrame(root_path, theme->scene.home_idle_frames[frame_index]))
            {
                TitanTheme_SetError(error, error_capacity, "Quadro idle HOME ausente ou invalido");
                return 0;
            }
        }
        for (frame_index = 0u; frame_index < theme->scene.top_idle_frame_count; frame_index++)
        {
            if (!TitanTheme_ValidateSceneFrame(root_path, theme->scene.top_idle_frames[frame_index]))
            {
                TitanTheme_SetError(error, error_capacity, "Quadro idle TOP ausente ou invalido");
                return 0;
            }
        }
    }
    if (theme->background_engine == TITAN_THEME_BACKGROUND_SCENE_V4)
    {
        char manifest_path[TITAN_THEME_PATH_CAPACITY];
        TitanStorageIoSize manifest_size;
        int manifest_is_directory;
        if (theme->shell_api != 4u || theme->scene_v4.manifest[0] == '\0' ||
            !TitanTheme_PathJoin(manifest_path, sizeof(manifest_path), root_path, theme->scene_v4.manifest) ||
            !TitanStorageIo_PathInfo64(manifest_path, &manifest_is_directory, &manifest_size) ||
            manifest_is_directory || manifest_size == 0u || manifest_size > 65536u)
        {
            TitanTheme_SetError(error, error_capacity, "scene-v4 requer shell_api=4 e manifesto declarativo valido");
            return 0;
        }
        if (theme->scene_v4.vram_budget + theme->scene_v4.reserved_headroom > 2u * 1024u * 1024u)
        {
            TitanTheme_SetError(error, error_capacity, "Orcamento VRAM scene-v4 invalido");
            return 0;
        }
    }

    if (theme->author[0] == '\0')
        TitanTheme_CopyText(theme->author, sizeof(theme->author), "Unknown");
    if (theme->version[0] == '\0')
        TitanTheme_CopyText(theme->version, sizeof(theme->version), "1.0");
    return 1;
}

static int TitanTheme_ReadActiveId(const char *path, char *id, unsigned int capacity)
{
    int descriptor;
    TitanStorageIoSize size;
    char buffer[TITAN_THEME_ID_CAPACITY];

    if (id == 0 || capacity == 0u ||
        !TitanStorageIo_PathInfo64(path, 0, &size) ||
        size == 0u || size >= sizeof(buffer))
        return 0;
    descriptor = TitanStorageIo_Open(path, TITAN_STORAGE_IO_READ);
    if (descriptor < 0)
        return 0;
    memset(buffer, 0, sizeof(buffer));
    if (!TitanStorageIo_ReadExact(descriptor, buffer, (unsigned int)size) ||
        TitanStorageIo_Close(descriptor) < 0)
        return 0;
    buffer[(unsigned int)size] = '\0';
    if (!TitanTheme_IdSafe(buffer) && strcmp(buffer, "DEFAULT") != 0)
        return 0;
    TitanTheme_CopyText(id, capacity, buffer);
    return 1;
}

static int TitanTheme_SaveActiveId(const char *id)
{
    int descriptor;
    char verified[TITAN_THEME_ID_CAPACITY];
    unsigned int length;

    if (id == 0 || (!TitanTheme_IdSafe(id) && strcmp(id, "DEFAULT") != 0))
        return 0;
    length = (unsigned int)strlen(id);
    TitanStorageIo_CreateDirectory(TITAN_STORAGE_DEVICE_ROOT);
    TitanStorageIo_CreateDirectory(TITAN_THEME_SETTINGS_ROOT);
    TitanStorageIo_Remove(TITAN_THEME_SETTINGS_TEMP);
    descriptor = TitanStorageIo_Open(
        TITAN_THEME_SETTINGS_TEMP,
        TITAN_STORAGE_IO_WRITE | TITAN_STORAGE_IO_CREATE | TITAN_STORAGE_IO_TRUNCATE
    );
    if (descriptor < 0)
        return 0;
    if (!TitanStorageIo_WriteExact(descriptor, id, length) ||
        TitanStorageIo_Flush(descriptor) < 0 ||
        TitanStorageIo_Close(descriptor) < 0)
    {
        TitanStorageIo_Remove(TITAN_THEME_SETTINGS_TEMP);
        return 0;
    }
    if (!TitanTheme_ReadActiveId(TITAN_THEME_SETTINGS_TEMP, verified, sizeof(verified)) ||
        strcmp(verified, id) != 0 ||
        !TitanStorageIo_AtomicReplace(
            TITAN_THEME_SETTINGS_TEMP,
            TITAN_THEME_SETTINGS_PATH,
            TITAN_THEME_SETTINGS_BACKUP))
    {
        TitanStorageIo_Remove(TITAN_THEME_SETTINGS_TEMP);
        return 0;
    }
    if (!TitanTheme_ReadActiveId(TITAN_THEME_SETTINGS_PATH, verified, sizeof(verified)) ||
        strcmp(verified, id) != 0)
    {
        TitanStorageIo_Remove(TITAN_THEME_SETTINGS_PATH);
        TitanStorageIo_Rename(TITAN_THEME_SETTINGS_BACKUP, TITAN_THEME_SETTINGS_PATH);
        return 0;
    }
    TitanStorageIo_Remove(TITAN_THEME_SETTINGS_BACKUP);
    return 1;
}

void TitanTheme_Initialize(void)
{
    memset(g_themes, 0, sizeof(g_themes));
    TitanTheme_AddBuiltIns();
    g_active_index = 0u;
    g_generation = 1u;
}

unsigned int TitanTheme_Refresh(void)
{
    DIR *directory;
    struct dirent *entry;
    char active_id[TITAN_THEME_ID_CAPACITY];
    unsigned int index;

    TitanTheme_CopyText(active_id, sizeof(active_id), g_themes[g_active_index].id);
    for (index = 0u; index < TITAN_THEME_CAPACITY; index++)
        memset(&g_themes[index], 0, sizeof(g_themes[index]));
    TitanTheme_AddBuiltIns();

    directory = opendir(TITAN_THEME_ROOT);
    if (directory != 0)
    {
        while (g_theme_count < TITAN_THEME_CAPACITY && (entry = readdir(directory)) != 0)
        {
            char root[TITAN_THEME_PATH_CAPACITY];
            int is_directory;
            TitanThemeInfo parsed;

            if (entry->d_name[0] == '.' || !TitanTheme_IdSafe(entry->d_name) ||
                !TitanTheme_PathJoin(root, sizeof(root), TITAN_THEME_ROOT, entry->d_name) ||
                !TitanStorageIo_PathInfo64(root, &is_directory, 0) || !is_directory ||
                !TitanTheme_ParseConfig(root, &parsed, 0, 0) ||
                strcmp(parsed.id, entry->d_name) != 0)
                continue;
            if (TitanTheme_IsBuiltInId(parsed.id))
                continue;
            parsed.built_in = 0;
            g_themes[g_theme_count++] = parsed;
        }
        closedir(directory);
    }

    g_active_index = 0u;
    for (index = 0u; index < g_theme_count; index++)
    {
        if (strcmp(g_themes[index].id, active_id) == 0)
        {
            g_active_index = index;
            break;
        }
    }
    g_generation++;
    if (g_generation == 0u)
        g_generation = 1u;
    return g_theme_count;
}

int TitanTheme_LoadPersistentSetting(void)
{
    char id[TITAN_THEME_ID_CAPACITY];
    unsigned int index;

    TitanTheme_Refresh();
    if (!TitanTheme_ReadActiveId(TITAN_THEME_SETTINGS_PATH, id, sizeof(id)))
        return 1;
    for (index = 0u; index < g_theme_count; index++)
    {
        if (strcmp(g_themes[index].id, id) == 0)
        {
            g_active_index = index;
            g_generation++;
            return 1;
        }
    }
    g_active_index = 0u;
    return 0;
}

unsigned int TitanTheme_GetCount(void)
{
    return g_theme_count;
}

const TitanThemeInfo *TitanTheme_Get(unsigned int index)
{
    return index < g_theme_count ? &g_themes[index] : 0;
}

void TitanTheme_GetContextPolicy(TitanThemeContext context, TitanThemeContextPolicy *policy)
{
    if (policy == 0) return;
    policy->full_scene = context == TITAN_THEME_CONTEXT_HOME;
    policy->system_waves = context == TITAN_THEME_CONTEXT_SYSTEM;
    policy->functional_ui = context == TITAN_THEME_CONTEXT_SYSTEM;
}


const TitanThemeInfo *TitanTheme_GetActive(void)
{
    return g_active_index < g_theme_count ? &g_themes[g_active_index] : &g_themes[0];
}

unsigned int TitanTheme_GetActiveIndex(void)
{
    return g_active_index;
}

const TitanThemeShellStyle *TitanTheme_GetShellStyle(void)
{
    return &TitanTheme_GetActive()->shell;
}

const TitanThemeFlowStyle *TitanTheme_GetFlowStyle(void)
{
    return &TitanTheme_GetActive()->flow;
}

const TitanThemeSceneStyle *TitanTheme_GetSceneStyle(void)
{
    return &TitanTheme_GetActive()->scene;
}

const TitanThemeSceneV4Style *TitanTheme_GetSceneV4Style(void)
{
    return &TitanTheme_GetActive()->scene_v4;
}

const TitanThemeAudioStyle *TitanTheme_GetAudioStyle(void)
{
    return &TitanTheme_GetActive()->audio;
}

const TitanThemeIconStyle *TitanTheme_GetIconStyle(void)
{
    return &TitanTheme_GetActive()->icons;
}

int TitanTheme_BuildAssetPath(const char *relative, char *output, unsigned int capacity)
{
    const TitanThemeInfo *theme;

    if (output == 0 || capacity == 0u || !TitanTheme_AssetPathSafe(relative) ||
        relative == 0 || relative[0] == '\0')
        return 0;
    theme = TitanTheme_GetActive();
    if (theme == 0 || theme->built_in || theme->path[0] == '\0')
        return 0;
    return TitanTheme_PathJoin(output, capacity, theme->path, relative);
}

unsigned int TitanTheme_GetGeneration(void)
{
    return g_generation;
}

int TitanTheme_Apply(unsigned int index)
{
    unsigned int previous;

    if (index >= g_theme_count)
        return 0;
    previous = g_active_index;
    g_active_index = index;
    if (!TitanTheme_SaveActiveId(g_themes[index].id))
    {
        g_active_index = previous;
        return 0;
    }
    g_generation++;
    if (g_generation == 0u)
        g_generation = 1u;
    return 1;
}

int TitanTheme_CanDelete(unsigned int index)
{
    return index < g_theme_count && !g_themes[index].built_in && g_themes[index].path[0] != '\0';
}

int TitanTheme_Delete(unsigned int index, char *error, unsigned int error_capacity)
{
    char deleting_id[TITAN_THEME_ID_CAPACITY];
    char deleting_path[TITAN_THEME_PATH_CAPACITY];
    int was_active;
    if(error && error_capacity)error[0]='\0';
    if(!TitanTheme_CanDelete(index)){
        TitanTheme_SetError(error,error_capacity,"Tema integrado nao pode ser excluido");
        return 0;
    }
    TitanTheme_CopyText(deleting_id,sizeof(deleting_id),g_themes[index].id);
    TitanTheme_CopyText(deleting_path,sizeof(deleting_path),g_themes[index].path);
    was_active=(index==g_active_index);
    if(was_active && !TitanTheme_Apply(0u)){
        TitanTheme_SetError(error,error_capacity,"Nao foi possivel retornar ao tema Default");
        return 0;
    }
    if(!TitanTheme_RemoveTree(deleting_path)){
        TitanTheme_SetError(error,error_capacity,"Falha ao remover arquivos do tema");
        return 0;
    }
    TitanTheme_Refresh();
    if(was_active) TitanTheme_SaveActiveId("DEFAULT");
    (void)deleting_id;
    return 1;
}

int TitanTheme_ValidateDirectoryForId(
    const char *root_path,
    const char *expected_id,
    char *error,
    unsigned int error_capacity
)
{
    TitanThemeInfo parsed;

    if (error != 0 && error_capacity > 0u)
        error[0] = '\0';
    if (root_path == 0 || root_path[0] == '\0')
    {
        TitanTheme_SetError(error, error_capacity, "Diretorio de tema invalido");
        return 0;
    }
    if (!TitanTheme_ParseConfig(root_path, &parsed, error, error_capacity))
        return 0;
    if (expected_id != 0 && expected_id[0] != '\0' && strcmp(parsed.id, expected_id) != 0)
    {
        TitanTheme_SetError(error, error_capacity, "ID do tema nao corresponde ao pacote");
        return 0;
    }
    return 1;
}

int TitanTheme_ValidateDirectory(
    const char *root_path,
    char *error,
    unsigned int error_capacity
)
{
    return TitanTheme_ValidateDirectoryForId(
        root_path, 0, error, error_capacity
    );
}
