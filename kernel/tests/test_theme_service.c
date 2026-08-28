#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "titan_storage_io.h"
#include "titan_theme.h"

#define TEST_ROOT TITAN_STORAGE_DEVICE_ROOT
#define TEST_THEMES TEST_ROOT "/themes"
#define TEST_THEME TEST_THEMES "/SCARLETFLOW"
#define TEST_CONFIG TEST_THEME "/theme.cfg"
#define TEST_SCENE_THEME TEST_THEMES "/SCENETEST"
#define TEST_SCENE_CONFIG TEST_SCENE_THEME "/theme.cfg"
#define TEST_SCENE_HOME TEST_SCENE_THEME "/home.rgb565"
#define TEST_SCENE_TOP TEST_SCENE_THEME "/top.rgb565"
#define TEST_SCENE_MID TEST_SCENE_THEME "/mid.rgb565"
#define TEST_IDLE_THEME TEST_THEMES "/SCENEIDLE"
#define TEST_IDLE_CONFIG TEST_IDLE_THEME "/theme.cfg"
#define TEST_IDLE_HOME TEST_IDLE_THEME "/home.rgb565"
#define TEST_IDLE_TOP TEST_IDLE_THEME "/top.rgb565"
#define TEST_IDLE_HOME1 TEST_IDLE_THEME "/home-idle-1.rgb565"
#define TEST_IDLE_TOP1 TEST_IDLE_THEME "/top-idle-1.rgb565"
#define TEST_SCENE_BYTES (512u * 256u * 2u)


static unsigned int find_theme_index(const char *id)
{
    unsigned int index;
    for (index = 0u; index < TitanTheme_GetCount(); index++)
    {
        const TitanThemeInfo *candidate = TitanTheme_Get(index);
        if (candidate != 0 && strcmp(candidate->id, id) == 0)
            return index;
    }
    return (unsigned int)-1;
}

static void write_config(void)
{
    static const char config[] =
        "format=1\n"
        "id=SCARLETFLOW\n"
        "name=Ribbon Scarlet\n"
        "author=Project Titan\n"
        "version=5.0.0\n"
        "shell_api=1\n"
        "background_engine=flow3d-v1\n"
        "bg_top_r=0\n"
        "bg_top_g=0\n"
        "bg_top_b=0\n"
        "bg_bottom_r=0\n"
        "bg_bottom_g=0\n"
        "bg_bottom_b=0\n"
        "wave_r=148\n"
        "wave_g=14\n"
        "wave_b=25\n"
        "wave_alpha=22\n"
        "primary_wave_count=1\n"
        "primary_wave1_start_y_milli=250000\n"
        "primary_wave1_control1_y_milli=112000\n"
        "primary_wave1_control2_y_milli=112000\n"
        "primary_wave1_end_y_milli=250000\n"
        "primary_wave1_drift_y_milli=5000\n"
        "primary_wave1_thickness_milli=1450\n"
        "primary_wave1_amplitude_milli=360\n"
        "primary_wave1_alpha_milli=1000\n"
        "primary_wave1_phase=0\n"
        "primary_wave1_sweep=0\n"
        "primary_wave1_wander=1\n"
        "primary_wave1_wander_speed_milli=2\n"
        "primary_wave1_wander_extent_x_milli=540000\n"
        "primary_wave1_wander_extent_y_milli=340000\n"
        "primary_wave1_wander_curve_milli=145000\n"
        "primary_wave1_wander_visibility_min_milli=220\n"
        "primary_wave1_wander_seed=47831\n"
        "primary_wave1_sweep_span_milli=1450\n"
        "shell_accent_r=196\n"
        "shell_accent_g=28\n"
        "shell_accent_b=42\n";
    int fd;

    assert(TitanStorageIo_CreateDirectory(TEST_ROOT) >= 0);
    assert(TitanStorageIo_CreateDirectory(TEST_THEMES) >= 0);
    assert(TitanStorageIo_CreateDirectory(TEST_THEME) >= 0);
    fd = TitanStorageIo_Open(
        TEST_CONFIG,
        TITAN_STORAGE_IO_WRITE | TITAN_STORAGE_IO_CREATE | TITAN_STORAGE_IO_TRUNCATE
    );
    assert(fd >= 0);
    assert(TitanStorageIo_WriteExact(fd, config, (unsigned int)(sizeof(config) - 1u)));
    assert(TitanStorageIo_Close(fd) >= 0);
}

static void write_scene_frame(const char *path, unsigned char value)
{
    unsigned char block[4096];
    unsigned int remaining = TEST_SCENE_BYTES;
    int fd;
    memset(block, value, sizeof(block));
    fd = TitanStorageIo_Open(path, TITAN_STORAGE_IO_WRITE | TITAN_STORAGE_IO_CREATE | TITAN_STORAGE_IO_TRUNCATE);
    assert(fd >= 0);
    while (remaining > 0u)
    {
        unsigned int amount = remaining > sizeof(block) ? sizeof(block) : remaining;
        assert(TitanStorageIo_WriteExact(fd, block, amount));
        remaining -= amount;
    }
    assert(TitanStorageIo_Close(fd) >= 0);
}

static void write_scene_config(void)
{
    static const char config[] =
        "format=1\n"
        "id=SCENETEST\n"
        "name=Scene API Test\n"
        "author=Project Titan\n"
        "version=1.0.0\n"
        "shell_api=2\n"
        "background_engine=scene-v2\n"
        "scene_home=home.rgb565\n"
        "scene_top=top.rgb565\n"
        "scene_transition_count=1\n"
        "scene_transition_0=mid.rgb565\n"
        "scene_system_flow=1\n"
        "audio_music=audio/bgm.wav\n"
        "audio_top_enter=audio/transform.wav\n"
        "icon_tint_enabled=1\n"
        "icon_tint_r=255\n"
        "icon_tint_g=220\n"
        "icon_tint_b=96\n"
        "icon_decorator=corners\n"
        "icon_decorator_r=255\n"
        "icon_decorator_g=196\n"
        "icon_decorator_b=48\n"
        "wave_r=244\n"
        "wave_g=190\n"
        "wave_b=52\n";
    int fd;

    assert(TitanStorageIo_CreateDirectory(TEST_SCENE_THEME) >= 0);
    write_scene_frame(TEST_SCENE_HOME, 0x11u);
    write_scene_frame(TEST_SCENE_TOP, 0x22u);
    write_scene_frame(TEST_SCENE_MID, 0x33u);
    fd = TitanStorageIo_Open(TEST_SCENE_CONFIG, TITAN_STORAGE_IO_WRITE | TITAN_STORAGE_IO_CREATE | TITAN_STORAGE_IO_TRUNCATE);
    assert(fd >= 0);
    assert(TitanStorageIo_WriteExact(fd, config, (unsigned int)(sizeof(config) - 1u)));
    assert(TitanStorageIo_Close(fd) >= 0);
}


static void write_idle_scene_config(void)
{
    static const char config[] =
        "format=1\n"
        "id=SCENEIDLE\n"
        "name=Scene Idle API Test\n"
        "author=Project Titan\n"
        "version=1.0.0\n"
        "shell_api=3\n"
        "background_engine=scene-v2\n"
        "scene_home=home.rgb565\n"
        "scene_top=top.rgb565\n"
        "scene_transition_count=0\n"
        "scene_home_idle_count=1\n"
        "scene_home_idle_0=home-idle-1.rgb565\n"
        "scene_home_idle_ms=160\n"
        "scene_home_idle_mode=pingpong\n"
        "scene_top_idle_count=1\n"
        "scene_top_idle_0=top-idle-1.rgb565\n"
        "scene_top_idle_ms=90\n"
        "scene_top_idle_mode=loop\n";
    int fd;

    assert(TitanStorageIo_CreateDirectory(TEST_IDLE_THEME) >= 0);
    write_scene_frame(TEST_IDLE_HOME, 0x44u);
    write_scene_frame(TEST_IDLE_TOP, 0x55u);
    write_scene_frame(TEST_IDLE_HOME1, 0x66u);
    write_scene_frame(TEST_IDLE_TOP1, 0x77u);
    fd = TitanStorageIo_Open(TEST_IDLE_CONFIG, TITAN_STORAGE_IO_WRITE | TITAN_STORAGE_IO_CREATE | TITAN_STORAGE_IO_TRUNCATE);
    assert(fd >= 0);
    assert(TitanStorageIo_WriteExact(fd, config, (unsigned int)(sizeof(config) - 1u)));
    assert(TitanStorageIo_Close(fd) >= 0);
}

int main(void)
{
    const TitanThemeInfo *theme;
    char error[128];

    write_config();
    write_scene_config();
    write_idle_scene_config();
    TitanTheme_Initialize();
    assert(TitanTheme_Refresh() == 7u);
    assert(TitanTheme_GetCount() == 7u);
    assert(strcmp(TitanTheme_Get(0)->id, "DEFAULT") == 0);
    assert(strcmp(TitanTheme_Get(1)->id, "SCARLETFLOW") == 0);
    assert(strcmp(TitanTheme_Get(2)->id, "FLOWWINE") == 0);
    assert(strcmp(TitanTheme_Get(3)->id, "FLOWCYAN") == 0);
    assert(strcmp(TitanTheme_Get(4)->id, "FLOWTWILIGHT") == 0);
    assert(!TitanTheme_CanDelete(0u));
    assert(!TitanTheme_CanDelete(1u));
    {
        unsigned int scarlet_index = find_theme_index("SCARLETFLOW");
        assert(scarlet_index != (unsigned int)-1);
        theme = TitanTheme_Get(scarlet_index);
        assert(theme != 0);
    assert(strcmp(theme->id, "SCARLETFLOW") == 0);
    assert(strcmp(theme->name, "Ribbon Scarlet") == 0);
    assert(theme->background_engine == TITAN_THEME_BACKGROUND_FLOW3D);
    assert(theme->flow.background_top.red == 0u);
    assert(theme->flow.background_top.green == 0u);
    assert(theme->flow.background_top.blue == 0u);
    assert(theme->flow.wave_base.red == 148u);
    assert(theme->flow.wave_base.green == 14u);
    assert(theme->flow.wave_base.blue == 25u);
    assert(theme->flow.primary_wave_count == 1u);
    assert(theme->flow.primary_waves[0].start_y > 249.9f && theme->flow.primary_waves[0].start_y < 250.1f);
    assert(theme->flow.primary_waves[0].end_y > 249.9f && theme->flow.primary_waves[0].end_y < 250.1f);
    assert(theme->flow.primary_waves[0].control1_y > 179.9f && theme->flow.primary_waves[0].control1_y < 180.1f);
    assert(theme->flow.primary_waves[0].control2_y > 299.9f && theme->flow.primary_waves[0].control2_y < 300.1f);
    assert(theme->flow.primary_waves[0].drift_y > 4.49f && theme->flow.primary_waves[0].drift_y < 4.51f);
    assert(theme->flow.primary_waves[0].sweep_enabled == 0u);
    assert(theme->flow.primary_waves[0].wander_enabled == 0u);
    assert(theme->flow.primary_waves[0].snake_enabled == 1u);
    assert(theme->flow.primary_waves[0].reference_ribbon_enabled == 0u);
    assert(theme->flow.primary_waves[0].snake_speed > 0.0019f && theme->flow.primary_waves[0].snake_speed < 0.0021f);
    assert(theme->flow.primary_waves[0].snake_extent_x > 519.9f && theme->flow.primary_waves[0].snake_extent_x < 520.1f);
    assert(theme->flow.primary_waves[0].snake_extent_y > 319.9f && theme->flow.primary_waves[0].snake_extent_y < 320.1f);
    assert(theme->flow.primary_waves[0].snake_seed == 52391u);
    assert(theme->flow.primary_waves[0].sweep_span_scale > 1.44f && theme->flow.primary_waves[0].sweep_span_scale < 1.46f);
    assert(theme->flow.vu1_deform_speed > 0.719f && theme->flow.vu1_deform_speed < 0.721f);
    assert(theme->flow.vu1_deform_frequency > 1.49f && theme->flow.vu1_deform_frequency < 1.51f);
    assert(theme->flow.vu1_deform_amplitude > 7.19f && theme->flow.vu1_deform_amplitude < 7.21f);
    assert(theme->shell.accent.red == 196u);
    assert(TitanTheme_ValidateDirectoryForId(TEST_THEME, "SCARLETFLOW", error, sizeof(error)));
        assert(TitanTheme_Apply(scarlet_index));
        assert(TitanTheme_GetActiveIndex() == scarlet_index);
    }

    {
        unsigned int scene_index = find_theme_index("SCENETEST");
        assert(scene_index != (unsigned int)-1);
        theme = TitanTheme_Get(scene_index);
        assert(theme != 0);
    assert(strcmp(theme->id, "SCENETEST") == 0);
    assert(TitanTheme_CanDelete(scene_index));
    assert(theme->shell_api == 2u);
    assert(theme->background_engine == TITAN_THEME_BACKGROUND_SCENE_V2);
    assert(strcmp(theme->scene.home_frame, "home.rgb565") == 0);
    assert(strcmp(theme->scene.top_frame, "top.rgb565") == 0);
    assert(theme->scene.transition_frame_count == 1u);
    assert(strcmp(theme->scene.transition_frames[0], "mid.rgb565") == 0);
    assert(strcmp(theme->audio.music, "audio/bgm.wav") == 0);
    assert(strcmp(theme->audio.top_enter, "audio/transform.wav") == 0);
    assert(theme->icons.tint_enabled == 1u);
    assert(theme->icons.decorator == TITAN_THEME_ICON_DECORATOR_CORNERS);
    assert(TitanTheme_ValidateDirectoryForId(TEST_SCENE_THEME, "SCENETEST", error, sizeof(error)));
        assert(TitanTheme_Apply(scene_index));
        assert(TitanTheme_GetActiveIndex() == scene_index);
        {
            char asset_path[TITAN_THEME_PATH_CAPACITY];
        assert(TitanTheme_BuildAssetPath("home.rgb565", asset_path, sizeof(asset_path)));
        assert(strstr(asset_path, "/SCENETEST/home.rgb565") != 0);
            assert(!TitanTheme_BuildAssetPath("../escape.bin", asset_path, sizeof(asset_path)));
        }
    }


    {
        unsigned int idle_index = find_theme_index("SCENEIDLE");
        assert(idle_index != (unsigned int)-1);
        theme = TitanTheme_Get(idle_index);
        assert(theme != 0);
        assert(TitanTheme_CanDelete(idle_index));
        assert(theme->shell_api == 3u);
        assert(theme->scene.home_idle_frame_count == 1u);
        assert(theme->scene.top_idle_frame_count == 1u);
        assert(strcmp(theme->scene.home_idle_frames[0], "home-idle-1.rgb565") == 0);
        assert(strcmp(theme->scene.top_idle_frames[0], "top-idle-1.rgb565") == 0);
        assert(theme->scene.home_idle_frame_ms == 160u);
        assert(theme->scene.top_idle_frame_ms == 90u);
        assert(theme->scene.home_idle_mode == TITAN_THEME_SCENE_IDLE_PINGPONG);
        assert(theme->scene.top_idle_mode == TITAN_THEME_SCENE_IDLE_LOOP);
        assert(TitanTheme_ValidateDirectoryForId(TEST_IDLE_THEME, "SCENEIDLE", error, sizeof(error)));
    }

    TitanTheme_Initialize();
    assert(TitanTheme_LoadPersistentSetting());
    assert(strcmp(TitanTheme_GetActive()->id, "SCENETEST") == 0);
    puts("theme-service: OK API v1/v2 compatibility + scene-v2 API v3 idle validation/persistence");
    return 0;
}
