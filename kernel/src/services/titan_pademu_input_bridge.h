#ifndef TITAN_PADEMU_INPUT_BRIDGE_H
#define TITAN_PADEMU_INPUT_BRIDGE_H
#include <stdint.h>
#include <titan_pademu_telemetry.h>
typedef struct TitanPademuBridgeMetrics { uint32_t bind_attempts,rpc_calls,rpc_errors,active_ports,inactive_skips; } TitanPademuBridgeMetrics;
typedef enum TitanPademuPairingState {
    TITAN_PADEMU_PAIRING_IDLE = 0,
    TITAN_PADEMU_PAIRING_PENDING,
    TITAN_PADEMU_PAIRING_STARTED,
    TITAN_PADEMU_PAIRING_UNAVAILABLE,
    TITAN_PADEMU_PAIRING_ERROR,
    TITAN_PADEMU_PAIRING_TIMED_OUT
} TitanPademuPairingState;
typedef struct TitanPademuPairingRuntime {
    TitanPademuPairingState state;
    uint32_t available, started, requests, completions, rpc_errors;
    uint32_t transport_stage, usb_status, endpoints_mask, usb_identity;
    int32_t transport_error, usb_dev_id, config_result, reset_submit_result, scan_submit_result;
    uint32_t last_hci_event, last_hci_opcode, last_hci_status, probe_count, connect_count;
} TitanPademuPairingRuntime;
void TitanPademuInputBridge_Initialize(void);
void TitanPademuInputBridge_Shutdown(void);
void TitanPademuInputBridge_BeginFrame(void);
int TitanPademuInputBridge_SubmitPort(unsigned int port,int connected,uint16_t buttons,uint8_t lx,uint8_t ly,uint8_t rx,uint8_t ry);
const TitanPademuBridgeMetrics *TitanPademuInputBridge_GetMetrics(void);
/* Queue pairing without ever waiting for the IOP from the frame thread. */
int TitanPademuInputBridge_StartBluetoothPairing(void);
const TitanPademuPairingRuntime *TitanPademuInputBridge_GetBluetoothPairingRuntime(void);
/* P10 Wireless Accessories Manager commands share the existing single-flight
 * PADEMU RPC client and are therefore safe for the 60 Hz shell thread. */
void TitanPademuInputBridge_RequestBondRestore(void);
int TitanPademuInputBridge_RequestWireless(unsigned int command, unsigned int index);
const TitanPademuWirelessState *TitanPademuInputBridge_GetWirelessState(void);
int TitanPademuInputBridge_HasWirelessState(void);
/* P10.5: EE-owned persistence for Bluetooth bonds. */
int TitanPademuInputBridge_HasStorageWork(void);
int TitanPademuInputBridge_StorageWorkerPump(void);

#if defined(TITAN_PADEMU_BRIDGE_HOST_TEST)
#include <titan_pademu_telemetry.h>
void TitanPademuInputBridge_TestSetSnapshot(const TitanPademuTelemetrySnapshot *snapshot);
#endif
#endif
