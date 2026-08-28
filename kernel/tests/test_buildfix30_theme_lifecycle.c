#include "../src/services/titan_theme_lifecycle.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    const TitanThemeLifecycleSnapshot *s;
    TitanThemeLifecycle_Initialize();
    s=TitanThemeLifecycle_GetSnapshot();
    assert(s->phase==TITAN_THEME_LIFECYCLE_PRE_HOME && !s->dynamic_scene_allowed && s->ribbon_required && !s->music_allowed);
    TitanThemeLifecycle_SetPhase(TITAN_THEME_LIFECYCLE_HOME_ENTER,0.25f);
    s=TitanThemeLifecycle_GetSnapshot();
    assert(s->dynamic_scene_allowed && s->scene_opacity>0.24f && s->scene_opacity<0.26f && s->ribbon_required && s->music_allowed);
    TitanThemeLifecycle_SetPhase(TITAN_THEME_LIFECYCLE_HOME,1.0f);
    assert(TitanThemeLifecycle_DynamicSceneAllowed() && TitanThemeLifecycle_GetSceneOpacity()==1.0f && !TitanThemeLifecycle_RibbonRequired());
    TitanThemeLifecycle_SetPhase(TITAN_THEME_LIFECYCLE_SUBMENU,1.0f);
    assert(!TitanThemeLifecycle_DynamicSceneAllowed() && TitanThemeLifecycle_RibbonRequired() && TitanThemeLifecycle_MusicAllowed());
    TitanThemeLifecycle_SetPhase(TITAN_THEME_LIFECYCLE_RETURN_HOME,0.5f);
    s=TitanThemeLifecycle_GetSnapshot();
    assert(s->return_entry && s->dynamic_scene_allowed && s->scene_opacity==0.5f);
    puts("buildfix30-theme-lifecycle-host: OK");
    return 0;
}
