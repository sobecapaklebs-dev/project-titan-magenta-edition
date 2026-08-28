/*
 * watch_ammo_switchstate.c — pure watch ammo-panel weapon-switch early-out for
 * FID-0084. See the header for the authoritative VERSION_US ASM. ROM-free.
 */
#include "watch_ammo_switchstate.h"

int watchAmmoPanelHiddenByWeaponSwitch(int handState, int legacy) {
    if (legacy) {
        return 0; /* pre-fix port: draw the panel through the switch */
    }
    /* Retail: beq $v0,6 / beq $v0,7 -> return before any drawing. */
    return handState == 6 || handState == 7;
}
