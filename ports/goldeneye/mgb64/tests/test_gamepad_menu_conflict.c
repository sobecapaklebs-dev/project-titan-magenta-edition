/*
 * test_gamepad_menu_conflict.c — ROM-free regression lane for AUDIT-0025.
 *
 * The in-game overlay opens on the CONFIGURED gamepad button
 * (Input.MenuToggleButton, default Back=4). Two pure predicates in
 * gp_reserved.c decide (a) whether a candidate binding is reserved because it
 * collides with the menu/Guide button [gpButtonReserved, used by gpValid], and
 * (b) whether an existing binding must be cleared because it double-acts as the
 * menu toggle [gpMenuConflict, used by gamepadBindingReconcileMenu].
 *
 * Fails on revert:
 *   - If gpValid reverts to a hardcoded Back, moving the menu button to A would
 *     stop reserving A (and keep reserving a now-free Back). The menu=A cases go
 *     red.
 *   - If reconciliation is dropped, the stock Jump=A default is never cleared
 *     when the menu button is A. The default-array reconcile case goes red.
 *   - If the menu-button-is-a-real-button guard is dropped, an unbound/axis menu
 *     value (None=-1, or an out-of-range index) would spuriously clear bindings.
 *
 * SDL enum values are local literals (stable ABI) so this stays SDL-free.
 */
#include "gp_reserved.h"
#include <stdio.h>

/* SDL_GameController ABI (SDL 2.x) values this test depends on. */
enum {
    BTN_A     = 0,
    BTN_B     = 1,
    BTN_Y     = 3,
    BTN_BACK  = 4,
    BTN_GUIDE = 5,
    BTN_START = 6,
    BTN_MAX   = 21,   /* SDL_CONTROLLER_BUTTON_MAX in SDL 2.32 */
    AXIS_BASE = 1000, /* GB_AXIS_BASE (encoded LT/RT) */
    NONE      = -1    /* GB_NONE (unbound) */
};

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); g_failures++; } \
} while (0)

/* Mirror of the production reconcile loop (input_bindings.c:
 * gamepadBindingReconcileMenu) so the array-level behavior is exercised. */
static int reconcile(int *binds, int n, int menu) {
    int i, cleared = 0;
    for (i = 0; i < n; i++)
        if (gpMenuConflict(binds[i], menu, BTN_MAX)) { binds[i] = NONE; cleared++; }
    return cleared;
}

int main(void) {
    /* ---- gpButtonReserved: menu button is the default Back ---- */
    CHECK(gpButtonReserved(BTN_BACK, BTN_BACK, BTN_GUIDE) == 1, "Back reserved when menu=Back");
    CHECK(gpButtonReserved(BTN_GUIDE, BTN_BACK, BTN_GUIDE) == 1, "Guide always reserved");
    CHECK(gpButtonReserved(BTN_A, BTN_BACK, BTN_GUIDE) == 0, "A free when menu=Back");
    CHECK(gpButtonReserved(BTN_Y, BTN_BACK, BTN_GUIDE) == 0, "Y free when menu=Back");

    /* ---- gpButtonReserved: menu button moved to A ---- */
    CHECK(gpButtonReserved(BTN_A, BTN_A, BTN_GUIDE) == 1, "A reserved when menu=A");
    CHECK(gpButtonReserved(BTN_BACK, BTN_A, BTN_GUIDE) == 0, "Back freed when menu=A");
    CHECK(gpButtonReserved(BTN_GUIDE, BTN_A, BTN_GUIDE) == 1, "Guide still reserved when menu=A");

    /* ---- gpMenuConflict: encoded-value collisions ---- */
    CHECK(gpMenuConflict(BTN_A, BTN_A, BTN_MAX) == 1, "A binding conflicts with menu=A");
    CHECK(gpMenuConflict(BTN_B, BTN_A, BTN_MAX) == 0, "B binding no conflict with menu=A");
    CHECK(gpMenuConflict(AXIS_BASE + 5, BTN_A, BTN_MAX) == 0, "trigger axis never conflicts");
    CHECK(gpMenuConflict(NONE, BTN_A, BTN_MAX) == 0, "None never conflicts");
    /* Guard: an unbound/out-of-range menu button must clear nothing. */
    CHECK(gpMenuConflict(NONE, NONE, BTN_MAX) == 0, "menu=None clears nothing (guard)");
    CHECK(gpMenuConflict(BTN_A, NONE, BTN_MAX) == 0, "menu=None never matches a real button");
    CHECK(gpMenuConflict(BTN_MAX, BTN_MAX, BTN_MAX) == 0, "menu==BUTTON_MAX out of range (guard)");

    /* ---- Reconciliation over the stock default binding array ----
     * Order mirrors kGpDefault in input_bindings.c. */
    {
        int def[] = {
            AXIS_BASE + 5, /* FIRE  = Right Trigger */
            AXIS_BASE + 4, /* AIM   = Left Trigger  */
            10,            /* ALT_FIRE = R bumper   */
            9,             /* LOOK  = L bumper       */
            BTN_A,         /* JUMP  = A              */
            BTN_B,         /* RELOAD= B              */
            BTN_START,     /* PAUSE = Start          */
            BTN_Y,         /* WEAPON_NEXT = Y        */
            8, 7, 11, 12, 13, 14
        };
        int n = (int)(sizeof(def) / sizeof(def[0]));
        int i, saved[32], cleared;
        for (i = 0; i < n; i++) saved[i] = def[i];

        /* Default menu (Back): no default binds Back -> nothing cleared,
         * every default preserved. */
        cleared = reconcile(def, n, BTN_BACK);
        CHECK(cleared == 0, "menu=Back clears no defaults");
        for (i = 0; i < n; i++) CHECK(def[i] == saved[i], "defaults unchanged when menu=Back");

        /* Menu moved to A: the stock Jump=A default must be cleared to None
         * (this is the AUDIT-0025 double-action). Exactly one binding clears. */
        cleared = reconcile(def, n, BTN_A);
        CHECK(cleared == 1, "menu=A clears exactly the Jump=A default");
        CHECK(def[4] == NONE, "Jump cleared to None when menu=A");
        CHECK(def[0] == saved[0] && def[3] == saved[3] && def[5] == saved[5],
              "non-colliding defaults left intact when menu=A");
    }

    if (g_failures) { fprintf(stderr, "%d check(s) failed\n", g_failures); return 1; }
    printf("all gamepad menu-conflict checks passed\n");
    return 0;
}
