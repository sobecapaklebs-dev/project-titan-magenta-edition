/*
 * watch_inv_aspect.c — see watch_inv_aspect.h.
 *
 * 4.0f / 3.0f folds to exactly 0x3FAAAAAB (== retail's immediate a3), and the
 * legacy 1.2857143f folds to 0x3FA49249 (== the pre-fix port literal). The unit
 * test asserts both bit patterns so a revert of either reddens. See FID-0098.
 */
#include "watch_inv_aspect.h"

float watchInvPerspAspect(int legacy)
{
    if (legacy) {
        return 1.2857143f; /* port defect: 0x3FA49249 = 9/7 */
    }
    return 4.0f / 3.0f;    /* retail: 0x3FAAAAAB = 4/3 */
}
