#ifndef PROJECT_TITAN_TARGET_MEMORY_H
#define PROJECT_TITAN_TARGET_MEMORY_H

#include <stdint.h>

typedef struct TitanTargetMemoryTelemetry
{
    unsigned int ee_valid;
    uint32_t ee_used_estimate;
    uint32_t ee_free_estimate;
    uint32_t ee_peak_used;
    uint32_t ee_min_free;
    uint32_t ee_allocation_failures;
    unsigned int iop_ram_valid;
    uint32_t iop_used;
    uint32_t iop_free;
    uint32_t iop_min_free;
    uint32_t iop_modules_known_resident;
} TitanTargetMemoryTelemetry;

void TitanTargetMemory_Initialize(void);
void TitanTargetMemory_Capture(void);
void TitanTargetMemory_RecordEeAllocationFailure(void);
void TitanTargetMemory_SetIopKnownResidentModules(uint32_t count);
void TitanTargetMemory_GetTelemetry(TitanTargetMemoryTelemetry *telemetry);

#ifdef TITAN_TARGET_MEMORY_TEST
void TitanTargetMemory_SetHostEeSample(uint32_t used, uint32_t free_bytes);
void TitanTargetMemory_SetHostIopSample(uint32_t used, uint32_t free_bytes, uint32_t modules);
#endif

#endif
