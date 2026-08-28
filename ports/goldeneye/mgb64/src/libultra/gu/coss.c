/*
 * Clean-room libultra-style fixed-angle cosine helper for the matching target.
 */

#include "guint.h"

signed short coss(unsigned short angle)
{
    return sins((unsigned short)(angle + 0x4000));
}
