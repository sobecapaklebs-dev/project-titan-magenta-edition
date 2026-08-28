/*
 * autoaim_score.c — see autoaim_score.h. AUDIT-0008.
 *
 * Byte-for-byte faithful to sub_GAME_7F03D188 (src/game/chrprop.c): the divisor
 * is (right-left)*1.5f, optionally *difficulty in single-player; the score is
 * the three-branch center-relative-to-projected-bounds formula. Do NOT reorder
 * the float operations — the autoaim_x-on paths must stay identical to the
 * input-tape sim-hash baselines.
 */
#include "autoaim_score.h"

float autoaimTargetDivisor(float projLeft, float projRight,
                           int singlePlayer, float difficulty)
{
    float divisor = (projRight - projLeft) * 1.5f;

    if (singlePlayer)
    {
        divisor = divisor * difficulty;
    }

    return divisor;
}

float autoaimTargetScore(float center, float projLeft, float projRight,
                         float divisor)
{
    if (center >= projLeft && center <= projRight)
    {
        return 1;
    }

    if (center >= projLeft)
    {
        return 1 - (center - projRight) / divisor;
    }

    return 1 - (projLeft - center) / divisor;
}
