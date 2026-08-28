#ifndef TITAN_BLUETOOTH_RUNTIME_H
#define TITAN_BLUETOOTH_RUNTIME_H

typedef enum TitanBluetoothRuntimeState {
    TITAN_BT_RUNTIME_IDLE = 0,
    TITAN_BT_RUNTIME_WAIT_EARLY,
    TITAN_BT_RUNTIME_LOADING,
    TITAN_BT_RUNTIME_READY,
    TITAN_BT_RUNTIME_FAILED
} TitanBluetoothRuntimeState;

typedef struct TitanBluetoothRuntimeStatus {
    TitanBluetoothRuntimeState state;
    int smsutils_module_id;
    int pademu_module_id;
    int last_module_result;
    unsigned int requests;
} TitanBluetoothRuntimeStatus;

void TitanBluetoothRuntime_Initialize(void);
int TitanBluetoothRuntime_Request(void);
const TitanBluetoothRuntimeStatus *TitanBluetoothRuntime_GetStatus(void);

#endif
