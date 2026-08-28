#ifndef TITAN_PADEMU_TELEMETRY_H
#define TITAN_PADEMU_TELEMETRY_H
#include <stdint.h>
#define TITAN_PADEMU_TELEMETRY_RPC_ID 0x54495029u
#define TITAN_PADEMU_BT_TELEMETRY_RPC_ID 0x5449502Au
#define TITAN_PADEMU_TELEMETRY_VERSION 2u
#define TITAN_PADEMU_TELEMETRY_MAX_PADS 4u
#define TITAN_PADEMU_RPC_GET_SNAPSHOT 1
#define TITAN_PADEMU_RPC_BT_BEGIN_PAIRING 2
#define TITAN_PADEMU_RPC_GET_TRACE 3
#define TITAN_PADEMU_RPC_WIRELESS_ACCESSORIES 4
#define TITAN_PADEMU_RPC_BOND_SYNC 5
#define TITAN_PADEMU_TRACE_VERSION 1u
#define TITAN_PADEMU_TRACE_MAX_EVENTS 8u
#define TITAN_PADEMU_TRACE_BUILD_ID 0x42543038u /* "BT08" */
#define TITAN_PADEMU_SYSTEM_CMD_ID 0x29u
#define TITAN_PADEMU_PAIRING_STATUS_VERSION 2u


typedef enum TitanPademuTraceCode {
 TITAN_PADEMU_TRACE_MODULE_START=1,
 TITAN_PADEMU_TRACE_ARGS=2,
 TITAN_PADEMU_TRACE_EXPORT_REGISTER=3,
 TITAN_PADEMU_TRACE_SIO2_POLICY=4,
 TITAN_PADEMU_TRACE_PAD_INIT_BEGIN=5,
 TITAN_PADEMU_TRACE_PAD_INIT_END=6,
 TITAN_PADEMU_TRACE_RPC_START=7,
 TITAN_PADEMU_TRACE_BT_INIT_BEGIN=16,
 TITAN_PADEMU_TRACE_BT_MUTEX_CREATE=17,
 TITAN_PADEMU_TRACE_BT_DRIVER_REGISTER=18,
 TITAN_PADEMU_TRACE_BT_CHARGE_DRIVER=19,
 TITAN_PADEMU_TRACE_BT_USB_PROBE=32,
 TITAN_PADEMU_TRACE_BT_USB_DEVICE=33,
 TITAN_PADEMU_TRACE_BT_USB_INTERFACE=34,
 TITAN_PADEMU_TRACE_BT_USB_PROBE_RESULT=35,
 TITAN_PADEMU_TRACE_BT_USB_CONNECT=36,
 TITAN_PADEMU_TRACE_BT_USB_ENDPOINT=37,
 TITAN_PADEMU_TRACE_BT_USB_SET_CONFIG=38,
 TITAN_PADEMU_TRACE_BT_USB_CONFIG_CB=39,
 TITAN_PADEMU_TRACE_BT_USB_RX_ARM=40,
 TITAN_PADEMU_TRACE_BT_USB_DISCONNECT=41,
 TITAN_PADEMU_TRACE_BT_HCI_COMMAND=48,
 TITAN_PADEMU_TRACE_BT_HCI_EVENT=49,
 TITAN_PADEMU_TRACE_BT_HCI_CONNECT=50,
 TITAN_PADEMU_TRACE_BT_L2CAP_RX=64,
 TITAN_PADEMU_TRACE_BT_HID_RUNNING=65,
 TITAN_PADEMU_TRACE_BT_HID_INPUT=66,
 TITAN_PADEMU_TRACE_BT_PAIR_REQUEST=80,
 TITAN_PADEMU_TRACE_BT_ACCESSORY_UI=81,
 TITAN_PADEMU_TRACE_BT_ACCESSORY_DISCOVERY=82,
 TITAN_PADEMU_TRACE_BT_ACCESSORY_PAIRING=83
} TitanPademuTraceCode;
typedef struct TitanPademuTraceEvent {
 uint32_t sequence;
 uint16_t code;
 uint16_t flags;
 int32_t result;
 uint32_t a, b, c;
} TitanPademuTraceEvent;
typedef struct TitanPademuTraceBlock {
 uint32_t version, count, dropped, build_id;
 TitanPademuTraceEvent events[TITAN_PADEMU_TRACE_MAX_EVENTS];
} TitanPademuTraceBlock;

typedef enum TitanPademuDeviceType {
 TITAN_PADEMU_DEVICE_NONE=0, TITAN_PADEMU_DEVICE_DS3=1, TITAN_PADEMU_DEVICE_DS4=2,
 TITAN_PADEMU_DEVICE_DUALSENSE=3, TITAN_PADEMU_DEVICE_XBOX_ONE=4,
 TITAN_PADEMU_DEVICE_XBOX_SERIES=5
} TitanPademuDeviceType;
typedef enum TitanPademuTransport { TITAN_PADEMU_TRANSPORT_NONE=0, TITAN_PADEMU_TRANSPORT_USB=1, TITAN_PADEMU_TRANSPORT_BT=2 } TitanPademuTransport;
typedef enum TitanPademuBatteryState { TITAN_PADEMU_BATTERY_UNSUPPORTED=0, TITAN_PADEMU_BATTERY_UNKNOWN=1, TITAN_PADEMU_BATTERY_LEVEL=2, TITAN_PADEMU_BATTERY_CHARGING=3, TITAN_PADEMU_BATTERY_FULL=4 } TitanPademuBatteryState;
typedef struct TitanPademuPadTelemetry {
 uint8_t connected, port, device_type, transport;
 uint8_t system_button, battery_state, battery_percent, battery_valid;
 uint8_t battery_raw, identity_quality, reserved0[2];
 uint16_t vendor_id, product_id;
 uint8_t address[6]; uint8_t reserved1[2];
 uint32_t stable_key, generation, packets, errors;
 /* v2: live transport input for Titan shell transport-only mode.  The v1
  * identity/battery prefix stays unchanged; the complete snapshot remains
  * below the 256-byte SIF RPC buffer used by the EE bridge. */
 uint16_t buttons;
 uint8_t left_x, left_y, right_x, right_y;
 uint8_t trigger_l, trigger_r, input_valid, reserved2;
} TitanPademuPadTelemetry;
typedef struct TitanPademuSystemButtonEvent { uint32_t header[4]; uint32_t port, device_type, generation, pressed; } TitanPademuSystemButtonEvent;
typedef enum TitanPademuBtTransportStage {
 TITAN_PADEMU_BT_STAGE_IDLE=0,
 TITAN_PADEMU_BT_STAGE_DRIVER_REGISTERED=1,
 TITAN_PADEMU_BT_STAGE_PROBE_SEEN=2,
 TITAN_PADEMU_BT_STAGE_PROBE_ACCEPTED=3,
 TITAN_PADEMU_BT_STAGE_CONNECT_SEEN=4,
 TITAN_PADEMU_BT_STAGE_ENDPOINTS_READY=5,
 TITAN_PADEMU_BT_STAGE_CONFIG_SUBMITTED=6,
 TITAN_PADEMU_BT_STAGE_CONFIGURED=7,
 TITAN_PADEMU_BT_STAGE_HCI_RESET_SUBMITTED=8,
 TITAN_PADEMU_BT_STAGE_HCI_RESET_COMPLETE=9,
 TITAN_PADEMU_BT_STAGE_SCAN_ENABLE_SUBMITTED=10,
 TITAN_PADEMU_BT_STAGE_READY=11,
 TITAN_PADEMU_BT_STAGE_PROBE_REJECTED=12,
 TITAN_PADEMU_BT_STAGE_CONNECT_FAILED=13,
 TITAN_PADEMU_BT_STAGE_CONFIG_FAILED=14,
 TITAN_PADEMU_BT_STAGE_HCI_RESET_FAILED=15,
 TITAN_PADEMU_BT_STAGE_SCAN_ENABLE_FAILED=16,
 TITAN_PADEMU_BT_STAGE_DRIVER_REGISTER_FAILED=17,
 TITAN_PADEMU_BT_STAGE_DISCONNECTED=18
} TitanPademuBtTransportStage;
typedef struct TitanPademuBluetoothTransportStatus {
 uint32_t stage;
 int32_t last_error;
 int32_t usb_dev_id;
 uint32_t usb_status;
 uint32_t endpoints_mask;
 uint32_t usb_identity;
 uint32_t last_hci_event;
 uint32_t last_hci_opcode;
 uint32_t last_hci_status;
 uint32_t probe_count;
 uint32_t connect_count;
 int32_t config_result;
 int32_t reset_submit_result;
 int32_t scan_submit_result;
} TitanPademuBluetoothTransportStatus;
typedef struct TitanPademuBluetoothPairingStatus {
 uint32_t version, available, started, reserved;
 TitanPademuBluetoothTransportStatus transport;
} TitanPademuBluetoothPairingStatus;

#define TITAN_PADEMU_WIRELESS_VERSION 1u
#define TITAN_PADEMU_WIRELESS_MAX_ACCESSORIES 4u

typedef enum TitanPademuWirelessCommand {
 TITAN_PADEMU_WIRELESS_QUERY=0,
 TITAN_PADEMU_WIRELESS_BEGIN_SCAN=1,
 TITAN_PADEMU_WIRELESS_CONNECT=2,
 TITAN_PADEMU_WIRELESS_DISCONNECT=3,
 TITAN_PADEMU_WIRELESS_FORGET=4,
 /* P10.6.1: initialize the already-loaded BT transport and publish
  * imported bond state only. No discovery, SSP, or forced re-pair. */
 TITAN_PADEMU_WIRELESS_BOOTSTRAP=5
} TitanPademuWirelessCommand;

typedef enum TitanPademuWirelessFlags {
 TITAN_PADEMU_WIRELESS_SEEN=0x01,
 TITAN_PADEMU_WIRELESS_PAIRED=0x02,
 TITAN_PADEMU_WIRELESS_CONNECTED=0x04,
 TITAN_PADEMU_WIRELESS_CONNECTING=0x08,
 TITAN_PADEMU_WIRELESS_CONTROLLER=0x10,
 TITAN_PADEMU_WIRELESS_BLE=0x20
} TitanPademuWirelessFlags;

typedef enum TitanPademuWirelessStateFlags {
 TITAN_PADEMU_WIRELESS_TRANSPORT_READY=0x01,
 TITAN_PADEMU_WIRELESS_SCANNING=0x02,
 TITAN_PADEMU_WIRELESS_CONNECT_IN_PROGRESS=0x04,
 TITAN_PADEMU_WIRELESS_HAS_ERROR=0x08,
 TITAN_PADEMU_WIRELESS_SSP_READY=0x10
} TitanPademuWirelessStateFlags;

typedef struct TitanPademuWirelessRequest {
 uint32_t version;
 uint32_t command;
 uint32_t index;
 uint32_t reserved;
} TitanPademuWirelessRequest;

typedef struct TitanPademuWirelessAccessory {
 uint8_t address[6];
 uint8_t flags;
 uint8_t device_type;
 uint32_t class_of_device;
 uint16_t clock_offset;
 uint8_t page_scan_repetition_mode;
 uint8_t battery_percent;
 char name[32];
} TitanPademuWirelessAccessory;

typedef struct TitanPademuWirelessState {
 uint32_t version;
 uint32_t generation;
 uint32_t flags;
 uint32_t count;
 int32_t last_error;
 uint32_t selected;
 uint32_t last_hci_event;
 uint32_t last_hci_opcode;
 TitanPademuWirelessAccessory accessories[TITAN_PADEMU_WIRELESS_MAX_ACCESSORIES];
} TitanPademuWirelessState;


#define TITAN_PADEMU_BOND_SYNC_VERSION 1u
#define TITAN_PADEMU_CLASSIC_BOND_MAGIC 0x54424331u /* "TBC1" */
#define TITAN_PADEMU_CLASSIC_BOND_VERSION 1u
#define TITAN_PADEMU_XBOX_BOND_MAGIC 0x54424f4eu /* Titan BLE bond valid */

typedef enum TitanPademuBondSyncCommand {
 TITAN_PADEMU_BOND_SYNC_QUERY=0,
 TITAN_PADEMU_BOND_SYNC_IMPORT=1,
 TITAN_PADEMU_BOND_SYNC_ACK=2
} TitanPademuBondSyncCommand;

typedef enum TitanPademuBondSyncFlags {
 TITAN_PADEMU_BOND_SYNC_CLASSIC_VALID=0x01,
 TITAN_PADEMU_BOND_SYNC_XBOX_VALID=0x02,
 TITAN_PADEMU_BOND_SYNC_CLASSIC_DIRTY=0x04,
 TITAN_PADEMU_BOND_SYNC_XBOX_DIRTY=0x08
} TitanPademuBondSyncFlags;

typedef struct TitanPademuClassicBondEntry {
 uint8_t address[6];
 uint8_t device_type;
 uint8_t reserved0;
 uint8_t key[16];
 char name[32];
} TitanPademuClassicBondEntry;

typedef struct TitanPademuClassicBondFile {
 uint32_t magic;
 uint32_t version;
 uint32_t count;
 uint32_t reserved;
 TitanPademuClassicBondEntry entries[TITAN_PADEMU_WIRELESS_MAX_ACCESSORIES];
} TitanPademuClassicBondFile;

typedef struct TitanPademuXboxBond {
 uint32_t magic;
 uint8_t addr_type;
 uint8_t addr[6];
 uint8_t ltk[16];
 uint8_t irk[16];
 uint8_t identity_addr_type;
 uint8_t identity_addr[6];
} TitanPademuXboxBond;

typedef struct TitanPademuBondSync {
 uint32_t version;
 uint32_t command;
 uint32_t generation;
 uint32_t flags;
 TitanPademuClassicBondFile classic;
 TitanPademuXboxBond xbox;
} TitanPademuBondSync;

typedef struct TitanPademuTelemetrySnapshot {
 uint32_t version, generation, count, reserved;
 TitanPademuPadTelemetry pads[TITAN_PADEMU_TELEMETRY_MAX_PADS];
} TitanPademuTelemetrySnapshot;
#endif
