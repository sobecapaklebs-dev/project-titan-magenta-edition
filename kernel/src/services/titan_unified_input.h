#ifndef TITAN_UNIFIED_INPUT_H
#define TITAN_UNIFIED_INPUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TITAN_UNIFIED_INPUT_API_VERSION 1u

#define TITAN_UNIFIED_INPUT_MAX_DEVICES 8u
#define TITAN_UNIFIED_INPUT_MAX_PLAYERS 4u
#define TITAN_INPUT_PLAYER_UNASSIGNED 0xffu
#define TITAN_UNIFIED_INPUT_EVENT_QUEUE 24u

typedef enum TitanInputDeviceType {
    TITAN_INPUT_DEVICE_NONE = 0,
    TITAN_INPUT_DEVICE_DUALSHOCK2,
    TITAN_INPUT_DEVICE_KEYBOARD,
    TITAN_INPUT_DEVICE_MOUSE,
    TITAN_INPUT_DEVICE_DUALSHOCK3,
    TITAN_INPUT_DEVICE_DUALSHOCK4,
    TITAN_INPUT_DEVICE_DUALSENSE,
    TITAN_INPUT_DEVICE_XBOX_ONE,
    TITAN_INPUT_DEVICE_XBOX_SERIES,
    TITAN_INPUT_DEVICE_GENERIC_HID
} TitanInputDeviceType;

typedef enum TitanInputTransport {
    TITAN_INPUT_TRANSPORT_NONE = 0,
    TITAN_INPUT_TRANSPORT_SIO2,
    TITAN_INPUT_TRANSPORT_USB,
    TITAN_INPUT_TRANSPORT_BLUETOOTH
} TitanInputTransport;

typedef enum TitanInputBackend {
    TITAN_INPUT_BACKEND_NONE = 0,
    TITAN_INPUT_BACKEND_PADMAN,
    TITAN_INPUT_BACKEND_PADEMU_USB,
    TITAN_INPUT_BACKEND_PADEMU_BT,
    TITAN_INPUT_BACKEND_USB_HID,
    TITAN_INPUT_BACKEND_TITAN_BT_HOGP
} TitanInputBackend;

typedef enum TitanInputBatteryState {
    TITAN_INPUT_BATTERY_UNSUPPORTED = 0,
    TITAN_INPUT_BATTERY_UNKNOWN,
    TITAN_INPUT_BATTERY_LEVEL,
    TITAN_INPUT_BATTERY_CHARGING,
    TITAN_INPUT_BATTERY_FULL
} TitanInputBatteryState;

enum TitanInputCapability {
    TITAN_INPUT_CAP_BUTTONS         = 1u << 0,
    TITAN_INPUT_CAP_DPAD            = 1u << 1,
    TITAN_INPUT_CAP_ANALOG_STICKS   = 1u << 2,
    TITAN_INPUT_CAP_ANALOG_TRIGGERS = 1u << 3,
    TITAN_INPUT_CAP_RUMBLE          = 1u << 4,
    TITAN_INPUT_CAP_LIGHT           = 1u << 5,
    TITAN_INPUT_CAP_BATTERY         = 1u << 6,
    TITAN_INPUT_CAP_KEYBOARD        = 1u << 7,
    TITAN_INPUT_CAP_POINTER         = 1u << 8,
    TITAN_INPUT_CAP_WIRELESS        = 1u << 9,
    TITAN_INPUT_CAP_WIRED           = 1u << 10,
    TITAN_INPUT_CAP_SYSTEM_BUTTON   = 1u << 11
};

enum TitanNormalizedButton {
    TITAN_NBTN_SELECT   = 1u << 0,
    TITAN_NBTN_L3       = 1u << 1,
    TITAN_NBTN_R3       = 1u << 2,
    TITAN_NBTN_START    = 1u << 3,
    TITAN_NBTN_UP       = 1u << 4,
    TITAN_NBTN_RIGHT    = 1u << 5,
    TITAN_NBTN_DOWN     = 1u << 6,
    TITAN_NBTN_LEFT     = 1u << 7,
    TITAN_NBTN_L2       = 1u << 8,
    TITAN_NBTN_R2       = 1u << 9,
    TITAN_NBTN_L1       = 1u << 10,
    TITAN_NBTN_R1       = 1u << 11,
    TITAN_NBTN_TRIANGLE = 1u << 12,
    TITAN_NBTN_CIRCLE   = 1u << 13,
    TITAN_NBTN_CROSS    = 1u << 14,
    TITAN_NBTN_SQUARE   = 1u << 15,
    TITAN_NBTN_SYSTEM   = 1u << 16,
    TITAN_NBTN_TOUCHPAD = 1u << 17,
    TITAN_NBTN_MIC      = 1u << 18
};

typedef struct TitanNormalizedInputState {
    uint32_t buttons;
    uint32_t pressed;
    uint32_t released;
    uint8_t left_x;
    uint8_t left_y;
    uint8_t right_x;
    uint8_t right_y;
    uint8_t trigger_l;
    uint8_t trigger_r;
    int16_t pointer_dx;
    int16_t pointer_dy;
    int8_t wheel;
    uint8_t reserved0;
} TitanNormalizedInputState;

typedef struct TitanInputDeviceDescriptor {
    uint32_t device_id;
    uint32_t stable_key;
    TitanInputDeviceType type;
    TitanInputTransport transport;
    TitanInputBackend backend;
    uint32_t capabilities;
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t connected;
    uint8_t player;
    uint8_t preferred_player;
    uint8_t battery_percent;
    uint8_t battery_valid; /* compatibility: true only when percentage is reliable */
    TitanInputBatteryState battery_state;
    uint8_t battery_raw;
    uint16_t reserved1;
    uint32_t packet_rate;
    uint32_t errors;
    uint32_t last_latency_us;
    uint32_t reconnect_count;
    uint32_t connection_generation;
    uint32_t activity_serial;
    TitanNormalizedInputState state;
} TitanInputDeviceDescriptor;

#define TITAN_UNIFIED_INPUT_KEY_QUEUE 32u
typedef struct TitanUnifiedKeyboardEvent {
    uint8_t down;
    uint8_t hid_usage;
    uint8_t modifiers;
    uint8_t ascii;
} TitanUnifiedKeyboardEvent;

typedef enum TitanInputEventType {
    TITAN_INPUT_EVENT_NONE = 0,
    TITAN_INPUT_EVENT_CONNECTED,
    TITAN_INPUT_EVENT_DISCONNECTED,
    TITAN_INPUT_EVENT_RECONNECTED,
    TITAN_INPUT_EVENT_PLAYER_ASSIGNED,
    TITAN_INPUT_EVENT_BATTERY_CHANGED,
    TITAN_INPUT_EVENT_SYSTEM_BUTTON
} TitanInputEventType;

typedef struct TitanInputEvent {
    TitanInputEventType type;
    uint32_t device_id;
    TitanInputDeviceType device_type;
    TitanInputTransport transport;
    uint8_t player;
    TitanInputBatteryState battery_state;
    uint8_t battery_percent;
    uint8_t battery_valid;
    uint32_t generation;
} TitanInputEvent;

typedef struct TitanUnifiedInputMetrics {
    uint32_t connected_devices;
    uint32_t connect_events;
    uint32_t disconnect_events;
    uint32_t reconnect_events;
    uint32_t assignment_events;
    uint32_t system_button_events;
    uint32_t battery_events;
    uint32_t packets;
    uint32_t errors;
    uint32_t frame_updates;
    uint32_t inactive_backend_skips;
    uint32_t event_queue_drops;
} TitanUnifiedInputMetrics;

void TitanUnifiedInput_Initialize(void);
void TitanUnifiedInput_Shutdown(void);
void TitanUnifiedInput_BeginFrame(void);

uint32_t TitanUnifiedInput_Connect(
    TitanInputDeviceType type,
    TitanInputTransport transport,
    uint16_t vendor_id,
    uint16_t product_id,
    uint32_t capabilities,
    uint32_t stable_hint
);
uint32_t TitanUnifiedInput_ConnectEx(
    TitanInputDeviceType type,
    TitanInputTransport transport,
    TitanInputBackend backend,
    uint16_t vendor_id,
    uint16_t product_id,
    uint32_t capabilities,
    uint32_t stable_key
);
void TitanUnifiedInput_Disconnect(uint32_t device_id);
int TitanUnifiedInput_AssignPlayer(uint32_t device_id, unsigned int player);
int TitanUnifiedInput_ClaimPlayerOnFirstInput(uint32_t device_id, unsigned int player);
int TitanUnifiedInput_SubmitState(uint32_t device_id, const TitanNormalizedInputState *state,
                                  uint8_t battery_percent, int battery_valid,
                                  uint32_t latency_us);
int TitanUnifiedInput_SubmitStateEx(uint32_t device_id, const TitanNormalizedInputState *state,
                                    TitanInputBatteryState battery_state,
                                    uint8_t battery_percent, uint8_t battery_raw,
                                    uint32_t latency_us);
int TitanUnifiedInput_SubmitStateWithBattery(uint32_t device_id, const TitanNormalizedInputState *state,
                                    TitanInputBatteryState battery_state,
                                    uint8_t battery_percent, int battery_percent_valid,
                                    uint8_t battery_raw, uint32_t latency_us);
int TitanUnifiedInput_UpdateBattery(uint32_t device_id, TitanInputBatteryState state,
                                    uint8_t percent, uint8_t raw_value);
int TitanUnifiedInput_UpdateBatteryEx(uint32_t device_id, TitanInputBatteryState state,
                                    uint8_t percent, int percent_valid, uint8_t raw_value);
void TitanUnifiedInput_SubmitLegacyPad(unsigned int physical_port, int connected,
                                       uint16_t buttons, uint8_t left_x, uint8_t left_y,
                                       uint8_t right_x, uint8_t right_y);

const TitanInputDeviceDescriptor *TitanUnifiedInput_GetDevice(unsigned int index);
const TitanInputDeviceDescriptor *TitanUnifiedInput_GetPlayer(unsigned int player);
const TitanInputDeviceDescriptor *TitanUnifiedInput_GetDeviceById(uint32_t device_id);
const TitanInputDeviceDescriptor *TitanUnifiedInput_GetWelcomeCandidate(void);
unsigned int TitanUnifiedInput_GetDeviceCapacity(void);
const TitanUnifiedInputMetrics *TitanUnifiedInput_GetMetrics(void);
int TitanUnifiedInput_PollEvent(TitanInputEvent *event);
void TitanUnifiedInput_SetBootUiActive(int active);
int TitanUnifiedInput_IsBootUiActive(void);
int TitanUnifiedInput_SubmitKeyboardEvent(uint32_t device_id, uint8_t down, uint8_t hid_usage, uint8_t modifiers, uint8_t ascii);
int TitanUnifiedInput_PollKeyboardEvent(TitanUnifiedKeyboardEvent *event);

const char *TitanUnifiedInput_DeviceName(TitanInputDeviceType type);
const char *TitanUnifiedInput_TransportName(TitanInputTransport transport);
const char *TitanUnifiedInput_BackendName(TitanInputBackend backend);
const char *TitanUnifiedInput_BatteryStateName(TitanInputBatteryState state);

#ifdef __cplusplus
}
#endif
#endif
