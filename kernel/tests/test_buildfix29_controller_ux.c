#include "titan_controller_ux.h"
#include "titan_unified_input.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned pushes; static char last_title[64],last_message[128];
int TitanNotifications_Push(int kind,const char*t,const char*m){(void)kind;pushes++;snprintf(last_title,sizeof(last_title),"%s",t?t:"");snprintf(last_message,sizeof(last_message),"%s",m?m:"");return 1;}
int main(void){uint32_t id;TitanNormalizedInputState s;TitanUnifiedInput_Initialize();TitanUnifiedInput_SetBootUiActive(1);TitanControllerUx_Initialize();id=TitanUnifiedInput_ConnectEx(TITAN_INPUT_DEVICE_DUALSENSE,TITAN_INPUT_TRANSPORT_BLUETOOTH,TITAN_INPUT_BACKEND_PADEMU_BT,0x054c,0x0ce6,TITAN_INPUT_CAP_BUTTONS|TITAN_INPUT_CAP_BATTERY|TITAN_INPUT_CAP_SYSTEM_BUTTON|TITAN_INPUT_CAP_WIRELESS,0x5501);memset(&s,0,sizeof(s));s.left_x=s.left_y=s.right_x=s.right_y=128;s.buttons=TITAN_NBTN_SYSTEM;TitanUnifiedInput_SubmitStateEx(id,&s,TITAN_INPUT_BATTERY_LEVEL,82,8,0);assert(TitanUnifiedInput_ClaimPlayerOnFirstInput(id,0));TitanControllerUx_Update(0);assert(pushes==0);TitanControllerUx_Update(1);assert(pushes==1);assert(strstr(last_title,"DualSense"));assert(strstr(last_message,"Controller 1"));assert(strstr(last_message,"82%"));TitanControllerUx_Update(1);assert(pushes==1);TitanUnifiedInput_Disconnect(id);TitanControllerUx_Update(1);assert(pushes==2);assert(strstr(last_message,"disconnected"));puts("buildfix29-controller-ux: OK");return 0;}
