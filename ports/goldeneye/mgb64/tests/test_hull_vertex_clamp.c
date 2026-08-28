/*
 * test_hull_vertex_clamp.c — ROM-free regression lane for FID-0096.
 *
 * Guards the bbox-projection hull vertex-count clamp (hull_vertex_clamp.c). The
 * port clamps to 4 by default (memory-safe for all callers incl. the tank, whose
 * rect4f is followed by live fields); GE007_HULL_VERTS_RETAIL opts into the
 * retail true count (no clamp).
 *
 * Fails if the default clamp is dropped (would return 6 instead of 4) or the
 * opt-in re-clamps.
 */
#include "hull_vertex_clamp.h"

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
    /* default: clamp to 4 (memory-safe port policy) */
    CHECK(hullVertexCount(4, 0) == 4, "default: 4 -> 4");
    CHECK(hullVertexCount(5, 0) == 4, "default: 5 -> 4 (clamped)");
    CHECK(hullVertexCount(6, 0) == 4, "default: 6 -> 4 (clamped)");
    CHECK(hullVertexCount(8, 0) == 4, "default: 8 -> 4 (clamped)");
    CHECK(hullVertexCount(3, 0) == 3, "default: 3 -> 3 (below clamp, unchanged)");

    /* opt-in: retail true count (no clamp) */
    CHECK(hullVertexCount(4, 1) == 4, "retail: 4 -> 4");
    CHECK(hullVertexCount(5, 1) == 5, "retail: 5 -> 5 (unclamped)");
    CHECK(hullVertexCount(6, 1) == 6, "retail: 6 -> 6 (unclamped)");
    CHECK(hullVertexCount(8, 1) == 8, "retail: 8 -> 8 (unclamped, fits through unk40)");

    if (g_failures == 0) {
        printf("PASS: hull_vertex_clamp\n");
        return 0;
    }
    fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
}
