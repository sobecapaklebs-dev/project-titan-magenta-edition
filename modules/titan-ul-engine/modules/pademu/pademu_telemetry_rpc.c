#include "irx_imports.h"
#include "pademu_telemetry_rpc.h"
#include <titan_pademu_telemetry.h>
extern int pademu_transport_init_on_demand(void);
extern int pademu_transport_request_init(void);
extern int pademu_transport_init_ready(void);
extern int pademu_transport_init_state(void);
#ifdef BT
#include "ds34bt.h"
#define TELEMETRY_GET ds34bt_get_telemetry
#define TITAN_PADEMU_RPC_SERVER_ID TITAN_PADEMU_BT_TELEMETRY_RPC_ID
#elif defined(USB)
#include "ds34usb.h"
#define TELEMETRY_GET ds34usb_get_telemetry
#define TITAN_PADEMU_RPC_SERVER_ID TITAN_PADEMU_TELEMETRY_RPC_ID
#else
#error "PADEMU telemetry needs USB or BT"
#endif

static SifRpcDataQueue_t g_queue;
static SifRpcServerData_t g_server;
/* P10.5.2: SIF RPC DMA writes the complete request into this registered
 * receive buffer before rpc_handler() runs. P10.5 added BondSync (308 bytes)
 * but accidentally retained the historical 32-byte receive buffer, so the
 * first startup IMPORT could overwrite adjacent RPC state and stall the BT
 * queue before BEGIN_SCAN reached the PAD_INIT worker. Keep explicit headroom
 * for the largest request and fail the target build if the ABI grows past it. */
#define TITAN_PADEMU_RPC_RX_BYTES 384u
static unsigned char g_rx[TITAN_PADEMU_RPC_RX_BYTES] __attribute__((aligned(16)));
#ifdef BT
typedef char TitanPademuRpcRxMustFitBondSync[
    (TITAN_PADEMU_RPC_RX_BYTES >= sizeof(TitanPademuBondSync)) ? 1 : -1];
#endif
typedef union TitanPademuRpcReply {
    TitanPademuTelemetrySnapshot snapshot;
    TitanPademuBluetoothPairingStatus pairing;
    TitanPademuTraceBlock trace;
    TitanPademuWirelessState wireless;
} TitanPademuRpcReply;
static TitanPademuRpcReply g_tx __attribute__((aligned(16)));
#ifdef BT
static TitanPademuBondSync g_bond_tx __attribute__((aligned(16)));
#endif
static int g_thread=-1;
static u32 g_generation;
static u8 g_system_last[TITAN_PADEMU_TELEMETRY_MAX_PADS];
#ifdef BT
/* A user action received while the BT init worker is still running must not
 * be lost. QUERY is never stored here; the next QUERY acts as the lightweight
 * pump that executes this pending action once transport init is READY. */
static TitanPademuWirelessRequest g_wireless_pending;
static u8 g_wireless_pending_valid;
#endif

#define TITAN_PADEMU_TRACE_RING_CAPACITY 128u
static TitanPademuTraceEvent g_trace_ring[TITAN_PADEMU_TRACE_RING_CAPACITY];
static volatile u32 g_trace_head;
static volatile u32 g_trace_tail;
static volatile u32 g_trace_sequence;
static volatile u32 g_trace_dropped;

void pademu_trace_record(unsigned int code, int result, unsigned int a, unsigned int b, unsigned int c)
{
    int oldstate;
    u32 head, next;
    TitanPademuTraceEvent *event;
    CpuSuspendIntr(&oldstate);
    head = g_trace_head;
    next = (head + 1u) & (TITAN_PADEMU_TRACE_RING_CAPACITY - 1u);
    if (next == g_trace_tail) {
        g_trace_tail = (g_trace_tail + 1u) & (TITAN_PADEMU_TRACE_RING_CAPACITY - 1u);
        g_trace_dropped++;
    }
    event = &g_trace_ring[head];
    event->sequence = ++g_trace_sequence;
    event->code = (u16)code;
    event->flags = 0u;
    event->result = result;
    event->a = a;
    event->b = b;
    event->c = c;
    g_trace_head = next;
    CpuResumeIntr(oldstate);
}

static void pademu_trace_drain(TitanPademuTraceBlock *block)
{
    int oldstate;
    u32 count = 0u;
    mips_memset(block, 0, sizeof(*block));
    block->version = TITAN_PADEMU_TRACE_VERSION;
    block->build_id = TITAN_PADEMU_TRACE_BUILD_ID;
    CpuSuspendIntr(&oldstate);
    block->dropped = g_trace_dropped;
    while (g_trace_tail != g_trace_head && count < TITAN_PADEMU_TRACE_MAX_EVENTS) {
        mips_memcpy(&block->events[count],
                    &g_trace_ring[g_trace_tail],
                    sizeof(block->events[count]));
        count++;
        g_trace_tail = (g_trace_tail + 1u) & (TITAN_PADEMU_TRACE_RING_CAPACITY - 1u);
    }
    CpuResumeIntr(oldstate);
    block->count = count;
}

static void *rpc_handler(int fno, void *buffer, int nbytes)
{
    int i; (void)buffer; (void)nbytes;
    mips_memset(&g_tx,0,sizeof(g_tx));
    if (fno == TITAN_PADEMU_RPC_GET_TRACE) {
        pademu_trace_drain(&g_tx.trace);
        return &g_tx.trace;
    }
#ifdef BT
    if (fno == TITAN_PADEMU_RPC_BOND_SYNC) {
        TitanPademuBondSync request;
        mips_memset(&request, 0, sizeof(request));
        request.version = TITAN_PADEMU_BOND_SYNC_VERSION;
        request.command = TITAN_PADEMU_BOND_SYNC_QUERY;
        if (buffer && nbytes >= (int)sizeof(request))
            mips_memcpy(&request, buffer, sizeof(request));
        mips_memset(&g_bond_tx, 0, sizeof(g_bond_tx));
        (void)ds34bt_bond_sync(&request, &g_bond_tx);
        return &g_bond_tx;
    }
    if (fno == TITAN_PADEMU_RPC_WIRELESS_ACCESSORIES) {
        TitanPademuWirelessRequest request;
        int ready;
        int init_state;
        int request_ok = 1;
        mips_memset(&request, 0, sizeof(request));
        request.version = TITAN_PADEMU_WIRELESS_VERSION;
        request.command = TITAN_PADEMU_WIRELESS_QUERY;
        if (buffer && nbytes >= (int)sizeof(request))
            mips_memcpy(&request, buffer, sizeof(request));

        /* P10.5.3: QUERY is periodic state sampling, not a user action. Do not
         * flood the 128-entry IOP recorder and erase the HCI/L2CAP breadcrumbs. */
        if (request.command != TITAN_PADEMU_WIRELESS_QUERY)
            pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_UI, 0,
                                0x424100F0u, request.command, request.index);

        /* P10.2: never run UsbRegisterDriver()/PAD_INIT from this RPC thread.
         * BEGIN_SCAN/CONNECT are retained until the worker reaches READY. */
        if (request.command != TITAN_PADEMU_WIRELESS_QUERY) {
            g_wireless_pending = request;
            g_wireless_pending_valid = 1u;
            /* P10.4: publish BEGIN_SCAN intent before PAD_INIT starts.  HCI
             * Read_BD_ADDR can complete before this RPC gets another QUERY,
             * so ds34bt must know a Classic user action is already pending. */
            ds34bt_note_pending_wireless_command(request.command);
            request_ok = pademu_transport_request_init();
        }

        ready = pademu_transport_init_ready();
        init_state = pademu_transport_init_state();
        g_tx.wireless.version = TITAN_PADEMU_WIRELESS_VERSION;

        if (!ready) {
            if (!request_ok || init_state == 3) {
                g_tx.wireless.last_error = -710;
                g_tx.wireless.flags = TITAN_PADEMU_WIRELESS_HAS_ERROR;
            }
            pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_UI,
                                g_tx.wireless.last_error, 0x424100F2u,
                                (u32)init_state, (u32)g_wireless_pending_valid);
            return &g_tx.wireless;
        }

        if (g_wireless_pending_valid) {
            TitanPademuWirelessRequest pending = g_wireless_pending;
            int command_ok;
            g_wireless_pending_valid = 0u;
            command_ok = ds34bt_wireless_command(&pending, &g_tx.wireless);
            pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_UI,
                                command_ok ? 0 : g_tx.wireless.last_error,
                                0x424100F1u, pending.command, g_tx.wireless.flags);
            return &g_tx.wireless;
        }

        {
            int command_ok = ds34bt_wireless_command(&request, &g_tx.wireless);
            if (request.command != TITAN_PADEMU_WIRELESS_QUERY)
                pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_UI,
                                    command_ok ? 0 : g_tx.wireless.last_error,
                                    0x424100F1u, request.command, g_tx.wireless.flags);
        }
        return &g_tx.wireless;
    }
    if (fno == TITAN_PADEMU_RPC_BT_BEGIN_PAIRING) {
        int requested;
        int ready;
        g_tx.pairing.version = TITAN_PADEMU_PAIRING_STATUS_VERSION;
        requested = pademu_transport_init_on_demand();
        ready = pademu_transport_init_ready();
        if (ready) {
            g_tx.pairing.available = ds34bt_pairing_available() ? 1u : 0u;
            g_tx.pairing.started = ds34bt_begin_pairing() ? 1u : 0u;
            (void)ds34bt_get_transport_status(&g_tx.pairing.transport);
        } else {
            /* Request accepted: transport is starting asynchronously.  Do not
             * call ds34bt helpers until PAD_INIT has completed. */
            g_tx.pairing.available = requested ? 1u : 0u;
            g_tx.pairing.started = 0u;
            g_tx.pairing.transport.last_error = requested ? 0 : -710;
        }
        return &g_tx.pairing;
    }
#endif
    g_tx.snapshot.version=TITAN_PADEMU_TELEMETRY_VERSION;
    if(fno!=TITAN_PADEMU_RPC_GET_SNAPSHOT)return &g_tx.snapshot;
    g_tx.snapshot.generation=++g_generation;
    for(i=0;i<(int)TITAN_PADEMU_TELEMETRY_MAX_PADS;i++) {
        if(TELEMETRY_GET(i,&g_tx.snapshot.pads[i])) {
            if(g_tx.snapshot.pads[i].connected) g_tx.snapshot.count++;
        }
    }
    return &g_tx.snapshot;
}

static void rpc_thread(void *arg)
{
    (void)arg;
    sceSifSetRpcQueue(&g_queue,GetThreadId());
    sceSifRegisterRpc(&g_server,TITAN_PADEMU_RPC_SERVER_ID,rpc_handler,g_rx,0,0,&g_queue);
    sceSifRpcLoop(&g_queue);
}

int pademu_telemetry_rpc_start(void)
{
    iop_thread_t t;
    if(g_thread>0)return 1;
    mips_memset(&t,0,sizeof(t));
    t.attr=TH_C;t.option=TITAN_PADEMU_RPC_SERVER_ID;t.thread=rpc_thread;t.priority=0x28;t.stacksize=0x800;
    g_thread=CreateThread(&t);
    if(g_thread<=0)return 0;
    if(StartThread(g_thread,0)<0){DeleteThread(g_thread);g_thread=-1;return 0;}
    return 1;
}
void pademu_telemetry_rpc_stop(void)
{
    if(g_thread>0){TerminateThread(g_thread);DeleteThread(g_thread);g_thread=-1;}
}

void pademu_telemetry_system_button(unsigned int port, unsigned int device_type, int pressed)
{
    TitanPademuSystemButtonEvent e;
    if(port>=TITAN_PADEMU_TELEMETRY_MAX_PADS)return;
    if((pressed?1:0)==g_system_last[port])return;
    g_system_last[port]=pressed?1:0;
    if(!pressed)return;
    mips_memset(&e,0,sizeof(e));
    e.port=port;e.device_type=device_type;e.generation=++g_generation;e.pressed=1;
    sceSifSendCmd(TITAN_PADEMU_SYSTEM_CMD_ID,&e,sizeof(e),0,0,0);
}
