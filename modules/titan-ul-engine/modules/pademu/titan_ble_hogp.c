#include "titan_ble_hogp.h"
#include "titan_ble_crypto.h"
#include <string.h>

#ifdef _IOP
#include "sys_utils.h"
/* PS2SDK sysclib.h maps these names to compiler builtins.  The explicit
 * aliases keep source-level copies on SMSUTILS while titan_iop_mem_compat.c
 * catches compiler-generated block-copy libcalls (for example struct copies). */
#ifdef memcpy
#undef memcpy
#endif
#ifdef memset
#undef memset
#endif
#define memcpy mips_memcpy
#define memset mips_memset
#endif
#define OGF_LE 0x08
#define OP(ogf,ocf) ((uint16_t)(((ogf)<<10)|(ocf)))
#define HCI_LE_SET_SCAN_PARAMS OP(OGF_LE,0x000b)
#define HCI_LE_SET_SCAN_ENABLE OP(OGF_LE,0x000c)
#define HCI_LE_CREATE_CONN OP(OGF_LE,0x000d)
#define HCI_LE_START_ENCRYPTION OP(OGF_LE,0x0019)
#define HCI_LE_READ_P256 OP(OGF_LE,0x0025)
#define HCI_LE_GENERATE_DHKEY OP(OGF_LE,0x0026)
#define EVT_LE_META 0x3e
#define LE_CONN_COMPLETE 0x01
#define LE_ADV_REPORT 0x02
#define LE_P256_COMPLETE 0x08
#define LE_DHKEY_COMPLETE 0x09
#define ATT_ERROR 0x01
#define ATT_MTU_REQ 0x02
#define ATT_MTU_RSP 0x03
#define ATT_FIND_INFO_REQ 0x04
#define ATT_FIND_INFO_RSP 0x05
#define ATT_READ_BY_TYPE_REQ 0x08
#define ATT_READ_BY_TYPE_RSP 0x09
#define ATT_READ_BY_GROUP_REQ 0x10
#define ATT_READ_BY_GROUP_RSP 0x11
#define ATT_WRITE_REQ 0x12
#define ATT_WRITE_RSP 0x13
#define ATT_NOTIFY 0x1b
#define SMP_PAIR_REQ 0x01
#define SMP_PAIR_RSP 0x02
#define SMP_PUBLIC_KEY 0x0c
#define SMP_DHKEY_CHECK 0x0d
#define SMP_PAIR_FAILED 0x05
#define SMP_ENC_INFO 0x06
#define SMP_ID_INFO 0x08
#define SMP_ID_ADDR_INFO 0x09
static uint16_t le16(const uint8_t*p){return (uint16_t)p[0]|((uint16_t)p[1]<<8);} static void put16(uint8_t*p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void st(TitanBleHogp*b,TitanBleState s){b->state=s;if(b->io.state_changed)b->io.state_changed(b->io.user,s);} 
static int hci(TitanBleHogp*b,uint16_t op,const uint8_t*p,unsigned n){uint8_t c[80];if(n>77||!b->io.send_hci)return 0;c[0]=(uint8_t)op;c[1]=(uint8_t)(op>>8);c[2]=(uint8_t)n;if(n)memcpy(c+3,p,n);return b->io.send_hci(b->io.user,c,n+3)>=0;}
static int acl(TitanBleHogp*b,uint16_t cid,const uint8_t*p,unsigned n){return b->io.send_acl&&b->io.send_acl(b->io.user,b->handle,cid,p,n)>=0;}
static int att_group(TitanBleHogp*b,uint16_t s,uint16_t e,uint16_t uuid){uint8_t p[7]={ATT_READ_BY_GROUP_REQ};put16(p+1,s);put16(p+3,e);put16(p+5,uuid);return acl(b,TITAN_BLE_CID_ATT,p,7);}static int att_type(TitanBleHogp*b,uint16_t s,uint16_t e,uint16_t uuid){uint8_t p[7]={ATT_READ_BY_TYPE_REQ};put16(p+1,s);put16(p+3,e);put16(p+5,uuid);return acl(b,TITAN_BLE_CID_ATT,p,7);}static int att_info(TitanBleHogp*b,uint16_t s,uint16_t e){uint8_t p[5]={ATT_FIND_INFO_REQ};put16(p+1,s);put16(p+3,e);return acl(b,TITAN_BLE_CID_ATT,p,5);}static int att_write16(TitanBleHogp*b,uint16_t h,uint16_t v){uint8_t p[5]={ATT_WRITE_REQ};put16(p+1,h);put16(p+3,v);return acl(b,TITAN_BLE_CID_ATT,p,5);}
void TitanBleHogp_Init(TitanBleHogp*b,const TitanBleIo*io){memset(b,0,sizeof(*b));if(io)b->io=*io;b->handle=0xffff;st(b,TITAN_BLE_OFF);}
int TitanBleHogp_StartScan(TitanBleHogp*b){uint8_t p[7]={1,0x10,0,0x10,0,0,0};uint8_t en[2]={1,0};if(!hci(b,HCI_LE_SET_SCAN_PARAMS,p,7)||!hci(b,HCI_LE_SET_SCAN_ENABLE,en,2))return 0;st(b,TITAN_BLE_SCANNING);return 1;}
int TitanBleHogp_StopScan(TitanBleHogp*b){uint8_t dis[2]={0,0};if(!b)return 0;if(b->state!=TITAN_BLE_SCANNING)return 1;if(!hci(b,HCI_LE_SET_SCAN_ENABLE,dis,2))return 0;st(b,TITAN_BLE_OFF);return 1;}
static int create(TitanBleHogp*b,uint8_t type,const uint8_t a[6]){uint8_t p[25]={0x10,0,0x10,0,0,type,0,0,0,0,0,0,0x18,0,0x28,0,0,0,0xc8,0,0,0,0,0};memcpy(p+7,a,6);p[6]=type;st(b,TITAN_BLE_CONNECTING);return hci(b,HCI_LE_CREATE_CONN,p,25);}
int TitanBleHogp_StartEarlyReconnect(TitanBleHogp*b){if(!b->io.load_bond||!b->io.load_bond(b->io.user,&b->bond)||b->bond.magic!=TITAN_BLE_BOND_VALID)return 0;return create(b,b->bond.addr_type,b->bond.addr);}
static int pair(TitanBleHogp*b){uint8_t p[7]={SMP_PAIR_REQ,3,0,1,16,1,1};memcpy(b->preq,p,7);if(b->io.random&&!b->io.random(b->io.user,b->local_random,16))return 0;st(b,TITAN_BLE_PAIRING);return acl(b,TITAN_BLE_CID_SMP,p,7)&&hci(b,HCI_LE_READ_P256,0,0);}
static int start_enc_bond(TitanBleHogp*b){uint8_t p[28];memset(p,0,sizeof(p));put16(p,b->handle);memcpy(p+12,b->bond.ltk,16);st(b,TITAN_BLE_ENCRYPTING);return hci(b,HCI_LE_START_ENCRYPTION,p,28);}
static int finish_sc(TitanBleHogp*b);
static int gatt_start(TitanBleHogp*b){uint8_t mtu[3]={ATT_MTU_REQ,0x80,0};acl(b,TITAN_BLE_CID_ATT,mtu,3);st(b,TITAN_BLE_GATT_SERVICE);return att_group(b,1,0xffff,0x2800);}
static int is_xbox_adv(const uint8_t*d,unsigned n){unsigned i=0;while(i<n){unsigned l=d[i];if(!l||i+l>=n+1)break;if((d[i+1]==0x09||d[i+1]==0x08)&&l>4){const char*s=(const char*)(d+i+2);unsigned m=l-1;if((m>=4&&memcmp(s,"Xbox",4)==0)||(m>=6&&memcmp(s,"Xbox W",6)==0))return 1;}i+=l+1;}return 0;}
int TitanBleHogp_OnHciEvent(TitanBleHogp*b,const uint8_t*e,unsigned n){if(!b||!e||n<2)return 0;if(e[0]==EVT_LE_META&&n>=4){uint8_t sub=e[2];if(sub==LE_ADV_REPORT&&b->state==TITAN_BLE_SCANNING&&n>13){unsigned off=4;uint8_t type=e[off+1];const uint8_t*a=e+off+2;unsigned dl=e[off+8];if(off+9+dl<=n&&is_xbox_adv(e+off+9,dl)){uint8_t dis[2]={0,0};hci(b,HCI_LE_SET_SCAN_ENABLE,dis,2);memcpy(b->peer,a,6);b->peer_type=type;return create(b,type,a);}}else if(sub==LE_CONN_COMPLETE&&n>=21&&e[3]==0){b->handle=le16(e+4)&0x0fff;b->peer_type=e[7];memcpy(b->peer,e+8,6);st(b,TITAN_BLE_CONNECTED);if(b->bond.magic==TITAN_BLE_BOND_VALID&&memcmp(b->bond.addr,b->peer,6)==0)return start_enc_bond(b);return pair(b);}else if(sub==LE_P256_COMPLETE&&n>=68&&e[3]==0){memcpy(b->local_pub,e+4,64);b->have_local_pub=1;if(b->state==TITAN_BLE_PAIRING){uint8_t p[65];p[0]=SMP_PUBLIC_KEY;memcpy(p+1,b->local_pub,64);acl(b,TITAN_BLE_CID_SMP,p,65);}if(b->have_peer_pub)return hci(b,HCI_LE_GENERATE_DHKEY,b->peer_pub,64);}else if(sub==LE_DHKEY_COMPLETE&&n>=36&&e[3]==0){memcpy(b->dhkey,e+4,32);b->have_dhkey=1;if(b->have_peer_random)return finish_sc(b);return 1;}}if(e[0]==0x08&&n>=4&&e[2]==0&&b->handle!=(uint16_t)0xffff){if(b->bond.magic==TITAN_BLE_BOND_VALID&&b->io.store_bond)b->io.store_bond(b->io.user,&b->bond);return gatt_start(b);}return 1;}
static void addr7(uint8_t out[7],uint8_t type,const uint8_t a[6]){out[0]=type;memcpy(out+1,a,6);}static int finish_sc(TitanBleHogp*b){uint8_t a1[7],a2[7],r[16]={0},iocap[3]={3,0,1},check[16],p[17];if(!b->have_dhkey)return 0;addr7(a1,b->local_type,b->local);addr7(a2,b->peer_type,b->peer);titan_ble_sc_f5(b->dhkey,b->local_random,b->peer_random,a1,a2,b->mackey,b->ltk);titan_ble_sc_f6(b->mackey,b->local_random,b->peer_random,r,iocap,a1,a2,check);p[0]=SMP_DHKEY_CHECK;memcpy(p+1,check,16);return acl(b,TITAN_BLE_CID_SMP,p,17);}
static int on_smp(TitanBleHogp*b,const uint8_t*p,unsigned n){if(!n)return 0;switch(p[0]){case SMP_PAIR_RSP:if(n<7)return 0;memcpy(b->pres,p,7);return 1;case SMP_PUBLIC_KEY:if(n<65)return 0;memcpy(b->peer_pub,p+1,64);b->have_peer_pub=1;if(b->have_local_pub)return hci(b,HCI_LE_GENERATE_DHKEY,b->peer_pub,64);return 1;case 0x03:if(n<17)return 0;memcpy(b->peer_confirm,p+1,16);{uint8_t q[17];q[0]=0x04;memcpy(q+1,b->local_random,16);return acl(b,TITAN_BLE_CID_SMP,q,17);}case 0x04:if(n<17)return 0;memcpy(b->peer_random,p+1,16);{uint8_t expected[16];titan_ble_sc_f4(b->peer_pub,b->local_pub,b->peer_random,0,expected);if(memcmp(expected,b->peer_confirm,16)!=0){b->errors++;st(b,TITAN_BLE_FAILED);return 0;}}b->have_peer_random=1;if(b->have_dhkey)return finish_sc(b);return 1;case SMP_DHKEY_CHECK:if(n<17)return 0;{uint8_t a1[7],a2[7],rr[16]={0},iocap[3],expected[16];addr7(a1,b->peer_type,b->peer);addr7(a2,b->local_type,b->local);iocap[0]=b->pres[1];iocap[1]=b->pres[2];iocap[2]=b->pres[3];titan_ble_sc_f6(b->mackey,b->peer_random,b->local_random,rr,iocap,a1,a2,expected);if(memcmp(expected,p+1,16)!=0){b->errors++;st(b,TITAN_BLE_FAILED);return 0;}memset(&b->bond,0,sizeof(b->bond));b->bond.magic=TITAN_BLE_BOND_VALID;b->bond.addr_type=b->peer_type;memcpy(b->bond.addr,b->peer,6);memcpy(b->bond.ltk,b->ltk,16);return start_enc_bond(b);}case SMP_ENC_INFO:if(n>=17)memcpy(b->bond.ltk,p+1,16);return 1;case SMP_ID_INFO:if(n>=17)memcpy(b->bond.irk,p+1,16);return 1;case SMP_ID_ADDR_INFO:if(n>=8){b->bond.identity_addr_type=p[1];memcpy(b->bond.identity_addr,p+2,6);}return 1;case SMP_PAIR_FAILED:b->errors++;st(b,TITAN_BLE_FAILED);return 0;default:return 1;}}
static int on_att(TitanBleHogp*b,const uint8_t*p,unsigned n){unsigned o;if(!n)return 0;if(p[0]==ATT_MTU_RSP)return 1;if(p[0]==ATT_READ_BY_GROUP_RSP&&b->state==TITAN_BLE_GATT_SERVICE&&n>=8){unsigned el=p[1];for(o=2;o+el<=n;o+=el){if(el>=6&&le16(p+o+4)==TITAN_BLE_UUID_HID){b->hid_start=le16(p+o);b->hid_end=le16(p+o+2);st(b,TITAN_BLE_GATT_CHARS);return att_type(b,b->hid_start,b->hid_end,0x2803);}}b->errors++;st(b,TITAN_BLE_FAILED);return 0;}if(p[0]==ATT_READ_BY_TYPE_RSP&&b->state==TITAN_BLE_GATT_CHARS&&n>=4){unsigned el=p[1];for(o=2;o+el<=n;o+=el){if(el>=7&&le16(p+o+5)==TITAN_BLE_UUID_REPORT){b->input_value=le16(p+o+3);}}if(b->input_value){st(b,TITAN_BLE_GATT_DESCS);return att_info(b,b->input_value+1,b->hid_end);}return 0;}if(p[0]==ATT_FIND_INFO_RSP&&b->state==TITAN_BLE_GATT_DESCS&&n>=6){unsigned fmt=p[1],el=fmt==1?4:18;for(o=2;o+el<=n;o+=el){if(fmt==1&&le16(p+o+2)==TITAN_BLE_UUID_CCCD){b->input_cccd=le16(p+o);break;}}if(b->input_cccd)return att_write16(b,b->input_cccd,1);return 0;}if(p[0]==ATT_WRITE_RSP&&b->state==TITAN_BLE_GATT_DESCS){st(b,TITAN_BLE_READY);return 1;}if(p[0]==ATT_NOTIFY&&n>=4&&le16(p+1)==b->input_value){b->notifications++;if(b->io.input_report)b->io.input_report(b->io.user,p+3,n-3);return 1;}if(p[0]==ATT_ERROR){b->errors++;return 0;}return 1;}
int TitanBleHogp_OnAcl(TitanBleHogp*b,uint16_t handle,uint16_t cid,const uint8_t*p,unsigned n){if(!b||handle!=b->handle)return 0;if(cid==TITAN_BLE_CID_SMP)return on_smp(b,p,n);if(cid==TITAN_BLE_CID_ATT)return on_att(b,p,n);return 0;}int TitanBleHogp_IsReady(const TitanBleHogp*b){return b&&b->state==TITAN_BLE_READY;}
