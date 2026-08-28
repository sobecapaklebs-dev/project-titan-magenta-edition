/*
 * watch_inv_aspect.h — the guPerspective aspect for the solo watch-inventory
 * 3D item (draw_watch_inventory_page, retail ASM src/game/watch.c:4147-4148,
 * VERSION_US glabel src/game/watch.c:4027).
 *
 * Pure (ROM-free, SDL-free) mirror of the retail aspect constant, factored out
 * so a ROM-free unit test can guard its bit pattern (FID-0098).
 *
 * DIVERGENCE. Retail passes a3 = immediate 0x3FAAAAAB = 1.3333334 = 4/3 (the
 * lui/ori pair at src/game/watch.c:4147-4148). The NONMATCHING port passes
 * 1.2857143f (src/game/watch.c:3926), which compiles to 0x3FA49249 = 9/7 and
 * does NOT round-trip to the retail 0x3FAAAAAB, so the HUD-space item is drawn
 * slightly narrower/taller than on N64. A HUD-space item aspect has no
 * widescreen-correction rationale, so this is an accidental transcription
 * error. See FID-0098.
 */
#ifndef MGB64_WATCH_INV_ASPECT_H
#define MGB64_WATCH_INV_ASPECT_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The guPerspective aspect for the watch inventory 3D item.
 *
 *   legacy : 0 => faithful retail 4/3 (0x3FAAAAAB); != 0 => reproduce the port
 *            defect 9/7 (0x3FA49249) for the GE007_NO_WATCH_INV_ASPECT_FIX A/B
 *            negative control.
 */
float watchInvPerspAspect(int legacy);

#ifdef __cplusplus
}
#endif

#endif /* MGB64_WATCH_INV_ASPECT_H */
