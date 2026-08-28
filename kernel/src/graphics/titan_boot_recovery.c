#include "titan_boot_recovery.h"
#include "titan_vram_boot_guard.h"

#include <limits.h>
#include <string.h>

static TitanBootRecoveryState g_recovery;

static void TitanBootRecovery_Latch(TitanBootRecoveryReason reason, unsigned int phase)
{
    if (g_recovery.home_ready)
        return;
    if (g_recovery.mode == TITAN_BOOT_RECOVERY_NORMAL)
    {
        g_recovery.mode = TITAN_BOOT_RECOVERY_MINIMUM;
        g_recovery.reason = reason;
        g_recovery.first_failure_phase = phase;
    }
    g_recovery.last_failure_phase = phase;
    if (g_recovery.failure_count != UINT32_MAX)
        g_recovery.failure_count++;
}

void TitanBootRecovery_Initialize(void)
{
    memset(&g_recovery, 0, sizeof(g_recovery));
}

void TitanBootRecovery_NotifyVramPressure(unsigned int pressure, unsigned int phase)
{
    if (pressure == (unsigned int)TITAN_VRAM_PRESSURE_CRITICAL)
        TitanBootRecovery_Latch(TITAN_BOOT_RECOVERY_REASON_VRAM_CRITICAL, phase);
}

void TitanBootRecovery_NotifyCriticalAllocationFailure(unsigned int phase)
{
    TitanBootRecovery_Latch(TITAN_BOOT_RECOVERY_REASON_CRITICAL_ALLOCATION, phase);
}

int TitanBootRecovery_ClearTransientVramCritical(void)
{
    if (g_recovery.mode != TITAN_BOOT_RECOVERY_MINIMUM ||
        g_recovery.reason != TITAN_BOOT_RECOVERY_REASON_VRAM_CRITICAL)
        return 0;

    /* Keep the historical counters/phase markers for diagnostics, but clear
     * the policy latch once a deterministic video-layout transition has
     * restored safe GS headroom. Critical allocation failures use a distinct
     * reason and are never cleared here. */
    g_recovery.mode = TITAN_BOOT_RECOVERY_NORMAL;
    g_recovery.reason = TITAN_BOOT_RECOVERY_REASON_NONE;
    return 1;
}

void TitanBootRecovery_MarkHomeReady(void)
{
    g_recovery.home_ready = 1u;
    /* A latched minimum mode intentionally remains active for this session.
     * This prevents a boot that barely survived from immediately reloading
     * decorative assets and entering a pressure/reload loop. */
}

int TitanBootRecovery_IsMinimum(void)
{
    return g_recovery.mode == TITAN_BOOT_RECOVERY_MINIMUM;
}

const TitanBootRecoveryState *TitanBootRecovery_GetState(void)
{
    return &g_recovery;
}

const char *TitanBootRecovery_GetModeName(void)
{
    return TitanBootRecovery_IsMinimum() ? "MINIMUM" : "NORMAL";
}

const char *TitanBootRecovery_GetReasonName(void)
{
    switch (g_recovery.reason)
    {
        case TITAN_BOOT_RECOVERY_REASON_VRAM_CRITICAL: return "VRAM_CRITICAL";
        case TITAN_BOOT_RECOVERY_REASON_CRITICAL_ALLOCATION: return "CRITICAL_ALLOCATION";
        default: return "NONE";
    }
}
