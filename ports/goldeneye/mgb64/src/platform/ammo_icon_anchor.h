/*
 * ammo_icon_anchor.h — the HUD ammo-icon rect-center anchor math for
 * microcode_generation_ammo_related (retail ASM src/game/gun.c:31574-...,
 * VERSION_US glabel 7F0694E8).
 *
 * Pure (ROM-free, SDL-free) mirror of the retail anchor/flip arithmetic,
 * factored out so a ROM-free unit test can guard the exact values (FID-0067).
 * The shared consumer draw_textured_rectangle (src/game/bondwalk2.c:34-37)
 * treats position[] as the rect CENTER and size[] as the half-extent.
 *
 * DIVERGENCE (all re-derived instruction-level from the US ASM):
 *  - D1 y>=0 branch: retail centers at y - h*0.5 (`sub.s $f16,$f12(y),$f6`
 *    at 7F069678, h*0.5 from `mul.s` 7F069674); the NATIVE_PORT rewrite passed
 *    the bare `y`, drawing the icon h/2 px too low in the watch inventory.
 *  - D2 y<0 branch (bottom-anchored in-game hand ammo): retail centers at
 *    H + S - frac(h), frac(h) = h*0.5 - floor(h/2) in {0, 0.5} (7F0696A4-E0);
 *    the port subtracted the full h*0.5, drawing the icon floor(h/2) px too high.
 *  - D3 mirror: retail passes the display_image_at_position flipX slot as the
 *    constant 0 (`sw $zero,0x18($sp)` 7F0698A4) and consumes the incoming flipX
 *    ONLY to negate the sub-pixel x fraction (`beqz`/`neg.s` 7F06962C-38); the
 *    port passed flipX?1:0 through, mirror-imaging the left dual-wield icon.
 *  - D4 x center: retail centers at x + (flipX ? -frac(w) : +frac(w))
 *    (7F069624-48); the port passed the bare x (drops the +/-0.5px odd-width
 *    correction and the flip's sub-pixel meaning).
 *
 * Each function takes a `legacy` toggle: 0 => faithful retail value; != 0 =>
 * reproduce the pre-fix port defect for the GE007_NO_AMMO_ICON_ANCHOR_FIX A/B
 * negative control. See FID-0067.
 */
#ifndef MGB64_AMMO_ICON_ANCHOR_H
#define MGB64_AMMO_ICON_ANCHOR_H

#ifdef __cplusplus
extern "C" {
#endif

/* frac(dim) = dim*0.5 - floor(dim/2): 0.0 for even dim, 0.5 for odd dim. */
float ammoIconHalfFrac(int dim);

/* x rect center (D4). legacy => x. */
float ammoIconCenterX(float x, int w, int flip_x, int legacy);

/* y rect center, y>=0 branch (D1). legacy => y. */
float ammoIconCenterYTop(float y, int h, int legacy);

/* y rect center, y<0 branch (D2), base_h = height arg (H), scale_s = unk_scale
 * arg (S). legacy => base_h + scale_s - h*0.5. */
float ammoIconCenterYBottom(float base_h, float scale_s, int h, int legacy);

/* the flipX slot passed to display_image_at_position (D3). legacy => flip_x?1:0;
 * fixed => 0 (retail never mirrors the icon texture). */
int ammoIconMirrorFlag(int flip_x, int legacy);

#ifdef __cplusplus
}
#endif

#endif /* MGB64_AMMO_ICON_ANCHOR_H */
