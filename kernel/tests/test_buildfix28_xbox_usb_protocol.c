#include "xbox_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void le16(uint8_t *p, uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
int main(void){
 uint8_t pkt[32]; int n, guide=0; struct ds2report ds2; memset(&ds2,0,sizeof(ds2));
 assert(titan_xbox_usb_pid_supported(XBOX_ONE_PID));
 assert(titan_xbox_usb_pid_supported(XBOX_ONE_S_PID));
 assert(titan_xbox_usb_pid_supported(XBOX_SERIES_PID));
 assert(titan_xbox_usb_pid_series(XBOX_SERIES_PID));
 assert(!titan_xbox_usb_pid_series(XBOX_ONE_S_PID));
 assert(titan_xbox_usb_needs_s_init(XBOX_ONE_S_PID));
 n=titan_xbox_gip_build_power_on(9,pkt,sizeof(pkt)); assert(n==5 && pkt[0]==0x05 && pkt[1]==0x20 && pkt[2]==9 && pkt[4]==0);
 n=titan_xbox_gip_build_s_init(10,pkt,sizeof(pkt)); assert(n==5 && pkt[3]==0x0f && pkt[4]==0x06);
 n=titan_xbox_gip_build_virtual_key_ack(7,pkt,sizeof(pkt)); assert(n==13 && pkt[0]==0x01 && pkt[2]==7 && pkt[5]==0x07 && pkt[7]==7);
 memset(pkt,0,sizeof(pkt)); pkt[0]=0x20;pkt[2]=1;pkt[3]=14; pkt[4]=0x14; pkt[5]=0x14; le16(&pkt[6],1023);le16(&pkt[8],512);le16(&pkt[10],32767);le16(&pkt[12],0);le16(&pkt[14],0x8000);le16(&pkt[16],32767);
 assert(titan_xbox_gip_parse_ds2(pkt,18,&ds2,&guide));
 assert(!(ds2.nButtonState & DS2ButtonStart)); assert(!(ds2.nButtonState & DS2ButtonCross)); assert(!(ds2.nButtonState & DS2ButtonLeft)); assert(!(ds2.nButtonState & DS2ButtonL1)); assert(ds2.PressureL2==255);
 memset(pkt,0,5);pkt[0]=0x07;pkt[1]=0x30;pkt[2]=4;pkt[3]=1;pkt[4]=1;assert(titan_xbox_gip_parse_ds2(pkt,5,&ds2,&guide)&&guide);
 puts("buildfix28-xbox-usb-protocol: OK"); return 0;
}
