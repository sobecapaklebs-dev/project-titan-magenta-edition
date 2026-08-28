/*
 * autoaim_score.h — pure (ROM-free, SDL-free) auto-aim target scoring helpers
 * factored out of sub_GAME_7F03D188 (retail 7F03D188, src/game/chrprop.c).
 *
 * AUDIT-0008: the scorer left the width-based score divisor (sp48) and the
 * horizontal output coordinate undefined when horizontal auto-aim was disabled
 * (vertical-only debug config: DEB_AUTOAIMX off / DEB_AUTOAIMY on). The divisor
 * feeds the returned score and therefore candidate selection, so a vertical-only
 * candidate whose screen-center X fell outside its projected horizontal bounds
 * was ranked by an indeterminate stack value.
 *
 * These two functions reproduce the retail arithmetic EXACTLY (float op order
 * preserved) so the autoaim_x-on paths (X-only / X+Y — the only paths any input
 * tape exercises, since the default player enables both axes) stay byte-identical
 * to retail. The divisor is now always well-defined, giving the vertical-only
 * path a deterministic native policy (the same width divisor the X-mode path
 * computes) in place of retail's undefined stack read.
 */
#ifndef MGB64_AUTOAIM_SCORE_H
#define MGB64_AUTOAIM_SCORE_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Width-based score divisor. Retail: (projRight - projLeft) * 1.5f, then, in
 * single-player, scaled by the global difficulty multiplier.
 *
 *   projLeft     : floor of the target's projected left screen-X   (sp8c[0])
 *   projRight    : ceil  of the target's projected right screen-X  (sp84[0])
 *   singlePlayer : nonzero when getPlayerCount() == 1
 *   difficulty   : the global difficulty multiplier
 */
float autoaimTargetDivisor(float projLeft, float projRight,
                           int singlePlayer, float difficulty);

/*
 * Horizontal target score (<= 1). Retail branch structure:
 *   center in [projLeft, projRight] -> 1
 *   center >  projRight             -> 1 - (center - projRight) / divisor
 *   center <  projLeft              -> 1 - (projLeft - center) / divisor
 *
 *   center  : screen-center X = screenleft + 0.5f * screenwidth
 *   divisor : autoaimTargetDivisor(...) — must be defined by the caller
 */
float autoaimTargetScore(float center, float projLeft, float projRight,
                         float divisor);

#ifdef __cplusplus
}
#endif

#endif /* MGB64_AUTOAIM_SCORE_H */
