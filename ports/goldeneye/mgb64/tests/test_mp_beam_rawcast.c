/*
 * test_mp_beam_rawcast.c — ROM-free fail-on-revert lane for FID-0094.
 *
 * Compiles the real struct player / struct hand (bondview.h) and proves, at
 * runtime, that the faithful NAMED-field offsets the default-ON fix uses in
 * playerTickBeams (firing flag = hands[i].weapon_firing_status; per-hand beam
 * source = hands[i].field_B50/B54/B58) never coincide with the legacy raw N64
 * byte offsets (0x875 / 0xC1D; i*936 + 0xB50 + word*4). This complements the
 * compile-time _Static_asserts in test_struct_layout.c: if the fix is reverted
 * to raw offsets, or the layout ever makes a raw offset correct, the divergence
 * would be illusory — this catches it. Also confirms the native hand stride is
 * 968 (not the N64 936 the legacy srcOff strength-reduces to).
 */
#include <stddef.h>
#include <stdio.h>

#include "bondview.h"          /* struct player / struct hand */
#include "mp_beam_rawcast.h"

int main(void) {
    int failures = 0;
    int hand;
    int word;

    /* Native hand stride must be 968, not the N64 936 the legacy path uses. */
    if (sizeof(struct hand) == MP_BEAM_N64_HAND_STRIDE) {
        printf("FAIL: native sizeof(struct hand)==%u == N64 936 "
               "(FID-0094 stride moot)\n", (unsigned)sizeof(struct hand));
        failures++;
    }

    /* The two legacy firing raws are exactly one N64 hand stride apart. */
    if (mpBeamLegacyFiringOffset(1) - mpBeamLegacyFiringOffset(0)
            != MP_BEAM_N64_HAND_STRIDE) {
        printf("FAIL: legacy firing raws not one N64 stride apart\n");
        failures++;
    }

    /* Firing flag: faithful named-field offset != legacy raw (0x875 / 0xC1D). */
    for (hand = 0; hand < 2; hand++) {
        size_t faithful = offsetof(struct player, hands)
                        + (size_t)hand * sizeof(struct hand)
                        + offsetof(struct hand, weapon_firing_status);
        size_t legacy = mpBeamLegacyFiringOffset(hand);
        if (faithful == legacy) {
            printf("FAIL: hand %d firing faithful 0x%zx == legacy raw 0x%zx\n",
                   hand, faithful, legacy);
            failures++;
        }
    }

    /* Per-hand beam source: faithful field_B50/B54/B58 word offsets != legacy
     * raw (i*936 + 0xB50 + word*4). */
    for (hand = 0; hand < 2; hand++) {
        size_t base = offsetof(struct player, hands)
                    + (size_t)hand * sizeof(struct hand)
                    + offsetof(struct hand, field_B50);
        for (word = 0; word < 3; word++) {
            size_t faithful = base + (size_t)word * 4u;
            size_t legacy = mpBeamLegacyHandSrcOffset(hand, word);
            if (faithful == legacy) {
                printf("FAIL: hand %d word %d src faithful 0x%zx == "
                       "legacy raw 0x%zx\n", hand, word, faithful, legacy);
                failures++;
            }
        }
    }

    /* field_B54 / field_B58.x must sit consecutively after field_B50 so the
     * three-word source copy maps to the faithful named fields. */
    if (offsetof(struct hand, field_B54) != offsetof(struct hand, field_B50) + 4u) {
        printf("FAIL: field_B54 not field_B50 + 4\n");
        failures++;
    }
    if (offsetof(struct hand, field_B58) != offsetof(struct hand, field_B50) + 8u) {
        printf("FAIL: field_B58 not field_B50 + 8\n");
        failures++;
    }

    if (failures == 0) {
        printf("test_mp_beam_rawcast: PASS (FID-0094 faithful named-field "
               "offsets != legacy raw N64 offsets; hand stride 968 != 936)\n");
        return 0;
    }
    printf("test_mp_beam_rawcast: FAIL (%d)\n", failures);
    return 1;
}
