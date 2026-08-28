#define TITAN_VRAM_FAULT_INJECTION 1
#include "titan_vram_boot_guard.h"
#include "titan_pikavr.h"
#include "titan_video_memory.h"
#include "titan_diagnostics.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static uint32_t free_bytes=2u*1024u*1024u; static TitanPikaVrTelemetry tele;
void TitanPikaVr_GetTelemetry(TitanPikaVrTelemetry*t){*t=tele;}
unsigned int TitanVideoMemory_GetFreeBytes(void){return free_bytes;}
unsigned int TitanVideoMemory_GetReservedBytes(void){return TITAN_VIDEO_MEMORY_TOTAL_BYTES-free_bytes;}
const TitanVideoMemoryAllocation*TitanVideoMemory_GetAllocation(unsigned int i){(void)i;return 0;}
void TitanDiagnostics_Event(TitanDiagLevel a,TitanDiagSubsystem b,uint16_t c,int32_t d,uint32_t e,uint32_t f,uint32_t g,uint32_t h){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;}
int main(void){memset(&tele,0,sizeof(tele));tele.vram_reserved=0;TitanVramBootGuard_Initialize();
 free_bytes=2u*1024u*1024u;TitanVramBootGuard_Capture();assert(TitanVramBootGuard_GetPressureState()==TITAN_VRAM_PRESSURE_NORMAL);assert(TitanVramBootGuard_AllowAllocation("theme-artwork",65536u));
 free_bytes=400u*1024u;TitanVramBootGuard_Capture();assert(TitanVramBootGuard_GetPressureState()==TITAN_VRAM_PRESSURE_WARNING);assert(TitanVramBootGuard_AllowAllocation("font-atlas",512u*1024u));
 free_bytes=200u*1024u;TitanVramBootGuard_Capture();assert(TitanVramBootGuard_GetPressureState()==TITAN_VRAM_PRESSURE_CRITICAL);assert(!TitanVramBootGuard_AllowAllocation("theme-artwork",4096u));assert(TitanVramBootGuard_AllowAllocation("framebuffer",1024u*1024u));
 TitanVramBootGuard_SetFaultInjection(2u,0u);assert(!TitanVramBootGuard_ShouldInjectFailure(32u));assert(!TitanVramBootGuard_ShouldInjectFailure(32u));assert(TitanVramBootGuard_ShouldInjectFailure(32u));
 TitanVramBootGuard_SetFaultInjection(0u,65536u);assert(!TitanVramBootGuard_ShouldInjectFailure(32768u));assert(TitanVramBootGuard_ShouldInjectFailure(65536u));TitanVramBootGuard_ResetFaultInjection();
 puts("titan-vram-fault-injection: PASS");return 0;}
