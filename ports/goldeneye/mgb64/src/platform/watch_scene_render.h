/*
 * watch_scene_render.h — the guPerspective aspect and the room-tint envcolour
 * word for the paused-watch 3D scene renderer sub_GAME_7F087E74
 * (retail ASM src/game/bondview.c, VERSION_US glabel 7F087E74).
 *
 * Pure (ROM-free, SDL-free) mirror of the two retail constants/idioms the
 * NATIVE_PORT rewrite diverged on, factored so a ROM-free unit test can guard
 * their exact bit patterns (FID-0068).
 *
 * DIVERGENCE (re-derived instruction-level from the US ASM):
 *  - ASPECT: retail passes guPerspective a3 = immediate 0x3FBA2E8C = 1.4545455
 *    = 16/11 (the standard GE `c_perspaspect`, lui/ori at 7F087F50-54); the port
 *    passed the literal 1.456f = 0x3FBA5E35, ~0.1% horizontally squeezing the
 *    watch scene.
 *  - TINT: retail assembles the tint as (r<<24)|(g<<16)|(b<<8)|a from the player
 *    tileColor bytes (or the constant 205 for raise/lower states 5 and 12) and
 *    stores it into renderdata.envcolour (+0x34) (ASM 7F0884E4-7F088548). The
 *    port stored it into renderdata.cullmode (+0x3c, inert) via an LE-byte-
 *    reversed *(u32*)&tileColor, so envcolour stayed {0,0,0,0} and the PropType-4
 *    material path (model.c:9344-9366, gDPSetFogColor from envcolour) lost the
 *    room-light tint and the states-5/12 alpha-205 darkening. This word helper
 *    reproduces the verified gun-subdraw sibling idiom at gun.c:15002-15006.
 *
 * See FID-0068.
 */
#ifndef MGB64_WATCH_SCENE_RENDER_H
#define MGB64_WATCH_SCENE_RENDER_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The guPerspective aspect for the paused-watch 3D scene.
 *   legacy : 0 => faithful retail 16/11 (0x3FBA2E8C); != 0 => reproduce the port
 *            defect 1.456f (0x3FBA5E35) for the GE007_NO_WATCH_RENDER_FIX A/B.
 */
float watchScenePerspAspect(int legacy);

/*
 * The renderdata.envcolour.word room tint. watch_state 5 or 12 (raise/lower) =>
 * 205 (black, alpha 205 darkening); otherwise the big-endian packed tileColor
 * (r<<24)|(g<<16)|(b<<8)|a. Host-endian-independent (the PropType-4 consumer
 * reads it back via _SHIFTR of the same word).
 */
unsigned watchSceneTintWord(int watch_state, int r, int g, int b, int a);

#ifdef __cplusplus
}
#endif

#endif /* MGB64_WATCH_SCENE_RENDER_H */
