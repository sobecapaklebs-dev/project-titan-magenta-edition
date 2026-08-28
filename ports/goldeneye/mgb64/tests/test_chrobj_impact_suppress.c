/*
 * test_chrobj_impact_suppress.c — ROM-free fail-on-revert lane for FID-0069.
 *
 * Locks the retail sub_GAME_7F04EA68 gate: suppress the object-hit bullet-impact
 * block for the Watch Laser (id 23), NOT the Taser (id 31). If the gate constant
 * is reverted to ITEM_TASER, faithful == legacy at 23 and 31 and this fails.
 */
#include <stdio.h>
#include "chrobj_impact_suppress.h"

int main(void) {
    int failures = 0;
    int w;

    /* Faithful: only the Watch Laser (23) is suppressed. */
    if (chrobjImpactFxSuppressed(CHROBJ_IMPACT_ITEM_WATCHLASER, 0) != 1) {
        printf("FAIL: faithful must suppress the Watch Laser (23)\n");
        failures++;
    }
    if (chrobjImpactFxSuppressed(CHROBJ_IMPACT_ITEM_TASER, 0) != 0) {
        printf("FAIL: faithful must NOT suppress the Taser (31)\n");
        failures++;
    }
    /* Faithful: every other weapon 0..40 spawns impact FX. */
    for (w = 0; w <= 40; w++) {
        int expect = (w == CHROBJ_IMPACT_ITEM_WATCHLASER) ? 1 : 0;
        if (chrobjImpactFxSuppressed(w, 0) != expect) {
            printf("FAIL: faithful weapon %d suppression != %d\n", w, expect);
            failures++;
        }
    }

    /* Legacy (pre-fix): only the Taser (31) is suppressed. */
    if (chrobjImpactFxSuppressed(CHROBJ_IMPACT_ITEM_TASER, 1) != 1) {
        printf("FAIL: legacy must suppress the Taser (31)\n");
        failures++;
    }
    if (chrobjImpactFxSuppressed(CHROBJ_IMPACT_ITEM_WATCHLASER, 1) != 0) {
        printf("FAIL: legacy must NOT suppress the Watch Laser (23)\n");
        failures++;
    }

    /* Fail-on-revert: faithful must differ from legacy at both 23 and 31. */
    if (chrobjImpactFxSuppressed(CHROBJ_IMPACT_ITEM_WATCHLASER, 0)
        == chrobjImpactFxSuppressed(CHROBJ_IMPACT_ITEM_WATCHLASER, 1)) {
        printf("FAIL: faithful == legacy at the Watch Laser (23)\n");
        failures++;
    }
    if (chrobjImpactFxSuppressed(CHROBJ_IMPACT_ITEM_TASER, 0)
        == chrobjImpactFxSuppressed(CHROBJ_IMPACT_ITEM_TASER, 1)) {
        printf("FAIL: faithful == legacy at the Taser (31)\n");
        failures++;
    }

    if (failures == 0) {
        printf("test_chrobj_impact_suppress: PASS "
               "(FID-0069 suppress Watch Laser impact FX, not Taser)\n");
        return 0;
    }
    printf("test_chrobj_impact_suppress: FAIL (%d)\n", failures);
    return 1;
}
