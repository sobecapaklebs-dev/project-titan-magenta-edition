/*
 * fire_rate_authentic.h — full-auto fire-cadence tick-scaling (FID-0056).
 *
 * Pure (ROM-free, SDL-free) so the gun tick (game/gun.c) and the ROM-free unit
 * test (tests/test_fire_rate_authentic.c) exercise the exact same arithmetic.
 *
 * FINDING (FID-0056, survey docs/RECOMP_LANDSCAPE_SURVEY_2026-07-10.md#F1):
 * GoldenEye gates full-auto fire on a per-RENDERED-FRAME counter, not a
 * tick-scaled one. In src/game/gun.c the fire counter `field_88C` increments
 * once per frame (unscaled) while the adjacent `field_890 += g_ClockTimer` IS
 * tick-scaled; full-auto fire is gated on `field_88C % fire_rate`. On N64 a
 * rendered frame cost 2-4 sim ticks (the counter effectively advanced at
 * 15-30Hz); the port runs a locked 60Hz loop with g_ClockTimer == 1, so
 * field_88C advances at 60Hz and automatics fire ~2-4x faster than hardware.
 * Measured on Dam with the AK47 (ITEM_AK47): 33.3 shots/100 ticks at locked
 * 60Hz vs 11.3 shots/100 ticks at the N64 ~20fps-combat frame cost
 * (GE007_DETERMINISTIC_SPEEDFRAMES=3) — a 2.95x overspeed. GoldenRecomp's
 * README confirms this empirically (unfixed in their tree).
 *
 * REMEDIATION (survey #F18): behind Input.FireRateAuthentic, DEFAULT ON (owner
 * decision 2026-07-10 — gameplay accuracy is the goal; this is a canonical
 * fidelity defect, so the faithful cadence ships as the default; set the flag to
 * 0 to opt OUT and restore the legacy ~3x-faster fire). When ON, (a) the fire counter
 * advances by g_ClockTimer (mirrors field_890, tick-scaled, no tick remainder
 * dropped) and (b) the automatic fire-rate divisor is multiplied by the assumed
 * N64 rendered-frame cost so the `field_88C % rate` gate fires once per
 * (fire_rate * n64_frame_cost) ticks, reproducing the N64 per-frame-at-
 * (60 / n64_frame_cost)fps cadence. At locked 60Hz (g_ClockTimer == 1) the
 * counter advance is byte-identical to the legacy `++`; only the divisor
 * scaling changes cadence.
 */
#ifndef MGB64_FIRE_RATE_AUTHENTIC_H
#define MGB64_FIRE_RATE_AUTHENTIC_H

#ifdef __cplusplus
extern "C" {
#endif

/* Assumed 60Hz sim-ticks per rendered N64 frame for authentic-cadence scaling.
 * GoldenEye's Dam combat rendered at ~20fps (3 retraces / frame) — the measured
 * FID-0056 value — so the locked-60Hz port divides the automatic cadence by 3.
 * The engine's nominal design target is 30fps (2 ticks/frame, lvl.c speedgraph
 * cap comment); heavy scenes drop to ~15fps (4). Exposed as
 * Input.FireRateN64FrameCost (range 2-4) because the true frame cost is
 * scene-dependent on real hardware. */
#define FIRE_RATE_N64_FRAME_COST_DEFAULT 3
#define FIRE_RATE_N64_FRAME_COST_MIN 2
#define FIRE_RATE_N64_FRAME_COST_MAX 4

/* Per-frame advance applied to the field_88C fire counter.
 *   authentic == 0 (legacy/default): returns 1 (the retail `field_88C++`).
 *   authentic != 0: returns clock_timer (mirror of field_890 += g_ClockTimer,
 *     tick-scaled; the full integer tick delta is accumulated so no sub-tick
 *     remainder is dropped).
 * At locked 60Hz clock_timer == 1 so both branches return 1 -> the counter
 * value stream is byte-identical between flag states. The caller advances the
 * counter only when g_ClockTimer > 0 (paused frames don't tick either counter),
 * mirroring the retail guard. */
int fireRateCounterAdvance(int clock_timer, int authentic);

/* Effective automatic fire-rate divisor for the `field_88C % rate` gate and the
 * burst catch-up divide.
 *   authentic == 0 (legacy/default): returns fire_rate unchanged.
 *   authentic != 0: returns fire_rate * n64_frame_cost (n64_frame_cost clamped
 *     to [FIRE_RATE_N64_FRAME_COST_MIN, MAX]), so the gate fires once per
 *     (fire_rate * n64_frame_cost) ticks.
 * fire_rate <= 0 is returned unchanged (retail treats a non-positive rate as
 * "not an automatic" and never reaches the gate). */
int fireRateEffectiveAutoRate(int fire_rate, int authentic, int n64_frame_cost);

#ifdef __cplusplus
}
#endif

#endif /* MGB64_FIRE_RATE_AUTHENTIC_H */
