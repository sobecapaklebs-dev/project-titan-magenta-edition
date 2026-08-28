#ifndef PROJECT_TITAN_SHELL_CATALOG_H
#define PROJECT_TITAN_SHELL_CATALOG_H

#include "titan_icons.h"

typedef enum TitanShellPage
{
    TITAN_SHELL_PAGE_HOME = 0,
    TITAN_SHELL_PAGE_APP_HOME,
    TITAN_SHELL_PAGE_FTP_HOME,
    TITAN_SHELL_PAGE_OVERVIEW,
    TITAN_SHELL_PAGE_APPLICATIONS,
    TITAN_SHELL_PAGE_BROWSER,
    TITAN_SHELL_PAGE_STORAGE,
    TITAN_SHELL_PAGE_INPUT,
    TITAN_SHELL_PAGE_NOTIFICATIONS,
    TITAN_SHELL_PAGE_USER_PROFILE,
    TITAN_SHELL_PAGE_SAVED_DATA,
    TITAN_SHELL_PAGE_SETTINGS,
    TITAN_SHELL_PAGE_POWER,
    TITAN_SHELL_PAGE_ABOUT,
    /* Appended to preserve persisted shell-page keys from RC14 and folders. */
    TITAN_SHELL_PAGE_LIBRARY
} TitanShellPage;

typedef struct TitanShellItem
{
    const char *title;
    const char *subtitle;
    const char *status;
    const char *title_id;
    const char *version;
    TitanIconId icon;
    TitanShellPage page;
} TitanShellItem;

unsigned int TitanShellCatalog_GetCount(void);

const TitanShellItem *TitanShellCatalog_Get(
    unsigned int index
);

unsigned int TitanShellCatalog_GetDexCount(void);

const TitanShellItem *TitanShellCatalog_GetDex(
    unsigned int index
);

unsigned int TitanShellCatalog_GetSystemCount(void);

const TitanShellItem *TitanShellCatalog_GetSystem(
    unsigned int index
);

#endif
