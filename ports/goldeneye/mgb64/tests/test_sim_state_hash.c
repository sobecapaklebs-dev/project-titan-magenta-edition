/* ROM-free unit test for the sim-state invariance hash (remaster P0.2 rail). */
#include "sim_state_hash.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    /* 1. Determinism: identical bytes -> identical hash across calls. */
    unsigned char buf[256];
    for (int i = 0; i < 256; i++) buf[i] = (unsigned char)(i * 7);
    SimHashRegion r1[1] = {{"buf", buf, sizeof buf}};
    uint64_t h1 = sim_state_hash_compute(r1, 1);
    uint64_t h2 = sim_state_hash_compute(r1, 1);
    assert(h1 == h2);

    /* 2. Sensitivity: a single-byte change flips the hash. */
    buf[100] ^= 0xFF;
    assert(sim_state_hash_compute(r1, 1) != h1);
    buf[100] ^= 0xFF;
    assert(sim_state_hash_compute(r1, 1) == h1);

    /* 3. Pointer normalization: the SAME logical layout at a DIFFERENT base
     *    address hashes identically. Regions share a name so only the pointer
     *    canonicalization is under test. */
    unsigned char a[64] = {0}, b[64] = {0};
    *(void **)(a + 8) = (void *)(a + 40);   /* self-pointer offset 8 -> offset 40 */
    *(void **)(b + 8) = (void *)(b + 40);   /* identical layout, different base    */
    SimHashRegion ra[1] = {{"r", a, sizeof a}};
    SimHashRegion rb[1] = {{"r", b, sizeof b}};
    assert(sim_state_hash_compute(ra, 1) == sim_state_hash_compute(rb, 1));

    /* 4. Pointer sensitivity: a pointer to a DIFFERENT in-region offset diverges. */
    unsigned char c[64] = {0};
    *(void **)(c + 8) = (void *)(c + 16);   /* -> offset 16, not 40 */
    SimHashRegion rc[1] = {{"r", c, sizeof c}};
    assert(sim_state_hash_compute(rc, 1) != sim_state_hash_compute(ra, 1));

    /* 5. Region identity: renaming a region changes the hash (region-set guard). */
    SimHashRegion rn[1] = {{"renamed", a, sizeof a}};
    assert(sim_state_hash_compute(rn, 1) != sim_state_hash_compute(ra, 1));

    /* 6. Opaque-arena range collision (FID-0046 regression): identical raw
     *    words must stay identical even when the literal happens to fall in
     *    one process's ASLR-moved region and not the other's. The old generic
     *    region-membership heuristic misclassified the second copy as a live
     *    in-region pointer and produced different hashes for identical bytes. */
    unsigned char pool_a[64] = {0}, pool_b[64] = {0};
    uintptr_t collision = (uintptr_t)(pool_b + 40);
    memcpy(pool_a + 8, &collision, sizeof collision);
    memcpy(pool_b + 8, &collision, sizeof collision);
    SimHashRegion opaque_a[1] = {{"pool", pool_a, sizeof pool_a,
                                 SIM_HASH_REGION_OPAQUE_POINTERS}};
    SimHashRegion opaque_b[1] = {{"pool", pool_b, sizeof pool_b,
                                 SIM_HASH_REGION_OPAQUE_POINTERS}};
    assert(sim_state_hash_compute(opaque_a, 1) ==
           sim_state_hash_compute(opaque_b, 1));

    /* Opaque mode still detects pointer liveness: NULL and non-NULL differ. */
    memset(pool_a + 8, 0, sizeof collision);
    assert(sim_state_hash_compute(opaque_a, 1) !=
           sim_state_hash_compute(opaque_b, 1));

    printf("test_sim_state_hash: OK\n");
    return 0;
}
