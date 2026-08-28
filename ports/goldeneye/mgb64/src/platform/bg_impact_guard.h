/*
 * bg_impact_guard.h — the background-only bullet-impact spawn gate of
 * chraiDefaultWeaponFireHandler (retail ASM src/game/chrprop.c:4050-4079,
 * VERSION_US glabel src/game/chrprop.c:3537).
 *
 * Pure (ROM-free, SDL-free) mirror of the retail impact-spawn condition,
 * factored out so a ROM-free unit test can guard it (FID-0097).
 *
 * PARITY-DIVERGENCE (documented; port behavior kept as the faithful default).
 * Retail reads g_Textures at index texture_index DIRECTLY (sll by 3, lbu byte 0,
 * andi 0xf = hitSound) with NO `texture_index >= 0` guard, then spawns a
 * bullet-impact explosionCreate unless hitSound == HIT_WATER(5) or HIT_SNOW(6).
 * On a background-only hit where the fine surface tracer returns <= 0,
 * texture_index is left at -1 (retail C src/game/chrprop.c:2731 / ASM L54C), so
 * retail performs an OUT-OF-BOUNDS read of g_Textures[-1] and branches on that
 * fixed N64 garbage byte.
 *
 * The port CANNOT faithfully reproduce this: in the port, g_Textures[] is a real
 * C array (src/game/image.c:318), so g_Textures[-1] is different memory than the
 * N64 word at g_Textures-8 — reproducing the read would be undefined behavior on
 * port-specific garbage, not the fixed N64 byte. Determining whether retail
 * actually spawns the effect requires the N64 g_Textures[-8] byte (its low
 * nibble != 5,6), which needs the ROM and the retail symbol layout. The port's
 * `texture_index >= 0` guard (src/game/chrprop.c:3115) is therefore kept as the
 * memory-safe faithful default; GE007_BG_IMPACT_RETAIL_OOB is an opt-in that
 * spawns the effect for texture_index < 0 (the "retail spawns" hypothesis)
 * WITHOUT reading OOB. See FID-0097.
 */
#ifndef MGB64_BG_IMPACT_GUARD_H
#define MGB64_BG_IMPACT_GUARD_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Decide whether a bullet-impact effect spawns at an impact point.
 *
 *   texture_index      : the resolved surface texture index; -1 on a
 *                        background-only hit with no detailed texture record.
 *   hit_sound          : g_Textures[texture_index].hitSound (0..15) when
 *                        texture_index >= 0; ignored (pass -1) when < 0.
 *   reproduce_retail_oob : 0 => port default (guard: no spawn for index < 0);
 *                        != 0 => reproduce the retail-spawn hypothesis (spawn
 *                        for index < 0) for the GE007_BG_IMPACT_RETAIL_OOB A/B
 *                        control. Never reads OOB in either mode.
 *
 * HIT_WATER == 5 and HIT_SNOW == 6 (bondconstants.h HIT_TYPE).
 *
 * Returns nonzero when the impact effect should spawn.
 */
int bgImpactShouldSpawn(int texture_index, int hit_sound, int reproduce_retail_oob);

#ifdef __cplusplus
}
#endif

#endif /* MGB64_BG_IMPACT_GUARD_H */
