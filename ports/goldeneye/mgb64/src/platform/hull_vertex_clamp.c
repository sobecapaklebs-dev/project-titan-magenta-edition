/*
 * hull_vertex_clamp.c — see hull_vertex_clamp.h.
 *
 * Default clamps to 4 (the port's memory-safe policy, byte-identical to the
 * pre-fix `if (count > 4) count = 4;`); the opt-in returns the true count
 * (retail, no clamp). See FID-0096.
 */
#include "hull_vertex_clamp.h"

int hullVertexCount(int count, int retail_unclamped)
{
    if (retail_unclamped) {
        return count; /* retail: store the true hull count (up to 8) */
    }
    return (count > 4) ? 4 : count; /* port default: clamp to the 4-vertex rect4f */
}
