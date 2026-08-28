/*
 * fp_weapon_perspnorm.h — first-person weapon perspnorm value + command-order
 * fix for sub_GAME_7F062BE4 (src/game/gun.c). Pure (ROM-free / SDL-free) mirror
 * of the retail perspnorm arithmetic, factored out so a ctest can lock the exact
 * value (FID-0077).
 *
 * AUTHORITY (VERSION_US retail ASM, glabel sub_GAME_7F062BE4):
 *  - D1 perspnorm arg: retail computes the FP-weapon gSPPerspNormalize scale via
 *    sub_GAME_7F05997C(0.0f, 300.0f) — `mtc1 $zero,$f12` at 7F062D8C with
 *    f14=300.0 (0x43960000) at 7F062D7C-80 — giving u16(0x20000/300) = 436. The
 *    NATIVE_PORT rewrite (gun.c:15030) passed (1.0f, 300.0f) => 0x20000/301 = 435
 *    (matrixmath.c sub_GAME_7F05997C = u16 0x20000/(a+b), clamped). The emitted
 *    G_MOVEWORD perspnorm differs by 1 on the first-person weapon.
 *  - D2 command order: retail emits the BC00000E perspnorm word BEFORE the
 *    numSwitches>=17 monitor-microcode block (perspnorm store at 7F062D90/D9C,
 *    numSwitches load at 7F062DA0, process_monitor_animation_microcode at
 *    7F062E24); the port emitted the monitor microcode FIRST then perspnorm, so
 *    the rocket-launcher sight screen ran under the SCENE perspnorm instead of
 *    the weapon perspnorm. gun.c gates the reorder on the same legacy flag.
 *
 * Each function takes a `legacy` toggle: 0 => faithful retail value (436, near
 * arg 0.0, perspnorm-before-monitor); != 0 => reproduce the pre-fix port defect
 * (435, near arg 1.0, monitor-before-perspnorm) for the GE007_NO_FP_WEAPON_
 * PERSPNORM_FIX A/B negative control.
 */
#ifndef MGB64_FP_WEAPON_PERSPNORM_H
#define MGB64_FP_WEAPON_PERSPNORM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Near-plane arg passed to sub_GAME_7F05997C: 0.0f faithful, 1.0f legacy. */
float fpWeaponPerspNormNearArg(int legacy);

/* Pure mirror of matrixmath.c sub_GAME_7F05997C(near, 300.0f): u16 0x20000/sum,
 * 0xffff if sum<=2, min 1. Faithful => 436, legacy => 435. */
unsigned short fpWeaponPerspNormValue(int legacy);

#ifdef __cplusplus
}
#endif

#endif /* MGB64_FP_WEAPON_PERSPNORM_H */
