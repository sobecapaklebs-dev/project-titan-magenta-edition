/*
 * glass_shot_depth.h — the object shot-depth acceptance decision of the retail
 * hit-depth gates sub_GAME_7F04E720 / sub_GAME_7F04E9BC (src/game/chrobjhandler.c).
 *
 * Pure (ROM-free, SDL-free) mirror of the port's glass-crack depth-tolerance
 * DECISION, factored so a ROM-free unit test can guard it (FID-0083).
 *
 * RETAIL BEHAVIOUR (authority, US ASM): both gates reject an object hit outright
 * when the transformed depth is behind the shot's limit plane — `c.le.s $f0,$f10`
 * then `bc1fl` return when `-transformed.z > shotdata->unk34` (7F04E720 region).
 * Retail has no acceptance window: a hit behind the plane is simply not registered.
 *
 * PORT OPT-IN MITIGATION: glass / tinted-glass panes are often coplanar with the
 * background collision surface, so a hit that grazes the pane can land a hair
 * behind the limit plane and be rejected, attaching the crack to the wall behind
 * instead of the pane. A positive `tolerance` accepts a glass-like hit up to
 * `tolerance` world units behind the plane (the caller then clamps the depth to
 * the plane). This is a deliberate deviation from retail — it registers an extra
 * prop hit (and the crack/shatter bookkeeping + PRNG draws that follow) that retail
 * never makes — so per charter rule 4 (n64-quirk / cosmetic mitigation) it is
 * OPT-IN: the faithful default is tolerance 0.0 (retail-exact rejection), and
 * GE007_GLASS_SHOT_DEPTH_TOLERANCE (> 0) enables the mitigation. Registered as
 * QUIRKS.md W2.
 *
 * Returns 1 to ACCEPT an otherwise-rejected glass-like hit, 0 to reject (the
 * faithful path — the caller falls through to retail's rejection). At tolerance
 * 0.0 this always returns 0, so the port is byte-identical to retail.
 */
#ifndef GE007_GLASS_SHOT_DEPTH_H
#define GE007_GLASS_SHOT_DEPTH_H

/*
 * is_glass_like : non-zero when the struck object is PROPDEF_GLASS / TINTED_GLASS.
 * depth         : -transformed.z, the hit depth behind the shot origin.
 * limit         : shotdata->unk34, the retail rejection plane.
 * tolerance     : the configured acceptance window (0.0 = faithful, retail-exact).
 */
int glassShotDepthAccept(int is_glass_like, float depth, float limit, float tolerance);

#endif /* GE007_GLASS_SHOT_DEPTH_H */
