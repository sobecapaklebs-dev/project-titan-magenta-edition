#ifndef PROJECT_TITAN_BOOT_RECOVERY_H
#define PROJECT_TITAN_BOOT_RECOVERY_H

#include <stdint.h>

typedef enum TitanBootRecoveryMode
{
    TITAN_BOOT_RECOVERY_NORMAL = 0,
    TITAN_BOOT_RECOVERY_MINIMUM
} TitanBootRecoveryMode;

typedef enum TitanBootRecoveryReason
{
    TITAN_BOOT_RECOVERY_REASON_NONE = 0,
    TITAN_BOOT_RECOVERY_REASON_VRAM_CRITICAL,
    TITAN_BOOT_RECOVERY_REASON_CRITICAL_ALLOCATION
} TitanBootRecoveryReason;

typedef struct TitanBootRecoveryState
{
    TitanBootRecoveryMode mode;
    TitanBootRecoveryReason reason;
    uint32_t failure_count;
    unsigned int first_failure_phase;
    unsigned int last_failure_phase;
    unsigned int home_ready;
} TitanBootRecoveryState;

void TitanBootRecovery_Initialize(void);
void TitanBootRecovery_NotifyVramPressure(unsigned int pressure, unsigned int phase);
void TitanBootRecovery_NotifyCriticalAllocationFailure(unsigned int phase);
int TitanBootRecovery_ClearTransientVramCritical(void);
void TitanBootRecovery_MarkHomeReady(void);
int TitanBootRecovery_IsMinimum(void);
const TitanBootRecoveryState *TitanBootRecovery_GetState(void);
const char *TitanBootRecovery_GetModeName(void);
const char *TitanBootRecovery_GetReasonName(void);

#endif
