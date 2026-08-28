/*
 * test_speedgraph_clamp.c — ROM-free regression lane for FID-0017 (M2.4).
 *
 * Guards the speedgraphframes spike clamp (commit eab7804): HUD/watch timers
 * read speedgraphframes directly, so a frame spike must be capped exactly like
 * the sim's g_ClockTimer (lvl.c). clampSpeedgraphFrames mirrors that policy:
 *   - raw deltaFrames far above the cap -> clamped to FRAME_SPIKE_CAP (4);
 *   - first tick after load -> 1;
 *   - RAMROM playback -> exempt (raw value preserved).
 *
 * Fails if the fix is reverted (no clamp, speedgraphframes = raw deltaFrames):
 * clampSpeedgraphFrames(13,0,0) would return 13 instead of 4 and go red.
 */
#include "frame_clamp.h"

#include <stdio.h>

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

int main(void) {
    CHECK(FRAME_SPIKE_CAP == 4, "spike cap is 4 (mirrors lvl.c)");

    /* --- big spike, not first tick, not ramrom -> clamp to 4 --- */
    CHECK(clampSpeedgraphFrames(13, 0, 0) == 4, "spike 13 -> 4");
    CHECK(clampSpeedgraphFrames(5, 0, 0) == 4, "5 -> 4 (just over cap)");
    CHECK(clampSpeedgraphFrames(4, 0, 0) == 4, "4 -> 4 (at cap)");

    /* --- within cap -> unchanged --- */
    CHECK(clampSpeedgraphFrames(1, 0, 0) == 1, "1 -> 1");
    CHECK(clampSpeedgraphFrames(2, 0, 0) == 2, "2 -> 2");
    CHECK(clampSpeedgraphFrames(3, 0, 0) == 3, "3 -> 3");

    /* --- first tick after load -> capped to 1 --- */
    CHECK(clampSpeedgraphFrames(13, 0, 1) == 1, "first tick spike -> 1");
    CHECK(clampSpeedgraphFrames(2, 0, 1) == 1, "first tick 2 -> 1");
    CHECK(clampSpeedgraphFrames(1, 0, 1) == 1, "first tick 1 -> 1");

    /* --- RAMROM playback exempt (raw preserved), overrides both caps --- */
    CHECK(clampSpeedgraphFrames(13, 1, 0) == 13, "ramrom spike preserved");
    CHECK(clampSpeedgraphFrames(100, 1, 1) == 100, "ramrom overrides first-tick cap");

    if (g_failures == 0) {
        printf("PASS: speedgraph_clamp\n");
        return 0;
    }
    fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
}
