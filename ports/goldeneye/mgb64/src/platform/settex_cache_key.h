/*
 * settex_cache_key.h — the G_SETTEX texture-cache key discriminator (TMEM-2).
 *
 * Rare's G_SETTEX loads a texture by number; the port decodes it to RGBA once
 * and caches the result (fast3d/gfx_pc.c, settex_cache[]). The cache was keyed
 * on the texture number ALONE, so the same number re-drawn against a different
 * TLUT returned the FIRST palette's decode — a CI texture whose palette was
 * re-stored by a runtime G_LOADTLUT kept stale colours (DAM_RENDER_DEEP_DIVE
 * 2026-07-18, TMEM-2; the structural blocker for the format-reinterpretation
 * work, TMEM-1).
 *
 * The fix adds the decode identity to the key. `fmt`/`siz` are stable per
 * texturenum (they come from the ROM texture header), so keying on them is
 * inert today and byte-identical; the live discriminator is the palette. This
 * predicate isolates the per-entry HIT/MISS decision so it is pure and
 * ROM-free unit-testable, and gfx_pc.c uses this exact function (no drift).
 *
 * A CI entry (fmt == GFX_SETTEX_KEY_FMT_CI) is a hit only when the palette
 * identity that produced the cached RGBA matches the query's current palette
 * identity (texGetPalette's pointer, which encodes both TLUT address and the
 * resolved 4-bit bank). Non-CI entries carry no palette and always hit on a
 * texturenum match, so their behaviour is unchanged.
 */
#ifndef SETTEX_CACHE_KEY_H
#define SETTEX_CACHE_KEY_H

#include <stdbool.h>
#include <stdint.h>

/* G_IM_FMT_CI (PR/gbi.h). Mirrored here so this header stays self-contained and
 * ROM-free; gfx_pc.c asserts it equals G_IM_FMT_CI so the two cannot drift. */
#define GFX_SETTEX_KEY_FMT_CI 2u

/*
 * Is a cached settex entry with (cached_fmt, cached_pal_key) still a valid hit
 * for a query whose current palette identity is query_pal_key? The caller has
 * already confirmed the texture number matches.
 *   - CI entry: hit iff the palette identity is unchanged.
 *   - non-CI entry: always a hit (no palette; pal keys ignored).
 */
static inline bool gfx_settex_cache_key_hit(uint32_t cached_fmt,
                                            uintptr_t cached_pal_key,
                                            uintptr_t query_pal_key)
{
    if (cached_fmt == GFX_SETTEX_KEY_FMT_CI) {
        return cached_pal_key == query_pal_key;
    }
    return true;
}

#endif /* SETTEX_CACHE_KEY_H */
