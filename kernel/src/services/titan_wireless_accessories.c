#include "titan_wireless_accessories.h"
#include "titan_pademu_input_bridge.h"
#include "titan_bluetooth_runtime.h"
#include "titan_notifications.h"
#include "titan_health_trace.h"

#include <stdio.h>
#include <string.h>

static unsigned int g_wireless_frame;
static int g_wireless_visible;
static unsigned int g_last_generation;
static unsigned int g_last_connected_mask;
static int g_connect_notification_pending;
static unsigned char g_connect_address[6];
static unsigned int g_connect_error_generation;
static int g_startup_reconnect_active;
static unsigned int g_startup_reconnect_attempts;
static unsigned int g_startup_reconnect_next_frame;
#define TITAN_STARTUP_RECONNECT_COOLDOWN_FRAMES 180u
#define TITAN_STARTUP_RECONNECT_MAX_ATTEMPTS 20u


static int address_equal(const unsigned char a[6], const unsigned char b[6])
{
    return memcmp(a, b, 6u) == 0;
}

static const TitanPademuWirelessAccessory *find_by_address(
    const TitanPademuWirelessState *state,
    const unsigned char address[6])
{
    unsigned int i;
    if (!state || !address) return 0;
    for (i = 0u; i < state->count && i < TITAN_PADEMU_WIRELESS_MAX_ACCESSORIES; i++)
        if (address_equal(state->accessories[i].address, address)) return &state->accessories[i];
    return 0;
}

static const char *accessory_name(const TitanPademuWirelessAccessory *accessory)
{
    if (!accessory) return "Acessório Bluetooth";
    if (accessory->name[0]) return accessory->name;
    if (accessory->flags & TITAN_PADEMU_WIRELESS_CONTROLLER) return "Controle sem fio";
    return "Acessório Bluetooth";
}

void TitanWirelessAccessories_Initialize(void)
{
    g_wireless_frame = 0u;
    g_wireless_visible = 0;
    g_last_generation = 0u;
    g_last_connected_mask = 0u;
    g_connect_notification_pending = 0;
    memset(g_connect_address, 0, sizeof(g_connect_address));
    g_connect_error_generation = 0u;
    g_startup_reconnect_active = 0;
    g_startup_reconnect_attempts = 0u;
    g_startup_reconnect_next_frame = 0u;
}


void TitanWirelessAccessories_BeginStartupReconnect(void)
{
    int request_result;
    int bootstrap_result;
    /* P10.6: Health & Safety is late enough for storage/USBD ownership and early
     * enough for couch-style wake/reconnect. The request is asynchronous. */
    TitanHealthTrace_Mark(TITAN_HT_WIRELESS_BEGIN, g_wireless_frame,
        g_startup_reconnect_active, g_startup_reconnect_attempts, 0u);
    g_startup_reconnect_active = 1;
    g_startup_reconnect_attempts = 0u;
    g_startup_reconnect_next_frame = g_wireless_frame;

    TitanHealthTrace_Critical(TITAN_HT_WIRELESS_BT_REQUEST_BEFORE,
        g_wireless_frame, 0u, 0u, 0u);
    request_result = TitanBluetoothRuntime_Request();
    TitanHealthTrace_Critical(TITAN_HT_WIRELESS_BT_REQUEST_AFTER,
        g_wireless_frame, (uint32_t)request_result, 0u, 0u);

    /* P10.6.1: BOOTSTRAP is intentionally non-QUERY so the P10.2 IOP
     * worker executes pademu_transport_request_init(). It is init/state
     * only: discovery remains an explicit Settings action. */
    TitanHealthTrace_Critical(TITAN_HT_WIRELESS_BOOTSTRAP_BEFORE,
        g_wireless_frame, TITAN_PADEMU_WIRELESS_BOOTSTRAP, 0u, 0u);
    bootstrap_result = TitanPademuInputBridge_RequestWireless(
        TITAN_PADEMU_WIRELESS_BOOTSTRAP, 0u);
    TitanHealthTrace_Critical(TITAN_HT_WIRELESS_BOOTSTRAP_AFTER,
        g_wireless_frame, (uint32_t)bootstrap_result, 0u, 0u);

    /* P10.6.3: defer P10.5 persisted-bond IMPORT until BT RPC bind. */
    TitanHealthTrace_Critical(TITAN_HT_WIRELESS_BOND_BEFORE,
        g_wireless_frame, 0u, 0u, 0u);
    TitanPademuInputBridge_RequestBondRestore();
    TitanHealthTrace_Critical(TITAN_HT_WIRELESS_BOND_AFTER,
        g_wireless_frame, 0u, 0u, 0u);
    TitanHealthTrace_Mark(TITAN_HT_WIRELESS_END, g_wireless_frame,
        (uint32_t)request_result, (uint32_t)bootstrap_result, 0u);
}

void TitanWirelessAccessories_EndStartupReconnect(void)
{
    g_startup_reconnect_active = 0;
}

int TitanWirelessAccessories_IsStartupReconnectActive(void)
{
    return g_startup_reconnect_active;
}

void TitanWirelessAccessories_SetVisible(int visible)
{
    int next = visible ? 1 : 0;
    if (next && !g_wireless_visible)
        (void)TitanPademuInputBridge_RequestWireless(TITAN_PADEMU_WIRELESS_QUERY, 0u);
    g_wireless_visible = next;
}

void TitanWirelessAccessories_Update(void)
{
    const TitanPademuWirelessState *state;
    unsigned int connected_mask = 0u;
    unsigned int i;

    g_wireless_frame++;
    /* QUERY is asynchronous/coalesced by the bridge. Four snapshots per second
     * are enough for Settings and startup reconnect without a 60 Hz RPC burden. */
    if ((g_wireless_visible || g_connect_notification_pending || g_startup_reconnect_active) &&
        (g_wireless_frame % 15u) == 1u)
        (void)TitanPademuInputBridge_RequestWireless(TITAN_PADEMU_WIRELESS_QUERY, 0u);

    state = TitanPademuInputBridge_GetWirelessState();
    if (!state) return;
    for (i = 0u; i < state->count && i < TITAN_PADEMU_WIRELESS_MAX_ACCESSORIES; i++)
        if (state->accessories[i].flags & TITAN_PADEMU_WIRELESS_CONNECTED)
            connected_mask |= 1u << i;

    /* P10.6 startup reconnect uses only persisted/paired controller records.
     * It never issues BEGIN_SCAN and therefore never enters discovery/SSP just
     * because the console booted. One bounded Create Connection may be active. */
    if (g_startup_reconnect_active &&
        g_startup_reconnect_attempts < TITAN_STARTUP_RECONNECT_MAX_ATTEMPTS &&
        g_wireless_frame >= g_startup_reconnect_next_frame &&
        (state->flags & (TITAN_PADEMU_WIRELESS_SCANNING |
                         TITAN_PADEMU_WIRELESS_CONNECT_IN_PROGRESS)) == 0u)
    {
        for (i = 0u; i < state->count && i < TITAN_PADEMU_WIRELESS_MAX_ACCESSORIES; i++) {
            const TitanPademuWirelessAccessory *accessory = &state->accessories[i];
            if ((accessory->flags & TITAN_PADEMU_WIRELESS_PAIRED) == 0u ||
                (accessory->flags & TITAN_PADEMU_WIRELESS_CONTROLLER) == 0u ||
                (accessory->flags & (TITAN_PADEMU_WIRELESS_CONNECTED |
                                     TITAN_PADEMU_WIRELESS_CONNECTING)) != 0u)
                continue;
            if (TitanPademuInputBridge_RequestWireless(TITAN_PADEMU_WIRELESS_CONNECT, i)) {
                g_startup_reconnect_attempts++;
                g_startup_reconnect_next_frame = g_wireless_frame +
                    TITAN_STARTUP_RECONNECT_COOLDOWN_FRAMES;
            }
            break;
        }
    }

    if (g_connect_notification_pending) {
        const TitanPademuWirelessAccessory *pending = find_by_address(state, g_connect_address);
        if (pending && (pending->flags & TITAN_PADEMU_WIRELESS_CONNECTED)) {
            (void)TitanNotifications_Push(
                TITAN_NOTIFICATION_SUCCESS,
                accessory_name(pending),
                "Acessório conectado");
            g_connect_notification_pending = 0;
            g_connect_error_generation = 0u;
        } else if ((state->flags & TITAN_PADEMU_WIRELESS_HAS_ERROR) && state->last_error != 0 &&
                   state->generation != g_connect_error_generation) {
            char message[64];
            snprintf(message, sizeof(message), "Falha ao conectar (%d)", (int)state->last_error);
            (void)TitanNotifications_Push(TITAN_NOTIFICATION_ERROR, "Acessório Bluetooth", message);
            g_connect_error_generation = state->generation;
            g_connect_notification_pending = 0;
        }
    }

    g_last_generation = state->generation;
    g_last_connected_mask = connected_mask;
}

int TitanWirelessAccessories_BeginScan(void)
{
    g_connect_notification_pending = 0;
    g_connect_error_generation = 0u;
    return TitanPademuInputBridge_RequestWireless(TITAN_PADEMU_WIRELESS_BEGIN_SCAN, 0u);
}

int TitanWirelessAccessories_Connect(unsigned int index)
{
    const TitanPademuWirelessState *state = TitanWirelessAccessories_GetState();
    const TitanPademuWirelessAccessory *accessory;
    if (!state || index >= state->count || index >= TITAN_PADEMU_WIRELESS_MAX_ACCESSORIES) return 0;
    accessory = &state->accessories[index];
    memcpy(g_connect_address, accessory->address, sizeof(g_connect_address));
    g_connect_notification_pending = 1;
    g_connect_error_generation = state->generation;
    /* P10.5.4: repeated X/click while the same accessory is already in the
     * ACL/HID handshake is a no-op.  The IOP has the same defense, but keeping
     * this at the UX boundary avoids an unnecessary SIF RPC too. */
    if (accessory->flags & (TITAN_PADEMU_WIRELESS_CONNECTING | TITAN_PADEMU_WIRELESS_CONNECTED))
        return 1;
    if (!TitanPademuInputBridge_RequestWireless(TITAN_PADEMU_WIRELESS_CONNECT, index)) return 0;
    return 1;
}

int TitanWirelessAccessories_Disconnect(unsigned int index)
{
    g_connect_notification_pending = 0;
    return TitanPademuInputBridge_RequestWireless(TITAN_PADEMU_WIRELESS_DISCONNECT, index);
}

int TitanWirelessAccessories_Forget(unsigned int index)
{
    g_connect_notification_pending = 0;
    return TitanPademuInputBridge_RequestWireless(TITAN_PADEMU_WIRELESS_FORGET, index);
}

const TitanPademuWirelessState *TitanWirelessAccessories_GetState(void)
{
    return TitanPademuInputBridge_GetWirelessState();
}

unsigned int TitanWirelessAccessories_GetSeenCount(void)
{
    const TitanPademuWirelessState *state = TitanWirelessAccessories_GetState();
    unsigned int i, count = 0u;
    if (!state) return 0u;
    for (i = 0u; i < state->count; i++)
        if (state->accessories[i].flags & TITAN_PADEMU_WIRELESS_SEEN) count++;
    return count;
}

unsigned int TitanWirelessAccessories_GetRegisteredCount(void)
{
    const TitanPademuWirelessState *state = TitanWirelessAccessories_GetState();
    unsigned int i, count = 0u;
    if (!state) return 0u;
    for (i = 0u; i < state->count; i++)
        if (state->accessories[i].flags & (TITAN_PADEMU_WIRELESS_PAIRED | TITAN_PADEMU_WIRELESS_CONNECTED)) count++;
    return count;
}

const TitanPademuWirelessAccessory *TitanWirelessAccessories_GetSeen(unsigned int visible_index, unsigned int *source_index)
{
    const TitanPademuWirelessState *state = TitanWirelessAccessories_GetState();
    unsigned int i, count = 0u;
    if (!state) return 0;
    for (i = 0u; i < state->count; i++) {
        if (!(state->accessories[i].flags & TITAN_PADEMU_WIRELESS_SEEN)) continue;
        if (count++ == visible_index) { if (source_index) *source_index = i; return &state->accessories[i]; }
    }
    return 0;
}

const TitanPademuWirelessAccessory *TitanWirelessAccessories_GetRegistered(unsigned int visible_index, unsigned int *source_index)
{
    const TitanPademuWirelessState *state = TitanWirelessAccessories_GetState();
    unsigned int i, count = 0u;
    if (!state) return 0;
    for (i = 0u; i < state->count; i++) {
        if (!(state->accessories[i].flags & (TITAN_PADEMU_WIRELESS_PAIRED | TITAN_PADEMU_WIRELESS_CONNECTED))) continue;
        if (count++ == visible_index) { if (source_index) *source_index = i; return &state->accessories[i]; }
    }
    return 0;
}
