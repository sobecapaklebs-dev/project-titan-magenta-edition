/*
 * texture_pack.h -- optional HD texture-pack loader (Remaster Phase 2).
 *
 * When a pack directory is configured (Video.TexturePack / GE007_TEXTURE_PACK),
 * the renderer's static-texture path looks up an artist-supplied HD PNG keyed by
 * the texture's token and uploads it in place of the decoded N64 texels. UVs
 * normalize by the logical tile dims, so a higher-resolution replacement needs no
 * geometry change. Default (no pack) => byte-identical to stock.
 *
 * HD packs are ROM-derived (built from YOUR dump via tools/texpack) and are
 * personal-use/local only -- never shipped with this repo.
 */
#ifndef GE007_TEXTURE_PACK_H
#define GE007_TEXTURE_PACK_H

#include <stdbool.h>
#include <stdint.h>

/* True iff a pack directory is configured (cheap; safe in the hot path). */
bool texture_pack_enabled(void);

/*
 * Try to load <pack>/textures/tok<token>.png for the given static texture token.
 * On success returns true and sets *out_rgba (malloc'd RGBA8, caller frees with
 * free()), *out_w, *out_h. On miss returns false; misses are remembered so a
 * texture without a replacement is not re-stat'd on every reload.
 */
bool texture_pack_try_load(int token, uint8_t **out_rgba, int *out_w, int *out_h);

#endif /* GE007_TEXTURE_PACK_H */
