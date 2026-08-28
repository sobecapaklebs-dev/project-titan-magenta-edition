#include "titan_input.h"

#include "titan_iop_service_manager.h"
#include "titan_unified_input.h"
#include "titan_pademu_input_bridge.h"
#include "titan_bluetooth_runtime.h"
#include "titan_hid_input.h"
#include "titan_platform_overhead.h"
#include "titan_diagnostics.h"
#include "titan_time.h"

#include <kernel.h>
#include <libpad.h>
#include <loadfile.h>

#include <string.h>

#define TITAN_INPUT_DEFAULT_PORT 0
#define TITAN_INPUT_SLOT 0
#define TITAN_PAD_BUFFER_SIZE 256
#define TITAN_INPUT_ANALOG_CENTER 128
#define TITAN_INPUT_STEP_WARN_US 25000u
#define TITAN_INPUT_STEP_STALL_US 250000u

static void TitanInput_DiagSlowStep(uint32_t step, uint64_t started_us)
{
    uint64_t now = TitanTime_NowMicroseconds();
    uint32_t elapsed = now > started_us ? (uint32_t)(now - started_us) : 0u;
    if (elapsed < TITAN_INPUT_STEP_WARN_US) return;
    TitanDiagnostics_Event(
        elapsed >= TITAN_INPUT_STEP_STALL_US ? TITAN_DIAG_ERROR : TITAN_DIAG_WARN,
        TITAN_DIAG_INPUT, 0x0312u, 0, step, elapsed, 0u, 0u);
}


static unsigned char g_pad_buffers[TITAN_INPUT_PHYSICAL_PORTS][TITAN_PAD_BUFFER_SIZE]
    __attribute__((aligned(64)));
static int g_pad_opened[TITAN_INPUT_PHYSICAL_PORTS];
static int g_pad_rumble_ready[TITAN_INPUT_PHYSICAL_PORTS];
static TitanInputPadState g_pads[TITAN_INPUT_MAX_PADS];

static TitanInputState g_input;

static TitanInputProgressCallback
    g_progress_callback;

static void *g_progress_user_data;

static void TitanInput_ResetPadState(unsigned int index)
{
    TitanInputPadState *pad;
    if (index >= TITAN_INPUT_MAX_PADS) return;
    pad = &g_pads[index];
    memset(pad, 0, sizeof(*pad));
    pad->port = index < TITAN_INPUT_PHYSICAL_PORTS ? (int)index : -1;
    pad->slot = TITAN_INPUT_SLOT;
    pad->left_stick_x = TITAN_INPUT_ANALOG_CENTER;
    pad->left_stick_y = TITAN_INPUT_ANALOG_CENTER;
    pad->right_stick_x = TITAN_INPUT_ANALOG_CENTER;
    pad->right_stick_y = TITAN_INPUT_ANALOG_CENTER;
}

static void TitanInput_ResetAllPads(void)
{
    unsigned int index;
    memset(g_pad_buffers, 0, sizeof(g_pad_buffers));
    memset(g_pad_opened, 0, sizeof(g_pad_opened));
    memset(g_pad_rumble_ready, 0, sizeof(g_pad_rumble_ready));
    for (index = 0u; index < TITAN_INPUT_MAX_PADS; index++)
        TitanInput_ResetPadState(index);
}

static void TitanInput_MirrorActivePad(void)
{
    const TitanInputDeviceDescriptor *chosen = TitanUnifiedInput_GetPlayer(0u);
    const TitanInputPadState *pad;
    uint32_t newest_activity = 0u;
    unsigned int index, i;

    /* P10.6: explicit Player 1 owns shell input. If P1 is temporarily absent,
     * choose one connected gamepad as a fail-open fallback without changing
     * persistent assignment. Never OR several controllers into one UI stream. */
    if (chosen == 0 || !chosen->connected ||
        chosen->type == TITAN_INPUT_DEVICE_KEYBOARD || chosen->type == TITAN_INPUT_DEVICE_MOUSE)
    {
        chosen = 0;
        for (i = 0u; i < TitanUnifiedInput_GetDeviceCapacity(); ++i) {
            const TitanInputDeviceDescriptor *d = TitanUnifiedInput_GetDevice(i);
            if (!d || !d->connected || d->type == TITAN_INPUT_DEVICE_KEYBOARD ||
                d->type == TITAN_INPUT_DEVICE_MOUSE) continue;
            if (!chosen || d->activity_serial > newest_activity) {
                chosen = d;
                newest_activity = d->activity_serial;
            }
        }
    }

    if (chosen) {
        g_input.connected = 1;
        g_input.pad_state = 0;
        g_input.last_read_result = 1;
        g_input.main_mode_requested = 0;
        g_input.buttons = (u16)(chosen->state.buttons & 0xffffu);
        g_input.pressed = (u16)(chosen->state.pressed & 0xffffu);
        g_input.released = (u16)(chosen->state.released & 0xffffu);
        g_input.left_stick_x = chosen->state.left_x;
        g_input.left_stick_y = chosen->state.left_y;
        g_input.right_stick_x = chosen->state.right_x;
        g_input.right_stick_y = chosen->state.right_y;
        return;
    }

    /* Compatibility fallback for early bootstrap or a Unified Input failure. */
    index = (unsigned int)(g_input.active_port >= 0 ? g_input.active_port : 0);
    if (index >= TITAN_INPUT_PHYSICAL_PORTS) index = 0u;
    pad = &g_pads[index];
    g_input.connected = pad->connected;
    g_input.pad_state = pad->pad_state;
    g_input.last_read_result = pad->last_read_result;
    g_input.main_mode_requested = pad->main_mode_requested;
    g_input.buttons = pad->buttons;
    g_input.pressed = pad->pressed;
    g_input.released = pad->released;
    g_input.left_stick_x = pad->left_stick_x;
    g_input.left_stick_y = pad->left_stick_y;
    g_input.right_stick_x = pad->right_stick_x;
    g_input.right_stick_y = pad->right_stick_y;
}

static int TitanInput_OpenPhysicalPort(unsigned int port)
{
    int result;
    if (port >= TITAN_INPUT_PHYSICAL_PORTS) return 0;
    if (g_pad_opened[port]) return 1;
    memset(g_pad_buffers[port], 0, TITAN_PAD_BUFFER_SIZE);
    result = padPortOpen((int)port, TITAN_INPUT_SLOT, g_pad_buffers[port]);
    if (result == 0) return 0;
    g_pad_opened[port] = 1;
    TitanInput_ResetPadState(port);
    return 1;
}

static void TitanInput_NotifyProgress(
    TitanInputBootstrapStage stage,
    int detail
)
{
    g_input.bootstrap_stage = stage;
    g_input.bootstrap_detail = detail;

    if (g_progress_callback != 0)
    {
        g_progress_callback(
            g_progress_user_data
        );
    }
}

static TitanResult TitanInput_Fail(
    TitanResult result
)
{
    g_input.initialized = 0;
    g_input.connected = 0;

    g_input.initialization_result =
        result;

    TitanInput_NotifyProgress(
        TITAN_INPUT_BOOTSTRAP_FAILED,
        (int)result
    );

    return result;
}

static int TitanInput_LoadRomModule(
    const char *primary_path,
    const char *alternate_path,
    int *used_alternate
)
{
    int module_id;

    if (used_alternate != 0)
    {
        *used_alternate = 0;
    }

    module_id = SifLoadModule(
        primary_path,
        0,
        0
    );

    if (
        module_id < 0 &&
        alternate_path != 0
    )
    {
        module_id = SifLoadModule(
            alternate_path,
            0,
            0
        );

        if (module_id >= 0 && used_alternate != 0)
        {
            *used_alternate = 1;
        }
    }

    return module_id;
}

const char *TitanInput_GetBootstrapStageName(
    TitanInputBootstrapStage stage
)
{
    switch (stage)
    {
        case TITAN_INPUT_BOOTSTRAP_RESETTING_IOP:
            return "RESET IOP";

        case TITAN_INPUT_BOOTSTRAP_SYNCING_IOP:
            return "SYNC IOP";

        case TITAN_INPUT_BOOTSTRAP_INITIALIZING_LOADFILE:
            return "LOADFILE";

        case TITAN_INPUT_BOOTSTRAP_LOADING_SIO2MAN:
            return "SIO2MAN";

        case TITAN_INPUT_BOOTSTRAP_LOADING_PADMAN:
            return "PADMAN";

        case TITAN_INPUT_BOOTSTRAP_BINDING_PAD_RPC:
            return "PAD RPC";

        case TITAN_INPUT_BOOTSTRAP_OPENING_PORT:
            return "OPEN PORT";

        case TITAN_INPUT_BOOTSTRAP_READY:
            return "READY";

        case TITAN_INPUT_BOOTSTRAP_FAILED:
            return "FAILED";

        case TITAN_INPUT_BOOTSTRAP_IDLE:
        default:
            return "IDLE";
    }
}

static void TitanInput_OnIopProgress(
    TitanIopBootstrapStage stage,
    int detail,
    void *user_data
)
{
    TitanInputBootstrapStage input_stage;

    (void)user_data;
    input_stage = TITAN_INPUT_BOOTSTRAP_IDLE;

    switch (stage)
    {
        case TITAN_IOP_BOOTSTRAP_RESETTING:
            input_stage = TITAN_INPUT_BOOTSTRAP_RESETTING_IOP;
            break;
        case TITAN_IOP_BOOTSTRAP_SYNCING:
            input_stage = TITAN_INPUT_BOOTSTRAP_SYNCING_IOP;
            break;
        case TITAN_IOP_BOOTSTRAP_INITIALIZING_LOADFILE:
            input_stage = TITAN_INPUT_BOOTSTRAP_INITIALIZING_LOADFILE;
            break;
        case TITAN_IOP_BOOTSTRAP_READY:
            input_stage = TITAN_INPUT_BOOTSTRAP_INITIALIZING_LOADFILE;
            break;
        case TITAN_IOP_BOOTSTRAP_FAILED:
            input_stage = TITAN_INPUT_BOOTSTRAP_FAILED;
            break;
        case TITAN_IOP_BOOTSTRAP_IDLE:
        default:
            input_stage = TITAN_INPUT_BOOTSTRAP_IDLE;
            break;
    }

    TitanInput_NotifyProgress(input_stage, detail);
}

TitanResult TitanInput_InitializePreserveIop(
    TitanInputProgressCallback progress_callback,
    void *progress_user_data
)
{
    int result;

    memset(&g_input, 0, sizeof(g_input));
    TitanInput_ResetAllPads();
    TitanUnifiedInput_Initialize();
    TitanBluetoothRuntime_Initialize();
    TitanPademuInputBridge_Initialize();

    g_progress_callback = progress_callback;
    g_progress_user_data = progress_user_data;
    g_input.initialization_result = TITAN_RESULT_OK;
    g_input.bootstrap_stage = TITAN_INPUT_BOOTSTRAP_IDLE;
    g_input.sio2man_module_id = -1;
    g_input.padman_module_id = -1;
    g_input.left_stick_x = TITAN_INPUT_ANALOG_CENTER;
    g_input.left_stick_y = TITAN_INPUT_ANALOG_CENTER;
    g_input.right_stick_x = TITAN_INPUT_ANALOG_CENTER;
    g_input.right_stick_y = TITAN_INPUT_ANALOG_CENTER;
    g_input.active_port = TITAN_INPUT_DEFAULT_PORT;

    /* The launcher already owns a live IOP image. Do not call the global IOP
     * reset/synchronization service here: doing so tears down the very mass:
     * device that contains the installer/TINS. Bind directly to the resident
     * PADMAN RPC service instead. */
    g_input.iop_ready = 1;

    TitanInput_NotifyProgress(TITAN_INPUT_BOOTSTRAP_BINDING_PAD_RPC, 0);
    result = padInit(0);
    if (result == 0)
    {
        g_input.iop_ready = 0;
        return TitanInput_Fail(TITAN_ERROR_PAD_INITIALIZATION);
    }

    TitanInput_NotifyProgress(TITAN_INPUT_BOOTSTRAP_OPENING_PORT, 0);
    result = TitanInput_OpenPhysicalPort(0u);
    if (result == 0)
    {
        padEnd();
        g_input.iop_ready = 0;
        return TitanInput_Fail(TITAN_ERROR_PAD_PORT_OPEN);
    }
    /* Port 1 is optional: its absence must never make controller 1 unusable. */
    (void)TitanInput_OpenPhysicalPort(1u);

    g_input.initialized = 1;
    g_input.connected = 0;
    g_input.initialization_result = TITAN_RESULT_OK;
    TitanInput_NotifyProgress(TITAN_INPUT_BOOTSTRAP_READY, 0);
    return TITAN_RESULT_OK;
}

TitanResult TitanInput_Initialize(
    TitanInputProgressCallback progress_callback,
    void *progress_user_data
)
{
    int result;
    TitanResult iop_result;

    memset(&g_input, 0, sizeof(g_input));
    TitanInput_ResetAllPads();
    TitanUnifiedInput_Initialize();

    g_progress_callback = progress_callback;
    g_progress_user_data = progress_user_data;
    g_input.initialization_result = TITAN_RESULT_OK;
    g_input.bootstrap_stage = TITAN_INPUT_BOOTSTRAP_IDLE;
    g_input.sio2man_module_id = -1;
    g_input.padman_module_id = -1;
    g_input.left_stick_x = TITAN_INPUT_ANALOG_CENTER;
    g_input.left_stick_y = TITAN_INPUT_ANALOG_CENTER;
    g_input.right_stick_x = TITAN_INPUT_ANALOG_CENTER;
    g_input.right_stick_y = TITAN_INPUT_ANALOG_CENTER;
    g_input.active_port = TITAN_INPUT_DEFAULT_PORT;

    /*
     * IOP ownership belongs to TitanIopServiceManager. Input is merely one
     * dependent client. A PADMAN failure can no longer invalidate LOADFILE,
     * storage, audio, DEX or library scanning.
     */
    iop_result = TitanIopServiceManager_Initialize(
        TitanInput_OnIopProgress,
        0
    );
    g_input.iop_ready = TitanIopServiceManager_IsReady();

    if (iop_result != TITAN_RESULT_OK)
    {
        return TitanInput_Fail(iop_result);
    }

    TitanInput_NotifyProgress(TITAN_INPUT_BOOTSTRAP_LOADING_SIO2MAN, 0);
    g_input.sio2man_module_id = TitanInput_LoadRomModule(
        "rom0:SIO2MAN",
        "rom0:XSIO2MAN",
        &g_input.sio2man_uses_extended_module
    );

    if (g_input.sio2man_module_id < 0)
    {
        return TitanInput_Fail(TITAN_ERROR_SIO2MAN_LOAD);
    }

    TitanInput_NotifyProgress(TITAN_INPUT_BOOTSTRAP_LOADING_PADMAN, 0);
    g_input.padman_module_id = TitanInput_LoadRomModule(
        "rom0:PADMAN",
        "rom0:XPADMAN",
        0
    );

    if (g_input.padman_module_id < 0)
    {
        return TitanInput_Fail(TITAN_ERROR_PADMAN_LOAD);
    }

    TitanInput_NotifyProgress(TITAN_INPUT_BOOTSTRAP_BINDING_PAD_RPC, 0);
    result = padInit(0);

    if (result == 0)
    {
        return TitanInput_Fail(TITAN_ERROR_PAD_INITIALIZATION);
    }

    TitanInput_NotifyProgress(TITAN_INPUT_BOOTSTRAP_OPENING_PORT, 0);
    result = TitanInput_OpenPhysicalPort(0u);

    if (result == 0)
    {
        padEnd();
        return TitanInput_Fail(TITAN_ERROR_PAD_PORT_OPEN);
    }
    (void)TitanInput_OpenPhysicalPort(1u);

    g_input.initialized = 1;
    g_input.connected = 0;
    g_input.initialization_result = TITAN_RESULT_OK;
    TitanInput_NotifyProgress(TITAN_INPUT_BOOTSTRAP_READY, 0);
    return TITAN_RESULT_OK;
}

void TitanInput_Shutdown(void)
{
    unsigned int port;
    TitanInput_StopAllRumble();
    if (g_input.initialized)
    {
        for (port = 0u; port < TITAN_INPUT_PHYSICAL_PORTS; port++)
        {
            if (g_pad_opened[port])
                padPortClose((int)port, TITAN_INPUT_SLOT);
        }
        padEnd();
    }
    TitanInput_ResetAllPads();
    TitanHidInput_Shutdown();
    TitanPademuInputBridge_Shutdown();
    TitanUnifiedInput_Shutdown();
    g_input.initialized = 0;
    g_input.connected = 0;
    g_input.buttons = 0;
    g_input.pressed = 0;
    g_input.released = 0;
    g_input.main_mode_requested = 0;
}

static void TitanInput_UpdatePhysicalPort(unsigned int port)
{
    struct padButtonStatus pad_status;
    TitanInputPadState *pad;
    u16 previous_buttons;
    u16 current_buttons;
    int state;
    int read_result;
    int was_connected;

    if (port >= TITAN_INPUT_PHYSICAL_PORTS) return;
    pad = &g_pads[port];
    pad->pressed = 0;
    pad->released = 0;
    was_connected = pad->connected;
    if (!g_pad_opened[port]) return;

    state = padGetState((int)port, TITAN_INPUT_SLOT);
    pad->pad_state = state;
    if (state == PAD_STATE_DISCONN || state == PAD_STATE_ERROR)
    {
        if (was_connected) pad->released = pad->buttons;
        pad->connected = 0; pad->buttons = 0; pad->last_read_result = 0;
        pad->main_mode_requested = 0;
        pad->left_stick_x = pad->left_stick_y = TITAN_INPUT_ANALOG_CENTER;
        pad->right_stick_x = pad->right_stick_y = TITAN_INPUT_ANALOG_CENTER;
        return;
    }
    if (state != PAD_STATE_STABLE && state != PAD_STATE_FINDCTP1)
    {
        pad->last_read_result = 0;
        return;
    }
    if (!pad->main_mode_requested &&
        padSetMainMode((int)port, TITAN_INPUT_SLOT, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK) != 0)
        pad->main_mode_requested = 1;

    if (!g_pad_rumble_ready[port] && padInfoAct((int)port, TITAN_INPUT_SLOT, -1, 0) >= 2)
    {
        unsigned char alignment[6] = { 0u, 1u, 0xffu, 0xffu, 0xffu, 0xffu };
        if (padSetActAlign((int)port, TITAN_INPUT_SLOT, alignment) != 0)
            g_pad_rumble_ready[port] = 1;
    }

    memset(&pad_status, 0, sizeof(pad_status));
    read_result = padRead((int)port, TITAN_INPUT_SLOT, &pad_status);
    pad->last_read_result = read_result;
    if (read_result == 0)
    {
        if (was_connected) pad->released = pad->buttons;
        pad->connected = 0; pad->buttons = 0; pad->main_mode_requested = 0;
        pad->left_stick_x = pad->left_stick_y = TITAN_INPUT_ANALOG_CENTER;
        pad->right_stick_x = pad->right_stick_y = TITAN_INPUT_ANALOG_CENTER;
        return;
    }

    previous_buttons = pad->buttons;
    current_buttons = (u16)(0xFFFFu ^ pad_status.btns);
    pad->buttons = current_buttons;
    if (!was_connected)
    {
        pad->pressed = 0; pad->released = 0;
    }
    else
    {
        pad->pressed = (u16)(current_buttons & (u16)(~previous_buttons));
        pad->released = (u16)(previous_buttons & (u16)(~current_buttons));
    }

    if ((pad_status.mode & 0xF0u) == 0x70u)
    {
        pad->left_stick_x = pad_status.ljoy_h; pad->left_stick_y = pad_status.ljoy_v;
        pad->right_stick_x = pad_status.rjoy_h; pad->right_stick_y = pad_status.rjoy_v;
    }
    else
    {
        pad->left_stick_x = pad->left_stick_y = TITAN_INPUT_ANALOG_CENTER;
        pad->right_stick_x = pad->right_stick_y = TITAN_INPUT_ANALOG_CENTER;
    }
    pad->connected = 1;
}

void TitanInput_Update(void)
{
    unsigned int port;
    uint64_t overhead_token = TitanOverhead_Begin(TITAN_OVERHEAD_INPUT, g_input.initialized != 0);
    g_input.pressed = 0;
    g_input.released = 0;
    if (!g_input.initialized) { TitanOverhead_End(TITAN_OVERHEAD_INPUT, overhead_token, 0); return; }
    { uint64_t started = TitanTime_NowMicroseconds(); TitanUnifiedInput_BeginFrame(); TitanInput_DiagSlowStep(1u, started); }
    { uint64_t started = TitanTime_NowMicroseconds(); TitanPademuInputBridge_BeginFrame(); TitanInput_DiagSlowStep(2u, started); }
    for (port = 0u; port < TITAN_INPUT_PHYSICAL_PORTS; port++) {
        uint64_t started = TitanTime_NowMicroseconds();
        TitanInput_UpdatePhysicalPort(port);
        /* A real SIO2 pad is always submitted. PADEMU transport-only devices
         * are additional Unified Input devices and cannot consume this source. */
        TitanUnifiedInput_SubmitLegacyPad(port, g_pads[port].connected, g_pads[port].buttons,
                                          g_pads[port].left_stick_x, g_pads[port].left_stick_y,
                                          g_pads[port].right_stick_x, g_pads[port].right_stick_y);
        (void)TitanPademuInputBridge_SubmitPort(port, g_pads[port].connected, g_pads[port].buttons,
                                               g_pads[port].left_stick_x, g_pads[port].left_stick_y,
                                               g_pads[port].right_stick_x, g_pads[port].right_stick_y);
        TitanInput_DiagSlowStep(10u + port, started);
    }
    { uint64_t started = TitanTime_NowMicroseconds(); TitanHidInput_Update(); TitanInput_DiagSlowStep(20u, started); }
    { uint64_t started = TitanTime_NowMicroseconds(); TitanInput_MirrorActivePad(); TitanInput_DiagSlowStep(21u, started); }
    TitanOverhead_End(TITAN_OVERHEAD_INPUT, overhead_token, 1);
}

const TitanInputState *TitanInput_GetState(void)
{
    return &g_input;
}

const TitanInputPadState *TitanInput_GetPadState(unsigned int index)
{
    if (index >= TITAN_INPUT_MAX_PADS) return 0;
    return &g_pads[index];
}

unsigned int TitanInput_GetConnectedPadCount(void)
{
    unsigned int index;
    unsigned int count = 0u;
    for (index = 0u; index < TITAN_INPUT_MAX_PADS; index++)
        if (g_pads[index].connected) count++;
    return count;
}

unsigned int TitanInput_GetPadCapacity(void)
{
    return TITAN_INPUT_MAX_PADS;
}

int TitanInput_SetRumble(
    unsigned int index,
    unsigned char small_motor,
    unsigned char large_motor
)
{
    unsigned char direct[6] = { 0u, 0u, 0u, 0u, 0u, 0u };
    if (index >= TITAN_INPUT_PHYSICAL_PORTS || !g_pad_opened[index] ||
        !g_pads[index].connected || !g_pad_rumble_ready[index])
        return 0;
    direct[0] = small_motor != 0u ? 1u : 0u;
    direct[1] = large_motor;
    return padSetActDirect((int)index, TITAN_INPUT_SLOT, direct) != 0;
}

void TitanInput_StopAllRumble(void)
{
    unsigned int index;
    unsigned char direct[6] = { 0u, 0u, 0u, 0u, 0u, 0u };
    for (index = 0u; index < TITAN_INPUT_PHYSICAL_PORTS; index++)
        if (g_pad_opened[index] && g_pad_rumble_ready[index])
            (void)padSetActDirect((int)index, TITAN_INPUT_SLOT, direct);
}

int TitanInput_IsIopReady(void)
{
    return g_input.iop_ready;
}

int TitanInput_UsesExtendedModules(void)
{
    return g_input.sio2man_uses_extended_module;
}

int TitanInput_GetPort(void)
{
    return g_input.active_port;
}

int TitanInput_SetPort(int port)
{
    if (!g_input.initialized || (port != 0 && port != 1)) return 0;
    if (!g_pad_opened[(unsigned int)port] && !TitanInput_OpenPhysicalPort((unsigned int)port))
        return 0;
    g_input.active_port = port;
    TitanHidInput_Update();
    TitanInput_MirrorActivePad();
    return 1;
}

int TitanInput_IsDown(
    TitanInputButton button
)
{
    return
        (
            g_input.buttons &
            (u16)button
        ) != 0;
}

int TitanInput_WasPressed(
    TitanInputButton button
)
{
    return
        (
            g_input.pressed &
            (u16)button
        ) != 0;
}

int TitanInput_WasReleased(
    TitanInputButton button
)
{
    return
        (
            g_input.released &
            (u16)button
        ) != 0;
}
