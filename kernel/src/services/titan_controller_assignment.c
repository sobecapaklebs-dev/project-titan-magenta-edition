#include "titan_controller_assignment.h"
#include "titan_storage_io.h"

#include <string.h>
#include <stddef.h>

#if defined(__EE__) || defined(_EE)
#include <kernel.h>
#endif

#define TITAN_CONTROLLER_ASSIGNMENT_MAGIC 0x54434131u /* "TCA1" */
#define TITAN_CONTROLLER_ASSIGNMENT_PATH "mass:/TITAN/settings/controller_assignment.bin"
#define TITAN_CONTROLLER_ASSIGNMENT_TEMP "mass:/TITAN/settings/controller_assignment.tmp"
#define TITAN_CONTROLLER_ASSIGNMENT_BACKUP "mass:/TITAN/settings/controller_assignment.bak"
#define TITAN_CONTROLLER_ASSIGNMENT_ROOT "mass:/TITAN"
#define TITAN_CONTROLLER_ASSIGNMENT_DIR "mass:/TITAN/settings"

typedef struct TitanControllerAssignmentBindingWire
{
    uint32_t stable_key;
    uint8_t type;
    uint8_t transport;
    uint8_t valid;
    uint8_t reserved;
} TitanControllerAssignmentBindingWire;

typedef struct TitanControllerAssignmentWire
{
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t sequence;
    TitanControllerAssignmentBindingWire players[TITAN_CONTROLLER_ASSIGNMENT_PLAYER_COUNT];
    uint8_t multitap_enabled;
    uint8_t multitap_port;
    uint8_t reserved[2];
    uint32_t checksum;
} TitanControllerAssignmentWire;

typedef struct TitanControllerAssignmentState
{
    TitanControllerAssignmentBinding bindings[TITAN_CONTROLLER_ASSIGNMENT_PLAYER_COUNT];
    uint8_t multitap_enabled;
    uint8_t multitap_port;
    volatile uint32_t generation;
    volatile uint32_t persisted_generation;
    uint32_t sequence;
    int initialized;
} TitanControllerAssignmentState;

static TitanControllerAssignmentState g_assignment;

static int assignment_lock(void)
{
#if defined(__EE__) || defined(_EE)
    return DIntr();
#else
    return 0;
#endif
}

static void assignment_unlock(int state)
{
#if defined(__EE__) || defined(_EE)
    if (state != 0) EIntr();
#else
    (void)state;
#endif
}

static uint32_t assignment_checksum(const void *data, unsigned int size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t hash = 2166136261u;
    unsigned int i;
    for (i = 0u; i < size; i++) {
        hash ^= (uint32_t)bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static int assignment_is_gamepad_type(TitanInputDeviceType type)
{
    return type != TITAN_INPUT_DEVICE_NONE &&
        type != TITAN_INPUT_DEVICE_KEYBOARD &&
        type != TITAN_INPUT_DEVICE_MOUSE;
}

static int assignment_is_modern_type(TitanInputDeviceType type)
{
    return type == TITAN_INPUT_DEVICE_DUALSHOCK3 ||
        type == TITAN_INPUT_DEVICE_DUALSHOCK4 ||
        type == TITAN_INPUT_DEVICE_DUALSENSE ||
        type == TITAN_INPUT_DEVICE_XBOX_ONE ||
        type == TITAN_INPUT_DEVICE_XBOX_SERIES ||
        type == TITAN_INPUT_DEVICE_GENERIC_HID;
}

static int assignment_matches(const TitanControllerAssignmentBinding *binding,
                              const TitanInputDeviceDescriptor *device)
{
    return binding != 0 && binding->valid && device != 0 && device->connected &&
        binding->stable_key == device->stable_key &&
        binding->type == device->type &&
        binding->transport == device->transport;
}

static const TitanInputDeviceDescriptor *assignment_find_binding(
    const TitanControllerAssignmentBinding *binding)
{
    unsigned int i;
    if (binding == 0 || !binding->valid) return 0;
    for (i = 0u; i < TitanUnifiedInput_GetDeviceCapacity(); i++) {
        const TitanInputDeviceDescriptor *device = TitanUnifiedInput_GetDevice(i);
        if (assignment_matches(binding, device)) return device;
    }
    return 0;
}

static void assignment_mark_dirty_locked(void)
{
    g_assignment.generation++;
    if (g_assignment.generation == 0u) g_assignment.generation = 1u;
}

static void assignment_defaults(void)
{
    unsigned int i;
    memset(&g_assignment, 0, sizeof(g_assignment));
    for (i = 0u; i < TITAN_CONTROLLER_ASSIGNMENT_PLAYER_COUNT; i++) {
        g_assignment.bindings[i].type = TITAN_INPUT_DEVICE_NONE;
        g_assignment.bindings[i].transport = TITAN_INPUT_TRANSPORT_NONE;
    }
    g_assignment.multitap_port = 0u;
    g_assignment.generation = 1u;
    g_assignment.persisted_generation = 1u;
    g_assignment.sequence = 1u;
}

static int assignment_wire_valid(const TitanControllerAssignmentWire *wire)
{
    uint32_t expected;
    unsigned int i;
    if (wire == 0 || wire->magic != TITAN_CONTROLLER_ASSIGNMENT_MAGIC ||
        wire->version != TITAN_CONTROLLER_ASSIGNMENT_VERSION ||
        wire->size != (uint16_t)sizeof(*wire) || wire->multitap_enabled > 1u ||
        wire->multitap_port > 1u)
        return 0;
    expected = assignment_checksum(wire, (unsigned int)offsetof(TitanControllerAssignmentWire, checksum));
    if (expected != wire->checksum) return 0;
    for (i = 0u; i < TITAN_CONTROLLER_ASSIGNMENT_PLAYER_COUNT; i++) {
        const TitanControllerAssignmentBindingWire *b = &wire->players[i];
        if (b->valid > 1u) return 0;
        if (b->valid && (!assignment_is_gamepad_type((TitanInputDeviceType)b->type) ||
            b->transport == TITAN_INPUT_TRANSPORT_NONE)) return 0;
    }
    return 1;
}

static int assignment_read_wire(const char *path, TitanControllerAssignmentWire *wire)
{
    int fd;
    int ok;
    fd = TitanStorageIo_Open(path, TITAN_STORAGE_IO_READ);
    if (fd < 0) return 0;
    ok = TitanStorageIo_ReadExact(fd, wire, sizeof(*wire));
    TitanStorageIo_Close(fd);
    return ok && assignment_wire_valid(wire);
}

static void assignment_apply_wire(const TitanControllerAssignmentWire *wire)
{
    unsigned int i;
    for (i = 0u; i < TITAN_CONTROLLER_ASSIGNMENT_PLAYER_COUNT; i++) {
        g_assignment.bindings[i].stable_key = wire->players[i].stable_key;
        g_assignment.bindings[i].type = (TitanInputDeviceType)wire->players[i].type;
        g_assignment.bindings[i].transport = (TitanInputTransport)wire->players[i].transport;
        g_assignment.bindings[i].valid = wire->players[i].valid;
    }
    g_assignment.multitap_enabled = wire->multitap_enabled;
    g_assignment.multitap_port = wire->multitap_port;
    g_assignment.sequence = wire->sequence ? wire->sequence : 1u;
    g_assignment.generation = 1u;
    g_assignment.persisted_generation = 1u;
}

void TitanControllerAssignment_Initialize(void)
{
    TitanControllerAssignmentWire wire;
    assignment_defaults();
    memset(&wire, 0, sizeof(wire));
    if (assignment_read_wire(TITAN_CONTROLLER_ASSIGNMENT_PATH, &wire) ||
        assignment_read_wire(TITAN_CONTROLLER_ASSIGNMENT_BACKUP, &wire))
        assignment_apply_wire(&wire);
    g_assignment.initialized = 1;
}

void TitanControllerAssignment_Update(void)
{
    unsigned int player;
    if (!g_assignment.initialized) return;
    for (player = 0u; player < TITAN_CONTROLLER_ASSIGNMENT_PLAYER_COUNT; player++) {
        TitanControllerAssignmentBinding binding;
        const TitanInputDeviceDescriptor *device;
        int state = assignment_lock();
        binding = g_assignment.bindings[player];
        assignment_unlock(state);
        if (!binding.valid) continue;
        device = assignment_find_binding(&binding);
        if (device != 0 && device->player != player)
            (void)TitanUnifiedInput_AssignPlayer(device->device_id, player);
    }
}

int TitanControllerAssignment_AssignDevice(unsigned int player, uint32_t device_id)
{
    const TitanInputDeviceDescriptor *device;
    unsigned int i;
    int state;
    if (player >= TITAN_CONTROLLER_ASSIGNMENT_PLAYER_COUNT) return 0;
    device = TitanUnifiedInput_GetDeviceById(device_id);
    if (device == 0 || !assignment_is_gamepad_type(device->type)) return 0;
    state = assignment_lock();
    for (i = 0u; i < TITAN_CONTROLLER_ASSIGNMENT_PLAYER_COUNT; i++) {
        if (i != player && assignment_matches(&g_assignment.bindings[i], device))
            memset(&g_assignment.bindings[i], 0, sizeof(g_assignment.bindings[i]));
    }
    g_assignment.bindings[player].stable_key = device->stable_key;
    g_assignment.bindings[player].type = device->type;
    g_assignment.bindings[player].transport = device->transport;
    g_assignment.bindings[player].valid = 1u;
    assignment_mark_dirty_locked();
    assignment_unlock(state);
    return TitanUnifiedInput_AssignPlayer(device_id, player);
}

int TitanControllerAssignment_SetAutomatic(unsigned int player)
{
    int state;
    if (player >= TITAN_CONTROLLER_ASSIGNMENT_PLAYER_COUNT) return 0;
    state = assignment_lock();
    memset(&g_assignment.bindings[player], 0, sizeof(g_assignment.bindings[player]));
    assignment_mark_dirty_locked();
    assignment_unlock(state);
    return 1;
}

int TitanControllerAssignment_CyclePlayer(unsigned int player)
{
    TitanControllerAssignmentBinding current;
    int found_current = 0;
    unsigned int i;
    if (player >= TITAN_CONTROLLER_ASSIGNMENT_PLAYER_COUNT) return 0;
    (void)TitanControllerAssignment_GetBinding(player, &current);
    for (i = 0u; i < TitanUnifiedInput_GetDeviceCapacity(); i++) {
        const TitanInputDeviceDescriptor *device = TitanUnifiedInput_GetDevice(i);
        if (device == 0 || !device->connected || !assignment_is_gamepad_type(device->type)) continue;
        if (!current.valid)
            return TitanControllerAssignment_AssignDevice(player, device->device_id);
        if (found_current)
            return TitanControllerAssignment_AssignDevice(player, device->device_id);
        if (assignment_matches(&current, device)) found_current = 1;
    }
    return TitanControllerAssignment_SetAutomatic(player);
}

int TitanControllerAssignment_IsAutomatic(unsigned int player)
{
    TitanControllerAssignmentBinding binding;
    if (!TitanControllerAssignment_GetBinding(player, &binding)) return 1;
    return !binding.valid;
}

int TitanControllerAssignment_GetBinding(unsigned int player, TitanControllerAssignmentBinding *binding)
{
    int state;
    if (player >= TITAN_CONTROLLER_ASSIGNMENT_PLAYER_COUNT || binding == 0) return 0;
    state = assignment_lock();
    *binding = g_assignment.bindings[player];
    assignment_unlock(state);
    return 1;
}

const TitanInputDeviceDescriptor *TitanControllerAssignment_GetPreferredConnected(unsigned int player)
{
    TitanControllerAssignmentBinding binding;
    if (!TitanControllerAssignment_GetBinding(player, &binding) || !binding.valid) return 0;
    return assignment_find_binding(&binding);
}

const TitanInputDeviceDescriptor *TitanControllerAssignment_GetWelcomeCandidate(void)
{
    const TitanInputDeviceDescriptor *preferred = TitanControllerAssignment_GetPreferredConnected(0u);
    return preferred != 0 ? preferred : TitanUnifiedInput_GetWelcomeCandidate();
}

int TitanControllerAssignment_SetMultitapEnabled(int enabled)
{
    uint8_t value = enabled ? 1u : 0u;
    int state = assignment_lock();
    if (g_assignment.multitap_enabled != value) {
        g_assignment.multitap_enabled = value;
        assignment_mark_dirty_locked();
    }
    assignment_unlock(state);
    return 1;
}

int TitanControllerAssignment_GetMultitapEnabled(void)
{
    return g_assignment.multitap_enabled != 0u;
}

int TitanControllerAssignment_SetMultitapPort(unsigned int port)
{
    int state;
    if (port > 1u) return 0;
    state = assignment_lock();
    if (g_assignment.multitap_port != (uint8_t)port) {
        g_assignment.multitap_port = (uint8_t)port;
        assignment_mark_dirty_locked();
    }
    assignment_unlock(state);
    return 1;
}

unsigned int TitanControllerAssignment_GetMultitapPort(void)
{
    return (unsigned int)g_assignment.multitap_port;
}

int TitanControllerAssignment_GetGameplayPademuMode(void)
{
    const TitanInputDeviceDescriptor *device = TitanControllerAssignment_GetPreferredConnected(0u);
    if (device == 0) device = TitanUnifiedInput_GetPlayer(0u);
    if (device == 0 || !assignment_is_modern_type(device->type)) return -1;
    if (device->transport == TITAN_INPUT_TRANSPORT_BLUETOOTH) return 1;
    if (device->transport == TITAN_INPUT_TRANSPORT_USB) return 0;
    return -1;
}

int TitanControllerAssignment_HasStorageWork(void)
{
    return g_assignment.initialized && g_assignment.generation != g_assignment.persisted_generation;
}

static void assignment_build_wire(TitanControllerAssignmentWire *wire, uint32_t sequence)
{
    unsigned int i;
    memset(wire, 0, sizeof(*wire));
    wire->magic = TITAN_CONTROLLER_ASSIGNMENT_MAGIC;
    wire->version = TITAN_CONTROLLER_ASSIGNMENT_VERSION;
    wire->size = (uint16_t)sizeof(*wire);
    wire->sequence = sequence;
    for (i = 0u; i < TITAN_CONTROLLER_ASSIGNMENT_PLAYER_COUNT; i++) {
        wire->players[i].stable_key = g_assignment.bindings[i].stable_key;
        wire->players[i].type = (uint8_t)g_assignment.bindings[i].type;
        wire->players[i].transport = (uint8_t)g_assignment.bindings[i].transport;
        wire->players[i].valid = g_assignment.bindings[i].valid;
    }
    wire->multitap_enabled = g_assignment.multitap_enabled;
    wire->multitap_port = g_assignment.multitap_port;
    wire->checksum = assignment_checksum(wire, (unsigned int)offsetof(TitanControllerAssignmentWire, checksum));
}

int TitanControllerAssignment_StorageWorkerPump(void)
{
    TitanControllerAssignmentWire wire, verify;
    uint32_t snapshot_generation;
    uint32_t sequence;
    int state;
    int fd;
    if (!TitanControllerAssignment_HasStorageWork()) return 0;

    state = assignment_lock();
    snapshot_generation = g_assignment.generation;
    sequence = g_assignment.sequence + 1u;
    assignment_build_wire(&wire, sequence);
    assignment_unlock(state);

    (void)TitanStorageIo_CreateDirectory(TITAN_CONTROLLER_ASSIGNMENT_ROOT);
    (void)TitanStorageIo_CreateDirectory(TITAN_CONTROLLER_ASSIGNMENT_DIR);
    TitanStorageIo_Remove(TITAN_CONTROLLER_ASSIGNMENT_TEMP);
    fd = TitanStorageIo_Open(TITAN_CONTROLLER_ASSIGNMENT_TEMP,
        TITAN_STORAGE_IO_WRITE | TITAN_STORAGE_IO_CREATE | TITAN_STORAGE_IO_TRUNCATE);
    if (fd < 0) return 0;
    if (!TitanStorageIo_WriteExact(fd, &wire, sizeof(wire)) ||
        TitanStorageIo_Flush(fd) < 0 || TitanStorageIo_Close(fd) < 0) {
        TitanStorageIo_Close(fd);
        TitanStorageIo_Remove(TITAN_CONTROLLER_ASSIGNMENT_TEMP);
        return 0;
    }
    if (!TitanStorageIo_AtomicReplace(TITAN_CONTROLLER_ASSIGNMENT_TEMP,
            TITAN_CONTROLLER_ASSIGNMENT_PATH, TITAN_CONTROLLER_ASSIGNMENT_BACKUP)) {
        TitanStorageIo_Remove(TITAN_CONTROLLER_ASSIGNMENT_TEMP);
        return 0;
    }
    memset(&verify, 0, sizeof(verify));
    if (!assignment_read_wire(TITAN_CONTROLLER_ASSIGNMENT_PATH, &verify) ||
        verify.sequence != wire.sequence) {
        TitanStorageIo_Remove(TITAN_CONTROLLER_ASSIGNMENT_PATH);
        (void)TitanStorageIo_Rename(TITAN_CONTROLLER_ASSIGNMENT_BACKUP,
                                    TITAN_CONTROLLER_ASSIGNMENT_PATH);
        return 0;
    }
    TitanStorageIo_Remove(TITAN_CONTROLLER_ASSIGNMENT_BACKUP);

    state = assignment_lock();
    if (g_assignment.generation == snapshot_generation) {
        g_assignment.persisted_generation = snapshot_generation;
        g_assignment.sequence = sequence;
    }
    assignment_unlock(state);
    return 1;
}
