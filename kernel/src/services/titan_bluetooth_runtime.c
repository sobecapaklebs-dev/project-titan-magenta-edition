#include "titan_bluetooth_runtime.h"
#include "titan_early_input.h"
#include "titan_iop_modules.h"
#include "titan_early_input_wire.h"
#include "titan_diagnostics.h"
#include <kernel.h>
#include <loadfile.h>
#include <string.h>

#define TITAN_BT_RUNTIME_STACK_BYTES (16u * 1024u)
#define TITAN_BT_RUNTIME_PRIORITY 61

#define TITAN_BT_RUNTIME_IMAGE_EVENT 0x0317u
#define TITAN_BT_RUNTIME_FINGERPRINT 0x50313034u /* "P104" */

static uint32_t bt_runtime_fnv1a32(const unsigned char *data, unsigned int size)
{
    uint32_t h = 2166136261u;
    unsigned int i;
    if (!data) return 0u;
    for (i = 0; i < size; i++) {
        h ^= (uint32_t)data[i];
        h *= 16777619u;
    }
    return h;
}

static void bt_runtime_image_diag(void)
{
    TitanDiagnostics_Event(
        TITAN_DIAG_INFO, TITAN_DIAG_INPUT, TITAN_BT_RUNTIME_IMAGE_EVENT, 0,
        (uint32_t)size_titan_bt_pademu_irx,
        bt_runtime_fnv1a32(titan_bt_pademu_irx, size_titan_bt_pademu_irx),
        TITAN_BT_RUNTIME_FINGERPRINT,
        (size_titan_bt_pademu_irx >= 4u)
            ? ((uint32_t)titan_bt_pademu_irx[0] | ((uint32_t)titan_bt_pademu_irx[1] << 8) |
               ((uint32_t)titan_bt_pademu_irx[2] << 16) | ((uint32_t)titan_bt_pademu_irx[3] << 24))
            : 0u);
}

static TitanBluetoothRuntimeStatus g_bt_runtime;
#if defined(__EE__) || defined(_EE)
extern void *_gp;
static unsigned char g_bt_runtime_stack[TITAN_BT_RUNTIME_STACK_BYTES] __attribute__((aligned(16)));
static volatile int g_bt_runtime_worker_active;
#endif

static void bt_runtime_diag(void)
{
    TitanDiagnostics_Event(
        g_bt_runtime.state == TITAN_BT_RUNTIME_FAILED ? TITAN_DIAG_ERROR : TITAN_DIAG_INFO,
        TITAN_DIAG_INPUT, 0x0316u, (int32_t)g_bt_runtime.last_module_result,
        (uint32_t)g_bt_runtime.state, (uint32_t)g_bt_runtime.smsutils_module_id,
        (uint32_t)g_bt_runtime.pademu_module_id, g_bt_runtime.requests);
}

static unsigned int bt_runtime_make_args(char *out, unsigned int cap)
{
    uint32_t settings = 3u | (3u << 8) | TITAN_PADEMU_SETTINGS_TITAN_TRANSPORT_ONLY;
    uint32_t macro = 0xABu;
    TitanPademuBootExtension ext;
    unsigned int n = 8u + sizeof(ext);
    if (cap < n) return 0u;
    memset(out, 0, n);
    memcpy(out, &settings, 4);
    memcpy(out + 4, &macro, 4);
    memset(&ext, 0, sizeof(ext));
    ext.magic = TITAN_PADEMU_BOOT_EXT_MAGIC;
    memcpy(out + 8, &ext, sizeof(ext));
    return n;
}

#if defined(__EE__) || defined(_EE)
static int bt_runtime_exec(const unsigned char *image, unsigned int size,
                           int arg_len, const char *args, int *module_result)
{
    int result = -1;
    int id = SifExecModuleBuffer((void *)image, (int)size, arg_len, args, &result);
    if (module_result) *module_result = result;
    return (id >= 0 && result >= 0) ? id : -1;
}

static void bt_runtime_worker(void *arg)
{
    const TitanEarlyInputStatus *early;
    char args[96];
    unsigned int arg_len;
    (void)arg;

    early = TitanEarlyInput_GetStatus();
    if (early && early->smsutils_module_id >= 0)
        g_bt_runtime.smsutils_module_id = early->smsutils_module_id;
    else
        g_bt_runtime.smsutils_module_id = bt_runtime_exec(
            titan_smsutils_irx, size_titan_smsutils_irx, 0, 0,
            &g_bt_runtime.last_module_result);

    if (g_bt_runtime.smsutils_module_id < 0) {
        g_bt_runtime.state = TITAN_BT_RUNTIME_FAILED;
        g_bt_runtime_worker_active = 0;
        bt_runtime_diag();
        ExitDeleteThread();
        return;
    }

    arg_len = bt_runtime_make_args(args, sizeof(args));
    if (!arg_len) {
        g_bt_runtime.last_module_result = -2;
        g_bt_runtime.state = TITAN_BT_RUNTIME_FAILED;
        g_bt_runtime_worker_active = 0;
        bt_runtime_diag();
        ExitDeleteThread();
        return;
    }

    /* Log identity of the exact embedded bytes immediately before LOADFILE.
     * This makes stale staging impossible to confuse with an HCI bug. */
    bt_runtime_image_diag();
    g_bt_runtime.pademu_module_id = bt_runtime_exec(
        titan_bt_pademu_irx, size_titan_bt_pademu_irx,
        (int)arg_len, args, &g_bt_runtime.last_module_result);
    g_bt_runtime.state = g_bt_runtime.pademu_module_id >= 0
        ? TITAN_BT_RUNTIME_READY : TITAN_BT_RUNTIME_FAILED;
    g_bt_runtime_worker_active = 0;
    bt_runtime_diag();
    ExitDeleteThread();
}

static int bt_runtime_start_worker(void)
{
    ee_thread_t t;
    int tid;
    memset(&t, 0, sizeof(t));
    t.func = bt_runtime_worker;
    t.stack = g_bt_runtime_stack;
    t.stack_size = sizeof(g_bt_runtime_stack);
    t.gp_reg = &_gp;
    t.initial_priority = TITAN_BT_RUNTIME_PRIORITY;
    tid = CreateThread(&t);
    if (tid < 0) return 0;
    g_bt_runtime_worker_active = 1;
    if (StartThread(tid, 0) < 0) {
        g_bt_runtime_worker_active = 0;
        DeleteThread(tid);
        return 0;
    }
    return 1;
}
#endif

void TitanBluetoothRuntime_Initialize(void)
{
    memset(&g_bt_runtime, 0, sizeof(g_bt_runtime));
    g_bt_runtime.smsutils_module_id = -1;
    g_bt_runtime.pademu_module_id = -1;
#if defined(__EE__) || defined(_EE)
    g_bt_runtime_worker_active = 0;
#endif
}

int TitanBluetoothRuntime_Request(void)
{
    const TitanEarlyInputStatus *early = TitanEarlyInput_GetStatus();
    if (g_bt_runtime.requests < 0xffffffffu) g_bt_runtime.requests++;

    if (g_bt_runtime.state == TITAN_BT_RUNTIME_READY) return 1;
    if (g_bt_runtime.state == TITAN_BT_RUNTIME_LOADING) return 1;
    if (g_bt_runtime.state == TITAN_BT_RUNTIME_FAILED) return 0;

    /* Never race the Early Input LOADFILE worker for SMSUTILS ownership. */
    if (early && early->enabled &&
        (early->state == TITAN_EARLY_INPUT_STATE_STARTING ||
         early->state == TITAN_EARLY_INPUT_STATE_LOADING)) {
        g_bt_runtime.state = TITAN_BT_RUNTIME_WAIT_EARLY;
        return 1;
    }

    /* If Early Input already owns the BT transport, reuse that exact module.
     * Its P10.2 build registers the dedicated BT RPC id. */
    if (early && early->backend == TITAN_EARLY_INPUT_BT &&
        early->state == TITAN_EARLY_INPUT_STATE_READY && early->pademu_module_id >= 0) {
        g_bt_runtime.smsutils_module_id = early->smsutils_module_id;
        g_bt_runtime.pademu_module_id = early->pademu_module_id;
        g_bt_runtime.last_module_result = early->last_module_result;
        g_bt_runtime.state = TITAN_BT_RUNTIME_READY;
        bt_runtime_diag();
        return 1;
    }

#if defined(__EE__) || defined(_EE)
    if (g_bt_runtime_worker_active) {
        g_bt_runtime.state = TITAN_BT_RUNTIME_LOADING;
        return 1;
    }
    g_bt_runtime.state = TITAN_BT_RUNTIME_LOADING;
    if (!bt_runtime_start_worker()) {
        g_bt_runtime.last_module_result = -3;
        g_bt_runtime.state = TITAN_BT_RUNTIME_FAILED;
        bt_runtime_diag();
        return 0;
    }
    bt_runtime_diag();
    return 1;
#else
    g_bt_runtime.state = TITAN_BT_RUNTIME_FAILED;
    return 0;
#endif
}

const TitanBluetoothRuntimeStatus *TitanBluetoothRuntime_GetStatus(void)
{
    /* A WAIT_EARLY request is opportunistically advanced by callers once the
     * Early Input worker reaches a terminal state. */
    if (g_bt_runtime.state == TITAN_BT_RUNTIME_WAIT_EARLY)
        (void)TitanBluetoothRuntime_Request();
    return &g_bt_runtime;
}
