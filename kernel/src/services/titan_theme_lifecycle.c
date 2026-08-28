#include "titan_theme_lifecycle.h"

#include <string.h>

static TitanThemeLifecycleSnapshot g_theme_lifecycle;

static float TitanThemeLifecycle_Clamp(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

void TitanThemeLifecycle_Initialize(void)
{
    memset(&g_theme_lifecycle, 0, sizeof(g_theme_lifecycle));
    g_theme_lifecycle.phase = TITAN_THEME_LIFECYCLE_PRE_HOME;
    g_theme_lifecycle.ribbon_required = 1u;
}

void TitanThemeLifecycle_SetPhase(TitanThemeLifecyclePhase phase, float progress)
{
    float p = TitanThemeLifecycle_Clamp(progress);
    g_theme_lifecycle.phase = phase;
    g_theme_lifecycle.return_entry = phase == TITAN_THEME_LIFECYCLE_RETURN_HOME;
    switch (phase)
    {
        case TITAN_THEME_LIFECYCLE_HOME_ENTER:
        case TITAN_THEME_LIFECYCLE_RETURN_HOME:
            g_theme_lifecycle.dynamic_scene_allowed = 1u;
            g_theme_lifecycle.scene_opacity = p;
            g_theme_lifecycle.ribbon_required = p < 0.999f;
            g_theme_lifecycle.music_allowed = 1u;
            break;
        case TITAN_THEME_LIFECYCLE_HOME:
        case TITAN_THEME_LIFECYCLE_TOP:
            g_theme_lifecycle.dynamic_scene_allowed = 1u;
            g_theme_lifecycle.scene_opacity = 1.0f;
            g_theme_lifecycle.ribbon_required = 0u;
            g_theme_lifecycle.music_allowed = 1u;
            break;
        case TITAN_THEME_LIFECYCLE_SUBMENU:
            g_theme_lifecycle.dynamic_scene_allowed = 0u;
            g_theme_lifecycle.scene_opacity = 0.0f;
            g_theme_lifecycle.ribbon_required = 1u;
            g_theme_lifecycle.music_allowed = 1u;
            break;
        case TITAN_THEME_LIFECYCLE_HOME_EXIT:
            g_theme_lifecycle.dynamic_scene_allowed = 1u;
            g_theme_lifecycle.scene_opacity = 1.0f - p;
            g_theme_lifecycle.ribbon_required = p > 0.001f;
            g_theme_lifecycle.music_allowed = 1u;
            break;
        case TITAN_THEME_LIFECYCLE_PRE_HOME:
        default:
            g_theme_lifecycle.dynamic_scene_allowed = 0u;
            g_theme_lifecycle.scene_opacity = 0.0f;
            g_theme_lifecycle.ribbon_required = 1u;
            g_theme_lifecycle.music_allowed = 0u;
            break;
    }
}

const TitanThemeLifecycleSnapshot *TitanThemeLifecycle_GetSnapshot(void) { return &g_theme_lifecycle; }
int TitanThemeLifecycle_DynamicSceneAllowed(void) { return g_theme_lifecycle.dynamic_scene_allowed != 0u; }
float TitanThemeLifecycle_GetSceneOpacity(void) { return g_theme_lifecycle.scene_opacity; }
int TitanThemeLifecycle_RibbonRequired(void) { return g_theme_lifecycle.ribbon_required != 0u; }
int TitanThemeLifecycle_MusicAllowed(void) { return g_theme_lifecycle.music_allowed != 0u; }
