#ifndef PROJECT_TITAN_APP_RUNTIME_H
#define PROJECT_TITAN_APP_RUNTIME_H

#include <stdint.h>

typedef struct TitanInputState TitanInputState;

#define TITAN_APP_RUNTIME_PATH_CAPACITY 256u
#define TITAN_APP_RUNTIME_TITLE_CAPACITY 80u

typedef enum TitanAppRuntimeClass
{
    TITAN_APP_RUNTIME_CLASS_NONE = 0,
    TITAN_APP_RUNTIME_CLASS_MODULE,
    TITAN_APP_RUNTIME_CLASS_GAME
} TitanAppRuntimeClass;

typedef enum TitanAppLifecycleState
{
    TITAN_APP_LIFECYCLE_IDLE = 0,
    TITAN_APP_LIFECYCLE_PREPARE,
    TITAN_APP_LIFECYCLE_LOAD,
    TITAN_APP_LIFECYCLE_CREATE,
    TITAN_APP_LIFECYCLE_START,
    TITAN_APP_LIFECYCLE_ACTIVE,
    TITAN_APP_LIFECYCLE_EXIT_REQUEST,
    TITAN_APP_LIFECYCLE_QUIESCE,
    TITAN_APP_LIFECYCLE_DESTROY,
    TITAN_APP_LIFECYCLE_FAILED
} TitanAppLifecycleState;

typedef enum TitanAppExitReason
{
    TITAN_APP_EXIT_NONE = 0,
    TITAN_APP_EXIT_USER_CHORD,
    TITAN_APP_EXIT_APP_REQUEST,
    TITAN_APP_EXIT_SYSTEM_SHUTDOWN,
    TITAN_APP_EXIT_LAUNCH_FAILURE
} TitanAppExitReason;

typedef struct TitanAppRuntimeContext
{
    TitanAppLifecycleState lifecycle;
    TitanAppRuntimeClass runtime_class;
    TitanAppExitReason exit_reason;
    uint32_t allocation_count;
    uint32_t allocation_bytes;
    uint8_t storage_exclusive;
    uint8_t active;
    uint8_t launching;
    uint8_t fullscreen;
    char title[TITAN_APP_RUNTIME_TITLE_CAPACITY];
    char root_path[TITAN_APP_RUNTIME_PATH_CAPACITY];
} TitanAppRuntimeContext;

int TitanAppRuntime_ValidateModuleFile(const char *path);
/* Host/release guard: performs allocation + relocation but never executes app code. */
int TitanAppRuntime_ValidateRuntimeModuleFile(const char *path);
int TitanAppRuntime_ValidateGameModuleFile(const char *path);
int TitanAppRuntime_Open(
    const char *title,
    const char *root_path,
    const char *module_path,
    const char *background_path,
    int trusted_game
);
void TitanAppRuntime_Update(const TitanInputState *input, unsigned int delta_ms);
void TitanAppRuntime_Draw(unsigned int frame);
void TitanAppRuntime_Close(void);
void TitanAppRuntime_QuiesceForSystemShutdown(void);
int TitanAppRuntime_IsActive(void);
int TitanAppRuntime_IsLaunching(void);
int TitanAppRuntime_OwnsShell(void);
/* True while a Titan Game worker has exclusive ownership of the GS frame lane. */
int TitanAppRuntime_OwnsExclusiveVideo(void);
int TitanAppRuntime_IsFullscreen(void);
void TitanAppRuntime_GetContext(TitanAppRuntimeContext *context);
const char *TitanAppRuntime_GetTitle(void);
const char *TitanAppRuntime_GetLastError(void);

#endif
