/*
 * texture_pack.c -- optional HD texture-pack loader. See texture_pack.h.
 *
 * Hooked from the static-texture (G_SETTEX) decode path in fast3d/gfx_pc.c: after
 * the N64 texels are decoded to RGBA, if a pack PNG exists for the texture token
 * we upload that instead, at its native (higher) resolution. Because we replace
 * the *already-decoded* RGBA, CI-palette and I/IA prim-tint handling are inherited
 * for free -- the HD image is just a higher-res version of the same decoded master.
 */
#include "texture_pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "stb_image.h"

/* Configured pack directory: the Video.TexturePack string (also fed by the
 * GE007_TEXTURE_PACK env via the settings layer). Empty => loader disabled. */
extern char g_pcTexturePack[];

/* texturenum is (w1 & 0xFFF) in gfx_handle_settex, so 0..4095. */
#define TEXPACK_MAX_TOKENS 4096

/* Per-token miss cache: 1 = known-missing (skip the disk stat). Hits are cheap to
 * re-load (the settex GL cache means a token is loaded at most once per residency)
 * so we only memoize misses, which is what would otherwise re-stat repeatedly. */
static uint8_t s_miss[TEXPACK_MAX_TOKENS];
static int s_miss_init = 0;

bool texture_pack_enabled(void) {
    return g_pcTexturePack[0] != '\0';
}

/* One-time sanity check of the configured pack. A non-empty Video.TexturePack is
 * the ONLY thing that "enables" the pack, so a typo'd or missing directory would
 * otherwise silently render stock with zero diagnostics — the single most common
 * user error for a headline remaster feature. Warn once (and confirm on success). */
static void texture_pack_validate_once(void) {
    static int validated = 0;
    if (validated) {
        return;
    }
    validated = 1;
    if (!texture_pack_enabled()) {
        return;
    }

    char dir[1200];
    snprintf(dir, sizeof(dir), "%s/textures", g_pcTexturePack);
    struct stat st;
    if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr,
                "[texpack] WARNING: texture pack '%s' has no readable 'textures/' "
                "directory (looked for '%s'). HD textures are DISABLED — rendering "
                "stock. Check Video.TexturePack / GE007_TEXTURE_PACK.\n",
                g_pcTexturePack, dir);
    } else {
        fprintf(stderr, "[texpack] HD texture pack active: %s\n", g_pcTexturePack);
    }
}

bool texture_pack_try_load(int token, uint8_t **out_rgba, int *out_w, int *out_h) {
    if (!texture_pack_enabled() || token < 0 || token >= TEXPACK_MAX_TOKENS) {
        return false;
    }
    texture_pack_validate_once();
    if (!s_miss_init) { memset(s_miss, 0, sizeof(s_miss)); s_miss_init = 1; }
    if (s_miss[token]) {
        return false;
    }

    char path[1200];
    snprintf(path, sizeof(path), "%s/textures/tok%04d.png", g_pcTexturePack, token);

    int w = 0, h = 0, n = 0;
    /* Force 4-channel RGBA8 to match the engine's upload_texture contract. */
    unsigned char *rgba = stbi_load(path, &w, &h, &n, 4);
    if (rgba == NULL || w <= 0 || h <= 0) {
        if (rgba) stbi_image_free(rgba);
        s_miss[token] = 1;
        return false;
    }

    *out_rgba = (uint8_t *)rgba; /* stbi uses malloc; caller frees with free() */
    *out_w = w;
    *out_h = h;
    return true;
}
