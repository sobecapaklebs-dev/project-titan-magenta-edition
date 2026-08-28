#ifndef SIM_STATE_HASH_H
#define SIM_STATE_HASH_H
/*
 * Sim-state invariance hash (remaster P0.2 rail).
 *
 * Computes a deterministic 64-bit hash over the native port's mutable simulation
 * state so a RAMROM replay can be proven bit-identical with remaster rendering
 * flags on vs off. State lives in native host memory (the 8 MB s_pcPool arena +
 * curated game globals), so raw pointer values differ run-to-run under ASLR.
 * Typed regions canonicalize in-region pointers to base-independent targets.
 * Untyped arenas preserve NULL/non-NULL liveness but neutralize target addresses,
 * because a pointer-shaped data word cannot be classified safely from bytes alone.
 */
#include <stddef.h>
#include <stdint.h>

/* Upper bound on registered state regions (pool + curated globals + the stan
 * collision-navmesh region set appended by stanBuildHashRegions). */
#define SIM_HASH_MAX_REGIONS 64

typedef struct {
    const char *name;   /* stable identity; hashed so region-set changes are caught */
    const void *base;   /* runtime base address of the region                       */
    size_t      size;   /* region length in bytes                                   */
    uint32_t    flags;  /* SIM_HASH_REGION_* pointer-canonicalization policy         */
} SimHashRegion;

/* An opaque byte arena has no field types, so membership in one process's
 * ASLR-moved address range is not sufficient proof that a pointer-shaped word
 * is a live pointer. Preserve NULL/non-NULL sensitivity but neutralize the
 * target address. Typed regions retain relative-target sensitivity. */
#define SIM_HASH_REGION_OPAQUE_POINTERS (1u << 0)

/*
 * FNV-1a over the regions with policy-aware pointer normalization. Typed regions
 * retain base-independent (index,offset) targets. Opaque regions neutralize every
 * non-NULL userspace pointer value. Anything else is hashed literally.
 */
uint64_t sim_state_hash_compute(const SimHashRegion *regions, int n);

/*
 * Hash the contribution of a single region `k` in isolation, using the full
 * region set `regions` for cross-region pointer canonicalization (so links stay
 * ASLR-invariant). Diagnostic only: lets an A/B attribute a full-hash divergence
 * to the specific region whose bytes changed (e.g. render->sim room_rendered vs
 * downstream guard/pool state). Returns FNV64_OFFSET-seeded hash of region k.
 */
uint64_t sim_state_hash_compute_region(const SimHashRegion *regions, int n, int k);

/* Canonicalized-byte writer for region k (pointer words tokenized exactly as the
 * hash sees them). out must hold regions[k].size bytes; returns bytes written. */
size_t sim_state_hash_canon_region(const SimHashRegion *regions, int n, int k,
                                   unsigned char *out);

/* Write the gate result as JSON (hash, frame, replay, region names+sizes). */
int sim_state_hash_emit_json(const char *path, uint64_t hash,
                             const SimHashRegion *regions, int n,
                             int frame, const char *replay);

/* Fill `out` with the state region set: [0] = pool, [1..] = curated globals.
 * `*n` returns the count. Defined in sim_state_hash_registry.c (game build only). */
void simHashRegistryBuild(SimHashRegion *out, int *n);

/* Append the stan collision-navmesh region set starting at index *n (advances
 * *n). Defined in src/game/stan.c where the collision globals' types are visible
 * (M8.1/FID-0030). Game build only. */
void stanBuildHashRegions(SimHashRegion *out, int *n);

/* Append the g_BgRoomInfo region (room_rendered render->sim read-back, FID-0012)
 * at index *n (advances *n). Defined in src/game/bg.c. Game build only. */
void bgBuildHashRegions(SimHashRegion *out, int *n);

/* If g_simStateHashOut is set, build the registry, hash it, and emit the JSON.
 * Called at deterministic screenshot-exit, before teardown. Game build only. */
void simStateHashEmitIfRequested(int frame, const char *replay);

/* Per-frame region-hash trace for frame-locking A/B divergences (FID-0058).
 * No-op unless GE007_SIM_HASH_EVERY_FRAME is set. Game build only. */
void simStateHashPerFrameTrace(int global_timer);

#endif /* SIM_STATE_HASH_H */
