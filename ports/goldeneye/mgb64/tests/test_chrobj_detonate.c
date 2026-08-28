/*
 * test_chrobj_detonate.c — ROM-free regression lane for FID-0074.
 *
 * Guards the five maybe_detonate_object divergences (retail ASM
 * src/game/chrobjhandler.c:38029-38459, VRAM 0x7F04E108) by exercising the
 * pure helpers the real function now calls, on BOTH the faithful (legacy=0)
 * and the negative-control (legacy=1 = GE007_NO_DETONATE_OBJECT_FIX) sides:
 *
 *   D2  chrobjArmourCollectAmount   — armour polarity (the P1 headline).
 *   D3  chrobjDetonateGate          — unarmed non-collectable gate polarity.
 *   D4  chrobjDetonateGate          — armed-path INVINCIBLE early-out.
 *   D5  chrobjDetonateGate          — type-7/8 non-explosive return.
 *   D1  chrobjAmmoSalvageSlotUsable / chrobjAmmoSalvageAmmoType.
 *
 * Fails on revert: flipping any polarity back moves a faithful assertion, and
 * the legacy assertions pin the pre-fix behavior so the opt-out stays
 * byte-identical.
 */
#include "chrobj_detonate.h"

#include <stdio.h>

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

static int close_to(float a, float b)
{
    float d = a - b;
    if (d < 0.0f) d = -d;
    return d <= 1e-3f;
}

/* Shorthand gate call (armed/unarmed decision helper). */
static ChrobjDetonateAction gate(int unarmed, int collectable, unsigned int flags,
                                 int type, int wexpl, int aexpl, int legacy)
{
    return chrobjDetonateGate(unarmed, collectable, flags, type, wexpl, aexpl, legacy);
}

int main(void)
{
    /* ---------------- D2 armour amount (the P1 headline) ---------------- */
    /* A vest: initialamount=200, threshold(damage)=1000. One non-lethal shot
     * accumulates maxdamage=125 (0.5*250) -> NOT destroyed. */
    {
        float faithful_survived = chrobjArmourCollectAmount(0, 200.0f, 1000.0f, 125.0f, 0);
        float legacy_survived   = chrobjArmourCollectAmount(0, 200.0f, 1000.0f, 125.0f, 1);

        /* Faithful: a partially-shot vest yields the proportional remainder. */
        CHECK(close_to(faithful_survived, 175.0f),
              "D2 faithful survived vest -> 200*(1000-125)/1000 = 175");
        /* Legacy defect: the same shot ZEROES the pickup's armour value. */
        CHECK(close_to(legacy_survived, 0.0f),
              "D2 legacy survived vest -> 0.0 (the bullet-zeroes-a-vest bug)");
        CHECK(!close_to(faithful_survived, legacy_survived),
              "D2 fail-on-revert: faithful and legacy survived amounts must differ");
    }
    /* A destroyed vest: maxdamage=1250 > threshold=1000. */
    {
        float faithful_dead = chrobjArmourCollectAmount(1, 200.0f, 1000.0f, 1250.0f, 0);
        float legacy_dead   = chrobjArmourCollectAmount(1, 200.0f, 1000.0f, 1250.0f, 1);

        /* Faithful: a destroyed vest yields nothing. */
        CHECK(close_to(faithful_dead, 0.0f), "D2 faithful destroyed vest -> 0.0");
        /* Legacy defect: destroyed vest runs the formula -> negative armour. */
        CHECK(close_to(legacy_dead, -50.0f),
              "D2 legacy destroyed vest -> 200*(1000-1250)/1000 = -50 (defect)");
    }

    /* ---------------- D3 unarmed non-collectable gate ---------------- */
    /* PROPFLAG_01000000 SET: retail SKIPs, the port defect PROCEEDs. */
    CHECK(gate(1, 0, CHROBJ_DETONATE_FLAG_UNARMED_DISABLE, 3, 0, 0, 0) == CHROBJ_DETONATE_SKIP,
          "D3 faithful: unarmed non-collectable with 0x01000000 SET -> SKIP");
    CHECK(gate(1, 0, CHROBJ_DETONATE_FLAG_UNARMED_DISABLE, 3, 0, 0, 1) == CHROBJ_DETONATE_APPLY,
          "D3 legacy: unarmed non-collectable with 0x01000000 SET -> APPLY (defect)");
    /* PROPFLAG_01000000 CLEAR: retail PROCEEDs, the port defect SKIPs. */
    CHECK(gate(1, 0, 0u, 3, 0, 0, 0) == CHROBJ_DETONATE_APPLY,
          "D3 faithful: unarmed non-collectable with 0x01000000 CLEAR -> APPLY");
    CHECK(gate(1, 0, 0u, 3, 0, 0, 1) == CHROBJ_DETONATE_SKIP,
          "D3 legacy: unarmed non-collectable with 0x01000000 CLEAR -> SKIP (defect)");

    /* Collectable-unarmed gate is correct on both sides (0x00800000). */
    CHECK(gate(1, 1, CHROBJ_DETONATE_FLAG_COLLECT_UNARMED, 8, 0, 0, 0) == CHROBJ_DETONATE_APPLY,
          "unarmed collectable with 0x00800000 SET -> APPLY (faithful)");
    CHECK(gate(1, 1, 0u, 8, 0, 0, 0) == CHROBJ_DETONATE_SKIP,
          "unarmed collectable with 0x00800000 CLEAR -> SKIP (faithful)");
    CHECK(gate(1, 1, CHROBJ_DETONATE_FLAG_COLLECT_UNARMED, 8, 0, 0, 1) == CHROBJ_DETONATE_APPLY,
          "unarmed collectable gate identical under legacy");

    /* ---------------- D4 armed-path INVINCIBLE early-out ---------------- */
    /* An invincible explosive weapon: retail returns before arming; the port
     * defect still arms it (timer=0). */
    CHECK(gate(0, 0, CHROBJ_DETONATE_FLAG_INVINCIBLE, CHROBJ_DETONATE_TYPE_COLLECTABLE, 1, 0, 0)
              == CHROBJ_DETONATE_SKIP,
          "D4 faithful: armed INVINCIBLE explosive weapon -> SKIP before dispatch");
    CHECK(gate(0, 0, CHROBJ_DETONATE_FLAG_INVINCIBLE, CHROBJ_DETONATE_TYPE_COLLECTABLE, 1, 0, 1)
              == CHROBJ_DETONATE_ARM_WEAPON,
          "D4 legacy: armed INVINCIBLE explosive weapon -> ARM_WEAPON (defect)");
    /* An invincible magazine (explosive): same story. */
    CHECK(gate(0, 0, CHROBJ_DETONATE_FLAG_INVINCIBLE, CHROBJ_DETONATE_TYPE_MAGAZINE, 0, 1, 0)
              == CHROBJ_DETONATE_SKIP,
          "D4 faithful: armed INVINCIBLE explosive magazine -> SKIP");
    CHECK(gate(0, 0, CHROBJ_DETONATE_FLAG_INVINCIBLE, CHROBJ_DETONATE_TYPE_MAGAZINE, 0, 1, 1)
              == CHROBJ_DETONATE_ARM_MAGAZINE,
          "D4 legacy: armed INVINCIBLE explosive magazine -> ARM_MAGAZINE (defect)");

    /* ---------------- D5 type-7/8 non-explosive return ---------------- */
    /* Non-explosive dropped weapon (type 8, not invincible). */
    CHECK(gate(0, 0, 0u, CHROBJ_DETONATE_TYPE_COLLECTABLE, 0, 0, 0) == CHROBJ_DETONATE_TYPE78_INERT,
          "D5 faithful: armed non-explosive weapon -> TYPE78_INERT (returns)");
    CHECK(gate(0, 0, 0u, CHROBJ_DETONATE_TYPE_COLLECTABLE, 0, 0, 1) == CHROBJ_DETONATE_CHECK_MORTAL,
          "D5 legacy: armed non-explosive weapon -> CHECK_MORTAL (falls through, defect)");
    /* Non-explosive magazine (type 7, not invincible). */
    CHECK(gate(0, 0, 0u, CHROBJ_DETONATE_TYPE_MAGAZINE, 0, 0, 0) == CHROBJ_DETONATE_TYPE78_INERT,
          "D5 faithful: armed non-explosive magazine -> TYPE78_INERT (returns)");
    CHECK(gate(0, 0, 0u, CHROBJ_DETONATE_TYPE_MAGAZINE, 0, 0, 1) == CHROBJ_DETONATE_CHECK_MORTAL,
          "D5 legacy: armed non-explosive magazine -> CHECK_MORTAL (falls through, defect)");

    /* Explosive type-7/8 arm identically on both sides. */
    CHECK(gate(0, 0, 0u, CHROBJ_DETONATE_TYPE_COLLECTABLE, 1, 0, 0) == CHROBJ_DETONATE_ARM_WEAPON,
          "armed explosive weapon -> ARM_WEAPON (faithful)");
    CHECK(gate(0, 0, 0u, CHROBJ_DETONATE_TYPE_COLLECTABLE, 1, 0, 1) == CHROBJ_DETONATE_ARM_WEAPON,
          "armed explosive weapon -> ARM_WEAPON (legacy identical)");
    CHECK(gate(0, 0, 0u, CHROBJ_DETONATE_TYPE_MAGAZINE, 0, 1, 0) == CHROBJ_DETONATE_ARM_MAGAZINE,
          "armed explosive magazine -> ARM_MAGAZINE (faithful)");

    /* Armed non-7/8 type goes to the objIsMortal gate on both sides. */
    CHECK(gate(0, 0, 0u, 3 /*PROPDEF_PROP*/, 0, 0, 0) == CHROBJ_DETONATE_CHECK_MORTAL,
          "armed generic prop -> CHECK_MORTAL (faithful)");
    CHECK(gate(0, 0, 0u, 3, 0, 0, 1) == CHROBJ_DETONATE_CHECK_MORTAL,
          "armed generic prop -> CHECK_MORTAL (legacy identical)");

    /* ---------------- D1 ammo-salvage slot arithmetic ---------------- */
    CHECK(chrobjAmmoSalvageSlotUsable(3u, 5u) == 1, "D1 usable slot (qty>0, real modelnum)");
    CHECK(chrobjAmmoSalvageSlotUsable(0u, 5u) == 0, "D1 slot unusable when quantity==0");
    CHECK(chrobjAmmoSalvageSlotUsable(3u, 0xFFFFu) == 0, "D1 slot unusable when modelnum==0xFFFF");

    CHECK(chrobjAmmoSalvageAmmoType(0) == 1, "D1 slot 0 -> ammotype 1");
    CHECK(chrobjAmmoSalvageAmmoType(1) == 1, "D1 slot 1 folds into 9mm (ammotype 1)");
    CHECK(chrobjAmmoSalvageAmmoType(2) == 3, "D1 slot 2 -> ammotype 3");
    CHECK(chrobjAmmoSalvageAmmoType(4) == 5, "D1 slot 4 -> ammotype 5");

    if (g_failures == 0) {
        printf("test_chrobj_detonate: OK\n");
        return 0;
    }
    fprintf(stderr, "test_chrobj_detonate: %d failure(s)\n", g_failures);
    return 1;
}
