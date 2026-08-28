/*
 * test_texgen_coords.c — ROM-free regression lane for the RSP texgen
 * (environment mapping) coordinate mapping in src/platform/fast3d/gfx_texgen.h.
 *
 * Background: the port computed texgen coordinates at 2x the reference scale and
 * applied a spurious S mirror. Both pushed roughly half of all texgen coordinates
 * off the end of the environment map — pinned to the border texel on the
 * G_TX_CLAMP tiles the boot logos use — which is why the boot "GoldenEye" logo
 * rendered washed-out grey on top instead of gold. See RENDERER_SIM_AUDIT
 * 2026-07-06 finding 15, which spotted the 2x and then wrongly dismissed it.
 *
 * There was no test or baseline covering texgen at all, which is how the defect
 * survived that audit. This pins the mapping so it cannot silently drift again.
 *
 * Authority for the expected values is the F3DEX2 microcode
 * (Mr-Wiseguy/f3dex2 f3dex2.s): ST = 0x4000 * (1 + dot), then (ST * scale) >> 16,
 * i.e. coord = (1 + dot) * scale / 4.
 */
#include "gfx_texgen.h"

#include <stdio.h>

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

#define CHECK_EQ(got, want, msg) do { \
    if ((got) != (want)) { \
        fprintf(stderr, "FAIL: %s: got %d want %d (%s:%d)\n", \
                (msg), (int)(got), (int)(want), __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

int main(void)
{
    int32_t u;
    int32_t v;

    /*
     * The GoldenEye boot logo's material: 32-texel env map, gSPTexture scale
     * 0x0800 (2048). One texture span is 32 * 32 = 1024 in the 5.5 domain, and
     * scale/2 == 1024, so the full normal sweep must cover the map exactly once.
     */
    const float logo_scale = 2048.0f;

    /* dot == -1 (normal fully away from the lookat axis) -> map origin. */
    gfx_texgen_coords(-1.0f, -1.0f, logo_scale, logo_scale, false, false, &u, &v);
    CHECK_EQ(u, 0, "dot=-1 must map to the start of the env map");
    CHECK_EQ(v, 0, "dot=-1 must map to the start of the env map (T)");

    /*
     * dot == 0 (camera-facing surface) -> the CENTRE of the map. This is the
     * assertion the whole bug turned on: the old 2x mapping put a camera-facing
     * normal on the map's far edge, so the flat front faces of the logo sampled
     * the pale border instead of the gold interior.
     */
    gfx_texgen_coords(0.0f, 0.0f, logo_scale, logo_scale, false, false, &u, &v);
    CHECK_EQ(u, 512, "dot=0 must map to the centre of the env map");
    CHECK_EQ(v, 512, "dot=0 must map to the centre of the env map (T)");

    /* dot == +1 -> exactly one full span, i.e. the far edge and no further. */
    gfx_texgen_coords(1.0f, 1.0f, logo_scale, logo_scale, false, false, &u, &v);
    CHECK_EQ(u, 1024, "dot=+1 must map to exactly one texture span");
    CHECK_EQ(v, 1024, "dot=+1 must map to exactly one texture span (T)");

    /* S and T must be treated identically — the microcode applies the same
     * +0x4000*dot to both, differing only in which lookat vector feeds them.
     * The removed S mirror is exactly what broke this symmetry. */
    gfx_texgen_coords(0.37f, 0.37f, logo_scale, logo_scale, false, false, &u, &v);
    CHECK_EQ(u, v, "S and T must be symmetric for equal dot products");

    /*
     * Out-of-range dot must clamp, not run off the map. GE's s8 normals are
     * unit-length in practice so this is defensive, but it is also what makes
     * the int16 bound below provable.
     */
    gfx_texgen_coords(1.9f, -3.2f, logo_scale, logo_scale, false, false, &u, &v);
    CHECK_EQ(u, 1024, "dot > 1 must clamp to one span, not overshoot");
    CHECK_EQ(v, 0, "dot < -1 must clamp to the origin, not go negative");

    /*
     * FID-0020 texgen limb: at the maximum representable gSPTexture scale the
     * result must still fit the int16 staging in gfx_sp_vertex. With the /4
     * divisor the bound is scale/2 == 32767; the old mapping reached 65535 and
     * wrapped negative.
     */
    gfx_texgen_coords(1.0f, 1.0f, 65535.0f, 65535.0f, false, false, &u, &v);
    CHECK(u <= 32767 && u >= 0, "max-scale U must fit int16");
    CHECK(v <= 32767 && v >= 0, "max-scale V must fit int16");

    /* The in-world env-mapped material on Dam uses scale 3456 (54-texel map);
     * one span is 54 * 32 = 1728 == scale/2. Same invariant, different size —
     * this is what makes scale == width * 64 a structural relationship rather
     * than a coincidence of the logo's authoring. */
    gfx_texgen_coords(1.0f, 1.0f, 3456.0f, 3456.0f, false, false, &u, &v);
    CHECK_EQ(u, 1728, "54-texel map: dot=+1 must map to exactly one span");

    /*
     * Legacy A/B must still reproduce the pre-fix mapping, so GE007_TEXGEN_LEGACY
     * remains a usable revert switch — and these are the assertions that describe
     * the original defect. One span is 1024, so "== 1024" means "exactly on the
     * border texel" and "> 1024" means "off the map entirely".
     *
     * Old behaviour, 2x scale then U mirrored:
     *   dot =  0 (camera-facing) -> U = 2048-1024 = 1024, V = 1024
     *                               i.e. BOTH axes pinned to the border, which
     *                               is precisely the washed-out logo; and
     *   dot = +1                 -> V = 2048, two full spans off the map.
     */
    gfx_texgen_coords(0.0f, 0.0f, logo_scale, logo_scale, false, true, &u, &v);
    CHECK_EQ(u, 1024, "legacy: camera-facing S sits on the border, not the centre");
    CHECK_EQ(v, 1024, "legacy: camera-facing T sits on the border, not the centre");
    CHECK(v >= 1024, "legacy: camera-facing normal must NOT reach the map interior");

    gfx_texgen_coords(1.0f, 1.0f, logo_scale, logo_scale, false, true, &u, &v);
    CHECK_EQ(v, 2048, "legacy: dot=+1 runs two full spans off the map");

    /*
     * G_TEXTURE_GEN_LINEAR is never set by GE content, so this branch is
     * unreachable and unvalidated against console; pin it only against the
     * reference form acos(-dot)/(2*pi) * scale so it cannot silently drift.
     * Tolerance of 1 LSB: acosf/pi round-tripping in float truncates 1024 -> 1023.
     */
    gfx_texgen_coords(-1.0f, -1.0f, logo_scale, logo_scale, true, false, &u, &v);
    CHECK_EQ(u, 0, "linear: dot=-1 -> acos(1)=0 -> origin");
    gfx_texgen_coords(1.0f, 1.0f, logo_scale, logo_scale, true, false, &u, &v);
    CHECK(u >= 1023 && u <= 1024, "linear: dot=+1 -> acos(-1)=pi -> one full span");
    gfx_texgen_coords(0.0f, 0.0f, logo_scale, logo_scale, true, false, &u, &v);
    CHECK(u >= 511 && u <= 512, "linear: dot=0 -> acos(0)=pi/2 -> map centre");

    if (g_failures != 0) {
        fprintf(stderr, "%d texgen coordinate check(s) failed\n", g_failures);
        return 1;
    }
    printf("texgen coordinate mapping: all checks passed\n");
    return 0;
}
