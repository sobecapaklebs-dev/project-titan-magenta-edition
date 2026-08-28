/*
 * mp_beam_rawcast.h — legacy raw N64 byte offsets used by the multiplayer /
 * split-screen "other player" beam+aim tick (playerTickBeams, PATH 2, in
 * src/game/bondview.c), plus the pure offset arithmetic the FID-0094 A/B
 * negative control (GE007_NO_MP_BEAM_RAWCAST_FIX) restores.
 *
 * AUTHORITY (VERSION_US retail ASM, glabel playerTickBeams; see
 * docs/fidelity/derivations/FID-0094-mp-beam-rawcast.md):
 *   7F08BE40  81860875  lb    $a2, 0x875($t4)   hands[0] firing flag
 *   7F08BE54  81C60C1D  lb    $a2, 0xc1d($t6)   hands[1] firing flag
 *   7F08BEF0  C5C80B50  lwc1  $f8, 0xb50($t6)   hands[i] position source word 0
 *   (srcOff strength-reduces to i*936 = the N64 sizeof(struct hand))
 *
 * On the 64-bit port layout, pointer fields inside struct player (before
 * hands[]) and struct hand expand 4->8B, so BOTH the raw within-struct offsets
 * and the 936-byte hand stride are wrong (the native stride is 968 — locked in
 * tests/test_struct_layout.c, FID-0085). The two lock-verified sites — the
 * firing flags and the per-hand position source — are therefore reached by
 * NAMED field access in the default-ON fix (player->hands[i].weapon_firing_status
 * and player->hands[i].field_B50/B54/B58). This header carries the LEGACY raw
 * offsets so bondview.c's negative-control branch and the ROM-free unit test
 * share one source of truth.
 *
 * These functions are pure (no ROM, no SDL, no game headers) so a ctest can
 * assert that the faithful named-field offsets (offsetof, from bondview.h)
 * never coincide with these legacy raws — the compile-time divergence proof
 * that makes the default-ON fix load-bearing.
 */
#ifndef MGB64_MP_BEAM_RAWCAST_H
#define MGB64_MP_BEAM_RAWCAST_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Legacy raw N64 byte offsets (authoritative ASM sites above). */
#define MP_BEAM_N64_HAND0_FIRING   0x875u  /* lb player+0x875 (7F08BE40)   */
#define MP_BEAM_N64_HAND1_FIRING   0xC1Du  /* lb player+0xC1D (7F08BE54)   */
#define MP_BEAM_N64_HAND_STRIDE     936u   /* srcOff strength-reduce = i*936 */
#define MP_BEAM_N64_HANDSRC_BASE   0xB50u  /* lwc1 player+0xB50 (7F08BEF0) */

/* Legacy raw firing-flag byte offset for hand 0/1: 0x875 / 0xC1D. The two are
 * exactly one N64 hand stride (0x3A8 = 936) apart. */
size_t mpBeamLegacyFiringOffset(int hand);

/* Legacy raw per-hand position-source byte offset for hand 0/1, word 0/1/2:
 * hand*936 + 0xB50 + word*4. */
size_t mpBeamLegacyHandSrcOffset(int hand, int word);

#ifdef __cplusplus
}
#endif

#endif /* MGB64_MP_BEAM_RAWCAST_H */
