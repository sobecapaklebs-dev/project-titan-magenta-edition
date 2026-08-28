#ifndef PROJECT_TITAN_SESSION_SERVICE_H
#define PROJECT_TITAN_SESSION_SERVICE_H

#include <stdint.h>

#define TITAN_SESSION_SERVICE_API_VERSION 1u

#define TITAN_SESSION_ID_CAPACITY 32u
#define TITAN_SESSION_TITLE_ID_CAPACITY 24u
#define TITAN_SESSION_PATH_CAPACITY 256u
#define TITAN_SESSION_PROFILE_CAPACITY 40u
#define TITAN_SESSION_VMC_CAPACITY 64u

#define TITAN_SESSION_CAP_SAFE_RETURN       (1u << 0)
#define TITAN_SESSION_CAP_OVERLAY_MINIMAL   (1u << 1)
#define TITAN_SESSION_CAP_OVERLAY_FULL      (1u << 2)
#define TITAN_SESSION_CAP_RESTART           (1u << 3)
#define TITAN_SESSION_CAP_INPUT_NORMALIZED  (1u << 4)
#define TITAN_SESSION_CAP_STORAGE_QUIESCE   (1u << 5)
#define TITAN_SESSION_CAP_GS_STATE_RESTORE  (1u << 6)
#define TITAN_SESSION_CAP_NATIVE_SAFEPOINT  (1u << 7)

typedef enum TitanSessionState {
    TITAN_SESSION_IDLE = 0,
    TITAN_SESSION_PREPARING,
    TITAN_SESSION_LAUNCHING,
    TITAN_SESSION_RUNNING,
    TITAN_SESSION_OVERLAY_ACTIVE,
    TITAN_SESSION_RETURN_REQUESTED,
    TITAN_SESSION_RETURNING,
    TITAN_SESSION_FAILED,
    TITAN_SESSION_RECOVERING
} TitanSessionState;

typedef enum TitanSessionEngine {
    TITAN_SESSION_ENGINE_NONE = 0,
    TITAN_SESSION_ENGINE_NATIVE,
    TITAN_SESSION_ENGINE_NEUTRINO,
    TITAN_SESSION_ENGINE_UL,
    TITAN_SESSION_ENGINE_ELF,
    TITAN_SESSION_ENGINE_SAS,
    TITAN_SESSION_ENGINE_DISC
} TitanSessionEngine;

typedef enum TitanIgrCompatibility {
    TITAN_IGR_SUPPORTED = 0,
    TITAN_IGR_SUPPORTED_WITH_PROFILE,
    TITAN_IGR_UNKNOWN,
    TITAN_IGR_DISABLED,
    TITAN_IGR_INCOMPATIBLE
} TitanIgrCompatibility;

typedef enum TitanOverlayLevel {
    TITAN_OVERLAY_FULL = 0,
    TITAN_OVERLAY_MINIMAL,
    TITAN_OVERLAY_RETURN_ONLY,
    TITAN_OVERLAY_LEGACY,
    TITAN_OVERLAY_DISABLED
} TitanOverlayLevel;

typedef struct TitanSessionDescriptor {
    char session_id[TITAN_SESSION_ID_CAPACITY];
    char title_id[TITAN_SESSION_TITLE_ID_CAPACITY];
    char launch_path[TITAN_SESSION_PATH_CAPACITY];
    char profile[TITAN_SESSION_PROFILE_CAPACITY];
    char vmc[TITAN_SESSION_VMC_CAPACITY];
    uint32_t classification;
    TitanSessionEngine engine;
    uint32_t launch_source;
    uint32_t video_mode;
    uint32_t input_configuration;
    uint32_t compatibility_profile;
    uint32_t runtime_capabilities;
    TitanIgrCompatibility igr_compatibility;
    TitanOverlayLevel overlay_level;
    uint64_t start_time_ms;
} TitanSessionDescriptor;

typedef struct TitanSessionMetrics {
    uint32_t begin_count;
    uint32_t transition_count;
    uint32_t rejected_transition_count;
    uint32_t overlay_open_count;
    uint32_t return_request_count;
    uint32_t recovery_count;
    uint32_t update_cost_units;
} TitanSessionMetrics;

void TitanSession_Initialize(void);
void TitanSession_Shutdown(void);
int TitanSession_Begin(const TitanSessionDescriptor *descriptor);
int TitanSession_Transition(TitanSessionState next);
int TitanSession_RequestReturn(void);
int TitanSession_EnterOverlay(void);
int TitanSession_Resume(void);
void TitanSession_Fail(void);
void TitanSession_BeginRecovery(void);
void TitanSession_End(void);
void TitanSession_Update(uint64_t now_ms);
TitanSessionState TitanSession_GetState(void);
const TitanSessionDescriptor *TitanSession_GetDescriptor(void);
const TitanSessionMetrics *TitanSession_GetMetrics(void);
uint64_t TitanSession_GetElapsedMs(uint64_t now_ms);
int TitanSession_IsActive(void);
int TitanSession_CanOverlay(void);
int TitanSession_CanReturn(void);

#endif
