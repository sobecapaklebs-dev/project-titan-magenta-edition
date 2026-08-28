#ifndef TITAN_BLE_HOGP_H
#define TITAN_BLE_HOGP_H
#include <stdint.h>
#define TITAN_BLE_CID_ATT 0x0004u
#define TITAN_BLE_CID_SMP 0x0006u
#define TITAN_BLE_UUID_HID 0x1812u
#define TITAN_BLE_UUID_REPORT 0x2a4du
#define TITAN_BLE_UUID_REPORT_MAP 0x2a4bu
#define TITAN_BLE_UUID_CCCD 0x2902u
#define TITAN_BLE_UUID_REPORT_REF 0x2908u
#define TITAN_BLE_BOND_VALID 0x54424f4eu

typedef enum TitanBleState { TITAN_BLE_OFF=0,TITAN_BLE_SCANNING,TITAN_BLE_CONNECTING,TITAN_BLE_CONNECTED,TITAN_BLE_PAIRING,TITAN_BLE_ENCRYPTING,TITAN_BLE_GATT_SERVICE,TITAN_BLE_GATT_CHARS,TITAN_BLE_GATT_DESCS,TITAN_BLE_READY,TITAN_BLE_FAILED } TitanBleState;
typedef struct TitanBleBond { uint32_t magic; uint8_t addr_type; uint8_t addr[6]; uint8_t ltk[16]; uint8_t irk[16]; uint8_t identity_addr_type; uint8_t identity_addr[6]; } TitanBleBond;
typedef struct TitanBleIo {
 int (*send_hci)(void *u,const uint8_t *cmd,unsigned n);
 int (*send_acl)(void *u,uint16_t handle,uint16_t cid,const uint8_t *p,unsigned n);
 int (*random)(void *u,uint8_t *out,unsigned n);
 int (*load_bond)(void *u,TitanBleBond *bond);
 int (*store_bond)(void *u,const TitanBleBond *bond);
 void (*input_report)(void *u,const uint8_t *report,unsigned n);
 void (*state_changed)(void *u,TitanBleState state);
 void *user;
} TitanBleIo;
typedef struct TitanBleHogp {
 TitanBleIo io; TitanBleState state; uint16_t handle; uint8_t peer_type,peer[6],local_type,local[6];
 TitanBleBond bond; uint16_t hid_start,hid_end,input_value,input_cccd; uint8_t report_id;
 uint8_t preq[7],pres[7],local_random[16],peer_random[16],local_confirm[16],peer_confirm[16];
 uint8_t local_pub[64],peer_pub[64],dhkey[32],mackey[16],ltk[16]; uint8_t have_peer_pub,have_local_pub,have_dhkey,have_peer_random;
 uint32_t errors,notifications;
} TitanBleHogp;
void TitanBleHogp_Init(TitanBleHogp *b,const TitanBleIo *io);
int TitanBleHogp_StartScan(TitanBleHogp *b);
int TitanBleHogp_StopScan(TitanBleHogp *b);
int TitanBleHogp_StartEarlyReconnect(TitanBleHogp *b);
int TitanBleHogp_OnHciEvent(TitanBleHogp *b,const uint8_t *evt,unsigned n);
int TitanBleHogp_OnAcl(TitanBleHogp *b,uint16_t handle,uint16_t cid,const uint8_t *p,unsigned n);
int TitanBleHogp_IsReady(const TitanBleHogp *b);
#endif
