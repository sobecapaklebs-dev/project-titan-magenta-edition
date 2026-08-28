/*
 * test_watch_inv_aspect.c — ROM-free regression lane for FID-0098.
 *
 * Guards the watch-inventory guPerspective aspect (watch_inv_aspect.c). Retail
 * passes 0x3FAAAAAB (4/3); the port defect passed 0x3FA49249 (9/7). The test
 * asserts the exact IEEE-754 bit patterns so a revert of either reddens.
 */
#include "watch_inv_aspect.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

static uint32_t bits(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

int main(void)
{
    /* fixed (retail 4/3) */
    CHECK(bits(watchInvPerspAspect(0)) == 0x3FAAAAABu, "fixed aspect == 0x3FAAAAAB (4/3)");
    /* legacy (port defect 9/7) */
    CHECK(bits(watchInvPerspAspect(1)) == 0x3FA49249u, "legacy aspect == 0x3FA49249 (9/7)");

    if (g_failures == 0) {
        printf("PASS: watch_inv_aspect\n");
        return 0;
    }
    fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
}
