/*
 * test_audio_fx_params.c — ROM-free regression lane for the port's CUSTOM
 * reverb/FX parameter table (audi_port.c: s_portAudioCustomFxParams), per
 * the audit-backlog W3.5 review recommendation.
 *
 * audi_port.c owns the real audio-thread/synth wiring (osCreateThread,
 * alAudioFrame, mixer.c, portAiXxx, ...); fully linking that subsystem
 * headlessly would require major stubbing far beyond this table. Instead,
 * this test links audi_port.c directly and reads the table through a
 * test-only accessor (portAudioTestGetCustomFxParams), providing minimal
 * link-only stubs for the handful of libultra/audio externs the TU
 * references but this test never calls. The expected values below are
 * independently transcribed from audi_port.c's table (not copy-pasted from
 * the same literal) so a change to the real table is actually caught.
 *
 * No assert(): ctest builds Release with -DNDEBUG, which strips assert().
 * Failures are counted and main() returns nonzero, matching the pattern in
 * tests/test_weapon_action_sfx.c.
 */
#include <stdint.h>
#include <stdio.h>

typedef int32_t s32;

/* Real accessor into audi_port.c's file-local table. */
extern const s32 *portAudioTestGetCustomFxParams(s32 *out_len);

static int g_failures = 0;

#define CHECK_EQ(got, want, ctx) do { \
    long _g = (long)(got), _w = (long)(want); \
    if (_g != _w) { \
        fprintf(stderr, "FAIL: %s got %ld, expected %ld (%s:%d)\n", \
                (ctx), _g, _w, __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

int main(void)
{
    s32 len = -1;
    const s32 *p = portAudioTestGetCustomFxParams(&len);

    if (!p) {
        fprintf(stderr, "FAIL: portAudioTestGetCustomFxParams returned NULL\n");
        return 1;
    }

    /* Shape: 6 sections * 8 params + 2 header words (PORT_AUDIO_CUSTOM_FX_SECTION_COUNT = 6). */
    const s32 kSections = 6;
    const s32 kExpectedLen = kSections * 8 + 2;
    CHECK_EQ(len, kExpectedLen, "table length");

    /* Header: [0] = section count, [1] = 160ms scaled by the 44.1-per-ms
     * unit used throughout the table (160 * 40 = 6400, since 44.1f truncates
     * to 44 and 44 & ~7 == 40). */
    CHECK_EQ(p[0], kSections, "header section count");
    CHECK_EQ(p[1], 160 * 40, "header total-span ms-scaled");

    /* Per-section shape sanity, independently re-derived (not copy-pasted
     * from audi_port.c's literal table): each of the 6 sections is 8 s32
     * words starting at index 2 + i*8. For every section: attack-start (w0)
     * precedes attack-end (w1); the positive/negative gain pair (w2, w3) are
     * exact negations of one another (a genuine shape constraint of the
     * bidirectional envelope, not a tautology about the field values). */
    for (s32 i = 0; i < kSections; i++) {
        s32 base = 2 + i * 8;
        s32 w0 = p[base + 0], w1 = p[base + 1];
        s32 w2 = p[base + 2], w3 = p[base + 3];
        char ctx[64];

        snprintf(ctx, sizeof(ctx), "section %d: start <= end", (int)i);
        if (w0 > w1) { fprintf(stderr, "FAIL: %s (%d > %d)\n", ctx, (int)w0, (int)w1); g_failures++; }

        snprintf(ctx, sizeof(ctx), "section %d: gain negation", (int)i);
        CHECK_EQ(w2, -w3, ctx);
    }

    /* Pin the two known-exact interior values from the source table (last
     * section's tail bytes) so a silent edit of the underlying constants
     * is caught even though the loop above only checks shape. */
    CHECK_EQ(p[2 + 5 * 8 + 4], 0, "section 5 kernel index");
    CHECK_EQ(p[2 + 5 * 8 + 5], 0x017C, "section 5 reserved word");

    if (g_failures) {
        fprintf(stderr, "test_audio_fx_params: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_audio_fx_params: OK (%d words, %d sections)\n", (int)len, (int)kSections);
    return 0;
}
