/*
 * test_watch_ammo_switchstate.c — ROM-free fail-on-revert lane for FID-0084.
 *
 * Locks the retail sub_GAME_7F06A334 early-out: hide the watch ammo panel while
 * the right hand is mid weapon-switch (hand state 6 or 7), draw it otherwise. If
 * the fix is reverted (early-out dropped), faithful==legacy for states 6/7 and
 * this fails.
 */
#include <stdio.h>
#include "watch_ammo_switchstate.h"

int main(void) {
    int failures = 0;
    int s;

    /* Faithful: hidden for weapon-switch states 6 and 7. */
    if (watchAmmoPanelHiddenByWeaponSwitch(6, 0) != 1) {
        printf("FAIL: faithful state 6 must hide the panel\n");
        failures++;
    }
    if (watchAmmoPanelHiddenByWeaponSwitch(7, 0) != 1) {
        printf("FAIL: faithful state 7 must hide the panel\n");
        failures++;
    }
    /* Faithful: every other state draws (states 0..40 cover the hand FSM). */
    for (s = 0; s <= 40; s++) {
        if (s == 6 || s == 7) {
            continue;
        }
        if (watchAmmoPanelHiddenByWeaponSwitch(s, 0) != 0) {
            printf("FAIL: faithful state %d must draw the panel\n", s);
            failures++;
        }
    }

    /* Legacy (pre-fix): never hidden by this rule. */
    if (watchAmmoPanelHiddenByWeaponSwitch(6, 1) != 0) {
        printf("FAIL: legacy state 6 must draw (pre-fix behavior)\n");
        failures++;
    }
    if (watchAmmoPanelHiddenByWeaponSwitch(7, 1) != 0) {
        printf("FAIL: legacy state 7 must draw (pre-fix behavior)\n");
        failures++;
    }

    /* Fail-on-revert: faithful must differ from legacy for states 6 and 7. */
    if (watchAmmoPanelHiddenByWeaponSwitch(6, 0)
        == watchAmmoPanelHiddenByWeaponSwitch(6, 1)) {
        printf("FAIL: faithful == legacy at state 6\n");
        failures++;
    }
    if (watchAmmoPanelHiddenByWeaponSwitch(7, 0)
        == watchAmmoPanelHiddenByWeaponSwitch(7, 1)) {
        printf("FAIL: faithful == legacy at state 7\n");
        failures++;
    }

    if (failures == 0) {
        printf("test_watch_ammo_switchstate: PASS "
               "(FID-0084 hide panel during weapon-switch states 6/7)\n");
        return 0;
    }
    printf("test_watch_ammo_switchstate: FAIL (%d)\n", failures);
    return 1;
}
