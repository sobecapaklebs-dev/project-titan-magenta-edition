#include <assert.h>
#include <stdio.h>
#include "ds34common.h"

int main(void)
{
    struct dualsensereport in = {0};
    struct ds2report out = {0};
    in.LeftStickX = 1; in.LeftStickY = 2;
    in.RightStickX = 3; in.RightStickY = 4;
    in.Dpad = DS4DpadDirectionNE;
    in.Cross = 1; in.L1 = 1; in.L2 = 1;
    in.Create = 1; in.Options = 1; in.L3 = 1; in.R3 = 1;
    in.PressureL2 = 173; in.PressureR2 = 44;
    translate_pad_dualsense(&in, &out);
    assert(out.LeftStickX == 1 && out.LeftStickY == 2);
    assert(out.RightStickX == 3 && out.RightStickY == 4);
    assert(out.nUp == 0 && out.nRight == 0);
    assert(out.nCross == 0 && out.nL1 == 0 && out.nL2 == 0);
    assert(out.nSelect == 0 && out.nStart == 0 && out.nL3 == 0 && out.nR3 == 0);
    assert(out.PressureL2 == 173 && out.PressureR2 == 44);
    puts("buildfix28-dualsense-pademu: OK");
    return 0;
}
