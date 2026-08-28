/*
 * test_aimbone_dispatch.c — ROM-free regression lane for FID-0101.
 *
 * Guards the sub_GAME_7F02083C arg0 validity dispatch (aimbone_dispatch.c).
 * Retail proceeds for arg0 in {0,1,2,3} (bnezl arg0 -> return); the port defect
 * returned for arg0 == 0, deadening the gun-hand aim-bone pose block.
 *
 * Fails if the fix reverts: aimBoneArg0Proceeds(0, 0) would return 0 instead of
 * 1 and go red.
 */
#include "aimbone_dispatch.h"

#include <stdio.h>

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

int main(void)
{
    /* --- fixed (retail): proceed for {0,1,2,3}, return otherwise --- */
    CHECK(aimBoneArg0Proceeds(0, 0) != 0, "arg0==0 proceeds (the fix: gun-hand aim bone)");
    CHECK(aimBoneArg0Proceeds(1, 0) != 0, "arg0==1 proceeds");
    CHECK(aimBoneArg0Proceeds(2, 0) != 0, "arg0==2 proceeds");
    CHECK(aimBoneArg0Proceeds(3, 0) != 0, "arg0==3 proceeds");
    CHECK(aimBoneArg0Proceeds(4, 0) == 0, "arg0==4 returns (out of {0,1,2,3})");
    CHECK(aimBoneArg0Proceeds(-1, 0) == 0, "arg0==-1 returns");

    /* --- legacy (port defect): return for arg0==0 --- */
    CHECK(aimBoneArg0Proceeds(0, 1) == 0, "legacy: arg0==0 returns (the defect)");
    CHECK(aimBoneArg0Proceeds(1, 1) != 0, "legacy: arg0==1 still proceeds");
    CHECK(aimBoneArg0Proceeds(2, 1) != 0, "legacy: arg0==2 still proceeds");
    CHECK(aimBoneArg0Proceeds(3, 1) != 0, "legacy: arg0==3 still proceeds");
    CHECK(aimBoneArg0Proceeds(4, 1) == 0, "legacy: arg0==4 returns");

    if (g_failures == 0) {
        printf("PASS: aimbone_dispatch\n");
        return 0;
    }
    fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
}
