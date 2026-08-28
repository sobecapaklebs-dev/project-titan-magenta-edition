#include "titan_unified_input.h"

#include <string.h>

#define TITAN_INPUT_ANALOG_CENTER 128u
#define TITAN_DS2_CAPS (TITAN_INPUT_CAP_BUTTONS | TITAN_INPUT_CAP_DPAD | \
                        TITAN_INPUT_CAP_ANALOG_STICKS | TITAN_INPUT_CAP_RUMBLE | \
                        TITAN_INPUT_CAP_WIRED)

static TitanInputDeviceDescriptor g_devices[TITAN_UNIFIED_INPUT_MAX_DEVICES];
static TitanUnifiedInputMetrics g_metrics;
static uint32_t g_next_device_id = 1u;
static uint32_t g_activity_serial = 1u;
static uint32_t g_connection_generation = 1u;
static int g_boot_ui_active;
static TitanUnifiedKeyboardEvent g_key_queue[TITAN_UNIFIED_INPUT_KEY_QUEUE];
static unsigned int g_key_head, g_key_tail, g_key_count;
static TitanInputEvent g_event_queue[TITAN_UNIFIED_INPUT_EVENT_QUEUE];
static unsigned int g_event_head, g_event_tail, g_event_count;

static TitanInputDeviceDescriptor *find_id(uint32_t id)
{
    unsigned int i;
    for (i = 0; i < TITAN_UNIFIED_INPUT_MAX_DEVICES; ++i)
        if (g_devices[i].device_id == id) return &g_devices[i];
    return 0;
}

static TitanInputDeviceDescriptor *find_stable(TitanInputDeviceType type,
                                                TitanInputTransport transport,
                                                uint32_t stable_key)
{
    unsigned int i;
    if (stable_key == 0u) return 0;
    for (i = 0; i < TITAN_UNIFIED_INPUT_MAX_DEVICES; ++i) {
        TitanInputDeviceDescriptor *d = &g_devices[i];
        if (d->device_id != 0u && d->stable_key == stable_key &&
            d->type == type && d->transport == transport)
            return d;
    }
    return 0;
}

static void neutral_state(TitanNormalizedInputState *s)
{
    memset(s, 0, sizeof(*s));
    s->left_x = TITAN_INPUT_ANALOG_CENTER;
    s->left_y = TITAN_INPUT_ANALOG_CENTER;
    s->right_x = TITAN_INPUT_ANALOG_CENTER;
    s->right_y = TITAN_INPUT_ANALOG_CENTER;
}

static void queue_event(TitanInputEventType type, const TitanInputDeviceDescriptor *d)
{
    TitanInputEvent *e;
    if (!d || type == TITAN_INPUT_EVENT_NONE) return;
    if (g_event_count == TITAN_UNIFIED_INPUT_EVENT_QUEUE) {
        g_event_head = (g_event_head + 1u) % TITAN_UNIFIED_INPUT_EVENT_QUEUE;
        g_event_count--;
        g_metrics.event_queue_drops++;
    }
    e = &g_event_queue[g_event_tail];
    memset(e, 0, sizeof(*e));
    e->type = type;
    e->device_id = d->device_id;
    e->device_type = d->type;
    e->transport = d->transport;
    e->player = d->player;
    e->battery_state = d->battery_state;
    e->battery_percent = d->battery_percent;
    e->battery_valid = d->battery_valid;
    e->generation = d->connection_generation;
    g_event_tail = (g_event_tail + 1u) % TITAN_UNIFIED_INPUT_EVENT_QUEUE;
    g_event_count++;
}

static int has_meaningful_press(const TitanNormalizedInputState *s)
{
    return s != 0 && s->pressed != 0u;
}

void TitanUnifiedInput_Initialize(void)
{
    unsigned int i;
    memset(g_devices, 0, sizeof(g_devices));
    memset(&g_metrics, 0, sizeof(g_metrics));
    memset(g_event_queue, 0, sizeof(g_event_queue));
    g_next_device_id = 1u;
    g_activity_serial = 1u;
    g_connection_generation = 1u;
    /* Compatibility default: normal runtime auto-assignment remains enabled.
       Boot/Welcome explicitly enables first-input assignment before enumeration. */
    g_boot_ui_active = 0;
    g_key_head = g_key_tail = g_key_count = 0u;
    g_event_head = g_event_tail = g_event_count = 0u;
    for (i = 0; i < TITAN_UNIFIED_INPUT_MAX_DEVICES; ++i) {
        g_devices[i].player = TITAN_INPUT_PLAYER_UNASSIGNED;
        g_devices[i].preferred_player = TITAN_INPUT_PLAYER_UNASSIGNED;
        g_devices[i].battery_state = TITAN_INPUT_BATTERY_UNSUPPORTED;
        neutral_state(&g_devices[i].state);
    }
}

void TitanUnifiedInput_Shutdown(void)
{
    TitanUnifiedInput_Initialize();
    g_boot_ui_active = 0;
}

void TitanUnifiedInput_BeginFrame(void)
{
    unsigned int i;
    g_metrics.frame_updates++;
    for (i = 0; i < TITAN_UNIFIED_INPUT_MAX_DEVICES; ++i) {
        g_devices[i].state.pressed = 0u;
        g_devices[i].state.released = 0u;
    }
}

uint32_t TitanUnifiedInput_ConnectEx(TitanInputDeviceType type, TitanInputTransport transport,
                                     TitanInputBackend backend, uint16_t vendor_id,
                                     uint16_t product_id, uint32_t capabilities,
                                     uint32_t stable_key)
{
    TitanInputDeviceDescriptor *d = find_stable(type, transport, stable_key);
    unsigned int i;
    int reconnect = 0;
    if (d && d->connected) return d->device_id;
    if (d) reconnect = 1;
    if (!d) {
        for (i = 0; i < TITAN_UNIFIED_INPUT_MAX_DEVICES; ++i)
            if (!g_devices[i].connected && g_devices[i].device_id == 0u) { d = &g_devices[i]; break; }
    }
    if (!d) {
        /* Reuse the oldest disconnected slot, retaining no foreign assignment. */
        for (i = 0; i < TITAN_UNIFIED_INPUT_MAX_DEVICES; ++i)
            if (!g_devices[i].connected) { d = &g_devices[i]; break; }
        if (d) {
            uint32_t old_id = d->device_id;
            memset(d, 0, sizeof(*d));
            d->device_id = old_id;
            d->player = TITAN_INPUT_PLAYER_UNASSIGNED;
            d->preferred_player = TITAN_INPUT_PLAYER_UNASSIGNED;
        }
    }
    if (!d) { g_metrics.errors++; return 0u; }

    if (d->device_id == 0u) {
        /* BF28 compatibility: a non-zero stable hint was historically also the
           public logical id. Preserve that contract while keeping stable_key
           as a separate concept for reconnect/assignment policy. */
        d->device_id = stable_key ? stable_key : g_next_device_id++;
        if (d->device_id >= g_next_device_id) g_next_device_id = d->device_id + 1u;
    }
    d->stable_key = stable_key;
    d->type = type;
    d->transport = transport;
    d->backend = backend;
    d->vendor_id = vendor_id;
    d->product_id = product_id;
    d->capabilities = capabilities;
    d->connected = 1u;
    d->battery_valid = 0u;
    d->battery_percent = 0u;
    d->battery_raw = 0u;
    d->battery_state = (capabilities & TITAN_INPUT_CAP_BATTERY) ?
        TITAN_INPUT_BATTERY_UNKNOWN : TITAN_INPUT_BATTERY_UNSUPPORTED;
    d->packet_rate = 0u;
    d->errors = 0u;
    d->last_latency_us = 0u;
    d->connection_generation = g_connection_generation++;
    d->activity_serial = 0u;
    neutral_state(&d->state);

    if (reconnect) {
        d->reconnect_count++;
        if (d->preferred_player < TITAN_UNIFIED_INPUT_MAX_PLAYERS &&
            TitanUnifiedInput_GetPlayer(d->preferred_player) == 0)
            d->player = d->preferred_player;
        else
            d->player = TITAN_INPUT_PLAYER_UNASSIGNED;
        g_metrics.reconnect_events++;
        queue_event(TITAN_INPUT_EVENT_RECONNECTED, d);
    } else {
        d->player = TITAN_INPUT_PLAYER_UNASSIGNED;
        d->preferred_player = TITAN_INPUT_PLAYER_UNASSIGNED;
        g_metrics.connect_events++;
        queue_event(TITAN_INPUT_EVENT_CONNECTED, d);
        /* Preserve BF28 runtime semantics outside boot. During boot, Welcome
           owns Player 1 through deterministic first-input assignment. */
        if (!g_boot_ui_active) {
            for (i = 0; i < TITAN_UNIFIED_INPUT_MAX_PLAYERS; ++i) {
                if (TitanUnifiedInput_GetPlayer(i) == 0) {
                    (void)TitanUnifiedInput_AssignPlayer(d->device_id, i);
                    break;
                }
            }
        }
    }
    g_metrics.connected_devices++;
    return d->device_id;
}

uint32_t TitanUnifiedInput_Connect(TitanInputDeviceType type, TitanInputTransport transport,
                                   uint16_t vendor_id, uint16_t product_id,
                                   uint32_t capabilities, uint32_t stable_hint)
{
    TitanInputBackend backend = TITAN_INPUT_BACKEND_NONE;
    if (transport == TITAN_INPUT_TRANSPORT_SIO2) backend = TITAN_INPUT_BACKEND_PADMAN;
    else if (type == TITAN_INPUT_DEVICE_KEYBOARD || type == TITAN_INPUT_DEVICE_MOUSE)
        backend = TITAN_INPUT_BACKEND_USB_HID;
    return TitanUnifiedInput_ConnectEx(type, transport, backend, vendor_id, product_id,
                                       capabilities, stable_hint);
}

void TitanUnifiedInput_Disconnect(uint32_t device_id)
{
    TitanInputDeviceDescriptor *d = find_id(device_id);
    if (!d || !d->connected) return;
    d->state.released = d->state.buttons;
    d->state.buttons = 0u;
    d->connected = 0u;
    d->battery_valid = 0u;
    if ((d->capabilities & TITAN_INPUT_CAP_BATTERY) != 0u)
        d->battery_state = TITAN_INPUT_BATTERY_UNKNOWN;
    if (d->player < TITAN_UNIFIED_INPUT_MAX_PLAYERS)
        d->preferred_player = d->player;
    if (g_metrics.connected_devices) g_metrics.connected_devices--;
    g_metrics.disconnect_events++;
    queue_event(TITAN_INPUT_EVENT_DISCONNECTED, d);
    d->player = TITAN_INPUT_PLAYER_UNASSIGNED;
}

int TitanUnifiedInput_AssignPlayer(uint32_t device_id, unsigned int player)
{
    TitanInputDeviceDescriptor *d;
    unsigned int i;
    if (player >= TITAN_UNIFIED_INPUT_MAX_PLAYERS) return 0;
    d = find_id(device_id);
    if (!d || !d->connected) return 0;
    for (i = 0; i < TITAN_UNIFIED_INPUT_MAX_DEVICES; ++i) {
        if (g_devices[i].connected && g_devices[i].device_id != device_id &&
            g_devices[i].player == player) {
            g_devices[i].preferred_player = g_devices[i].player;
            g_devices[i].player = TITAN_INPUT_PLAYER_UNASSIGNED;
        }
    }
    if (d->player == player) return 1;
    d->player = (uint8_t)player;
    d->preferred_player = (uint8_t)player;
    g_metrics.assignment_events++;
    queue_event(TITAN_INPUT_EVENT_PLAYER_ASSIGNED, d);
    return 1;
}

int TitanUnifiedInput_ClaimPlayerOnFirstInput(uint32_t device_id, unsigned int player)
{
    TitanInputDeviceDescriptor *d = find_id(device_id);
    if (!d || !d->connected || player >= TITAN_UNIFIED_INPUT_MAX_PLAYERS) return 0;
    if (TitanUnifiedInput_GetPlayer(player) != 0 && d->player != player) return 0;
    if (d->activity_serial == 0u && d->state.pressed == 0u) return 0;
    return TitanUnifiedInput_AssignPlayer(device_id, player);
}

int TitanUnifiedInput_UpdateBatteryEx(uint32_t device_id, TitanInputBatteryState state,
                                      uint8_t percent, int percent_valid, uint8_t raw_value)
{
    TitanInputDeviceDescriptor *d = find_id(device_id);
    TitanInputBatteryState old_state;
    uint8_t old_percent, old_valid;
    if (!d || !d->connected) return 0;
    if ((d->capabilities & TITAN_INPUT_CAP_BATTERY) == 0u) {
        state = TITAN_INPUT_BATTERY_UNSUPPORTED;
        percent_valid = 0;
    }
    old_state = d->battery_state;
    old_percent = d->battery_percent;
    old_valid = d->battery_valid;
    d->battery_state = state;
    d->battery_raw = raw_value;
    if (state == TITAN_INPUT_BATTERY_FULL) {
        d->battery_valid = 1u;
        d->battery_percent = 100u;
    } else if ((state == TITAN_INPUT_BATTERY_LEVEL || state == TITAN_INPUT_BATTERY_CHARGING) && percent_valid) {
        d->battery_valid = 1u;
        d->battery_percent = percent > 100u ? 100u : percent;
    } else {
        d->battery_valid = 0u;
        d->battery_percent = 0u;
    }
    if (old_state != d->battery_state || old_percent != d->battery_percent || old_valid != d->battery_valid) {
        g_metrics.battery_events++;
        queue_event(TITAN_INPUT_EVENT_BATTERY_CHANGED, d);
    }
    return 1;
}

int TitanUnifiedInput_UpdateBattery(uint32_t device_id, TitanInputBatteryState state,
                                    uint8_t percent, uint8_t raw_value)
{
    int valid = (state == TITAN_INPUT_BATTERY_LEVEL || state == TITAN_INPUT_BATTERY_FULL ||
                 state == TITAN_INPUT_BATTERY_CHARGING) ? 1 : 0;
    return TitanUnifiedInput_UpdateBatteryEx(device_id, state, percent, valid, raw_value);
}

int TitanUnifiedInput_SubmitStateWithBattery(uint32_t device_id, const TitanNormalizedInputState *state,
                                    TitanInputBatteryState battery_state,
                                    uint8_t battery_percent, int battery_percent_valid,
                                    uint8_t battery_raw, uint32_t latency_us)
{
    TitanInputDeviceDescriptor *d = find_id(device_id);
    uint32_t old_buttons;
    if (!d || !d->connected || !state) { g_metrics.errors++; return 0; }
    old_buttons = d->state.buttons;
    d->state = *state;
    d->state.pressed = state->buttons & ~old_buttons;
    d->state.released = old_buttons & ~state->buttons;
    if (has_meaningful_press(&d->state)) d->activity_serial = g_activity_serial++;
    d->last_latency_us = latency_us;
    d->packet_rate++;
    g_metrics.packets++;
    if ((d->state.pressed & TITAN_NBTN_SYSTEM) != 0u &&
        (d->capabilities & TITAN_INPUT_CAP_SYSTEM_BUTTON) != 0u) {
        g_metrics.system_button_events++;
        queue_event(TITAN_INPUT_EVENT_SYSTEM_BUTTON, d);
    }
    (void)TitanUnifiedInput_UpdateBatteryEx(device_id, battery_state, battery_percent,
                                             battery_percent_valid, battery_raw);
    return 1;
}

int TitanUnifiedInput_SubmitStateEx(uint32_t device_id, const TitanNormalizedInputState *state,
                                    TitanInputBatteryState battery_state,
                                    uint8_t battery_percent, uint8_t battery_raw,
                                    uint32_t latency_us)
{
    int valid = (battery_state == TITAN_INPUT_BATTERY_LEVEL ||
                 battery_state == TITAN_INPUT_BATTERY_FULL ||
                 battery_state == TITAN_INPUT_BATTERY_CHARGING) ? 1 : 0;
    return TitanUnifiedInput_SubmitStateWithBattery(device_id, state, battery_state,
                                                     battery_percent, valid,
                                                     battery_raw, latency_us);
}

int TitanUnifiedInput_SubmitState(uint32_t device_id, const TitanNormalizedInputState *state,
                                  uint8_t battery_percent, int battery_valid,
                                  uint32_t latency_us)
{
    TitanInputDeviceDescriptor *d = find_id(device_id);
    TitanInputBatteryState bstate;
    if (!d) { g_metrics.errors++; return 0; }
    if (battery_valid) {
        /* Legacy BF28 callers only asserted battery_valid when the backend had
           a real reading. Promote that evidence into the explicit BF29 cap. */
        d->capabilities |= TITAN_INPUT_CAP_BATTERY;
        bstate = TITAN_INPUT_BATTERY_LEVEL;
    } else if ((d->capabilities & TITAN_INPUT_CAP_BATTERY) == 0u) {
        bstate = TITAN_INPUT_BATTERY_UNSUPPORTED;
    } else {
        bstate = TITAN_INPUT_BATTERY_UNKNOWN;
    }
    return TitanUnifiedInput_SubmitStateEx(device_id, state, bstate, battery_percent,
                                           battery_percent, latency_us);
}

void TitanUnifiedInput_SubmitLegacyPad(unsigned int physical_port, int connected,
                                       uint16_t buttons, uint8_t left_x, uint8_t left_y,
                                       uint8_t right_x, uint8_t right_y)
{
    const uint32_t stable_id = 0x100u + physical_port;
    TitanInputDeviceDescriptor *d = find_stable(TITAN_INPUT_DEVICE_DUALSHOCK2,
                                                 TITAN_INPUT_TRANSPORT_SIO2, stable_id);
    TitanNormalizedInputState s;
    uint32_t id;
    if (!connected) {
        if (d && d->connected) TitanUnifiedInput_Disconnect(d->device_id);
        return;
    }
    if (!d || !d->connected) {
        id = TitanUnifiedInput_ConnectEx(TITAN_INPUT_DEVICE_DUALSHOCK2,
                                         TITAN_INPUT_TRANSPORT_SIO2,
                                         TITAN_INPUT_BACKEND_PADMAN, 0u, 0u,
                                         TITAN_DS2_CAPS, stable_id);
        d = find_id(id);
        if (d && !g_boot_ui_active && physical_port < TITAN_UNIFIED_INPUT_MAX_PLAYERS)
            TitanUnifiedInput_AssignPlayer(id, physical_port);
    }
    if (!d) return;
    neutral_state(&s);
    s.buttons = (uint32_t)buttons;
    s.left_x = left_x; s.left_y = left_y;
    s.right_x = right_x; s.right_y = right_y;
    (void)TitanUnifiedInput_SubmitStateEx(d->device_id, &s, TITAN_INPUT_BATTERY_UNSUPPORTED,
                                          0u, 0u, 0u);
}

const TitanInputDeviceDescriptor *TitanUnifiedInput_GetDevice(unsigned int index)
{
    if (index >= TITAN_UNIFIED_INPUT_MAX_DEVICES) return 0;
    return &g_devices[index];
}

const TitanInputDeviceDescriptor *TitanUnifiedInput_GetDeviceById(uint32_t device_id)
{
    TitanInputDeviceDescriptor *d = find_id(device_id);
    return d && d->connected ? d : 0;
}

const TitanInputDeviceDescriptor *TitanUnifiedInput_GetPlayer(unsigned int player)
{
    unsigned int i;
    if (player >= TITAN_UNIFIED_INPUT_MAX_PLAYERS) return 0;
    for (i = 0; i < TITAN_UNIFIED_INPUT_MAX_DEVICES; ++i)
        if (g_devices[i].connected && g_devices[i].player == player) return &g_devices[i];
    return 0;
}

const TitanInputDeviceDescriptor *TitanUnifiedInput_GetWelcomeCandidate(void)
{
    const TitanInputDeviceDescriptor *p1 = TitanUnifiedInput_GetPlayer(0u);
    const TitanInputDeviceDescriptor *best = 0;
    unsigned int i;
    if (p1) return p1;
    /* Once input exists, the first meaningful press wins deterministically.
       Before that, show the most recently connected eligible controller so the
       Welcome glyph can adapt without changing Player 1 assignment. */
    for (i = 0; i < TITAN_UNIFIED_INPUT_MAX_DEVICES; ++i) {
        const TitanInputDeviceDescriptor *d = &g_devices[i];
        if (!d->connected || d->type == TITAN_INPUT_DEVICE_MOUSE || d->type == TITAN_INPUT_DEVICE_KEYBOARD) continue;
        if (d->activity_serial != 0u) {
            if (!best || best->activity_serial == 0u || d->activity_serial < best->activity_serial)
                best = d;
        } else if (!best || (best->activity_serial == 0u && d->connection_generation > best->connection_generation)) {
            best = d;
        }
    }
    return best;
}

unsigned int TitanUnifiedInput_GetDeviceCapacity(void) { return TITAN_UNIFIED_INPUT_MAX_DEVICES; }
const TitanUnifiedInputMetrics *TitanUnifiedInput_GetMetrics(void) { return &g_metrics; }

int TitanUnifiedInput_PollEvent(TitanInputEvent *event)
{
    if (!event || g_event_count == 0u) return 0;
    *event = g_event_queue[g_event_head];
    g_event_head = (g_event_head + 1u) % TITAN_UNIFIED_INPUT_EVENT_QUEUE;
    g_event_count--;
    return 1;
}

void TitanUnifiedInput_SetBootUiActive(int active) { g_boot_ui_active = active ? 1 : 0; }
int TitanUnifiedInput_IsBootUiActive(void) { return g_boot_ui_active; }

int TitanUnifiedInput_SubmitKeyboardEvent(uint32_t device_id,uint8_t down,uint8_t hid_usage,uint8_t modifiers,uint8_t ascii)
{
    TitanInputDeviceDescriptor *d=find_id(device_id); TitanUnifiedKeyboardEvent *e;
    if(!d||!d->connected||d->type!=TITAN_INPUT_DEVICE_KEYBOARD){g_metrics.errors++;return 0;}
    if(g_key_count==TITAN_UNIFIED_INPUT_KEY_QUEUE){g_key_head=(g_key_head+1u)%TITAN_UNIFIED_INPUT_KEY_QUEUE;g_key_count--;g_metrics.errors++;}
    e=&g_key_queue[g_key_tail]; e->down=down;e->hid_usage=hid_usage;e->modifiers=modifiers;e->ascii=ascii;
    g_key_tail=(g_key_tail+1u)%TITAN_UNIFIED_INPUT_KEY_QUEUE;g_key_count++;g_metrics.packets++;return 1;
}

int TitanUnifiedInput_PollKeyboardEvent(TitanUnifiedKeyboardEvent *event)
{
    if (!event || !g_key_count) return 0;
    *event = g_key_queue[g_key_head];
    g_key_head = (g_key_head + 1u) % TITAN_UNIFIED_INPUT_KEY_QUEUE;
    g_key_count--;
    return 1;
}

const char *TitanUnifiedInput_DeviceName(TitanInputDeviceType type)
{
    switch (type) {
        case TITAN_INPUT_DEVICE_DUALSHOCK2: return "DualShock 2";
        case TITAN_INPUT_DEVICE_DUALSHOCK3: return "DualShock 3";
        case TITAN_INPUT_DEVICE_DUALSHOCK4: return "DualShock 4";
        case TITAN_INPUT_DEVICE_DUALSENSE: return "DualSense";
        case TITAN_INPUT_DEVICE_XBOX_ONE: return "Xbox Wireless Controller";
        case TITAN_INPUT_DEVICE_XBOX_SERIES: return "Xbox Series Controller";
        case TITAN_INPUT_DEVICE_KEYBOARD: return "Keyboard";
        case TITAN_INPUT_DEVICE_MOUSE: return "Mouse";
        case TITAN_INPUT_DEVICE_GENERIC_HID: return "HID Controller";
        default: return "Controller";
    }
}

const char *TitanUnifiedInput_TransportName(TitanInputTransport transport)
{
    switch (transport) {
        case TITAN_INPUT_TRANSPORT_SIO2: return "SIO2";
        case TITAN_INPUT_TRANSPORT_USB: return "USB";
        case TITAN_INPUT_TRANSPORT_BLUETOOTH: return "Bluetooth";
        default: return "Unknown";
    }
}

const char *TitanUnifiedInput_BackendName(TitanInputBackend backend)
{
    switch (backend) {
        case TITAN_INPUT_BACKEND_PADMAN: return "PADMAN";
        case TITAN_INPUT_BACKEND_PADEMU_USB: return "PADEMU USB";
        case TITAN_INPUT_BACKEND_PADEMU_BT: return "PADEMU BT";
        case TITAN_INPUT_BACKEND_USB_HID: return "USB HID";
        case TITAN_INPUT_BACKEND_TITAN_BT_HOGP: return "Titan BT HOGP";
        default: return "None";
    }
}

const char *TitanUnifiedInput_BatteryStateName(TitanInputBatteryState state)
{
    switch (state) {
        case TITAN_INPUT_BATTERY_UNKNOWN: return "Unknown";
        case TITAN_INPUT_BATTERY_LEVEL: return "Level";
        case TITAN_INPUT_BATTERY_CHARGING: return "Charging";
        case TITAN_INPUT_BATTERY_FULL: return "Full";
        default: return "Unsupported";
    }
}
