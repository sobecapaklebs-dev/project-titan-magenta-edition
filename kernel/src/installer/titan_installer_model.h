#ifndef PROJECT_TITAN_INSTALLER_MODEL_H
#define PROJECT_TITAN_INSTALLER_MODEL_H

#include <tamtypes.h>

typedef enum TitanInstallerConsoleFamily
{
    TITAN_INSTALLER_CONSOLE_FAT = 0,
    TITAN_INSTALLER_CONSOLE_SLIM_70,
    TITAN_INSTALLER_CONSOLE_SLIM_90,
    TITAN_INSTALLER_CONSOLE_UNKNOWN
} TitanInstallerConsoleFamily;

typedef struct TitanInstallerConsoleInfo
{
    int model_id_valid;
    u16 model_id;
    unsigned char mechacon_major;
    unsigned char mechacon_minor;
    char model_name[32];
    char romver[20];
    TitanInstallerConsoleFamily family;
} TitanInstallerConsoleInfo;

void TitanInstallerModel_Detect(TitanInstallerConsoleInfo *info);
const char *TitanInstallerModel_LookupName(u16 model_id);
TitanInstallerConsoleFamily TitanInstallerModel_FamilyFromName(const char *name);
const char *TitanInstallerModel_GetFamilyName(TitanInstallerConsoleFamily family);
unsigned int TitanInstallerModel_GetRomVersionCode(const TitanInstallerConsoleInfo *info);
int TitanInstallerModel_IsAutonomousCompatible(const TitanInstallerConsoleInfo *info);

#endif
