#include <ultra64.h>
#include <memp.h>
#include "initexplosioncasing.h"
#include "explosions.h"
#include "lvl.h"
#include "player.h"
#ifdef NATIVE_PORT
#include "gfx_pc.h"
#include <string.h>
#include <stdlib.h>
#endif

#ifdef NATIVE_PORT
/*
 * FID-0046: effect-buffer zero-init (determinism, defaults ON).
 *
 * The explosion/smoke/scorch/impact/particle buffers are bump-allocated from the
 * stage pool right after the transient level-load file loads have used that same
 * pool region as scratch. Their init only writes the per-entry "free" flags
 * (Explosion.parts[j].frame, Smoke.parts[j].size, Scorch.roomid, BulletImpact.room,
 * FlyingParticles.unk00) plus prop=NULL, leaving the remaining fields
 * (part pos/size/rot/bb, vertex lists, models, timers, …) holding leftover
 * file-load bytes. The game never READS those fields while the entry is free
 * (an entry is only read after its free flag flips and its fields are written),
 * so on retail this is a benign uninitialized read made deterministic by fixed
 * RDRAM addressing. Under the port's ASLR, one of those leftover words is the
 * low 32 bits of a converted host pointer (value = base + k*16MB), which varies
 * per process and lands in the whole-pool sim-state invariance hash — making a
 * stage non-deterministic across processes even though gameplay is identical
 * (Streets/LEVELID 29 was the first level whose layout exposed it).
 *
 * Fully zeroing each buffer at allocation removes the uninitialized read at the
 * source: the pool becomes byte-deterministic across processes, and gameplay is
 * unchanged because the game only ever reads these fields after writing them.
 * GE007_NO_EFFECT_BUF_ZERO_INIT=1 restores the legacy leave-uninitialized
 * behavior (gameplay byte-identical either way; only the hash determinism moves).
 * N64/matching builds keep the retail partial-init untouched.
 */
static int effectBufZeroInitEnabled(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = (getenv("GE007_NO_EFFECT_BUF_ZERO_INIT") == NULL) ? 1 : 0;
    }
    return cached;
}

#define EFFECT_BUF_ZERO(ptr, nbytes)                                           \
    do {                                                                       \
        if ((ptr) != NULL && effectBufZeroInitEnabled()) {                     \
            memset((ptr), 0, (size_t)(nbytes));                                \
        }                                                                      \
    } while (0)
#else
#define EFFECT_BUF_ZERO(ptr, nbytes) ((void)0)
#endif



void alloc_explosion_smoke_casing_scorch_impact_buffers(void)
{
    s32 i;
    s32 j;

    g_NumExplosionEntries = 0;
    g_NumSmokeEntries = 0;
    g_NumParticleEntries = 0;
    g_NumScorchEntries = 0;
    g_NumImpactEntries = 0;
    g_SpExplosionDamageMult = 1.0f;

    g_ExplosionBuffer = (struct Explosion *)mempAllocBytesInBank(EXPLOSION_BUFFER_LEN * sizeof(struct Explosion), MEMPOOL_STAGE);
    EFFECT_BUF_ZERO(g_ExplosionBuffer, EXPLOSION_BUFFER_LEN * sizeof(struct Explosion));

    for (i=0; i<EXPLOSION_BUFFER_LEN; i++)
    {
        g_ExplosionBuffer[i].prop = NULL;
        
        for (j=0; j<EXPLOSION_PARTS_LEN; j++)
        {
            g_ExplosionBuffer[i].parts[j].frame = 0;
        }
    }

    g_SmokeBuffer = (struct Smoke *)mempAllocBytesInBank(SMOKE_BUFFER_LEN * sizeof(struct Smoke), MEMPOOL_STAGE);
    EFFECT_BUF_ZERO(g_SmokeBuffer, SMOKE_BUFFER_LEN * sizeof(struct Smoke));

    for (i=0; i<SMOKE_BUFFER_LEN; i++)
    {
        g_SmokeBuffer[i].prop = NULL;
        
        for (j=0; j<SMOKE_PARTS_LEN; j++)
        {
            g_SmokeBuffer[i].parts[j].size = 0.0f;
        }
    }

#ifdef NATIVE_PORT
    /* MP scorch: always allocate so split-screen has a non-NULL buffer. The N64
     * SP-only gate was a fill-rate/memory concession; the write/render gates in
     * explosions.c are lifted in tandem so MP persists ground scorch. */
    if (1)
#else
    if (getPlayerCount() == 1)
#endif
    {
        // scorches are the circle burn marks left on the ground from explosions
        g_ScorchBuffer = (struct Scorch *)mempAllocBytesInBank(SCORCH_BUFFER_LEN * sizeof(struct Scorch), MEMPOOL_STAGE);
        EFFECT_BUF_ZERO(g_ScorchBuffer, SCORCH_BUFFER_LEN * sizeof(struct Scorch));
#ifdef NATIVE_PORT
        gfx_register_pc_vertex_region(g_ScorchBuffer, SCORCH_BUFFER_LEN * sizeof(struct Scorch));
#endif

        for (i=0; i<SCORCH_BUFFER_LEN; i++)
        {
            g_ScorchBuffer[i].roomid = -1;
        }
    }

    g_BulletImpactBuffer = (struct BulletImpact *)mempAllocBytesInBank(BULLET_IMPACT_BUFFER_LEN * sizeof(struct BulletImpact), MEMPOOL_STAGE);
    EFFECT_BUF_ZERO(g_BulletImpactBuffer, BULLET_IMPACT_BUFFER_LEN * sizeof(struct BulletImpact));
#ifdef NATIVE_PORT
    gfx_register_pc_vertex_region(g_BulletImpactBuffer, BULLET_IMPACT_BUFFER_LEN * sizeof(struct BulletImpact));
#endif

    for (i=0; i<BULLET_IMPACT_BUFFER_LEN; i++)
    {
        g_BulletImpactBuffer[i].room = -1;
    }

    max_particles = MAX_FLYING_PARTICLES / getPlayerCount();

    if ((lvlGetCurrentStageToLoad() == LEVELID_STREETS) || (lvlGetCurrentStageToLoad() == LEVELID_DEPOT))
    {
        max_particles = (s32) max_particles >> 1;
    }

    g_FlyingParticlesBuffer = (struct FlyingParticles *)mempAllocBytesInBank(((max_particles * sizeof(struct FlyingParticles)) + 0xF) & ~0xF, MEMPOOL_STAGE);
    EFFECT_BUF_ZERO(g_FlyingParticlesBuffer, ((max_particles * sizeof(struct FlyingParticles)) + 0xF) & ~0xF);
#ifdef NATIVE_PORT
    gfx_register_pc_vertex_region(g_FlyingParticlesBuffer, ((max_particles * sizeof(struct FlyingParticles)) + 0xF) & ~0xF);
#endif

    for (i=0; i<max_particles; i++)
    {
        g_FlyingParticlesBuffer[i].unk00 = 0;
    }
}
