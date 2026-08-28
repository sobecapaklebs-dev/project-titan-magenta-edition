#include "titan_modern_gamepad_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void put16(uint8_t *p, uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}

int main(void){
 TitanGamepadProtocolResult r;
 uint8_t ds[64]={0};
 uint8_t xb_payload[14]={0};
 uint8_t gip[18]={0};
 uint8_t vkey[5]={0x07,0x30,7,1,1};
 uint8_t bt[18]={0};

 ds[0]=1; ds[1]=10;ds[2]=20;ds[3]=30;ds[4]=40;ds[5]=50;ds[6]=60;
 ds[8]=0x20|0; ds[9]=0x21; ds[10]=0x01; ds[53]=5;
 assert(TitanGamepad_ParseDualSenseUsb(ds,sizeof(ds),&r));
 assert(r.state.left_x==10 && r.state.trigger_l==50);
 assert((r.state.buttons&(TITAN_NBTN_CROSS|TITAN_NBTN_UP|TITAN_NBTN_L1|TITAN_NBTN_START|TITAN_NBTN_SYSTEM)) ==
        (TITAN_NBTN_CROSS|TITAN_NBTN_UP|TITAN_NBTN_L1|TITAN_NBTN_START|TITAN_NBTN_SYSTEM));
 assert(r.battery_valid && r.battery_percent==50);

 xb_payload[0]=(1u<<2)|(1u<<4); /* Menu + A */
 xb_payload[1]=(1u<<2)|(1u<<4); /* Left + LB */
 put16(&xb_payload[2],1023); put16(&xb_payload[4],512);
 put16(&xb_payload[6],0x7fff); put16(&xb_payload[8],0);
 put16(&xb_payload[10],0x8000); put16(&xb_payload[12],0x7fff);
 assert(TitanGamepad_ParseXboxGipInput(xb_payload,sizeof(xb_payload),&r));
 assert(r.state.buttons&TITAN_NBTN_START); assert(r.state.buttons&TITAN_NBTN_CROSS);
 assert(r.state.buttons&TITAN_NBTN_LEFT); assert(r.state.buttons&TITAN_NBTN_L1);
 assert(r.state.trigger_l==255 && (r.state.buttons&TITAN_NBTN_L2));

 gip[0]=0x20; gip[1]=0; gip[2]=3; gip[3]=14; memcpy(&gip[4],xb_payload,14);
 assert(TitanGamepad_ParseXboxGipPacket(gip,sizeof(gip),&r));
 assert(r.state.buttons&TITAN_NBTN_CROSS);
 assert(TitanGamepad_ParseXboxGipPacket(vkey,sizeof(vkey),&r));
 assert(r.state.buttons&TITAN_NBTN_SYSTEM);

 /* xpadneo/HID-BLE Report ID 1: axes, triggers, hat, 15 buttons. */
 bt[0]=1; put16(&bt[1],0xffff); put16(&bt[3],0x8000); put16(&bt[5],0); put16(&bt[7],0x7fff);
 put16(&bt[9],0x03ff); put16(&bt[11],0x0200); bt[13]=1; /* hat up */
 bt[14]=0x11; /* A + LB under Titan parser mapping */
 bt[15]=0x01; /* menu */
 bt[16]=0x01; /* share */
 assert(TitanGamepad_ParseXboxBluetoothHid(bt,sizeof(bt),&r));
 assert(r.state.buttons&TITAN_NBTN_UP); assert(r.state.trigger_l==255);
 assert(r.state.buttons&TITAN_NBTN_CROSS);
 puts("buildfix28-modern-gamepad-protocol: OK"); return 0;
}
