#include "titan_unified_input.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static TitanNormalizedInputState neutral(void)
{
    TitanNormalizedInputState s;
    memset(&s, 0, sizeof(s));
    s.left_x = s.left_y = s.right_x = s.right_y = 128;
    return s;
}

static int count_event(TitanInputEventType type)
{
    TitanInputEvent e;
    int n = 0;
    while (TitanUnifiedInput_PollEvent(&e)) if (e.type == type) n++;
    return n;
}

int main(void)
{
    uint32_t ds, xbox;
    TitanNormalizedInputState s;
    const TitanInputDeviceDescriptor *d;
    TitanInputEvent e;
    int system_events = 0;

    TitanUnifiedInput_Initialize();
    TitanUnifiedInput_SetBootUiActive(1);

    /* Before first input, Welcome follows the newest eligible connection. */
    {
        uint32_t boot_ds2 = TitanUnifiedInput_ConnectEx(TITAN_INPUT_DEVICE_DUALSHOCK2,
            TITAN_INPUT_TRANSPORT_SIO2, TITAN_INPUT_BACKEND_PADMAN, 0, 0,
            TITAN_INPUT_CAP_BUTTONS | TITAN_INPUT_CAP_DPAD | TITAN_INPUT_CAP_WIRED, 0x101u);
        assert(boot_ds2 != 0u);
        assert(TitanUnifiedInput_GetWelcomeCandidate()->device_id == boot_ds2);
    }

    ds = TitanUnifiedInput_ConnectEx(TITAN_INPUT_DEVICE_DUALSENSE,
        TITAN_INPUT_TRANSPORT_BLUETOOTH, TITAN_INPUT_BACKEND_PADEMU_BT,
        0x054c, 0x0ce6,
        TITAN_INPUT_CAP_BUTTONS | TITAN_INPUT_CAP_DPAD |
        TITAN_INPUT_CAP_ANALOG_STICKS | TITAN_INPUT_CAP_ANALOG_TRIGGERS |
        TITAN_INPUT_CAP_BATTERY | TITAN_INPUT_CAP_WIRELESS |
        TITAN_INPUT_CAP_SYSTEM_BUTTON, 0xd501u);
    assert(ds == 0xd501u);
    assert(TitanUnifiedInput_GetPlayer(0) == 0); /* Welcome owns P1. */
    assert(TitanUnifiedInput_GetWelcomeCandidate()->device_id == ds);

    d = TitanUnifiedInput_GetDeviceById(ds);
    assert(d && d->battery_state == TITAN_INPUT_BATTERY_UNKNOWN && !d->battery_valid);

    s = neutral();
    s.buttons = TITAN_NBTN_SYSTEM;
    assert(TitanUnifiedInput_SubmitStateEx(ds, &s, TITAN_INPUT_BATTERY_CHARGING, 42, 0x14, 700));
    d = TitanUnifiedInput_GetDeviceById(ds);
    assert(d && d->battery_state == TITAN_INPUT_BATTERY_CHARGING);
    assert(d->battery_valid && d->battery_percent == 42);
    assert(TitanUnifiedInput_ClaimPlayerOnFirstInput(ds, 0));
    assert(TitanUnifiedInput_GetPlayer(0)->device_id == ds);

    while (TitanUnifiedInput_PollEvent(&e)) if (e.type == TITAN_INPUT_EVENT_SYSTEM_BUTTON) system_events++;
    assert(system_events == 1);
    TitanUnifiedInput_BeginFrame();
    assert(TitanUnifiedInput_SubmitStateEx(ds, &s, TITAN_INPUT_BATTERY_CHARGING, 42, 0x14, 650));
    while (TitanUnifiedInput_PollEvent(&e)) if (e.type == TITAN_INPUT_EVENT_SYSTEM_BUTTON) system_events++;
    assert(system_events == 1); /* held button: edge only */

    TitanUnifiedInput_Disconnect(ds);
    assert(TitanUnifiedInput_GetPlayer(0) == 0);
    ds = TitanUnifiedInput_ConnectEx(TITAN_INPUT_DEVICE_DUALSENSE,
        TITAN_INPUT_TRANSPORT_BLUETOOTH, TITAN_INPUT_BACKEND_PADEMU_BT,
        0x054c, 0x0ce6,
        TITAN_INPUT_CAP_BUTTONS | TITAN_INPUT_CAP_DPAD |
        TITAN_INPUT_CAP_ANALOG_STICKS | TITAN_INPUT_CAP_BATTERY |
        TITAN_INPUT_CAP_WIRELESS | TITAN_INPUT_CAP_SYSTEM_BUTTON, 0xd501u);
    assert(ds == 0xd501u);
    assert(TitanUnifiedInput_GetPlayer(0) && TitanUnifiedInput_GetPlayer(0)->device_id == ds);
    assert(TitanUnifiedInput_GetPlayer(0)->reconnect_count == 1);

    /* A backend without reliable battery data must not create a percentage. */
    TitanUnifiedInput_SetBootUiActive(0);
    xbox = TitanUnifiedInput_ConnectEx(TITAN_INPUT_DEVICE_XBOX_SERIES,
        TITAN_INPUT_TRANSPORT_USB, TITAN_INPUT_BACKEND_PADEMU_USB,
        0x045e, 0x0b12,
        TITAN_INPUT_CAP_BUTTONS | TITAN_INPUT_CAP_DPAD |
        TITAN_INPUT_CAP_ANALOG_STICKS | TITAN_INPUT_CAP_ANALOG_TRIGGERS |
        TITAN_INPUT_CAP_WIRED | TITAN_INPUT_CAP_SYSTEM_BUTTON, 0xb120u);
    assert(xbox == 0xb120u);
    d = TitanUnifiedInput_GetDeviceById(xbox);
    assert(d && d->battery_state == TITAN_INPUT_BATTERY_UNSUPPORTED && !d->battery_valid);
    assert(TitanUnifiedInput_UpdateBattery(xbox, TITAN_INPUT_BATTERY_LEVEL, 88, 0x88));
    d = TitanUnifiedInput_GetDeviceById(xbox);
    assert(d && d->battery_state == TITAN_INPUT_BATTERY_UNSUPPORTED && !d->battery_valid);

    assert(TitanUnifiedInput_GetMetrics()->system_button_events == 1);
    assert(TitanUnifiedInput_GetMetrics()->reconnect_events == 1);
    (void)count_event(TITAN_INPUT_EVENT_NONE);
    puts("buildfix29-input-platform: OK");
    return 0;
}
