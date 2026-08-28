/*
 * test_watch_scroll_gate.c — ROM-free regression lane for FID-0100.
 *
 * Guards the watch-inventory UP snap-scroll gate (watch_scroll_gate.c). Retail
 * snaps up on button OR stick-full-up (||, matching the down sibling); the port
 * defect required button AND stick (&&), so a plain up-button tap with the stick
 * centered no longer snap-scrolled.
 *
 * Fails if the fix reverts to `&&`: watchInvUpSnapGate(1, 0, 0) would return 0
 * instead of 1 and go red.
 */
#include "watch_scroll_gate.h"

#include <stdio.h>

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

int main(void)
{
    /* --- fixed (retail ||): button OR stick-full-up --- */
    CHECK(watchInvUpSnapGate(1, 0x00, 0) != 0, "button tap, stick centered -> snaps (the fix)");
    CHECK(watchInvUpSnapGate(0, 0x50, 0) != 0, "no button, stick full up (>=0x47) -> snaps");
    CHECK(watchInvUpSnapGate(1, 0x50, 0) != 0, "button + stick full up -> snaps");
    CHECK(watchInvUpSnapGate(0, 0x46, 0) == 0, "no button, stick just under 0x47 -> no snap");
    CHECK(watchInvUpSnapGate(0, 0x47, 0) != 0, "stick exactly 0x47 -> snaps (>= threshold)");
    CHECK(watchInvUpSnapGate(0, 0x00, 0) == 0, "no button, stick centered -> no snap");

    /* --- legacy (port defect &&): button AND stick-full-up --- */
    CHECK(watchInvUpSnapGate(1, 0x00, 1) == 0, "legacy: button tap alone -> NO snap (the defect)");
    CHECK(watchInvUpSnapGate(0, 0x50, 1) == 0, "legacy: stick alone -> no snap");
    CHECK(watchInvUpSnapGate(1, 0x50, 1) != 0, "legacy: button + stick full up -> snaps");

    if (g_failures == 0) {
        printf("PASS: watch_scroll_gate\n");
        return 0;
    }
    fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
}
