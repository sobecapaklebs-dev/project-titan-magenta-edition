/*
 * mp_healthbar_gate.h — the health/armour-bar draw gate for maybe_mp_interface
 * (retail ASM src/game/bondview.c; VERSION_US glabel 7F089370, and the
 * LEFTOVERDEBUG variant 7F089208 — both audited identical on this gate).
 *
 * Pure (ROM-free, SDL-free) mirror of the retail gate condition, factored so a
 * ROM-free unit test can guard it (FID-0070).
 *
 * DIVERGENCE (re-derived instruction-level from both ASM variants): retail draws
 * the bars (sub_GAME_7F088618) iff bondviewGetIfCurrentPlayerHealthShowTime()
 * && watch_animation_state == 0 (US `jal` 7F089490 / `beqz`; `lw 0x1c8` /
 * `bnez` 7F0894A0-A8). bondviewGetIfCurrentPlayerDamageShowTime is called
 * nowhere in either variant. The NATIVE_PORT rewrite ORed it in:
 * (HealthShowTime() || DamageShowTime()) && watch==0. Because damageshowtime
 * idles at -1 and is >= 0 during a damage flash while healthshowtime is still 0,
 * the port drew the bars during damage flashes AND took the `if` branch, so it
 * skipped the else-branch `healthdisplaytime -= g_ClockTimer` decrement that
 * retail runs behind sub_GAME_7F0C6048.
 *
 * `legacy` toggle: 0 => faithful retail gate; != 0 => reproduce the port defect
 * for the GE007_NO_MP_HEALTHBAR_DAMAGE_GATE_FIX A/B negative control.
 *
 * All three args are the truthiness the game already computes: the two getter
 * results (which are pure reads) and (watch_animation_state == 0). See FID-0070.
 */
#ifndef MGB64_MP_HEALTHBAR_GATE_H
#define MGB64_MP_HEALTHBAR_GATE_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Returns non-zero when maybe_mp_interface should draw the health/armour bars
 * via the health-show-time path (the `if` branch).
 *   health_show : bondviewGetIfCurrentPlayerHealthShowTime()  (healthshowtime > 0)
 *   damage_show : bondviewGetIfCurrentPlayerDamageShowTime()  (damageshowtime >= 0)
 *   watch_idle  : (watch_animation_state == 0)
 *   legacy      : 0 => retail (health_show only); != 0 => port defect (|| damage)
 */
int mpHealthBarDrawGate(int health_show, int damage_show, int watch_idle, int legacy);

#ifdef __cplusplus
}
#endif

#endif /* MGB64_MP_HEALTHBAR_GATE_H */
