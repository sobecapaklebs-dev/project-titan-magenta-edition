#ifndef PROJECT_TITAN_ICONS_H
#define PROJECT_TITAN_ICONS_H

typedef enum TitanIconId
{
    TITAN_ICON_OVERVIEW = 0,
    TITAN_ICON_APPLICATIONS,
    TITAN_ICON_STORAGE,
    TITAN_ICON_INPUT,
    TITAN_ICON_USER,
    TITAN_ICON_SAVED_DATA,
    TITAN_ICON_SETTINGS,
    TITAN_ICON_VIDEO,
    TITAN_ICON_SYSTEM,
    TITAN_ICON_ABOUT,
    TITAN_ICON_RUNE,
    TITAN_ICON_PROFILE_SETTINGS,
    TITAN_ICON_NOTIFICATIONS,
    TITAN_ICON_POWER,
    TITAN_ICON_VMC_UTILITY,
    TITAN_ICON_DEBUG_SETTINGS,
    TITAN_ICON_APP_HOME,
    TITAN_ICON_FTP_HOME,
    TITAN_ICON_LIBRARY
} TitanIconId;

void TitanIcons_Draw(
    TitanIconId icon,
    float center_x,
    float center_y,
    float scale,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
);

#endif
