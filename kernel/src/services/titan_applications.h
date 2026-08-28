#ifndef PROJECT_TITAN_APPLICATIONS_H
#define PROJECT_TITAN_APPLICATIONS_H

#include "titan_sas.h"

#define TITAN_APPLICATION_CAPACITY 64u
#define TITAN_APPLICATION_TITLE_CAPACITY 80u
#define TITAN_APPLICATION_TITLE_ID_CAPACITY 16u
#define TITAN_APPLICATION_VERSION_CAPACITY 16u
#define TITAN_APPLICATION_AUTHOR_CAPACITY 48u
#define TITAN_APPLICATION_DESCRIPTION_CAPACITY 112u
#define TITAN_APPLICATION_CREDITS_CAPACITY 96u
#define TITAN_APPLICATION_SOURCE_CAPACITY 128u
#define TITAN_APPLICATION_PATH_CAPACITY 256u

#define TITAN_APPLICATION_API_VERSION 1u

typedef enum TitanApplicationType
{
    TITAN_APPLICATION_EXTERNAL_ELF = 0,
    TITAN_APPLICATION_TITAN_NATIVE = 1,
    TITAN_APPLICATION_SYSTEM_BROWSER = 2,
    TITAN_APPLICATION_TITAN_GAME = 3
} TitanApplicationType;


typedef enum TitanApplicationClassification
{
    TITAN_APPLICATION_CLASS_UNKNOWN_LEGACY = 0,
    TITAN_APPLICATION_CLASS_NATIVE = 1,
    TITAN_APPLICATION_CLASS_NATIVE_GAME = 2,
    TITAN_APPLICATION_CLASS_SAS = 3,
    TITAN_APPLICATION_CLASS_UL_ENGINE = 4,
    TITAN_APPLICATION_CLASS_NEUTRINO_ENGINE = 5,
    TITAN_APPLICATION_CLASS_DISC_ENGINE = 6
} TitanApplicationClassification;

typedef enum TitanApplicationMetadataSource
{
    TITAN_APPLICATION_METADATA_NAME_HEURISTIC = 0,
    TITAN_APPLICATION_METADATA_FOLDER_PREFIX = 1,
    TITAN_APPLICATION_METADATA_TITLE_CFG = 2,
    TITAN_APPLICATION_METADATA_APP_CFG = 3,
    TITAN_APPLICATION_METADATA_TITAN_MANIFEST = 4
} TitanApplicationMetadataSource;

typedef enum TitanApplicationExecutionModel
{
    TITAN_APPLICATION_EXECUTION_EXTERNAL_HANDOFF = 0,
    TITAN_APPLICATION_EXECUTION_NATIVE_IN_PROCESS = 1,
    TITAN_APPLICATION_EXECUTION_SYSTEM_SERVICE = 2
} TitanApplicationExecutionModel;

typedef enum TitanApplicationResult
{
    TITAN_APPLICATION_OK = 0,
    TITAN_APPLICATION_NOT_FOUND,
    TITAN_APPLICATION_INVALID_INDEX,
    TITAN_APPLICATION_INVALID_ELF,
    TITAN_APPLICATION_INVALID_TMOD,
    TITAN_APPLICATION_API_MISMATCH,
    TITAN_APPLICATION_RUNTIME_BUSY,
    TITAN_APPLICATION_HANDOFF_FAILED,
    TITAN_APPLICATION_EXEC_FAILED,
    TITAN_APPLICATION_NOT_INSTALLED,
    TITAN_APPLICATION_UNINSTALL_DENIED
} TitanApplicationResult;

typedef struct TitanApplication
{
    char title[TITAN_APPLICATION_TITLE_CAPACITY];
    char title_id[TITAN_APPLICATION_TITLE_ID_CAPACITY];
    char version[TITAN_APPLICATION_VERSION_CAPACITY];
    char author[TITAN_APPLICATION_AUTHOR_CAPACITY];
    char description[TITAN_APPLICATION_DESCRIPTION_CAPACITY];
    char credits[TITAN_APPLICATION_CREDITS_CAPACITY];
    char source[TITAN_APPLICATION_SOURCE_CAPACITY];
    char root_path[TITAN_APPLICATION_PATH_CAPACITY];
    char path[TITAN_APPLICATION_PATH_CAPACITY];
    char icon_path[TITAN_APPLICATION_PATH_CAPACITY];
    char tile_path[TITAN_APPLICATION_PATH_CAPACITY];
    char background_path[TITAN_APPLICATION_PATH_CAPACITY];
    unsigned int api_version;
    TitanApplicationType type;
    TitanApplicationClassification classification;
    TitanApplicationMetadataSource metadata_source;
    int installed;
    unsigned int sas_compatible;
    unsigned int sas_compliant;
    unsigned int sas_has_subfolders;
    unsigned int sas_osdsys_manageable;
    unsigned int sas_title_has_boot;
    unsigned int sas_prefix_known;
    TitanSasProfile sas_profile;
    TitanSasCopyability sas_copyability;
} TitanApplication;

void TitanApplications_Initialize(void);
unsigned int TitanApplications_Refresh(void);
unsigned int TitanApplications_RefreshFromRoot(const char *root_path);
int TitanApplications_AddInstalledDirectory(const char *directory_path);
void TitanApplications_ReplaceCatalog(
    const TitanApplication *applications,
    unsigned int count
);
unsigned int TitanApplications_GetCount(void);
unsigned int TitanApplications_GetCatalogGeneration(void);
const TitanApplication *TitanApplications_Get(unsigned int index);
int TitanApplications_IsSystemBrowser(unsigned int index);
unsigned int TitanApplications_GetHomeTileCount(void);
const TitanApplication *TitanApplications_GetHomeTile(
    unsigned int visible_index,
    unsigned int *application_index
);
int TitanApplications_IsNative(unsigned int index);
int TitanApplications_IsGame(unsigned int index);
const char *TitanApplications_GetClassificationName(
    TitanApplicationClassification classification
);
const char *TitanApplications_GetMetadataSourceName(
    TitanApplicationMetadataSource source
);
TitanApplicationExecutionModel TitanApplications_GetExecutionModel(unsigned int index);
const char *TitanApplications_GetExecutionModelName(TitanApplicationExecutionModel model);
int TitanApplications_IsSas(unsigned int index);
int TitanApplications_IsSasCompatible(unsigned int index);
int TitanApplications_IsSasCompliant(unsigned int index);
int TitanApplications_IsSasStateValid(const TitanApplication *application);
int TitanApplications_ReadInstalledManifest(
    const char *directory_path,
    TitanApplication *application
);
int TitanApplications_IsInstalled(unsigned int index);
TitanApplicationResult TitanApplications_Launch(unsigned int index);
int TitanApplications_GetUninstallTarget(
    unsigned int index,
    char *root_path,
    unsigned int root_capacity,
    char *title_id,
    unsigned int title_id_capacity,
    char *title,
    unsigned int title_capacity
);
int TitanApplications_RemoveInstalledFromCatalog(const char *title_id);
const char *TitanApplications_GetResultName(TitanApplicationResult result);

#endif
