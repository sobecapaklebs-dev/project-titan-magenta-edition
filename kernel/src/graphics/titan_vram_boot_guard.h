#ifndef PROJECT_TITAN_VRAM_BOOT_GUARD_H
#define PROJECT_TITAN_VRAM_BOOT_GUARD_H
#include <stdint.h>
#define TITAN_VRAM_BOOT_HISTORY_CAPACITY 16u
#define TITAN_VRAM_WARNING_HEADROOM_BYTES (512u * 1024u)
#define TITAN_VRAM_CRITICAL_HEADROOM_BYTES (256u * 1024u)
#define TITAN_VRAM_RECOVERY_HEADROOM_BYTES (640u * 1024u)
typedef enum TitanVramBootPhase {
 TITAN_VRAM_PHASE_VIDEO_INIT=0, TITAN_VRAM_PHASE_CORE_UI, TITAN_VRAM_PHASE_FONT,
 TITAN_VRAM_PHASE_WAVES_RIBBON, TITAN_VRAM_PHASE_WELCOME, TITAN_VRAM_PHASE_PROFILE,
 TITAN_VRAM_PHASE_THEME_BASE, TITAN_VRAM_PHASE_HOME, TITAN_VRAM_PHASE_OPTIONAL_ASSETS,
 TITAN_VRAM_PHASE_COUNT
} TitanVramBootPhase;
typedef enum TitanVramPressureState { TITAN_VRAM_PRESSURE_NORMAL=0, TITAN_VRAM_PRESSURE_WARNING, TITAN_VRAM_PRESSURE_CRITICAL } TitanVramPressureState;
typedef enum TitanVramAllocationClass { TITAN_VRAM_ALLOC_CRITICAL=0, TITAN_VRAM_ALLOC_OPTIONAL=1 } TitanVramAllocationClass;
typedef struct TitanVramBootSnapshot {
 TitanVramBootPhase phase; TitanVramPressureState pressure;
 uint32_t vram_total,vram_used,vram_free,vram_reserved,vram_framebuffers,vram_textures,vram_theme,vram_font,vram_clut,vram_transient;
 uint32_t resident_textures,resident_theme_frames,largest_recent_allocation;
 uint32_t allocation_failures,allocation_failures_critical,allocation_failures_optional,last_failed_size;
 TitanVramAllocationClass last_failed_class; TitanVramBootPhase last_failed_phase;
 char last_failed_resource[32]; uint32_t reclaims,evictions,palettized_bytes_saved,dedupe_bytes_saved;
 unsigned int ee_ram_valid,iop_ram_valid; uint32_t ee_used,ee_free,ee_peak,ee_min_free,ee_failures; uint32_t iop_used,iop_free,iop_min_free,iop_modules_known;
} TitanVramBootSnapshot;
void TitanVramBootGuard_Initialize(void);
void TitanVramBootGuard_ArmRecovery(void);
void TitanVramBootGuard_ReevaluateAfterVideoModeChange(void);
void TitanVramBootGuard_EnterPhase(TitanVramBootPhase phase);
void TitanVramBootGuard_Capture(void);
void TitanVramBootGuard_RecordAllocation(const char *resource,uint32_t bytes,int success);
int TitanVramBootGuard_AllowAllocation(const char *resource,uint32_t request_bytes);
void TitanVramBootGuard_RecordReclaim(uint32_t count);
void TitanVramBootGuard_SetThemeResidentFrames(uint32_t count);
uint32_t TitanVramBootGuard_GetThemeResidentFrames(void);
TitanVramPressureState TitanVramBootGuard_GetPressureState(void);
unsigned int TitanVramBootGuard_GetHistoryCount(void);
const TitanVramBootSnapshot *TitanVramBootGuard_GetHistory(unsigned int index);
const char *TitanVramBootGuard_PhaseName(TitanVramBootPhase phase);
#ifdef TITAN_VRAM_FAULT_INJECTION
void TitanVramBootGuard_SetFaultInjection(uint32_t fail_after_n,uint32_t fail_ge_bytes);
void TitanVramBootGuard_ResetFaultInjection(void);
#endif
int TitanVramBootGuard_ShouldInjectFailure(uint32_t request_bytes);
#endif
