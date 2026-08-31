#ifndef PROJECT_TITAN_NEUTRINO_H
#define PROJECT_TITAN_NEUTRINO_H

#define TITAN_NEUTRINO_GAME_CAPACITY 256u
#define TITAN_NEUTRINO_TITLE_CAPACITY 96u
#define TITAN_NEUTRINO_PATH_CAPACITY 256u
#define TITAN_NEUTRINO_STARTUP_CAPACITY 16u
#define TITAN_NEUTRINO_VERSION_CAPACITY 16u

typedef enum TitanNeutrinoGameFormat
{
    TITAN_NEUTRINO_GAME_ISO = 0,
    TITAN_NEUTRINO_GAME_USBEXTREME
} TitanNeutrinoGameFormat;

typedef enum TitanNeutrinoResult
{
    TITAN_NEUTRINO_OK = 0,
    TITAN_NEUTRINO_NOT_FOUND,
    TITAN_NEUTRINO_NO_GAMES,
    TITAN_NEUTRINO_INVALID_GAME,
    TITAN_NEUTRINO_NO_PROFILE,
    TITAN_NEUTRINO_VMC_NOT_READY,
    TITAN_NEUTRINO_USBEXTREME_INVALID,
    TITAN_NEUTRINO_USBEXTREME_PART_MISSING,
    TITAN_NEUTRINO_ARGUMENT_OVERFLOW,
    TITAN_NEUTRINO_OPL_UL_NOT_FOUND,
    TITAN_NEUTRINO_VIDEO_PROFILE_FAILED,
    TITAN_NEUTRINO_OPL_UL_LOAD_FAILED,
    TITAN_NEUTRINO_ELF_LOAD_FAILED,
    TITAN_NEUTRINO_EXEC_FAILED,
    /* Appended: keep every validated pre-feature result value unchanged. */
    TITAN_NEUTRINO_PATCH_PREPARE_FAILED,
    TITAN_NEUTRINO_UL_PATCH_RUNTIME_FAILED
} TitanNeutrinoResult;

typedef struct TitanNeutrinoGame
{
    char title[TITAN_NEUTRINO_TITLE_CAPACITY];
    char path[TITAN_NEUTRINO_PATH_CAPACITY];
    char startup[TITAN_NEUTRINO_STARTUP_CAPACITY];
    char version[TITAN_NEUTRINO_VERSION_CAPACITY];
    TitanNeutrinoGameFormat format;
    unsigned char parts;
    unsigned char media;
} TitanNeutrinoGame;

void TitanNeutrino_Initialize(void);
unsigned int TitanNeutrino_Refresh(void);
void TitanNeutrino_ReplaceCatalog(
    const TitanNeutrinoGame *games,
    unsigned int count,
    const char *neutrino_elf
);
int TitanNeutrino_FindInstalledElf(
    char *destination,
    unsigned int capacity
);
int TitanNeutrino_FindInstalledOplUlLoader(
    char *destination,
    unsigned int capacity
);
void TitanNeutrino_ReadIsoStartupMetadata(
    const char *path,
    char *startup,
    unsigned int startup_capacity
);
unsigned int TitanNeutrino_GetGameCount(void);
unsigned int TitanNeutrino_GetCatalogGeneration(void);
const TitanNeutrinoGame *TitanNeutrino_GetGame(unsigned int index);
const char *TitanNeutrino_GetElfPath(void);
TitanNeutrinoResult TitanNeutrino_GetLaunchCommand(
    unsigned int index,
    char *destination,
    unsigned int capacity
);
int TitanNeutrino_GetLastLoadError(void);
TitanNeutrinoResult TitanNeutrino_Launch(unsigned int index);

TitanNeutrinoResult TitanNeutrino_GetPhysicalDiscLaunchCommand(
    const char *boot_path,
    const char *vmc_argument,
    char *destination,
    unsigned int capacity
);
TitanNeutrinoResult TitanNeutrino_LaunchPhysicalDisc(
    const char *boot_path,
    const char *vmc_argument
);
const char *TitanNeutrino_GetResultName(TitanNeutrinoResult result);

#endif
