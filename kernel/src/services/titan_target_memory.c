#include "titan_target_memory.h"

#include <limits.h>
#include <string.h>
#include <unistd.h>

#define TITAN_TARGET_EE_RAM_BASE 0x00100000u
#define TITAN_TARGET_EE_RAM_LIMIT 0x02000000u

static TitanTargetMemoryTelemetry g_memory;
#ifdef TITAN_TARGET_MEMORY_TEST
static unsigned int g_host_ee_valid;
static uint32_t g_host_ee_used;
static uint32_t g_host_ee_free;
static unsigned int g_host_iop_valid;
static uint32_t g_host_iop_used;
static uint32_t g_host_iop_free;
#endif

#if defined(__EE__) || defined(_EE) || defined(TITAN_TARGET_MEMORY_TEST)
static void TitanTargetMemory_UpdateEeWatermarks(uint32_t used, uint32_t free_bytes)
{
    g_memory.ee_valid = 1u;
    g_memory.ee_used_estimate = used;
    g_memory.ee_free_estimate = free_bytes;
    if (used > g_memory.ee_peak_used)
        g_memory.ee_peak_used = used;
    if (g_memory.ee_min_free == UINT32_MAX || free_bytes < g_memory.ee_min_free)
        g_memory.ee_min_free = free_bytes;
}
#endif

void TitanTargetMemory_Initialize(void)
{
    memset(&g_memory, 0, sizeof(g_memory));
    g_memory.ee_min_free = UINT32_MAX;
    g_memory.iop_min_free = UINT32_MAX;
#ifdef TITAN_TARGET_MEMORY_TEST
    g_host_ee_valid = 0u;
    g_host_iop_valid = 0u;
#endif
    TitanTargetMemory_Capture();
}

void TitanTargetMemory_Capture(void)
{
#if defined(__EE__) || defined(_EE)
    void *break_pointer = sbrk(0);
    if (break_pointer != (void *)-1)
    {
        uintptr_t current_break = (uintptr_t)break_pointer;
        uint32_t used;
        uint32_t free_bytes;
        if (current_break <= TITAN_TARGET_EE_RAM_BASE)
            used = 0u;
        else if (current_break >= TITAN_TARGET_EE_RAM_LIMIT)
            used = TITAN_TARGET_EE_RAM_LIMIT - TITAN_TARGET_EE_RAM_BASE;
        else
            used = (uint32_t)(current_break - TITAN_TARGET_EE_RAM_BASE);
        free_bytes = used < (TITAN_TARGET_EE_RAM_LIMIT - TITAN_TARGET_EE_RAM_BASE)
            ? (TITAN_TARGET_EE_RAM_LIMIT - TITAN_TARGET_EE_RAM_BASE) - used
            : 0u;
        TitanTargetMemory_UpdateEeWatermarks(used, free_bytes);
    }
#elif defined(TITAN_TARGET_MEMORY_TEST)
    if (g_host_ee_valid)
        TitanTargetMemory_UpdateEeWatermarks(g_host_ee_used, g_host_ee_free);
#endif
#ifdef TITAN_TARGET_MEMORY_TEST
    if (g_host_iop_valid)
    {
        g_memory.iop_ram_valid = 1u;
        g_memory.iop_used = g_host_iop_used;
        g_memory.iop_free = g_host_iop_free;
        if (g_memory.iop_min_free == UINT32_MAX || g_memory.iop_free < g_memory.iop_min_free)
            g_memory.iop_min_free = g_memory.iop_free;
    }
#endif
}

void TitanTargetMemory_RecordEeAllocationFailure(void)
{
    if (g_memory.ee_allocation_failures != UINT32_MAX)
        g_memory.ee_allocation_failures++;
}

void TitanTargetMemory_SetIopKnownResidentModules(uint32_t count)
{
    g_memory.iop_modules_known_resident = count;
}

void TitanTargetMemory_GetTelemetry(TitanTargetMemoryTelemetry *telemetry)
{
    if (telemetry == 0)
        return;
    *telemetry = g_memory;
    if (!telemetry->ee_valid)
        telemetry->ee_min_free = 0u;
    if (!telemetry->iop_ram_valid)
    {
        telemetry->iop_used = 0u;
        telemetry->iop_free = 0u;
        telemetry->iop_min_free = 0u;
    }
}

#ifdef TITAN_TARGET_MEMORY_TEST
void TitanTargetMemory_SetHostEeSample(uint32_t used, uint32_t free_bytes)
{
    g_host_ee_valid = 1u;
    g_host_ee_used = used;
    g_host_ee_free = free_bytes;
}
void TitanTargetMemory_SetHostIopSample(uint32_t used, uint32_t free_bytes, uint32_t modules)
{
    g_host_iop_valid = 1u;
    g_host_iop_used = used;
    g_host_iop_free = free_bytes;
    g_memory.iop_modules_known_resident = modules;
}
#endif
