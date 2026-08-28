#ifndef PROJECT_TITAN_LOCALIZATION_H
#define PROJECT_TITAN_LOCALIZATION_H

typedef enum TitanLanguage {
    TITAN_LANGUAGE_ENGLISH = 0,
    TITAN_LANGUAGE_PORTUGUESE,
    TITAN_LANGUAGE_SPANISH,
    TITAN_LANGUAGE_COUNT
} TitanLanguage;

typedef enum TitanStringId {
    TITAN_STR_SETTINGS = 0,
    TITAN_STR_USERS_PROFILES,
    TITAN_STR_SAVED_DATA,
    TITAN_STR_NETWORK,
    TITAN_STR_ACCESSORIES,
    TITAN_STR_STORAGE,
    TITAN_STR_VIDEO_OUTPUT,
    TITAN_STR_THEMES,
    TITAN_STR_SYSTEM,
    TITAN_STR_ABOUT_SOFTWARE,
    TITAN_STR_DEBUG_SETTINGS,
    TITAN_STR_HEALTH_LINE1,
    TITAN_STR_HEALTH_LINE2,
    TITAN_STR_WELCOME_TITLE,
    TITAN_STR_WELCOME_X,
    TITAN_STR_WELCOME_SYSTEM,
    TITAN_STR_FOLDER_OPEN,
    TITAN_STR_SYSTEM_APP_OPEN,
    TITAN_STR_TITAN_APP_START,
    TITAN_STR_TITAN_GAME_START,
    TITAN_STR_ELF_APP_START,
    TITAN_STR_DISC_START,
    TITAN_STR_PS2_GAME,
    TITAN_STR_UL_START,
    TITAN_STR_NEUTRINO_START,
    TITAN_STR_LANGUAGE,
    TITAN_STR_LANGUAGE_DESCRIPTION,
    TITAN_STR_ENABLED,
    TITAN_STR_DISABLED,
    TITAN_STR_APPLIED,
    TITAN_STR_APPLY,
    TITAN_STR_BUILTIN_FALLBACK,
    TITAN_STR_INSTALLED_THEME,
    TITAN_STR_THEME_SCENE,
    TITAN_STR_THEME_SYSTEM,
    TITAN_STR_COUNT
} TitanStringId;

void TitanLocalization_Initialize(void);
TitanLanguage TitanLocalization_GetLanguage(void);
const char *TitanLocalization_GetLanguageName(void);
void TitanLocalization_CycleLanguage(int direction);
const char *TitanLocalization_Get(TitanStringId id);
/* Compatibility translator for legacy shell literals. It accepts an English,
 * Portuguese or Spanish variant and returns the active-language equivalent. */
const char *TitanLocalization_Compat(const char *text);
int TitanLocalization_HasStorageWork(void);
int TitanLocalization_StorageWorkerPump(void);

#endif
