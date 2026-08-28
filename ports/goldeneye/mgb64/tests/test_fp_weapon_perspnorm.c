/*
 * test_fp_weapon_perspnorm.c — ROM-free fail-on-revert lane for FID-0077.
 *
 * Locks the retail FP-weapon perspnorm value (near arg 0.0f => u16 0x20000/300 =
 * 436) against the pre-fix port (1.0f => 0x20000/301 = 435). If the fix is
 * reverted so the faithful path emits 1.0f, faithful==legacy and this fails.
 */
#include <stdio.h>
#include "fp_weapon_perspnorm.h"

int main(void) {
    int failures = 0;

    /* D1 near arg: 0.0f faithful, 1.0f legacy. */
    if (fpWeaponPerspNormNearArg(0) != 0.0f) {
        printf("FAIL: faithful near arg must be 0.0 (got %f)\n",
               fpWeaponPerspNormNearArg(0));
        failures++;
    }
    if (fpWeaponPerspNormNearArg(1) != 1.0f) {
        printf("FAIL: legacy near arg must be 1.0 (got %f)\n",
               fpWeaponPerspNormNearArg(1));
        failures++;
    }

    /* Retail 436, pre-fix port 435. */
    if (fpWeaponPerspNormValue(0) != 436) {
        printf("FAIL: faithful perspnorm must be 436 (got %u)\n",
               fpWeaponPerspNormValue(0));
        failures++;
    }
    if (fpWeaponPerspNormValue(1) != 435) {
        printf("FAIL: legacy perspnorm must be 435 (got %u)\n",
               fpWeaponPerspNormValue(1));
        failures++;
    }

    /* Fail-on-revert: faithful must differ from legacy by exactly 1. */
    if (fpWeaponPerspNormValue(0) == fpWeaponPerspNormValue(1)) {
        printf("FAIL: faithful perspnorm must differ from legacy\n");
        failures++;
    }
    if ((int)fpWeaponPerspNormValue(0) - (int)fpWeaponPerspNormValue(1) != 1) {
        printf("FAIL: faithful perspnorm must be legacy + 1\n");
        failures++;
    }

    if (failures == 0) {
        printf("test_fp_weapon_perspnorm: PASS "
               "(FID-0077 faithful perspnorm 436 != legacy 435)\n");
        return 0;
    }
    printf("test_fp_weapon_perspnorm: FAIL (%d)\n", failures);
    return 1;
}
