#ifndef PROJECT_TITAN_THEME_LIFECYCLE_H
#define PROJECT_TITAN_THEME_LIFECYCLE_H

#define TITAN_THEME_LIFECYCLE_API_VERSION 1u

typedef enum TitanThemeLifecyclePhase
{
    TITAN_THEME_LIFECYCLE_PRE_HOME = 0,
    TITAN_THEME_LIFECYCLE_HOME_ENTER,
    TITAN_THEME_LIFECYCLE_HOME,
    TITAN_THEME_LIFECYCLE_TOP,
    TITAN_THEME_LIFECYCLE_SUBMENU,
    TITAN_THEME_LIFECYCLE_HOME_EXIT,
    TITAN_THEME_LIFECYCLE_RETURN_HOME
} TitanThemeLifecyclePhase;

typedef struct TitanThemeLifecycleSnapshot
{
    TitanThemeLifecyclePhase phase;
    float scene_opacity;
    unsigned int dynamic_scene_allowed;
    unsigned int ribbon_required;
    unsigned int music_allowed;
    unsigned int return_entry;
} TitanThemeLifecycleSnapshot;

void TitanThemeLifecycle_Initialize(void);
void TitanThemeLifecycle_SetPhase(TitanThemeLifecyclePhase phase, float progress);
const TitanThemeLifecycleSnapshot *TitanThemeLifecycle_GetSnapshot(void);
int TitanThemeLifecycle_DynamicSceneAllowed(void);
float TitanThemeLifecycle_GetSceneOpacity(void);
int TitanThemeLifecycle_RibbonRequired(void);
int TitanThemeLifecycle_MusicAllowed(void);

#endif
