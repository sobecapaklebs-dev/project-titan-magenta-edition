/*
 * texpack_stb.c -- single translation unit that compiles the vendored stb_image
 * decoder used by the optional HD texture-pack loader (texture_pack.c).
 *
 * stb_image is public domain (see lib/stb/stb_image.h trailer + THIRD_PARTY.md).
 * We only need the loader (decode PNG -> RGBA); no writer is compiled here.
 */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG          /* HD packs are PNG; keep the binary lean */
/* Cap decoded dimensions to match both renderer backends' >4096 upload reject.
 * stb's default cap is 1<<24 per axis, so a corrupt/oversized PNG well above
 * our own limit (e.g. 8000x8000 -- still comfortably under stb's own separate
 * 2^30-pixel decode-overflow guard, which only catches the *most* extreme
 * cases) would otherwise be fully allocated+decoded (~256MB+ for that
 * example) before the upload path ever gets a chance to reject it. With this
 * define, stb fails fast at header-parse time instead (before any bulk
 * allocation), and texture_pack_try_load()'s existing NULL-return path below
 * still caches the miss in s_miss[] so it isn't re-attempted every
 * settex-cache reload. */
#define STBI_MAX_DIMENSIONS 4096
/* NB: do NOT define STBI_NO_STDIO -- stb checks definedness, not value, so even
 * "#define STBI_NO_STDIO 0" would remove the path-based stbi_load() we rely on. */
#include "stb_image.h"
