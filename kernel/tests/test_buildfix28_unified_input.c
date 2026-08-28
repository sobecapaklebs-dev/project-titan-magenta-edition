#include "titan_unified_input.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    TitanNormalizedInputState s;
    const TitanInputDeviceDescriptor *d;
    uint32_t id;
    TitanUnifiedInput_Initialize();
    TitanUnifiedInput_BeginFrame();
    TitanUnifiedInput_SubmitLegacyPad(0, 1, TITAN_NBTN_CROSS, 127,128,129,130);
    d = TitanUnifiedInput_GetPlayer(0);
    assert(d && d->type == TITAN_INPUT_DEVICE_DUALSHOCK2);
    assert(d->state.buttons & TITAN_NBTN_CROSS);
    assert(d->state.pressed & TITAN_NBTN_CROSS);
    TitanUnifiedInput_BeginFrame();
    TitanUnifiedInput_SubmitLegacyPad(0, 1, TITAN_NBTN_CROSS, 127,128,129,130);
    assert(TitanUnifiedInput_GetPlayer(0)->state.pressed == 0);
    TitanUnifiedInput_BeginFrame();
    TitanUnifiedInput_SubmitLegacyPad(0, 0, 0,128,128,128,128);
    assert(TitanUnifiedInput_GetPlayer(0) == 0);

    id = TitanUnifiedInput_Connect(TITAN_INPUT_DEVICE_DUALSENSE, TITAN_INPUT_TRANSPORT_USB,
                                   0x054c, 0x0ce6,
                                   TITAN_INPUT_CAP_BUTTONS|TITAN_INPUT_CAP_DPAD|
                                   TITAN_INPUT_CAP_ANALOG_STICKS|TITAN_INPUT_CAP_ANALOG_TRIGGERS|
                                   TITAN_INPUT_CAP_WIRED, 0x500u);
    assert(id == 0x500u);
    memset(&s,0,sizeof(s)); s.left_x=s.left_y=s.right_x=s.right_y=128;
    s.buttons=TITAN_NBTN_START|TITAN_NBTN_SYSTEM; s.trigger_l=77; s.trigger_r=88;
    assert(TitanUnifiedInput_SubmitState(id,&s,62,1,900));
    d=TitanUnifiedInput_GetPlayer(0); assert(d && d->device_id==id);
    assert(d->battery_valid && d->battery_percent==62);
    TitanUnifiedInput_Disconnect(id); assert(TitanUnifiedInput_GetPlayer(0)==0);
    assert(TitanUnifiedInput_GetMetrics()->connect_events >= 2);
    assert(TitanUnifiedInput_GetMetrics()->disconnect_events >= 2);
    puts("buildfix28-unified-input: OK");
    return 0;
}
