#include "titan_vram_boot_guard.h"
#include "titan_boot_recovery.h"
#include "titan_pikavr.h"
#include "titan_video_memory.h"
#include "titan_diagnostics.h"
#include "titan_target_memory.h"
#include <string.h>

typedef struct TitanVramBootGuardState { TitanVramBootPhase phase; TitanVramPressureState pressure; uint32_t largest_recent_allocation,fail_total,fail_critical,fail_optional,last_failed_size,reclaims,theme_frames; TitanVramAllocationClass last_failed_class; TitanVramBootPhase last_failed_phase; char last_failed_resource[32]; TitanVramBootSnapshot history[TITAN_VRAM_BOOT_HISTORY_CAPACITY]; unsigned int history_count; unsigned int recovery_armed; } TitanVramBootGuardState;
static TitanVramBootGuardState g_guard;
#ifdef TITAN_VRAM_FAULT_INJECTION
typedef struct TitanVramFaultState {uint32_t fail_after_n,fail_ge_bytes,attempts;int enabled;} TitanVramFaultState;
static TitanVramFaultState g_fault;
#endif
static uint32_t sat_add(uint32_t a,uint32_t b){return UINT32_MAX-a<b?UINT32_MAX:a+b;}
static TitanVramAllocationClass classify(const char*n){ if(!n)return TITAN_VRAM_ALLOC_CRITICAL; if(strstr(n,"theme")||strstr(n,"decor")||strstr(n,"particle")||strstr(n,"prefetch")||strstr(n,"artwork")||strstr(n,"background-optional")) return TITAN_VRAM_ALLOC_OPTIONAL; return TITAN_VRAM_ALLOC_CRITICAL; }
static TitanVramPressureState pressure_from_free(uint32_t free_bytes,TitanVramPressureState old){
 if(old==TITAN_VRAM_PRESSURE_CRITICAL&&free_bytes<TITAN_VRAM_RECOVERY_HEADROOM_BYTES)return TITAN_VRAM_PRESSURE_CRITICAL;
 if(free_bytes<=TITAN_VRAM_CRITICAL_HEADROOM_BYTES)return TITAN_VRAM_PRESSURE_CRITICAL;
 if(free_bytes<=TITAN_VRAM_WARNING_HEADROOM_BYTES)return TITAN_VRAM_PRESSURE_WARNING;
 return TITAN_VRAM_PRESSURE_NORMAL;
}
static uint32_t allocation_bytes_matching(const char *needle){uint32_t total=0;unsigned int i;for(i=0;i<TITAN_VIDEO_MEMORY_MAX_ALLOCATIONS;i++){const TitanVideoMemoryAllocation*a=TitanVideoMemory_GetAllocation(i);if(a&&strstr(a->name,needle))total=sat_add(total,a->bytes);}return total;}
void TitanVramBootGuard_Initialize(void){memset(&g_guard,0,sizeof(g_guard));TitanBootRecovery_Initialize();g_guard.phase=TITAN_VRAM_PHASE_VIDEO_INIT;g_guard.pressure=TITAN_VRAM_PRESSURE_NORMAL;
#ifdef TITAN_VRAM_FAULT_INJECTION
memset(&g_fault,0,sizeof(g_fault));
#endif
}
#ifdef TITAN_VRAM_FAULT_INJECTION
void TitanVramBootGuard_SetFaultInjection(uint32_t n,uint32_t ge){memset(&g_fault,0,sizeof(g_fault));g_fault.fail_after_n=n;g_fault.fail_ge_bytes=ge;g_fault.enabled=1;}
void TitanVramBootGuard_ResetFaultInjection(void){memset(&g_fault,0,sizeof(g_fault));}
int TitanVramBootGuard_ShouldInjectFailure(uint32_t bytes){if(!g_fault.enabled)return 0;g_fault.attempts++;if(g_fault.fail_ge_bytes!=0u&&bytes>=g_fault.fail_ge_bytes)return 1;if(g_fault.fail_after_n!=0u&&g_fault.attempts>g_fault.fail_after_n)return 1;return 0;}
#else
int TitanVramBootGuard_ShouldInjectFailure(uint32_t bytes){(void)bytes;return 0;}
#endif
const char *TitanVramBootGuard_PhaseName(TitanVramBootPhase p){static const char*n[]={"VIDEO_INIT","CORE_UI","FONT","WAVES_RIBBON","WELCOME","PROFILE","THEME_BASE","HOME","OPTIONAL_ASSETS"};return p<TITAN_VRAM_PHASE_COUNT?n[p]:"UNKNOWN";}
void TitanVramBootGuard_Capture(void){TitanPikaVrTelemetry t;TitanTargetMemoryTelemetry m;TitanVramBootSnapshot*s;uint32_t reserved=TitanVideoMemory_GetReservedBytes();uint32_t physical_free=reserved<TITAN_VIDEO_MEMORY_TOTAL_BYTES?TITAN_VIDEO_MEMORY_TOTAL_BYTES-reserved:0u;uint32_t free_bytes;TitanPikaVr_GetTelemetry(&t);TitanTargetMemory_Capture();TitanTargetMemory_GetTelemetry(&m);free_bytes=physical_free>t.vram_reserved?physical_free-t.vram_reserved:0u;g_guard.pressure=pressure_from_free(free_bytes,g_guard.pressure);if(g_guard.recovery_armed)TitanBootRecovery_NotifyVramPressure((unsigned int)g_guard.pressure,(unsigned int)g_guard.phase);if(g_guard.history_count<TITAN_VRAM_BOOT_HISTORY_CAPACITY)s=&g_guard.history[g_guard.history_count++];else{s=&g_guard.history[TITAN_VRAM_BOOT_HISTORY_CAPACITY-1u];memmove(g_guard.history,g_guard.history+1,(TITAN_VRAM_BOOT_HISTORY_CAPACITY-1u)*sizeof(*s));}memset(s,0,sizeof(*s));s->phase=g_guard.phase;s->pressure=g_guard.pressure;s->vram_total=TITAN_VIDEO_MEMORY_TOTAL_BYTES;s->vram_used=reserved;s->vram_free=free_bytes;s->vram_reserved=t.vram_reserved;s->vram_framebuffers=t.vram_framebuffers;s->vram_textures=t.vram_textures;s->vram_theme=t.vram_theme;s->vram_font=allocation_bytes_matching("font");s->vram_clut=t.vram_clut;s->vram_transient=t.vram_transient;s->resident_textures=t.textures_resident;s->resident_theme_frames=g_guard.theme_frames;s->largest_recent_allocation=g_guard.largest_recent_allocation;s->allocation_failures=g_guard.fail_total;s->allocation_failures_critical=g_guard.fail_critical;s->allocation_failures_optional=g_guard.fail_optional;s->last_failed_size=g_guard.last_failed_size;s->last_failed_class=g_guard.last_failed_class;s->last_failed_phase=g_guard.last_failed_phase;strncpy(s->last_failed_resource,g_guard.last_failed_resource,sizeof(s->last_failed_resource)-1u);s->reclaims=g_guard.reclaims;s->evictions=t.textures_evicted;s->palettized_bytes_saved=t.palettized_bytes_saved;s->dedupe_bytes_saved=t.dedupe_bytes_saved;s->ee_ram_valid=m.ee_valid;s->ee_used=m.ee_used_estimate;s->ee_free=m.ee_free_estimate;s->ee_peak=m.ee_peak_used;s->ee_min_free=m.ee_min_free;s->ee_failures=m.ee_allocation_failures;s->iop_ram_valid=m.iop_ram_valid;s->iop_used=m.iop_used;s->iop_free=m.iop_free;s->iop_min_free=m.iop_min_free;s->iop_modules_known=m.iop_modules_known_resident;TitanDiagnostics_Event(TITAN_DIAG_INFO,TITAN_DIAG_PIKAVR,0x650u,(int32_t)g_guard.pressure,(uint32_t)g_guard.phase,reserved,free_bytes,g_guard.fail_total);g_guard.largest_recent_allocation=0u;}
void TitanVramBootGuard_ArmRecovery(void){g_guard.recovery_armed=1u;TitanVramBootGuard_ReevaluateAfterVideoModeChange();}
void TitanVramBootGuard_ReevaluateAfterVideoModeChange(void){TitanPikaVrTelemetry t;uint32_t physical_free,usable;TitanPikaVr_GetTelemetry(&t);physical_free=TitanVideoMemory_GetFreeBytes();usable=physical_free>t.vram_reserved?physical_free-t.vram_reserved:0u;g_guard.pressure=pressure_from_free(usable,g_guard.pressure);if(g_guard.recovery_armed&&g_guard.pressure==TITAN_VRAM_PRESSURE_CRITICAL)TitanBootRecovery_NotifyVramPressure((unsigned int)g_guard.pressure,(unsigned int)g_guard.phase);else if(usable>=TITAN_VRAM_RECOVERY_HEADROOM_BYTES&&g_guard.fail_critical==0u)(void)TitanBootRecovery_ClearTransientVramCritical();}
void TitanVramBootGuard_EnterPhase(TitanVramBootPhase phase){if(phase>=TITAN_VRAM_PHASE_COUNT||phase==g_guard.phase)return;g_guard.phase=phase;TitanVramBootGuard_Capture();if(phase==TITAN_VRAM_PHASE_HOME)TitanBootRecovery_MarkHomeReady();}
int TitanVramBootGuard_AllowAllocation(const char*r,uint32_t bytes){TitanPikaVrTelemetry t;uint32_t physical_free,usable;TitanVramAllocationClass c=classify(r);if(c==TITAN_VRAM_ALLOC_CRITICAL)return 1;if(TitanBootRecovery_IsMinimum())return 0;TitanPikaVr_GetTelemetry(&t);physical_free=TitanVideoMemory_GetFreeBytes();usable=physical_free>t.vram_reserved?physical_free-t.vram_reserved:0u;g_guard.pressure=pressure_from_free(usable,g_guard.pressure);if(g_guard.pressure==TITAN_VRAM_PRESSURE_CRITICAL)return 0;if(bytes>usable)return 0;if(g_guard.pressure==TITAN_VRAM_PRESSURE_WARNING&&usable-bytes<TITAN_VRAM_CRITICAL_HEADROOM_BYTES)return 0;return 1;}
void TitanVramBootGuard_RecordAllocation(const char*r,uint32_t bytes,int success){TitanVramAllocationClass c=classify(r);if(bytes>g_guard.largest_recent_allocation)g_guard.largest_recent_allocation=bytes;if(success)return;g_guard.fail_total=sat_add(g_guard.fail_total,1u);if(c==TITAN_VRAM_ALLOC_CRITICAL){g_guard.fail_critical=sat_add(g_guard.fail_critical,1u);TitanBootRecovery_NotifyCriticalAllocationFailure((unsigned int)g_guard.phase);}else g_guard.fail_optional=sat_add(g_guard.fail_optional,1u);g_guard.last_failed_size=bytes;g_guard.last_failed_class=c;g_guard.last_failed_phase=g_guard.phase;if(r){strncpy(g_guard.last_failed_resource,r,sizeof(g_guard.last_failed_resource)-1u);g_guard.last_failed_resource[sizeof(g_guard.last_failed_resource)-1u]='\0';}TitanDiagnostics_Event(c==TITAN_VRAM_ALLOC_CRITICAL?TITAN_DIAG_ERROR:TITAN_DIAG_WARN,TITAN_DIAG_PIKAVR,0x651u,(int32_t)c,bytes,(uint32_t)g_guard.phase,g_guard.fail_total,0u);}
void TitanVramBootGuard_RecordReclaim(uint32_t c){g_guard.reclaims=sat_add(g_guard.reclaims,c);}
void TitanVramBootGuard_SetThemeResidentFrames(uint32_t c){g_guard.theme_frames=c>3u?3u:c;}
uint32_t TitanVramBootGuard_GetThemeResidentFrames(void){return g_guard.theme_frames;}
TitanVramPressureState TitanVramBootGuard_GetPressureState(void){return g_guard.pressure;}
unsigned int TitanVramBootGuard_GetHistoryCount(void){return g_guard.history_count;}
const TitanVramBootSnapshot *TitanVramBootGuard_GetHistory(unsigned int i){return i<g_guard.history_count?&g_guard.history[i]:0;}
