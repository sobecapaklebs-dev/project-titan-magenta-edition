/*
 * weapon_cycle_queue.h — queued weapon-cycle step accumulator + drain.
 *
 * Pure (ROM-free, SDL-free) so the producers (platform/stubs.c: SDL wheel,
 * macOS bridge, gamepad edges, scripted input) and the consumer (game/bondview.c
 * one-drain-per-tick) share one implementation and a ROM-free unit test can guard
 * it (FID-0016 / M2.2). g_pcWeaponCycleForward/Back are queued step counts, not
 * booleans: each mouse-wheel notch adds a pending step and exactly one step is
 * drained per tick, so an N-notch scroll produces N weapon switches over N ticks
 * instead of collapsing to one.
 */
#ifndef MGB64_WEAPON_CYCLE_QUEUE_H
#define MGB64_WEAPON_CYCLE_QUEUE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Cap on queued weapon-cycle steps per direction so a pathological wheel event
 * (or a stuck device reporting a huge delta) can't queue a long run of switches. */
#define PC_WEAPON_CYCLE_MAX_QUEUED_STEPS 5

/* Add `delta` pending weapon-cycle steps to *counter, clamped to
 * [0, PC_WEAPON_CYCLE_MAX_QUEUED_STEPS]. delta <= 0 is a no-op. */
void pcQueueWeaponCycleSteps(int *counter, int delta);

/* Drain at most one queued step from *counter. Returns 1 and decrements when a
 * step was pending, else returns 0 and leaves *counter untouched. */
int pcDrainWeaponCycleStep(int *counter);

#ifdef __cplusplus
}
#endif

#endif /* MGB64_WEAPON_CYCLE_QUEUE_H */
