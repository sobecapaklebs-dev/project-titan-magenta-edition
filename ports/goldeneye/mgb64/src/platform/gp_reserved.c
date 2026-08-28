// gp_reserved.c — see gp_reserved.h. Pure, SDL-free (AUDIT-0025).
#include "gp_reserved.h"

int gpButtonReserved(int button, int menu_btn, int guide_btn) {
    // A plain button index that also fires a system role: the configured
    // overlay-toggle button or the OS/Steam Guide button.
    return button == menu_btn || button == guide_btn;
}

int gpMenuConflict(int encoded, int menu_btn, int button_max) {
    // Only a menu button that is a real controller button can collide with a
    // button-encoded gameplay binding; None/axis menu values reserve nothing.
    if (menu_btn < 0 || menu_btn >= button_max) return 0;
    return encoded == menu_btn;
}
