#ifndef PROJECT_TITAN_PACKAGE_INSTALLER_H
#define PROJECT_TITAN_PACKAGE_INSTALLER_H

#define TITAN_PACKAGE_CAPACITY 96u
#define TITAN_PACKAGE_TITLE_CAPACITY 80u
#define TITAN_PACKAGE_TITLE_ID_CAPACITY 16u
#define TITAN_PACKAGE_VERSION_CAPACITY 16u
#define TITAN_PACKAGE_FILENAME_CAPACITY 128u
#define TITAN_PACKAGE_PATH_CAPACITY 256u

#define TITAN_PACKAGE_APP_EXTERNAL_ELF 0u
#define TITAN_PACKAGE_APP_TITAN_NATIVE 1u
#define TITAN_PACKAGE_APP_SYSTEM_BROWSER 2u
#define TITAN_PACKAGE_APP_THEME 3u
#define TITAN_PACKAGE_APP_TITAN_GAME 4u

typedef enum TitanPackageInstallerResult
{
    TITAN_PACKAGE_INSTALLER_OK = 0,
    TITAN_PACKAGE_INSTALLER_NO_PACKAGES,
    TITAN_PACKAGE_INSTALLER_INVALID_INDEX,
    TITAN_PACKAGE_INSTALLER_INVALID_PACKAGE,
    TITAN_PACKAGE_INSTALLER_UNSAFE_PATH,
    TITAN_PACKAGE_INSTALLER_CHECKSUM_FAILED,
    TITAN_PACKAGE_INSTALLER_ALREADY_INSTALLED,
    TITAN_PACKAGE_INSTALLER_STORAGE_ERROR,
    TITAN_PACKAGE_INSTALLER_INVALID_ELF,
    TITAN_PACKAGE_INSTALLER_INVALID_TMOD,
    TITAN_PACKAGE_INSTALLER_API_MISMATCH,
    TITAN_PACKAGE_INSTALLER_DEX_REQUIRED,
    TITAN_PACKAGE_INSTALLER_PUBLISH_FAILED,
    TITAN_PACKAGE_INSTALLER_CANCELLED
} TitanPackageInstallerResult;

typedef enum TitanPackageInstallerPhase
{
    TITAN_PACKAGE_PHASE_IDLE = 0,
    TITAN_PACKAGE_PHASE_PREPARING,
    TITAN_PACKAGE_PHASE_EXTRACTING,
    TITAN_PACKAGE_PHASE_VALIDATING,
    TITAN_PACKAGE_PHASE_PUBLISHING,
    TITAN_PACKAGE_PHASE_COMPLETE,
    TITAN_PACKAGE_PHASE_ERROR
} TitanPackageInstallerPhase;

typedef struct TitanPackageInfo
{
    char title[TITAN_PACKAGE_TITLE_CAPACITY];
    char title_id[TITAN_PACKAGE_TITLE_ID_CAPACITY];
    char version[TITAN_PACKAGE_VERSION_CAPACITY];
    char filename[TITAN_PACKAGE_FILENAME_CAPACITY];
    char path[TITAN_PACKAGE_PATH_CAPACITY];
    unsigned int format_version;
    unsigned int app_type;
    unsigned int api_version;
    unsigned int file_count;
    unsigned int payload_size;
    int installed;
} TitanPackageInfo;

typedef struct TitanPackageInstallerStatus
{
    TitanPackageInstallerPhase phase;
    TitanPackageInstallerResult result;
    unsigned int active;
    unsigned int progress_percent;
    unsigned int file_index;
    unsigned int file_count;
    char package_filename[TITAN_PACKAGE_FILENAME_CAPACITY];
    char message[112];
} TitanPackageInstallerStatus;

void TitanPackageInstaller_Initialize(void);
unsigned int TitanPackageInstaller_Refresh(void);
unsigned int TitanPackageInstaller_RefreshThemes(void);
unsigned int TitanPackageInstaller_RefreshFromRoot(const char *root_path);
unsigned int TitanPackageInstaller_GetCount(void);
const TitanPackageInfo *TitanPackageInstaller_Get(unsigned int index);

TitanPackageInstallerResult TitanPackageInstaller_BeginInstall(unsigned int index);
void TitanPackageInstaller_Update(unsigned int byte_budget);
void TitanPackageInstaller_Cancel(void);
int TitanPackageInstaller_IsActive(void);
const TitanPackageInstallerStatus *TitanPackageInstaller_GetStatus(void);
const char *TitanPackageInstaller_GetResultName(TitanPackageInstallerResult result);
const char *TitanPackageInstaller_GetTypeName(unsigned int app_type);

/* Side-effect-free structural package validation used by Download Service before atomic publish. */
int TitanPackageInstaller_ValidatePackageFile(const char *path, TitanPackageInfo *info);

#endif
