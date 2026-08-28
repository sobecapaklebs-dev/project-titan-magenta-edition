#ifndef PROJECT_TITAN_SHELL_LAYOUT_H
#define PROJECT_TITAN_SHELL_LAYOUT_H

#define TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT 4u
#define TITAN_SHELL_LAYOUT_SYSTEM_ITEM_COUNT 5u

typedef enum TitanShellLayoutStatus
{
    TITAN_SHELL_LAYOUT_NOT_INITIALIZED = 0,
    TITAN_SHELL_LAYOUT_READY,
    TITAN_SHELL_LAYOUT_READY_DEFAULTS,
    TITAN_SHELL_LAYOUT_IO_ERROR,
    TITAN_SHELL_LAYOUT_CORRUPT,
    TITAN_SHELL_LAYOUT_FUTURE_VERSION
} TitanShellLayoutStatus;

void TitanShellLayout_Initialize(void);
TitanShellLayoutStatus TitanShellLayout_GetStatus(void);

unsigned int TitanShellLayout_GetHomeCatalogIndex(unsigned int slot);
int TitanShellLayout_GetHomeSlot(unsigned int catalog_index);
int TitanShellLayout_IsHomeHidden(unsigned int catalog_index);
int TitanShellLayout_SetHomeHidden(unsigned int catalog_index, int hidden);
int TitanShellLayout_MoveHome(unsigned int catalog_index, int delta);

unsigned int TitanShellLayout_GetSystemCatalogIndex(unsigned int slot);
int TitanShellLayout_GetSystemSlot(unsigned int catalog_index);
int TitanShellLayout_MoveSystem(unsigned int catalog_index, int delta);

int TitanShellLayout_Save(void);
int TitanShellLayout_ResetDefaults(void);
int TitanShellLayout_Export(void);
int TitanShellLayout_Import(void);

#endif
