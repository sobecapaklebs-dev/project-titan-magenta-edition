#ifndef PROJECT_TITAN_GAME_PATCHES_H
#define PROJECT_TITAN_GAME_PATCHES_H

#define TITAN_GAME_PATCH_ENTRY_CAPACITY 64u
#define TITAN_GAME_PATCH_NAME_CAPACITY 96u
#define TITAN_GAME_PATCH_PAYLOAD_PATH_CAPACITY 256u

typedef enum TitanGamePatchKind
{
    TITAN_GAME_PATCH_CHEAT = 0,
    TITAN_GAME_PATCH_ENHANCEMENT,
    TITAN_GAME_PATCH_KIND_COUNT
} TitanGamePatchKind;

typedef enum TitanGamePatchResult
{
    TITAN_GAME_PATCH_OK = 0,
    TITAN_GAME_PATCH_INVALID_STARTUP,
    TITAN_GAME_PATCH_IO_ERROR,
    TITAN_GAME_PATCH_INVALID_FILE,
    TITAN_GAME_PATCH_TOO_LARGE,
    TITAN_GAME_PATCH_CODE_OVERFLOW,
    TITAN_GAME_PATCH_UL_RUNTIME_FAILED
} TitanGamePatchResult;

typedef struct TitanGamePatchEntryInfo
{
    char name[TITAN_GAME_PATCH_NAME_CAPACITY];
    int enabled;
    int automatic;
    int experimental;
    unsigned int code_count;
} TitanGamePatchEntryInfo;

typedef struct TitanGamePatchLaunchState
{
    int gameplay_active;
    int enhancement_active;
    unsigned int gameplay_count;
    unsigned int enhancement_count;
    char payload_path[TITAN_GAME_PATCH_PAYLOAD_PATH_CAPACITY];
    const char *taunt;
} TitanGamePatchLaunchState;

void TitanGamePatches_Initialize(void);
TitanGamePatchResult TitanGamePatches_Load(
    const char *startup,
    TitanGamePatchKind kind
);
unsigned int TitanGamePatches_GetEntryCount(TitanGamePatchKind kind);
int TitanGamePatches_GetEntry(
    TitanGamePatchKind kind,
    unsigned int visible_index,
    TitanGamePatchEntryInfo *info
);
TitanGamePatchResult TitanGamePatches_ToggleEntry(
    TitanGamePatchKind kind,
    unsigned int visible_index
);
TitanGamePatchResult TitanGamePatches_PrepareLaunch(
    const char *startup,
    TitanGamePatchLaunchState *state
);
TitanGamePatchResult TitanGamePatches_PrepareUlGoldenRuntime(
    const char *startup
);
void TitanGamePatches_RecoverUlGoldenRuntime(void);
const char *TitanGamePatches_GetKindTitle(TitanGamePatchKind kind);
const char *TitanGamePatches_GetResultName(TitanGamePatchResult result);

#endif
