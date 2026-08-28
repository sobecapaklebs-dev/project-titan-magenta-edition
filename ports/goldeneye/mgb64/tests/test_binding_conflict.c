/* test_binding_conflict.c — AUDIT-0050: the Controls panel must reject a gamepad
 * capture whose physical input is already owned by another action, for BUTTON
 * and TRIGGER bindings alike. The trigger-capture branch previously skipped the
 * owner check (ui_bindings.cpp:225-230), so binding e.g. Aim onto the Right
 * Trigger (owned by Fire) silently created a duplicate that fired both N64 bits.
 * This exercises the shared SDL-free ownership primitive bindingOwnerOf() that
 * the fixed reject path (padOwner -> gamepadBindingOwnerOf -> bindingOwnerOf)
 * now routes through.
 *
 * ROM-free and SDL-free: the gamepad default encoding is mirrored with plain
 * ints (real SDL enum values, stable ABI) so no <SDL.h> is pulled in. NDEBUG
 * strips assert(), so failures are counted and returned nonzero from main(). */
#include <stdio.h>
#include "binding_conflict.h"

/* Real SDL_GameController* enum values (SDL 2.x, stable ABI) used only to build
 * the encoded array so the test mirrors the runtime encoding without <SDL.h>. */
enum {
    AXIS_TRIGGERLEFT  = 4,   /* SDL_CONTROLLER_AXIS_TRIGGERLEFT  */
    AXIS_TRIGGERRIGHT = 5,   /* SDL_CONTROLLER_AXIS_TRIGGERRIGHT */
    BTN_A = 0, BTN_X = 2, BTN_MAX = 21  /* SDL_CONTROLLER_BUTTON_A / _X / _MAX */
};

/* GamepadAction indices — input_actions.h order (kept local; the test does not
 * include input_actions.h to stay SDL-free). */
enum { GB_FIRE, GB_AIM, GB_ALT_FIRE, GB_LOOK, GB_JUMP, GB_RELOAD, GB_PAUSE,
       GB_WEAPON_NEXT, GB_WEAPON_PREV, GB_CROUCH,
       GB_LOOK_UP, GB_LOOK_DOWN, GB_LOOK_LEFT, GB_LOOK_RIGHT, GB_COUNT };

static int fails = 0;
static void check(int cond, const char *msg, int got, int want) {
    if (!cond) { printf("FAIL: %s (got %d, want %d)\n", msg, got, want); fails++; }
}

int main(void) {
    /* Structural invariant: trigger encodings and button indices never alias, so
     * one ownership check safely covers both input classes. */
    check(GB_AXIS_BASE > BTN_MAX, "trigger/button encoding spaces disjoint",
          GB_AXIS_BASE, BTN_MAX);

    /* Gamepad default encoding (mirrors input_bindings.c kGpDefault). */
    int enc[GB_COUNT];
    enc[GB_FIRE]        = GB_AXIS_BASE + AXIS_TRIGGERRIGHT;
    enc[GB_AIM]         = GB_AXIS_BASE + AXIS_TRIGGERLEFT;
    enc[GB_ALT_FIRE]    = 10; /* Right Bumper */
    enc[GB_LOOK]        = 9;  /* Left Bumper  */
    enc[GB_JUMP]        = BTN_A;
    enc[GB_RELOAD]      = 1;  /* B */
    enc[GB_PAUSE]       = 6;  /* Start */
    enc[GB_WEAPON_NEXT] = 3;  /* Y */
    enc[GB_WEAPON_PREV] = 8;  /* Right Stick click */
    enc[GB_CROUCH]      = 7;  /* Left Stick click  */
    enc[GB_LOOK_UP]     = 11; enc[GB_LOOK_DOWN]  = 12;
    enc[GB_LOOK_LEFT]   = 13; enc[GB_LOOK_RIGHT] = 14;

    const int rt = GB_AXIS_BASE + AXIS_TRIGGERRIGHT;
    const int lt = GB_AXIS_BASE + AXIS_TRIGGERLEFT;

    /* TRIGGER conflict — the AUDIT-0050 regression: capturing the Right Trigger
     * for Aim must report Fire as the owner, so the UI rejects instead of
     * double-binding. */
    check(bindingOwnerOf(enc, GB_COUNT, rt, GB_AIM) == GB_FIRE,
          "RT owned by Fire when capturing for Aim",
          bindingOwnerOf(enc, GB_COUNT, rt, GB_AIM), GB_FIRE);
    /* Left Trigger captured for Fire -> owned by Aim. */
    check(bindingOwnerOf(enc, GB_COUNT, lt, GB_FIRE) == GB_AIM,
          "LT owned by Aim when capturing for Fire",
          bindingOwnerOf(enc, GB_COUNT, lt, GB_FIRE), GB_AIM);
    /* Re-selecting a trigger already on the SAME action is not a conflict. */
    check(bindingOwnerOf(enc, GB_COUNT, rt, GB_FIRE) == -1,
          "RT self-rebind not a conflict",
          bindingOwnerOf(enc, GB_COUNT, rt, GB_FIRE), -1);

    /* BUTTON conflict shares the policy: A owned by Jump. */
    check(bindingOwnerOf(enc, GB_COUNT, BTN_A, GB_RELOAD) == GB_JUMP,
          "A owned by Jump when capturing for Reload",
          bindingOwnerOf(enc, GB_COUNT, BTN_A, GB_RELOAD), GB_JUMP);
    check(bindingOwnerOf(enc, GB_COUNT, BTN_A, GB_JUMP) == -1,
          "A self-rebind not a conflict",
          bindingOwnerOf(enc, GB_COUNT, BTN_A, GB_JUMP), -1);

    /* A free button (X == 2 has no gameplay default) has no owner. */
    check(bindingOwnerOf(enc, GB_COUNT, BTN_X, -1) == -1,
          "free button X has no owner",
          bindingOwnerOf(enc, GB_COUNT, BTN_X, -1), -1);

    /* GB_NONE (unbound) never conflicts. */
    check(bindingOwnerOf(enc, GB_COUNT, GB_NONE, -1) == -1,
          "GB_NONE never owned",
          bindingOwnerOf(enc, GB_COUNT, GB_NONE, -1), -1);

    /* Simulated hazard: the duplicate the pre-fix trigger branch would have
     * created (Aim also placed on RT). The primitive flags the collision from
     * BOTH directions — exactly what the conflict UI + reject depend on. */
    enc[GB_AIM] = rt;
    check(bindingOwnerOf(enc, GB_COUNT, rt, GB_AIM) == GB_FIRE,
          "duplicate RT still owned by Fire",
          bindingOwnerOf(enc, GB_COUNT, rt, GB_AIM), GB_FIRE);
    check(bindingOwnerOf(enc, GB_COUNT, rt, GB_FIRE) == GB_AIM,
          "duplicate RT owned by Aim from Fire's view",
          bindingOwnerOf(enc, GB_COUNT, rt, GB_FIRE), GB_AIM);

    if (fails) { printf("test_binding_conflict: %d FAILED\n", fails); return 1; }
    printf("test_binding_conflict: all checks passed\n");
    return 0;
}
