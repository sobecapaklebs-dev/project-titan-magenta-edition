#ifndef PROJECT_TITAN_DISC_ENGINE_H
#define PROJECT_TITAN_DISC_ENGINE_H

#include "titan_disc_service.h"
#include "titan_session_service.h"

#define TITAN_DISC_ENGINE_API_VERSION 1u

#define TITAN_DISC_SESSION_SOURCE_CAPACITY 24u
#define TITAN_DISC_SESSION_PROFILE_CAPACITY 64u
#define TITAN_DISC_SESSION_VMC_CAPACITY 128u
#define TITAN_DISC_SESSION_BACKEND_CAPACITY 32u

typedef enum TitanDiscLaunchState {
    TITAN_DISC_LAUNCH_IDLE = 0,
    TITAN_DISC_LAUNCH_PREPARED,
    TITAN_DISC_LAUNCH_HANDOFF,
    TITAN_DISC_LAUNCH_FAILED
} TitanDiscLaunchState;

typedef struct TitanDiscSession {
    TitanDiscMediaType media_type;
    TitanDiscRegion region;
    TitanDiscLaunchState launch_state;
    char title_id[TITAN_DISC_TITLE_ID_CAPACITY];
    char title[TITAN_DISC_TITLE_CAPACITY];
    char boot_path[TITAN_DISC_BOOT_PATH_CAPACITY];
    char source_device[TITAN_DISC_SESSION_SOURCE_CAPACITY];
    char profile[TITAN_DISC_SESSION_PROFILE_CAPACITY];
    char vmc_argument[TITAN_DISC_SESSION_VMC_CAPACITY];
    char launch_backend[TITAN_DISC_SESSION_BACKEND_CAPACITY];
    uint32_t compatibility_profile;
    TitanIgrCompatibility igr_compatibility;
    TitanOverlayLevel overlay_level;
    int vmc_enabled;
} TitanDiscSession;

typedef int (*TitanDiscLaunchFn)(const char *boot_path, const char *vmc_argument, void *context);

void TitanDiscEngine_Initialize(void);
void TitanDiscEngine_Shutdown(void);
int TitanDiscEngine_Prepare(void);
int TitanDiscEngine_Launch(void);
const TitanDiscSession *TitanDiscEngine_GetSession(void);
const char *TitanDiscEngine_GetLastError(void);
void TitanDiscEngine_SetLaunchFunction(TitanDiscLaunchFn launch_fn, void *context);

#endif
