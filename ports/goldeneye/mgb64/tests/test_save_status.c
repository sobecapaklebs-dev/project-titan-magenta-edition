/*
 * test_save_status.c — ROM-free/SDL-free/ImGui-free unit test for the AUDIT-0036
 * pure slice (save_status.c): the settings-UI status-line chooser and the
 * hard-failure predicate that drives Retry + the "don't silently resume" overlay
 * policy.
 *
 * Pins:
 *   - saveStatusMessage(): the exact OK / SUPPRESSED / FAILED wording surfaced
 *     under Apply / Save Settings. Byte-fidelity so a reworded message is a
 *     deliberate edit, not an accident.
 *   - saveStatusIsFailure(): true ONLY for MGB_CONFIG_SAVE_FAILED — the
 *     intentional faithful-mode SUPPRESSED no-write is NOT an error.
 *
 * NDEBUG strips assert(), so failures are counted and returned nonzero.
 */
#include "save_status.h"
#include <stdio.h>
#include <string.h>

/* Em-dash (U+2014) as UTF-8 bytes, matching the message literals in save_status.c. */
#define EMDASH "\xE2\x80\x94"

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); g_failures++; } \
} while (0)

static void expectMsg(MgbConfigSaveResult r, const char *want) {
    const char *got = saveStatusMessage(r);
    if (got == NULL || strcmp(got, want) != 0) {
        fprintf(stderr, "FAIL: message[%d] -> \"%s\" (want \"%s\") (%s:%d)\n",
                (int)r, got ? got : "(null)", want, __FILE__, __LINE__);
        g_failures++;
    }
}

int main(void) {
    /* ---- saveStatusMessage: exact wording per outcome ---- */
    expectMsg(MGB_CONFIG_SAVE_OK,         "Saved to ge007.ini");
    expectMsg(MGB_CONFIG_SAVE_SUPPRESSED,
              "Applied to this run only " EMDASH " faithful/remaster session doesn't persist");
    expectMsg(MGB_CONFIG_SAVE_FAILED,     "Couldn't save " EMDASH " check permissions");

    /* Never NULL, even for an out-of-enum value (defensive default). */
    CHECK(saveStatusMessage((MgbConfigSaveResult)99) != NULL, "unknown outcome -> non-NULL");

    /* ---- saveStatusIsFailure: only FAILED is a hard failure ---- */
    CHECK(saveStatusIsFailure(MGB_CONFIG_SAVE_OK) == 0, "OK is not a failure");
    CHECK(saveStatusIsFailure(MGB_CONFIG_SAVE_SUPPRESSED) == 0, "SUPPRESSED is not a failure");
    CHECK(saveStatusIsFailure(MGB_CONFIG_SAVE_FAILED) == 1, "FAILED is a failure");

    if (g_failures) { fprintf(stderr, "%d check(s) failed\n", g_failures); return 1; }
    printf("all save_status checks passed\n");
    return 0;
}
