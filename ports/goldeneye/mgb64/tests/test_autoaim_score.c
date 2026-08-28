/*
 * test_autoaim_score.c — AUDIT-0008 regression lane.
 *
 * Guards the auto-aim target divisor + score extracted from sub_GAME_7F03D188.
 * The historical defect: in the vertical-only debug config (autoaim_x off) the
 * width divisor (sp48) was never initialized, yet the score formula divided by
 * it whenever the screen-center X fell outside the target's projected bounds,
 * so candidate ranking read an indeterminate stack value; the caller also copied
 * an uninitialized horizontal output.
 *
 * This test proves:
 *  (1) autoaimTargetDivisor reproduces the retail formula (1.5*width, optional
 *      *difficulty in single-player) — a change here would ALSO break the X+Y
 *      input-tape sim-hash baselines, so it is a fast local canary.
 *  (2) autoaimTargetScore matches the three retail branches exactly, including
 *      the inclusive boundary behavior.
 *  (3) With the divisor now always defined, a vertical-only center-OUTSIDE
 *      candidate scores a finite, deterministic value (the fix's whole point):
 *      identical across repeated calls and never NaN.
 *
 * ROM-free: compiles only the pure autoaim_score.c TU. NEVER uses assert()
 * (the ctest build is Release -DNDEBUG, which strips it); failures are counted
 * and main() returns nonzero.
 */
#include "autoaim_score.h"

#include <stdio.h>

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

static int close_to(float a, float b)
{
    float d = a - b;
    if (d < 0.0f) d = -d;
    return d <= 1e-5f;
}

int main(void)
{
    /* --- (1) divisor formula: width = 100 - 40 = 60; *1.5 = 90 --- */
    CHECK(close_to(autoaimTargetDivisor(40.0f, 100.0f, 0, 0.75f), 90.0f),
          "divisor multiplayer: 1.5*width, no difficulty scale");
    /* single-player scales by difficulty: 90 * 0.75 = 67.5 */
    CHECK(close_to(autoaimTargetDivisor(40.0f, 100.0f, 1, 0.75f), 67.5f),
          "divisor single-player: 1.5*width*difficulty");
    /* difficulty == 1.0 leaves it unchanged */
    CHECK(close_to(autoaimTargetDivisor(40.0f, 100.0f, 1, 1.0f), 90.0f),
          "divisor single-player difficulty 1.0 == multiplayer value");

    /* --- (2) score branches (divisor = 90) --- */
    {
        float div = 90.0f;
        /* center inside [40,100] -> exactly 1 */
        CHECK(autoaimTargetScore(70.0f, 40.0f, 100.0f, div) == 1.0f,
              "score: center inside bounds is exactly 1.0f");
        /* center right of bounds: 1 - (130-100)/90 */
        CHECK(close_to(autoaimTargetScore(130.0f, 40.0f, 100.0f, div),
                       1.0f - (130.0f - 100.0f) / div),
              "score: center right of bounds -> 1-(center-right)/div");
        /* center left of bounds: 1 - (40-10)/90 */
        CHECK(close_to(autoaimTargetScore(10.0f, 40.0f, 100.0f, div),
                       1.0f - (40.0f - 10.0f) / div),
              "score: center left of bounds -> 1-(left-center)/div");
        /* inclusive boundaries: center == right and center == left are inside */
        CHECK(autoaimTargetScore(100.0f, 40.0f, 100.0f, div) == 1.0f,
              "score: center == right boundary is inside -> 1");
        CHECK(autoaimTargetScore(40.0f, 40.0f, 100.0f, div) == 1.0f,
              "score: center == left boundary is inside -> 1");
    }

    /* --- (3) vertical-only determinism: center OUTSIDE bounds now yields a
     * finite, repeatable score because the divisor is always defined. --- */
    {
        float d  = autoaimTargetDivisor(40.0f, 100.0f, 1, 1.0f); /* 90 */
        float s1 = autoaimTargetScore(500.0f, 40.0f, 100.0f, d);
        float s2 = autoaimTargetScore(500.0f, 40.0f, 100.0f, d);
        CHECK(s1 == s2, "vertical-only: score is deterministic across calls");
        CHECK(s1 == s1, "vertical-only: score is not NaN (divisor is defined)");
        CHECK(close_to(s1, 1.0f - (500.0f - 100.0f) / d),
              "vertical-only: center-outside uses the defined width divisor");
    }

    if (g_failures == 0) {
        printf("test_autoaim_score: OK\n");
        return 0;
    }
    fprintf(stderr, "test_autoaim_score: %d failure(s)\n", g_failures);
    return 1;
}
