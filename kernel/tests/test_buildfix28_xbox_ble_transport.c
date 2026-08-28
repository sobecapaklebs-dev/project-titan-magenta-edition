#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../modules/titan-ul-engine/modules/pademu/titan_ble_hogp.h"
#include "../../modules/titan-ul-engine/modules/pademu/titan_ble_crypto.h"
typedef struct T { unsigned hci,acl,inputs; uint8_t last_hci[96]; unsigned last_hci_n; uint16_t last_cid; uint8_t last_acl[96]; unsigned last_acl_n; TitanBleBond bond; } T;
static int sh(void*u,const uint8_t*p,unsigned n){T*t=u;t->hci++;t->last_hci_n=n;memcpy(t->last_hci,p,n);return 0;}
static int sa(void*u,uint16_t h,uint16_t cid,const uint8_t*p,unsigned n){T*t=u;(void)h;t->acl++;t->last_cid=cid;t->last_acl_n=n;memcpy(t->last_acl,p,n);return 0;}
static int rnd(void*u,uint8_t*p,unsigned n){unsigned i;(void)u;for(i=0;i<n;i++)p[i]=(uint8_t)(i+1);return 1;}
static int lb(void*u,TitanBleBond*b){T*t=u;if(t->bond.magic!=TITAN_BLE_BOND_VALID)return 0;*b=t->bond;return 1;}
static int sb(void*u,const TitanBleBond*b){T*t=u;t->bond=*b;return 1;}
static void ir(void*u,const uint8_t*p,unsigned n){T*t=u;(void)p;(void)n;t->inputs++;}
static void sc(void*u,TitanBleState s){(void)u;(void)s;}
static void hx(const char*s,uint8_t*out,unsigned n){unsigned i,v;for(i=0;i<n;i++){sscanf(s+2*i,"%2x",&v);out[i]=(uint8_t)v;}}
int main(void){
 uint8_t key[16],msg[16],cm[16],want[16]; T t;TitanBleIo io;TitanBleHogp b;uint8_t adv[64]={0x3e,0,0x02,1,0,0,1,2,3,4,5,6,0};unsigned name=23;uint8_t conn[21]={0x3e,19,0x01,0,0x34,0x12,0,0,1,2,3,4,5,6};uint8_t enc[5]={0x08,3,0,0x34,0x12};uint8_t svc[8]={0x11,6,1,0,0xff,0xff,0x12,0x18};uint8_t chars[9]={0x09,7,2,0,0x10,3,0,0x4d,0x2a};uint8_t desc[6]={0x05,1,4,0,2,0x29};uint8_t wr[1]={0x13};uint8_t note[20]={0x1b,3,0,1};
 hx("2b7e151628aed2a6abf7158809cf4f3c",key,16);hx("6bc1bee22e409f96e93d7e117393172a",msg,16);hx("070a16b46b4d4144f79bdd9dd04a287c",want,16);titan_aes_cmac(key,msg,16,cm);assert(memcmp(cm,want,16)==0);
 memset(&t,0,sizeof(t));memset(&io,0,sizeof(io));io.user=&t;io.send_hci=sh;io.send_acl=sa;io.random=rnd;io.load_bond=lb;io.store_bond=sb;io.input_report=ir;io.state_changed=sc;TitanBleHogp_Init(&b,&io);assert(TitanBleHogp_StartScan(&b));assert(b.state==TITAN_BLE_SCANNING);
 adv[12]=(uint8_t)name;adv[13]=22;adv[14]=0x09;memcpy(adv+15,"Xbox Wireless Controller",20);assert(TitanBleHogp_OnHciEvent(&b,adv,40));assert(b.state==TITAN_BLE_CONNECTING);
 assert(TitanBleHogp_OnHciEvent(&b,conn,sizeof(conn)));assert(b.state==TITAN_BLE_PAIRING);assert(t.last_cid==TITAN_BLE_CID_SMP);
 /* Independently validate bonded early reconnect + encrypted GATT discovery path. */
 TitanBleHogp_Init(&b,&io);memset(&t.bond,0,sizeof(t.bond));t.bond.magic=TITAN_BLE_BOND_VALID;t.bond.addr_type=0;memcpy(t.bond.addr,(uint8_t[]){1,2,3,4,5,6},6);assert(TitanBleHogp_StartEarlyReconnect(&b));assert(b.state==TITAN_BLE_CONNECTING);assert(TitanBleHogp_OnHciEvent(&b,conn,sizeof(conn)));assert(b.state==TITAN_BLE_ENCRYPTING);assert(TitanBleHogp_OnHciEvent(&b,enc,sizeof(enc)));assert(b.state==TITAN_BLE_GATT_SERVICE);
 assert(TitanBleHogp_OnAcl(&b,b.handle,TITAN_BLE_CID_ATT,svc,sizeof(svc)));assert(b.state==TITAN_BLE_GATT_CHARS);assert(TitanBleHogp_OnAcl(&b,b.handle,TITAN_BLE_CID_ATT,chars,sizeof(chars)));assert(b.state==TITAN_BLE_GATT_DESCS);assert(TitanBleHogp_OnAcl(&b,b.handle,TITAN_BLE_CID_ATT,desc,sizeof(desc)));assert(b.input_cccd==4);assert(TitanBleHogp_OnAcl(&b,b.handle,TITAN_BLE_CID_ATT,wr,sizeof(wr)));assert(TitanBleHogp_IsReady(&b));assert(TitanBleHogp_OnAcl(&b,b.handle,TITAN_BLE_CID_ATT,note,sizeof(note)));assert(t.inputs==1);
 puts("buildfix28-xbox-ble-transport: OK");return 0;
}
