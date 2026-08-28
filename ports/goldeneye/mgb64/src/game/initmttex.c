#include <ultra64.h>
#include <memp.h>
#include "image.h"
#include "initmttex.h"
#include <token.h>
#ifdef NATIVE_PORT
#include <stdio.h>
#endif
#ifdef PROJECT_TITAN_TGR
#include "../../../titan_ge_runtime.h"
#endif

void set_mt_tex_alloc(void)
{
    g_TexCacheCount = 0;

#ifdef NATIVE_PORT
    /* Report the outgoing level's texture footprint before re-initialising.
     * With the grow-on-demand arena there is no fixed capacity to overflow, so
     * this is now a pure footprint/chunk-count readout (and overflow events,
     * which should stay 0 outside a true OOM). */
    if (texPoolStatsEnabled() && g_TexPoolHighWater != 0) {
        fprintf(stderr,
            "[TEXPOOL] level done: footprint=%u bytes across %u chunk(s), hard-overflow events=%d\n",
            g_TexPoolHighWater, texArenaChunkCount(), g_TexPoolOverflowCount);
        fflush(stderr);
    }
    g_TexPoolOverflowCount = 0;
    g_TexPoolHighWater = 0;

    /* On PC the arena ignores the N64 -mt per-level budget entirely. */
    if (!texArenaInit()) {
#ifdef PROJECT_TITAN_TGR
        /*
         * Titan must never fall back to the legacy fixed main texture pool:
         * its overflow path deliberately substitutes pool->start (garbage),
         * which is exactly the scrambled-grid failure P10.18 exposed.
         */
        TitanGeRuntime_Trace("E_TEX_ARENA_INIT");
        TitanGeRuntime_Fail();
        return;
#else
        /* Out of host memory: fall back to the legacy fixed pool so the game
         * still boots (degraded) rather than crashing at level load. */
        const char *tf = tokenFind(1, "-mt");
        if (tf) {
            bytes = strtol(tf, NULL, 0) * 1024;
        }
        texInitPool(ptr_texture_alloc_start, mempAllocBytesInBank(bytes, MEMPOOL_STAGE), bytes);
#endif
    }
#else
    if (tokenFind(1, "-mt"))
    {
        bytes = strtol(tokenFind(1, "-mt"), 0x0, 0) * 1024;
    }

    texInitPool(&ptr_texture_alloc_start, mempAllocBytesInBank(bytes, MEMPOOL_STAGE), bytes);
#endif
}
