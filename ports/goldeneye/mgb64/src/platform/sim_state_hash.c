#include "sim_state_hash.h"
#include <stdio.h>
#include <string.h>

/* ---- FNV-1a 64-bit ------------------------------------------------------- */
#define FNV64_OFFSET 1469598103934665603ULL
#define FNV64_PRIME  1099511628211ULL

static inline uint64_t fnv1a(uint64_t h, const void *p, size_t n) {
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= FNV64_PRIME; }
    return h;
}

/* Neutral token for a pointer word (its ASLR-varied value carries no logical
 * state). Chosen to be an implausible literal game-data word. */
#define SIM_NORM_TOKEN 0xFEEDFACECAFEBEEFULL

/*
 * Userspace-pointer value window (macOS arm64). Everything below PAGEZERO's top
 * (4 GB) cannot be a valid pointer; the ceiling (2^47) is the userspace address
 * limit and sits far below where float pairs land (~1e18), so genuine 32-bit
 * float pairs stored as 64-bit words keep full hash sensitivity. This is a pure
 * VALUE test, so it classifies a given word identically across ASLR'd runs —
 * unlike a range-membership test, whose ranges move with ASLR.
 */
#define SIM_PTR_LO 0x0000000100000000ULL
#define SIM_PTR_HI 0x0000800000000000ULL

/*
 * Canonicalize `w` for a typed source region:
 *  - inside a hashed region -> (index+1, offset) token: ASLR-invariant and keeps
 *    which in-region object it targets (full sensitivity for pool-internal links);
 *  - else in the pointer value window -> a single constant token: a pointer into
 *    some unregistered allocation (a dylib, the ROM buffer, the heap) whose
 *    absolute value is meaningless across runs;
 *  - else -> `w` literally (genuine non-pointer data: small ints, flags, floats).
 * Opaque source regions take the stable value-window branch first; see
 * SIM_HASH_REGION_OPAQUE_POINTERS and FID-0046.
 */
static uint64_t canon_word(uintptr_t w, const SimHashRegion *hr, int nh,
                           int source_region) {
    /* Untyped arenas cannot distinguish a real in-region pointer from an
     * identical stale/literal word that merely collides with this process's
     * ASLR-moved region range. Test the stable value window first and retain
     * pointer liveness (zero remains zero) without inventing target identity. */
    if ((hr[source_region].flags & SIM_HASH_REGION_OPAQUE_POINTERS) != 0 &&
        (uint64_t)w >= SIM_PTR_LO && (uint64_t)w < SIM_PTR_HI) {
        return SIM_NORM_TOKEN;
    }
    for (int k = 0; k < nh; k++) {
        if (!hr[k].base) continue;
        uintptr_t lo = (uintptr_t)hr[k].base, hi = lo + hr[k].size;
        if (w >= lo && w < hi) return ((uint64_t)(k + 1) << 48) ^ (uint64_t)(w - lo);
    }
    if ((uint64_t)w >= SIM_PTR_LO && (uint64_t)w < SIM_PTR_HI) return SIM_NORM_TOKEN;
    return (uint64_t)w;
}

uint64_t sim_state_hash_compute(const SimHashRegion *hr, int nh) {
    uint64_t h = FNV64_OFFSET;
    for (int k = 0; k < nh; k++) {
        /* Hash the region name so a changed/reordered region set is detectable. */
        h = fnv1a(h, hr[k].name ? hr[k].name : "", hr[k].name ? strlen(hr[k].name) : 0);
        if (!hr[k].base || hr[k].size == 0) continue;

        const unsigned char *b = (const unsigned char *)hr[k].base;
        size_t sz = hr[k].size, i = 0;
        /* Pointer-aligned words get canonicalized; a trailing sub-word tail is
         * hashed raw (it cannot hold a full pointer). */
        for (; i + sizeof(uintptr_t) <= sz; i += sizeof(uintptr_t)) {
            uintptr_t w;
            memcpy(&w, b + i, sizeof w);                 /* alignment-safe */
            uint64_t c = canon_word(w, hr, nh, k);
            h = fnv1a(h, &c, sizeof c);
        }
        if (i < sz) h = fnv1a(h, b + i, sz - i);
    }
    return h;
}

/* Write region k's CANONICALIZED word stream (exactly what the hash consumes:
 * pointers tokenized, non-pointer words literal) into out, which must hold
 * hr[k].size bytes. Returns bytes written. Companion to the byte-attribution
 * region dump (GE007_SIM_HASH_DUMP): raw dumps contain ASLR noise across runs,
 * canonical dumps diff cleanly to the exact semantically-diverging word. */
size_t sim_state_hash_canon_region(const SimHashRegion *hr, int nh, int k,
                                   unsigned char *out) {
    if (k < 0 || k >= nh || !hr[k].base || hr[k].size == 0 || out == NULL) return 0;
    const unsigned char *b = (const unsigned char *)hr[k].base;
    size_t sz = hr[k].size, i = 0;
    for (; i + sizeof(uintptr_t) <= sz; i += sizeof(uintptr_t)) {
        uintptr_t w;
        uint64_t c;
        memcpy(&w, b + i, sizeof w);
        c = canon_word(w, hr, nh, k);
        memcpy(out + i, &c, sizeof c);
    }
    if (i < sz) memcpy(out + i, b + i, sz - i);
    return sz;
}

uint64_t sim_state_hash_compute_region(const SimHashRegion *hr, int nh, int k) {
    uint64_t h = FNV64_OFFSET;
    if (k < 0 || k >= nh) return h;
    h = fnv1a(h, hr[k].name ? hr[k].name : "", hr[k].name ? strlen(hr[k].name) : 0);
    if (!hr[k].base || hr[k].size == 0) return h;
    const unsigned char *b = (const unsigned char *)hr[k].base;
    size_t sz = hr[k].size, i = 0;
    for (; i + sizeof(uintptr_t) <= sz; i += sizeof(uintptr_t)) {
        uintptr_t w;
        memcpy(&w, b + i, sizeof w);
        uint64_t c = canon_word(w, hr, nh, k); /* full set for canonicalization */
        h = fnv1a(h, &c, sizeof c);
    }
    if (i < sz) h = fnv1a(h, b + i, sz - i);
    return h;
}

int sim_state_hash_emit_json(const char *path, uint64_t hash,
                             const SimHashRegion *rg, int n,
                             int frame, const char *replay) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "{\n");
    fprintf(f, "  \"hash\": \"%016llx\",\n", (unsigned long long)hash);
    fprintf(f, "  \"frame\": %d,\n", frame);
    fprintf(f, "  \"replay\": \"%s\",\n", replay ? replay : "");
    fprintf(f, "  \"regions\": [");
    for (int k = 0; k < n; k++) {
        fprintf(f, "%s{\"name\": \"%s\", \"size\": %zu}",
                k ? ", " : "", rg[k].name ? rg[k].name : "", rg[k].size);
    }
    fprintf(f, "]\n}\n");
    fclose(f);
    return 0;
}

/* simHashRegistryBuild() lives in sim_state_hash_registry.c (game-coupled, so it
 * is kept out of this file to keep the ROM-free unit test dependency-free). */
