#ifndef PROJECT_TITAN_EARLY_INPUT_H
#define PROJECT_TITAN_EARLY_INPUT_H
#include "titan_result.h"
#include <stdint.h>
typedef enum TitanEarlyInputBackend {
    TITAN_EARLY_INPUT_NONE = 0,
    TITAN_EARLY_INPUT_USB = 1,
    TITAN_EARLY_INPUT_BT = 2
} TitanEarlyInputBackend;
typedef enum TitanEarlyInputState {
    TITAN_EARLY_INPUT_STATE_IDLE = 0,
    TITAN_EARLY_INPUT_STATE_STARTING,
    TITAN_EARLY_INPUT_STATE_LOADING,
    TITAN_EARLY_INPUT_STATE_READY,
    TITAN_EARLY_INPUT_STATE_FAILED,
    TITAN_EARLY_INPUT_STATE_TIMED_OUT
} TitanEarlyInputState;
typedef struct TitanEarlyInputStatus {
    int initialized;
    int enabled;
    TitanEarlyInputBackend backend;
    TitanEarlyInputState state;
    int smsutils_module_id;
    int pademu_module_id;
    int last_module_result;
    int bond_loaded;
    unsigned int pump_count;
    uint64_t deadline_ms;
} TitanEarlyInputStatus;
/* Starts a bounded fail-open worker after storage/USBD bootstrap. Returns immediately. */
TitanResult TitanEarlyInput_Initialize(void);
/* Called once per startup frame; never performs IRX loading itself. */
void TitanEarlyInput_Pump(uint64_t now_ms);
void TitanEarlyInput_Shutdown(void);
const TitanEarlyInputStatus *TitanEarlyInput_GetStatus(void);
TitanEarlyInputBackend TitanEarlyInput_GetConfiguredBackend(void);
int TitanEarlyInput_SaveBackendPreference(TitanEarlyInputBackend backend);
#endif
