#ifndef PROJECT_TITAN_DIAGNOSTICS_H
#define PROJECT_TITAN_DIAGNOSTICS_H

#include <stdint.h>

typedef enum TitanDiagSubsystem {
    TITAN_DIAG_BOOT=1, TITAN_DIAG_KERNEL, TITAN_DIAG_SHELL, TITAN_DIAG_INPUT,
    TITAN_DIAG_AUDIO, TITAN_DIAG_STORAGE, TITAN_DIAG_LIBRARY, TITAN_DIAG_PACKAGE,
    TITAN_DIAG_UPDATE, TITAN_DIAG_VMC, TITAN_DIAG_NETWORK, TITAN_DIAG_WIFI,
    TITAN_DIAG_NETMAN, TITAN_DIAG_PS2IP, TITAN_DIAG_ASSET, TITAN_DIAG_BROWSER,
    TITAN_DIAG_THEME, TITAN_DIAG_VIDEO, TITAN_DIAG_MEMORY, TITAN_DIAG_WATCHDOG,
    TITAN_DIAG_PIKAVR, TITAN_DIAG_SAS, TITAN_DIAG_BADGES, TITAN_DIAG_VLOMBIS,
    TITAN_DIAG_APP_RUNTIME
} TitanDiagSubsystem;

typedef enum TitanDiagLevel { TITAN_DIAG_ERROR=1, TITAN_DIAG_WARN, TITAN_DIAG_INFO, TITAN_DIAG_DEBUG, TITAN_DIAG_TRACE } TitanDiagLevel;

typedef enum TitanDiagPhase {
    TITAN_DIAG_PHASE_INPUT = 0,
    TITAN_DIAG_PHASE_AUDIO,
    TITAN_DIAG_PHASE_SERVICES,
    TITAN_DIAG_PHASE_UI,
    TITAN_DIAG_PHASE_RENDER,
    TITAN_DIAG_PHASE_COUNT
} TitanDiagPhase;

#define TITAN_DIAG_OPERATION_AREA_CAPACITY 16u
#define TITAN_DIAG_OPERATION_STAGE_CAPACITY 24u
#define TITAN_DIAG_OPERATION_MESSAGE_CAPACITY 64u

typedef struct TitanDiagnosticsOperationStatus {
    int active;
    TitanDiagLevel level;
    int progress_percent; /* -1 when progress is not applicable */
    uint32_t updated_ms;
    char area[TITAN_DIAG_OPERATION_AREA_CAPACITY];
    char stage[TITAN_DIAG_OPERATION_STAGE_CAPACITY];
    char message[TITAN_DIAG_OPERATION_MESSAGE_CAPACITY];
} TitanDiagnosticsOperationStatus;

typedef struct TitanDiagPerfSnapshot {
    uint32_t frame_index;
    uint32_t frame_us;
    uint32_t frame_avg_us;
    uint32_t frame_p95_us;
    uint32_t frame_worst_us;
    uint32_t fps_x100;
    uint32_t spikes;
    uint32_t ee_ram_used_kb;
    uint32_t network_state;
    uint32_t asset_queue_hint;
    uint32_t storage_busy;
    uint32_t heartbeat_main;
    uint32_t heartbeat_storage;
    uint32_t phase_input_us;
    uint32_t phase_audio_us;
    uint32_t phase_services_us;
    uint32_t phase_ui_us;
    uint32_t phase_render_us;
} TitanDiagPerfSnapshot;

void TitanDiagnostics_Initialize(void);
void TitanDiagnostics_Shutdown(void);
void TitanDiagnostics_Event(TitanDiagLevel level, TitanDiagSubsystem subsystem, uint16_t event, int32_t result, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
void TitanDiagnostics_BeginFrame(uint32_t frame_index);
void TitanDiagnostics_EndFrame(uint32_t frame_index);
void TitanDiagnostics_UpdateMainThread(uint32_t frame_index);
void TitanDiagnostics_StorageHeartbeat(void);
void TitanDiagnostics_PhaseBegin(TitanDiagPhase phase);
void TitanDiagnostics_PhaseEnd(TitanDiagPhase phase);
int TitanDiagnostics_HasStorageWork(void);
int TitanDiagnostics_StorageWorkerPump(void);
const TitanDiagPerfSnapshot *TitanDiagnostics_GetPerfSnapshot(void);
int TitanDiagnostics_ExportTextNow(void);
int TitanDiagnostics_ExportPreviousTextNow(void);
int TitanDiagnostics_HasPreviousSession(void);
void TitanDiagnostics_MarkCleanShutdown(void);
int TitanDiagnostics_IsOverlayEnabled(void);
void TitanDiagnostics_SetOverlayEnabled(int enabled);
void TitanDiagnostics_SetMaximumLevel(TitanDiagLevel maximum_level);
TitanDiagLevel TitanDiagnostics_GetMaximumLevel(void);
void TitanDiagnostics_SetSubsystemEnabled(TitanDiagSubsystem subsystem, int enabled);
int TitanDiagnostics_IsSubsystemEnabled(TitanDiagSubsystem subsystem);

/* DEX-only live operation ribbon. Any subsystem may publish bounded stage,
 * progress, and error text; the UI decides visibility based on DEX mode. */
void TitanDiagnostics_ReportOperation(
    const char *area,
    TitanDiagLevel level,
    const char *stage,
    const char *message,
    int progress_percent
);
void TitanDiagnostics_ClearOperation(const char *area);
int TitanDiagnostics_GetOperationStatus(TitanDiagnosticsOperationStatus *status);

#endif
