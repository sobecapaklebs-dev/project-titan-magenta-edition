/*
 * radial_deadzone.h — shared analog-stick radial deadzone + N64 mapping.
 *
 * Pure (ROM-free, SDL-free) so the aim stick (game/lvl.c), the movement stick
 * (platform/stubs.c), and the ROM-free unit test all exercise the exact same
 * arithmetic. See FID-0015 (M2.1): the aim stick had a radial magnitude
 * deadzone + rescale-from-edge while the movement stick used a per-axis square
 * deadzone that zeroed a real diagonal (6000,6000). Factoring the map here lets
 * both sticks share one behavior and lets tests/test_radial_deadzone.c guard it.
 */
#ifndef MGB64_RADIAL_DEADZONE_H
#define MGB64_RADIAL_DEADZONE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Radial (circular) deadzone + rescale-from-edge on a normalized stick vector
 * (*nx,*ny in [-1,1]); direction is preserved. deadzone is the inner dead
 * fraction of full deflection. radial_enabled == 0 is a no-op (the caller keeps
 * its legacy square map). Below the deadzone -> (0,0); above -> magnitude
 * rescaled so output is continuous from 0 at the edge. */
void platformApplyRadialDeadzone(float *nx, float *ny, float deadzone, int radial_enabled);

/* Map raw SDL left-stick axes (lx,ly in -32767..32767; ly SDL-oriented so
 * down = +) to the N64 analog movement range (-80..80, forward = +y). When
 * radial_enabled is on the shared radial helper is applied to the normalized
 * vector; otherwise the legacy per-axis square deadzone (square_deadzone, i.e.
 * GAMEPAD_DEADZONE) is used. Result is clamped to +/-80. Pure so both the
 * runtime movement path and the unit test share it. */
void pcMapMovementStickN64(int lx, int ly, float deadzone, int radial_enabled,
                           int square_deadzone, int *out_x, int *out_y);

#ifdef __cplusplus
}
#endif

#endif /* MGB64_RADIAL_DEADZONE_H */
