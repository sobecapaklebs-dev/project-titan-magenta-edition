/*
 * decor.h -- render-only scene-decoration layer (Video.SceneDecor, default 0).
 *
 * Draws imported 3D models (glTF via src/platform/decor_assets.c) on top of
 * the untouched simulation: the emitter appends native-endian display-list
 * commands after the room pass and reads no sim state beyond the render
 * globals rooms already use. OFF (the default) emits zero commands -- frames
 * stay byte-identical, so faithful mode is unaffected by construction.
 */
#ifndef _DECOR_H_
#define _DECOR_H_

#include <ultra64.h>

/* Append the decor draw (src/game/decor_native.c). Called from lvlRender
 * right after bgLevelRender; returns the advanced DL cursor. */
Gfx *decorRender(Gfx *gdl);

/* Stage slug for a raw LEVELID ("surface1", ...), or NULL (main_pc.c). */
const char *pcStageSlugForLevelId(s32 level_id);

#endif
