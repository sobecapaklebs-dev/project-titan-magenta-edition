#include "titan_boot_recovery.h"
#include "titan_vram_boot_guard.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    TitanBootRecovery_Initialize();
    assert(!TitanBootRecovery_IsMinimum());
    TitanBootRecovery_NotifyVramPressure(TITAN_VRAM_PRESSURE_WARNING, TITAN_VRAM_PHASE_THEME_BASE);
    assert(!TitanBootRecovery_IsMinimum());
    TitanBootRecovery_NotifyVramPressure(TITAN_VRAM_PRESSURE_CRITICAL, TITAN_VRAM_PHASE_THEME_BASE);
    assert(TitanBootRecovery_IsMinimum());
    assert(TitanBootRecovery_GetState()->failure_count == 1u);
    TitanBootRecovery_NotifyVramPressure(TITAN_VRAM_PRESSURE_NORMAL, TITAN_VRAM_PHASE_THEME_BASE);
    assert(TitanBootRecovery_IsMinimum());
    TitanBootRecovery_MarkHomeReady();
    assert(TitanBootRecovery_GetState()->home_ready == 1u);
    assert(TitanBootRecovery_IsMinimum());
    TitanBootRecovery_NotifyCriticalAllocationFailure(TITAN_VRAM_PHASE_HOME);
    assert(TitanBootRecovery_GetState()->failure_count == 1u);
    puts("titan-boot-recovery-latch: PASS");
    return 0;
}
