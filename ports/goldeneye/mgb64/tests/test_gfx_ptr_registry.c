/*
 * test_gfx_ptr_registry.c — ROM-free regression lane for AUDIT-0009.
 *
 * Guards the low-32 GBI pointer registry in src/platform/gfx_ptr.h. The
 * pre-fix table probed only 4 slots and, when they were full, silently
 * overwrote slot 0 (evicting a live mapping); its range invalidation zeroed
 * keys mid-probe-chain while resolve treated a zero key as end-of-chain, so a
 * deletion at the head/middle of a collision chain hid still-live mappings
 * stored later in it; and two live full pointers sharing one low-32 token were
 * silently overwritten.
 *
 * The fix converts the table to proper open addressing with an
 * EMPTY/OCCUPIED/TOMBSTONE state array: store never evicts, deletion writes a
 * tombstone, resolve skips tombstones, and an ambiguous same-low-32 collision
 * fails closed (incumbent kept, gfx_ptr_ambiguous counted).
 *
 * All registry logic is header-only static-inline, so this TU owns the table
 * and counter definitions the inline functions reference (the engine defines
 * them in src/platform/fast3d/gfx_pc.c). ROM-free; counts failures explicitly
 * and returns nonzero (the ctest build is Release/-DNDEBUG, so assert() is
 * stripped — never rely on it).
 */
#include "gfx_ptr.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* Definitions the static-inline registry functions link against. */
uintptr_t gfx_segment_table[16];
uint32_t  gfx_ptr_keys[GFX_PTR_TABLE_SIZE];
uintptr_t gfx_ptr_vals[GFX_PTR_TABLE_SIZE];
uint8_t   gfx_ptr_state[GFX_PTR_TABLE_SIZE];
uint32_t  gfx_ptr_ambiguous;
uint32_t  gfx_ptr_full_fails;
uint32_t  gfx_ptr_max_probe;
/* PERF-052: worker-thread store opt-out (gfx_ptr.h). Single-threaded test:
 * always 0, so every store path below behaves exactly as before. */
GFX_PTR_TLS int gfx_ptr_store_suppress;

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

static void reset_table(void) {
    memset(gfx_ptr_keys, 0, sizeof(gfx_ptr_keys));
    memset(gfx_ptr_vals, 0, sizeof(gfx_ptr_vals));
    memset(gfx_ptr_state, 0, sizeof(gfx_ptr_state)); /* GFX_PTR_EMPTY == 0 */
    gfx_ptr_ambiguous = 0;
    gfx_ptr_full_fails = 0;
    gfx_ptr_max_probe = 0;
}

/* Same base index expression the registry uses. */
static uint32_t base_index(uint32_t key) {
    return ((key >> 2) ^ (key >> 16)) & GFX_PTR_TABLE_MASK;
}

/* Collect `need` distinct nonzero keys that all hash to `target`. Returns the
 * count actually found (uint32 wraps to 0 to terminate). Each such key
 * collides into the same probe chain, exercising eviction/tombstone paths. */
static int collect_bucket(uint32_t target, uint32_t *out, int need) {
    int n = 0;
    uint32_t c;
    for (c = 1; c != 0 && n < need; c++) {
        if (base_index(c) == target) {
            out[n++] = c;
        }
    }
    return n;
}

static void *P(uint32_t key) { return (void *)(uintptr_t)key; }

int main(void) {
    uint32_t k[5];
    int found;
    int i;

    /* -------- Test 1: five same-bucket registrations, none evicted -------- */
    reset_table();
    found = collect_bucket(base_index(0x2ABCDEF0u), k, 5);
    CHECK(found == 5, "could not build five same-bucket keys");
    for (i = 0; i < found; i++) {
        CHECK(base_index(k[i]) == base_index(k[0]), "key not in target bucket");
    }
    for (i = 0; i < found; i++) {
        gfx_ptr_store(P(k[i]));
    }
    for (i = 0; i < found; i++) {
        CHECK(gfx_ptr_resolve(k[i]) == P(k[i]),
              "same-bucket mapping was evicted (pre-fix 4-slot overwrite)");
    }
    CHECK(gfx_ptr_full_fails == 0, "unexpected insert failure with a near-empty table");
    CHECK(gfx_ptr_ambiguous == 0, "distinct keys wrongly flagged ambiguous");

    /* -------- Test 2: delete at HEAD of chain keeps later mapping -------- */
    reset_table();
    gfx_ptr_store(P(k[0]));
    gfx_ptr_store(P(k[1]));
    CHECK(gfx_ptr_resolve(k[0]) == P(k[0]), "k0 not stored");
    CHECK(gfx_ptr_resolve(k[1]) == P(k[1]), "k1 not stored");
    gfx_ptr_invalidate_range((uintptr_t)k[0], (uintptr_t)k[0] + 1); /* drop k0 only */
    CHECK(gfx_ptr_resolve(k[0]) == NULL, "k0 survived its own invalidation");
    CHECK(gfx_ptr_resolve(k[1]) == P(k[1]),
          "head-of-chain deletion hid a later live mapping (the bug)");

    /* -------- Test 3: delete in MIDDLE of chain keeps tail mapping -------- */
    reset_table();
    gfx_ptr_store(P(k[0]));
    gfx_ptr_store(P(k[1]));
    gfx_ptr_store(P(k[2]));
    gfx_ptr_invalidate_range((uintptr_t)k[1], (uintptr_t)k[1] + 1); /* drop k1 only */
    CHECK(gfx_ptr_resolve(k[0]) == P(k[0]), "head mapping lost on middle delete");
    CHECK(gfx_ptr_resolve(k[1]) == NULL, "k1 survived its own invalidation");
    CHECK(gfx_ptr_resolve(k[2]) == P(k[2]),
          "middle-of-chain deletion hid the tail mapping (the bug)");

    /* -------- Test 4: duplicate registration is stable -------- */
    reset_table();
    gfx_ptr_store(P(k[0]));
    gfx_ptr_store(P(k[0]));
    gfx_ptr_store(P(k[0]));
    CHECK(gfx_ptr_resolve(k[0]) == P(k[0]), "duplicate store not stable");
    CHECK(gfx_ptr_ambiguous == 0, "identical re-store wrongly flagged ambiguous");

    /* -------- Test 5: re-insert into a tombstoned slot resolves -------- */
    reset_table();
    gfx_ptr_store(P(k[0]));
    gfx_ptr_store(P(k[1]));
    gfx_ptr_invalidate_range((uintptr_t)k[0], (uintptr_t)k[0] + 1); /* tombstone k0's slot */
    gfx_ptr_store(P(k[0]));                                          /* reuse the tombstone */
    CHECK(gfx_ptr_resolve(k[0]) == P(k[0]), "re-insert after delete did not resolve");
    CHECK(gfx_ptr_resolve(k[1]) == P(k[1]), "tail mapping lost across tombstone reuse");

    /* -------- Test 6: probe window wraps past the top of the table -------- */
    {
        uint32_t w[2];
        int wf;
        reset_table();
        wf = collect_bucket(GFX_PTR_TABLE_MASK, w, 2); /* base_idx == last slot */
        CHECK(wf == 2, "could not build wrap-bucket keys");
        if (wf == 2) {
            gfx_ptr_store(P(w[0]));   /* lands at slot MASK */
            gfx_ptr_store(P(w[1]));   /* probes (MASK+1)&MASK == 0 (wrap) */
            CHECK(gfx_ptr_resolve(w[0]) == P(w[0]), "wrap: first mapping unresolved");
            CHECK(gfx_ptr_resolve(w[1]) == P(w[1]), "wrap: wrapped mapping unresolved");
        }
    }

    /* -------- Test 7: key 0 resolves to NULL -------- */
    reset_table();
    CHECK(gfx_ptr_resolve(0) == NULL, "key 0 must resolve NULL");

    /* -------- Test 8: ambiguous same-low-32 live pointers fail closed --------
     * Requires a >32-bit uintptr_t to form two distinct full pointers that
     * share one low-32 token (the exact case the registry cannot disambiguate).
     * On a 32-bit host the truncation defect does not exist, so skip. */
#if UINTPTR_MAX > 0xFFFFFFFFull
    reset_table();
    {
        uint32_t key = k[0];
        uintptr_t fa = ((uintptr_t)0x11u << 32) | key;
        uintptr_t fb = ((uintptr_t)0x22u << 32) | key;
        gfx_ptr_store((const void *)fa);
        gfx_ptr_store((const void *)fb); /* same low-32, different full pointer */
        CHECK(gfx_ptr_resolve(key) == (void *)fa,
              "ambiguous same-low-32: incumbent must be kept, not overwritten");
        CHECK(gfx_ptr_ambiguous == 1, "ambiguous same-low-32 collision not counted");
    }
#endif

    if (g_failures == 0) {
        printf("test_gfx_ptr_registry: OK\n");
        return 0;
    }
    fprintf(stderr, "test_gfx_ptr_registry: %d failure(s)\n", g_failures);
    return 1;
}
