/*
 * test_msaa_sample_count.c — ROM-free regression lane for FID-0018.
 *
 * Guards the Metal MSAA sample-count plumbing. The Metal backend previously
 * hardcoded rasterSampleCount = 1 and never built a multisample target, so
 * Video.MSAA was silently ignored on Apple/Metal. The fix routes the setting
 * through gfxMsaaResolveSampleCount() (a requested->device-supported clamp ladder
 * mirroring gfx_opengl_effective_msaa_samples) and asserts, via
 * gfxMsaaPipelineCountsConsistent(), that every pipeline feeding the scene pass
 * shares the pass's sample count (a mismatch aborts Metal at draw time — the
 * reason the count was hardcoded).
 *
 * Asserts:
 *   - off contract: requested 0/1 (and negatives) -> 1 (single-sample), for any
 *     device support mask, so the default (MSAA off) path is byte-identical.
 *   - honor contract: 2/4/8 map to themselves when the device supports them.
 *   - clamp ladder: a requested level the device can't provide drops to the
 *     largest supported step <= requested, else off (1) — never over-samples.
 *   - same-pass consistency guard: a set of matching pipeline counts passes; any
 *     mismatch fails.
 *
 * FAIL-ON-REVERT: if the resolver reverts to "always 1" (the pre-fix hardcode),
 * every honor-contract assertion (2->2, 4->4, 8->8) goes red. If the ladder
 * clamp is removed (returns `requested` unclamped), the "unsupported over-sample"
 * assertions go red. If the consistency guard is loosened to always-true, the
 * mismatch assertion goes red.
 */
#include "gfx_msaa_util.h"

#include <stdio.h>

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

int main(void) {
    const unsigned ALL   = GFX_MSAA_SUP_2 | GFX_MSAA_SUP_4 | GFX_MSAA_SUP_8;
    const unsigned UP_TO_4 = GFX_MSAA_SUP_2 | GFX_MSAA_SUP_4;   /* no 8x */
    const unsigned UP_TO_2 = GFX_MSAA_SUP_2;                    /* no 4x/8x */
    const unsigned NONE  = 0u;

    /* --- off contract: 0/1/negatives -> 1 for any device mask --- */
    CHECK(gfxMsaaResolveSampleCount(0, ALL)  == 1, "MSAA 0 -> off (1)");
    CHECK(gfxMsaaResolveSampleCount(1, ALL)  == 1, "MSAA 1 -> off (1)");
    CHECK(gfxMsaaResolveSampleCount(-4, ALL) == 1, "MSAA <0 -> off (1)");
    CHECK(gfxMsaaResolveSampleCount(0, NONE) == 1, "MSAA 0 no-support -> off (1)");

    /* --- honor contract: setting takes effect when supported --- */
    CHECK(gfxMsaaResolveSampleCount(2, ALL) == 2, "MSAA 2 -> 2x");
    CHECK(gfxMsaaResolveSampleCount(4, ALL) == 4, "MSAA 4 -> 4x");
    CHECK(gfxMsaaResolveSampleCount(8, ALL) == 8, "MSAA 8 -> 8x");

    /* --- clamp ladder: drop to the largest supported step <= requested --- */
    CHECK(gfxMsaaResolveSampleCount(8, UP_TO_4) == 4, "MSAA 8 on 4x-max device -> 4x");
    CHECK(gfxMsaaResolveSampleCount(8, UP_TO_2) == 2, "MSAA 8 on 2x-max device -> 2x");
    CHECK(gfxMsaaResolveSampleCount(4, UP_TO_2) == 2, "MSAA 4 on 2x-max device -> 2x");
    CHECK(gfxMsaaResolveSampleCount(4, GFX_MSAA_SUP_8) == 1,
          "MSAA 4 but only 8x supported (no <=4 step) -> off (1)");
    CHECK(gfxMsaaResolveSampleCount(8, NONE) == 1, "MSAA 8 no MS support -> off (1)");
    CHECK(gfxMsaaResolveSampleCount(2, NONE) == 1, "MSAA 2 no MS support -> off (1)");

    /* --- never over-samples above the request --- */
    CHECK(gfxMsaaResolveSampleCount(2, ALL) <= 2, "MSAA 2 never exceeds 2x");
    CHECK(gfxMsaaResolveSampleCount(3, ALL) == 2, "MSAA 3 (odd) -> 2x, not 4x");

    /* --- same-pass consistency guard --- */
    {
        int match[3]   = {4, 4, 4};
        int mismatch[3] = {4, 1, 4};
        int one[1]     = {8};
        CHECK(gfxMsaaPipelineCountsConsistent(4, match, 3) == 1,
              "all pipelines share pass count -> consistent");
        CHECK(gfxMsaaPipelineCountsConsistent(4, mismatch, 3) == 0,
              "a 1x pipeline in a 4x pass -> inconsistent");
        CHECK(gfxMsaaPipelineCountsConsistent(8, one, 1) == 1, "single 8x pipeline -> consistent");
        CHECK(gfxMsaaPipelineCountsConsistent(1, 0, 0) == 1, "empty set -> vacuously consistent");
    }

    if (g_failures != 0) {
        fprintf(stderr, "test_msaa_sample_count: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    printf("test_msaa_sample_count: all checks passed\n");
    return 0;
}
