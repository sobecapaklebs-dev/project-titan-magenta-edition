#include "titan_unified_input.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static TitanNormalizedInputState neutral(void){TitanNormalizedInputState s;memset(&s,0,sizeof(s));s.left_x=s.left_y=s.right_x=s.right_y=128;return s;}
int main(void){uint32_t ds3,xbox;TitanNormalizedInputState s=neutral();const TitanInputDeviceDescriptor*d;TitanUnifiedInput_Initialize();
 ds3=TitanUnifiedInput_ConnectEx(TITAN_INPUT_DEVICE_DUALSHOCK3,TITAN_INPUT_TRANSPORT_USB,TITAN_INPUT_BACKEND_PADEMU_USB,0x054c,0x0268,TITAN_INPUT_CAP_BUTTONS|TITAN_INPUT_CAP_BATTERY|TITAN_INPUT_CAP_WIRED,0x3268);
 assert(ds3);assert(TitanUnifiedInput_SubmitStateWithBattery(ds3,&s,TITAN_INPUT_BATTERY_CHARGING,0,0,0xee,0));d=TitanUnifiedInput_GetDeviceById(ds3);assert(d&&d->battery_state==TITAN_INPUT_BATTERY_CHARGING&&!d->battery_valid&&d->battery_percent==0);
 assert(TitanUnifiedInput_SubmitStateWithBattery(ds3,&s,TITAN_INPUT_BATTERY_LEVEL,50,1,3,0));d=TitanUnifiedInput_GetDeviceById(ds3);assert(d&&d->battery_valid&&d->battery_percent==50);
 assert(TitanUnifiedInput_UpdateBatteryEx(ds3,TITAN_INPUT_BATTERY_FULL,0,0,0xef));d=TitanUnifiedInput_GetDeviceById(ds3);assert(d&&d->battery_state==TITAN_INPUT_BATTERY_FULL&&d->battery_valid&&d->battery_percent==100);
 xbox=TitanUnifiedInput_ConnectEx(TITAN_INPUT_DEVICE_XBOX_SERIES,TITAN_INPUT_TRANSPORT_USB,TITAN_INPUT_BACKEND_PADEMU_USB,0x045e,0x0b12,TITAN_INPUT_CAP_BUTTONS|TITAN_INPUT_CAP_WIRED,0xb12);assert(xbox);
 assert(TitanUnifiedInput_UpdateBatteryEx(xbox,TITAN_INPUT_BATTERY_LEVEL,77,1,77));d=TitanUnifiedInput_GetDeviceById(xbox);assert(d&&d->battery_state==TITAN_INPUT_BATTERY_UNSUPPORTED&&!d->battery_valid&&d->battery_percent==0);
 puts("titan-controller-battery-contract: OK");puts("titan-controller-usb-battery-contract: OK");puts("titan-controller-battery-unknown-contract: OK");return 0;}
