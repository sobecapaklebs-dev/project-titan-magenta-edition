/*
 * weapon_cycle_queue.c — queued weapon-cycle step accumulator + drain.
 *
 * Pure, SDL-free. Factored out of stubs.c (queue) and bondview.c (drain) so both
 * share one implementation and a ROM-free unit test can guard it (FID-0016 /
 * M2.2). Behavior is byte-identical to the original inline code paths.
 */
#include "weapon_cycle_queue.h"

void pcQueueWeaponCycleSteps(int *counter, int delta) {
    if (!counter || delta <= 0) {
        return;
    }
    *counter += delta;
    if (*counter > PC_WEAPON_CYCLE_MAX_QUEUED_STEPS) {
        *counter = PC_WEAPON_CYCLE_MAX_QUEUED_STEPS;
    }
}

int pcDrainWeaponCycleStep(int *counter) {
    if (counter && *counter > 0) {
        (*counter)--;
        return 1;
    }
    return 0;
}
