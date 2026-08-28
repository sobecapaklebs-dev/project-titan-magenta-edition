#include <ultra64.h>
#include "dyn.h"
#include <token.h>
#include <str.h>
#include <memp.h>
#include <macro.h>

#ifdef PROJECT_TITAN_TGR
#include "../../../titan_ge_runtime.h"
#endif

#ifdef NATIVE_PORT
#include <stddef.h>
#include "ge_debug.h" /* port_env_bool/int for the fail-closed hatch + stress clamp */
extern void debTryAdd(void *data, const char *name);
extern s32 getPlayerCount(void);
#endif

/**
 * This file handles memory usage for graphics related tasks.
 *
 * There are two pools, "gfx" and "vtx", which are used to store different data.
 *
 * The gfx pool (g_GfxBuffers) is sized based on the stage's -mgfx
 * argument. It contains only the master display list's GBI bytecode.
 * The master gdl is passed through all rendering functions in the game engine,
 * where each appends to the display list.
 *
 * The vtx pool (g_VtxBuffers) is sized based on the stage's -mvtx argument.
 * It is used for auxiliary graphics data such as vertex arrays, matrices and
 * colours.
 *
 * Both the gfx and vtx pools are split into two buffers of equal size.
 * Only one buffer is active at a time - the other is being drawn to the screen
 * while the active one is being built. Each time a frame is finished the active
 * buffer index is swapped to the other one.
 *
 * Both the gfx and vtx pools have a third element in them, but this is just a
 * marker for the end of the second element's allocation.
 */

u8 *g_GfxBuffers[3];
u8 *g_VtxBuffers[3];
u8 *g_GfxMemPos;
u8 g_GfxActiveBufferIndex;
s32 g_GfxRequestedDisplayList;
s32 D_800482E0 = 0;
#ifdef NATIVE_PORT
/* 64-bit: Gfx commands are 16 bytes (vs 8 on N64), so double the buffer sizes.
 * Additionally, brute-force room rendering (no portal culling) needs more space. */
s32 g_GfxSizesByPlayerCount[] = {0x40000, 0x60000, 0x80000, 0xA0000};
s32 g_VtxSizesByPlayerCount[] = {0x40000, 0x60000, 0x80000, 0xA0000};
#else
s32 g_GfxSizesByPlayerCount[] = {0x10000, 0x18000, 0x20000, 0x28000};
s32 g_VtxSizesByPlayerCount[] = {0x10000, 0x18000, 0x20000, 0x28000};
#endif

char membars_string1[] = ">>>>>>>>>>>>>>>>>>>>>>>>>";
char membars_string2[] = "=========================";
char membars_string3[] = "-------------------------";

#ifdef NATIVE_PORT
/* Fail-closed dyn-allocator contract (M1.2 / audit R3 remaining half).
 *
 * 9b9c4e2 stopped the out-of-bounds *writes* on arena exhaustion, but the
 * allocators still handed back the current (un-advanced) pointer — so two
 * overflowing users in one frame aliased each other's vertices/matrices,
 * producing warped limbs, bad billboards, and stray "shard" geometry under
 * memory pressure. This contract makes overflow fail closed: the byte
 * allocators return NULL (the caller skips its draw) and the matrix allocator
 * returns a distinguishable shared scratch matrix (never NULL, so no caller
 * NULL-derefs) that willing callers detect via dynIsOverflowMatrix() and skip.
 *
 * Normal play never overflows (the playability smokes prove g_dyn_overflow_count
 * stays 0 at defaults); this only changes behavior in the already-broken
 * exhaustion regime. GE007_DYN_LEGACY_ALIAS=1 restores the old aliasing as the
 * A/B hatch, and GE007_DYN_STRESS_LIMIT clamps the arena so overflow is
 * reachable deterministically for the negative-control route. */

/* Per-frame render-health counter, reset in dynGetMasterDisplayList (the single
 * per-frame DL-build entry, before any dynAllocate* call) so the state trace
 * reads exactly this frame's exhaustion. Nonzero fails the render-health audit. */
s32 g_dyn_overflow_count = 0;

/* Shared scratch handed out by dynAllocateMatrix on overflow. Distinguishable by
 * address via dynIsOverflowMatrix(); overflowed matrix producers alias this one
 * transform (better than the OOB write, still detectably degenerate). */
static Mtx s_dynOverflowMatrix;

static int dynLegacyAlias(void) {
    return port_env_bool("GE007_DYN_LEGACY_ALIAS", 0,
        "restore legacy dyn-allocator aliasing on overflow (return current pointer / shared scratch matrix instead of failing closed)");
}

static void dynNoteOverflow(const char *what, size_t need, ptrdiff_t have) {
    static int logged = 0;
    g_dyn_overflow_count++;
    if (!logged) {
        logged = 1; /* once per route */
        printf("[DYN][RENDER-HEALTH] %s arena overflow: fail-closed (need %lu, have %ld "
               "bytes); GE007_DYN_LEGACY_ALIAS=1 restores aliasing\n",
               what, (unsigned long)need, (long)have);
    }
}

/* Overflow test that cannot itself overflow: compares the requested element
 * count / byte size against the available bytes without ever computing
 * count*stride in a width that could wrap. A negative count/size fails closed. */
static int dynByteOverflow(u8 *pos, u8 *end, size_t bytes) {
    if (pos > end) {
        return 1;
    }
    return bytes > (size_t)(end - pos);
}

static int dynCountOverflow(u8 *pos, u8 *end, s32 count, size_t stride) {
    if (count < 0) {
        return 1;
    }
    if (pos > end) {
        return 1;
    }
    /* checked: divide the available bytes instead of multiplying count*stride */
    return (size_t)count > (size_t)(end - pos) / stride;
}

int dynIsOverflowMatrix(const void *m) {
    return m == (const void *)&s_dynOverflowMatrix;
}
#endif

void dynInit(void) {
    debTryAdd(&D_800482E0, "dyn_c_debug");
}

void dynInitMemory(void) {
    s32 playerCount = getPlayerCount();

#ifdef PROJECT_TITAN_TGR
    TitanGeRuntime_Trace("D00_DYN_INIT_ENTER");
    if (playerCount < 1 || playerCount > 4) {
        TitanGeRuntime_Trace("E_DYN_PLAYER_COUNT");
        TitanGeRuntime_Fail();
        return;
    }
    TitanGeRuntime_Trace("D01_PLAYER_COUNT_VALID");
#else
    if (playerCount < 1) playerCount = 1;
    if (playerCount > 4) playerCount = 4;
#endif

    if (tokenFind(1, "-mgfx")) {
        g_GfxSizesByPlayerCount[playerCount - 1] = strtol(tokenFind(1, "-mgfx"), NULL, 0) * 1024;
    }
    if (tokenFind(1, "-mvtx")) {
        g_VtxSizesByPlayerCount[playerCount - 1] = strtol(tokenFind(1, "-mvtx"), NULL, 0) * 1024;
    }

#ifdef NATIVE_PORT
#ifdef PROJECT_TITAN_TGR
    {
        /*
         * Titan/PS2 keeps transient display-list, vertex and texture-decode
         * storage inside GoldenEye's MEMPOOL_STAGE. This matches the original
         * game ownership model and avoids asking the fragmented EE/TGR system
         * heap for another large contiguous block after the native pool exists.
         *
         * The stage token sizes (-mgfx/-mvtx) are already parsed above. On the
         * R5900 N32 target Gfx retains the N64-sized command representation, so
         * the stage-authored byte budgets are the correct baseline. The existing
         * dyn overflow guards remain the final fail-closed protection.
         */
        s32 gfxSize = g_GfxSizesByPlayerCount[playerCount - 1];
        s32 vtxSize = g_VtxSizesByPlayerCount[playerCount - 1];
        const s32 texSize = 128 * 128 * 4;
        s32 stress;
        s32 stageLeft;
        size_t needed;
        u8 *texScratch;

        stress = port_env_int(
            "GE007_DYN_STRESS_LIMIT",
            0,
            "clamp per-buffer dyn VTX arena to this many bytes at init so allocator overflow is reachable for testing (0 = no clamp)"
        );
        if (stress > 0 && stress < vtxSize) {
            vtxSize = stress;
            printf(
                "[DYN] GE007_DYN_STRESS_LIMIT: VTX arena clamped to %dB/buffer\n",
                vtxSize
            );
        }

        TitanGeRuntime_Trace("S10_STAGEPOOL_PLAN_BEFORE");

        if (gfxSize <= 0 || vtxSize <= 0) {
            TitanGeRuntime_Trace("E_DYN_STAGEPOOL_SIZE");
            TitanGeRuntime_Fail();
            return;
        }

        needed = (size_t)gfxSize * 2u
               + (size_t)vtxSize * 2u
               + (size_t)texSize;
        stageLeft = mempGetBankSizeLeft(MEMPOOL_STAGE);

        /*
         * mempAllocBytesInBank intentionally hard-loops on overflow in the
         * original game. Preflight exact remaining space first so Titan keeps
         * the native-app fail-closed contract instead of inheriting that loop.
         */
        if (stageLeft < 0 || needed > (size_t)stageLeft) {
            TitanGeRuntime_Trace("E_DYN_STAGEPOOL_CAPACITY");
            TitanGeRuntime_Fail();
            return;
        }

        TitanGeRuntime_Trace("S11_STAGEPOOL_PLAN_OK");

        g_GfxBuffers[0] = (u8 *)mempAllocBytesInBank(
            (u32)((size_t)gfxSize * 2u),
            MEMPOOL_STAGE
        );
        g_GfxBuffers[1] = g_GfxBuffers[0] + gfxSize;
        g_GfxBuffers[2] = g_GfxBuffers[1] + gfxSize;
        TitanGeRuntime_Trace("S12_STAGEPOOL_GFX_AFTER");

        g_VtxBuffers[0] = (u8 *)mempAllocBytesInBank(
            (u32)((size_t)vtxSize * 2u),
            MEMPOOL_STAGE
        );
        g_VtxBuffers[1] = g_VtxBuffers[0] + vtxSize;
        g_VtxBuffers[2] = g_VtxBuffers[1] + vtxSize;
        TitanGeRuntime_Trace("S13_STAGEPOOL_VTX_AFTER");

        texScratch = (u8 *)mempAllocBytesInBank((u32)texSize, MEMPOOL_STAGE);
        TitanGeRuntime_Trace("S14_STAGEPOOL_TEX_AFTER");

        memset(g_GfxBuffers[0], 0, (size_t)gfxSize * 2u);
        memset(g_VtxBuffers[0], 0, (size_t)vtxSize * 2u);

        {
            extern void gfx_set_pc_dl_range(void *start, size_t size);
            extern void gfx_set_pc_vtx_range(void *start, size_t size);
            extern void gfx_set_pc_tex_decode_range(void *start, size_t size);

            gfx_set_pc_dl_range(g_GfxBuffers[0], (size_t)gfxSize * 2u);
            gfx_set_pc_vtx_range(g_VtxBuffers[0], (size_t)vtxSize * 2u);
            gfx_set_pc_tex_decode_range(texScratch, (size_t)texSize);
        }

        TitanGeRuntime_Trace("S15_STAGEPOOL_RANGES_READY");
        printf(
            "[DYN][TITAN] stagepool GFX=%dKBx2 VTX=%dKBx2 TEX=%dKB "
            "remaining-before=%d playercount=%d\n",
            gfxSize / 1024,
            vtxSize / 1024,
            texSize / 1024,
            stageLeft,
            playerCount
        );
    }
#else
    {
        s32 gfxSize = 512 * 1024;
        s32 vtxSize = 512 * 1024;
        {
            s32 stress = port_env_int("GE007_DYN_STRESS_LIMIT", 0,
                "clamp per-buffer dyn VTX arena to this many bytes at init so allocator overflow is reachable for testing (0 = no clamp)");
            if (stress > 0 && stress < vtxSize) {
                vtxSize = stress;
                printf("[DYN] GE007_DYN_STRESS_LIMIT: VTX arena clamped to %dB/buffer\n", vtxSize);
            }
        }
        g_GfxBuffers[0] = (u8 *)malloc(gfxSize * 2);
        g_GfxBuffers[1] = g_GfxBuffers[0] + gfxSize;
        g_GfxBuffers[2] = g_GfxBuffers[1] + gfxSize;
        g_VtxBuffers[0] = (u8 *)malloc(vtxSize * 2);
        g_VtxBuffers[1] = g_VtxBuffers[0] + vtxSize;
        g_VtxBuffers[2] = g_VtxBuffers[1] + vtxSize;
        memset(g_GfxBuffers[0], 0, gfxSize * 2);
        memset(g_VtxBuffers[0], 0, vtxSize * 2);
        {
            extern void gfx_set_pc_dl_range(void *start, size_t size);
            extern void gfx_set_pc_vtx_range(void *start, size_t size);
            gfx_set_pc_dl_range(g_GfxBuffers[0], gfxSize * 2);
            gfx_set_pc_vtx_range(g_VtxBuffers[0], vtxSize * 2);
        }
        printf("[DYN] PC malloc'd GFX=%dKBx2 VTX=%dKBx2 gfx=[%p,%p) vtx=%p (playercount=%d)\n",
               gfxSize/1024, vtxSize/1024,
               (void*)g_GfxBuffers[0], (void*)g_GfxBuffers[2],
               (void*)g_VtxBuffers[0], playerCount);
    }
#endif
#else
    g_GfxBuffers[0] = mempAllocBytesInBank(g_GfxSizesByPlayerCount[playerCount - 1] * 2, MEMPOOL_STAGE);
    g_GfxBuffers[1] = g_GfxBuffers[0] + g_GfxSizesByPlayerCount[playerCount - 1];
    g_GfxBuffers[2] = g_GfxBuffers[1] + g_GfxSizesByPlayerCount[playerCount - 1];
    g_VtxBuffers[0] = mempAllocBytesInBank(g_VtxSizesByPlayerCount[playerCount - 1] * 2, MEMPOOL_STAGE);
    g_VtxBuffers[1] = g_VtxBuffers[0] + g_VtxSizesByPlayerCount[playerCount - 1];
    g_VtxBuffers[2] = g_VtxBuffers[1] + g_VtxSizesByPlayerCount[playerCount - 1];
#endif
    g_GfxActiveBufferIndex = 0;
    g_GfxRequestedDisplayList = FALSE;
    g_GfxMemPos = g_VtxBuffers[0];
#ifdef PROJECT_TITAN_TGR
    TitanGeRuntime_Trace("D10_DYN_INIT_DONE");
#endif
}

Gfx *dynGetMasterDisplayList(void) {
    g_GfxRequestedDisplayList = TRUE;
#ifdef NATIVE_PORT
    g_dyn_overflow_count = 0; /* per-frame render-health reset (see contract above) */
#endif
    return (Gfx*)g_GfxBuffers[g_GfxActiveBufferIndex];
}

s32 dynGetFreeGfx2(Gfx *gdl) {
    return (Gfx*)g_GfxBuffers[g_GfxActiveBufferIndex + 1] - gdl;
}

void/*Vtx?*/ *dynAllocate7F0BD6C4(s32 count) {
    void *ptr = g_GfxMemPos;
#ifdef NATIVE_PORT
	/* Every effect billboard (explosion/smoke/spark quad) and model reserves its
	 * Vtx array here; a heavy frame can push g_GfxMemPos past the VTX buffer end.
	 * Fail closed (return NULL — the caller skips its draw) instead of aliasing
	 * the current pointer into another user's Vtx data. */
	u8 *end = g_VtxBuffers[g_GfxActiveBufferIndex + 1];
	if (dynCountOverflow(g_GfxMemPos, end, count, 0x10)) {
		dynNoteOverflow("Vtx", (size_t)(count < 0 ? 0 : count) * 0x10,
						end - g_GfxMemPos);
		return dynLegacyAlias() ? ptr : NULL;
	}
#endif
	g_GfxMemPos += count * 0x10/*sizeof(Vtx)?*/;
	return ptr;
}

Mtx *dynAllocateMatrix(void)
{
#ifdef NATIVE_PORT
	/* A heavy frame -- e.g. a window shattering into many glass shards, each
	 * allocating a matrix late in lvlRender -- can push g_GfxMemPos past the VTX
	 * buffer end. Return the shared overflow-scratch matrix (never NULL, so no
	 * caller NULL-derefs) instead of an out-of-bounds pointer; willing callers
	 * detect it via dynIsOverflowMatrix() and skip their draw. */
	if (g_GfxMemPos + sizeof(Mtx) > g_VtxBuffers[g_GfxActiveBufferIndex + 1]) {
		dynNoteOverflow("Mtx", sizeof(Mtx),
						g_VtxBuffers[g_GfxActiveBufferIndex + 1] - g_GfxMemPos);
		return &s_dynOverflowMatrix;
	}
#endif
	{
		void *ptr = g_GfxMemPos;
		g_GfxMemPos += sizeof(Mtx);
		return ptr;
	}
}

void/*Light?*/ *dynAllocate7F0BD6F8(s32 count) {
    void *ptr = g_GfxMemPos;
#ifdef NATIVE_PORT
	/* A LookAt/Light reservation on a VTX-pool-exhausted frame must not alias
	 * another user's data. Fail closed (return NULL — the caller skips). */
	u8 *end = g_VtxBuffers[g_GfxActiveBufferIndex + 1];
	if (dynCountOverflow(g_GfxMemPos, end, count, 0x10)) {
		dynNoteOverflow("Light", (size_t)(count < 0 ? 0 : count) * 0x10,
						end - g_GfxMemPos);
		return dynLegacyAlias() ? ptr : NULL;
	}
#endif
	g_GfxMemPos += count * 0x10/*sizeof(Light)?*/;
	return ptr;
}

void *dynAllocate(s32 size) {
    void *ptr = g_GfxMemPos;
	size = ALIGN16_a(size);
#ifdef NATIVE_PORT
	{
		u8 *end = g_VtxBuffers[g_GfxActiveBufferIndex + 1];
		if (size < 0 || dynByteOverflow(g_GfxMemPos, end, (size_t)size)) {
			dynNoteOverflow("VTX", (size_t)(size < 0 ? 0 : size), end - g_GfxMemPos);
			return dynLegacyAlias() ? ptr : NULL;
		}
	}
#endif
	g_GfxMemPos += size;
	return ptr;
}

void dynSwapBuffers(void) {
#ifdef NATIVE_PORT
    /* Stall-watchdog breadcrumb: g_GfxMemPos resets on swap, so the finished
     * frame's VTX-arena watermark must be captured here (one store/frame). */
    {
        extern volatile int g_portWatchdogDynVtxUsed;
        g_portWatchdogDynVtxUsed = (int)(g_GfxMemPos - g_VtxBuffers[g_GfxActiveBufferIndex]);
    }
#endif
    g_GfxActiveBufferIndex = (g_GfxActiveBufferIndex ^ 1);
    g_GfxRequestedDisplayList = FALSE;
    g_GfxMemPos = g_VtxBuffers[g_GfxActiveBufferIndex];
}

void dynRemovedFunc(Gfx *gdl) {
}

s32 dynGetFreeGfx(Gfx *gdl) {
    return (Gfx*)g_GfxBuffers[g_GfxActiveBufferIndex + 1] - gdl;
}

s32 dynGetFreeVtx(void) {
	return g_VtxBuffers[g_GfxActiveBufferIndex + 1] - g_GfxMemPos;
}

// Address 0x7F0BD7CC NTSC
void dynCalculateMembarLength(const char* arg0, f32 arg1, f32 arg2)
{
    s32 len;
    f32 zero = 0;
    
    len = strlen(arg0);
    
    arg1 /= arg2;
    
    if(zero);
    
    if (arg1 < zero && len > 1)
    {
        if (len > 1)
        {
            
        }
    }
}

void dynDrawMembars(Gfx *gdl) {
    dynCalculateMembarLength(membars_string2, ((Gfx*)g_GfxBuffers[g_GfxActiveBufferIndex + 1] - gdl), ((Gfx*)g_GfxBuffers[g_GfxActiveBufferIndex + 1] - (Gfx*)g_GfxBuffers[g_GfxActiveBufferIndex]));
    dynCalculateMembarLength(membars_string2, (g_VtxBuffers[g_GfxActiveBufferIndex + 1] - g_GfxMemPos), (g_VtxBuffers[g_GfxActiveBufferIndex + 1] - g_VtxBuffers[g_GfxActiveBufferIndex]));
}
