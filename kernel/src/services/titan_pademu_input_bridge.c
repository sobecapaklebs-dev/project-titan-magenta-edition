#include "titan_pademu_input_bridge.h"
#include "titan_unified_input.h"
#include "titan_bluetooth_runtime.h"
#include "titan_diagnostics.h"
#include "titan_storage_io.h"
#include <titan_pademu_telemetry.h>
#include <string.h>
#if defined(__EE__) || defined(_EE)
#include <kernel.h>
#include <sifrpc.h>
#endif

static TitanPademuTelemetrySnapshot g_usb_snapshot;
static TitanPademuTelemetrySnapshot g_bt_snapshot;
static TitanPademuBridgeMetrics g_metrics;
static uint32_t g_usb_device_ids[TITAN_PADEMU_TELEMETRY_MAX_PADS];
static uint32_t g_bt_device_ids[TITAN_PADEMU_TELEMETRY_MAX_PADS];
static uint32_t g_frame;
static int g_usb_snapshot_valid;
static int g_bt_snapshot_valid;
static TitanPademuPairingRuntime g_pairing;
static TitanPademuWirelessState g_wireless_state;
static int g_wireless_valid;
/* P10.5: bond files are owned by the EE storage lane, never by PADEMU/USBD. */
static TitanPademuBondSync g_bond_cache;
static volatile int g_bond_storage_load_pending;
static volatile int g_bond_storage_write_pending;
static volatile int g_bond_import_requested;
static volatile int g_bond_ack_requested;
static uint32_t g_bond_persist_generation;
#define TITAN_BT_CLASSIC_BOND_PATH "mass:/TITAN/BTBONDS.BIN"
#define TITAN_BT_CLASSIC_BOND_TMP  "mass:/TITAN/BTBONDS.BIN.tmp"
#define TITAN_BT_CLASSIC_BOND_BAK  "mass:/TITAN/BTBONDS.BIN.bak"
#define TITAN_BT_XBOX_BOND_PATH    "mass:/TITAN/XBOXBT.BIN"
#define TITAN_BT_XBOX_BOND_TMP     "mass:/TITAN/XBOXBT.BIN.tmp"
#define TITAN_BT_XBOX_BOND_BAK     "mass:/TITAN/XBOXBT.BIN.bak"
static volatile uint8_t g_system_latched[TITAN_PADEMU_TELEMETRY_MAX_PADS];

#if defined(__EE__) || defined(_EE)
/* P10.2: USB and BT PADEMU are independent shell transports. USB keeps the
 * historical RPC id; BT owns a dedicated id so an Early Input USB instance
 * can never answer Wireless Accessories RPCs by accident. */
static SifRpcClientData_t g_usb_rpc __attribute__((aligned(64)));
static SifRpcClientData_t g_bt_rpc __attribute__((aligned(64)));
static unsigned char g_usb_rpc_buf[256] __attribute__((aligned(64)));
static unsigned char g_bt_snapshot_rpc_buf[256] __attribute__((aligned(64)));
static int g_system_handler_installed;
static volatile int g_usb_snapshot_rpc_pending;
static volatile int g_usb_snapshot_rpc_done;
static volatile int g_bt_snapshot_rpc_pending;
static volatile int g_bt_snapshot_rpc_done;
static unsigned char g_pair_rpc_buf[128] __attribute__((aligned(64)));
static unsigned char g_trace_rpc_buf[256] __attribute__((aligned(64)));
static volatile int g_trace_rpc_pending;
static volatile int g_trace_rpc_done;
static uint32_t g_trace_dropped_seen;
static volatile int g_pair_rpc_pending;
static volatile int g_pair_rpc_done;
static volatile int g_pair_requested;
static uint32_t g_pair_request_frame;
static unsigned char g_wireless_rpc_buf[256] __attribute__((aligned(64)));
static unsigned char g_wireless_send_buf[64] __attribute__((aligned(64)));
typedef char TitanWirelessRpcReplyFitsBuffer[(sizeof(TitanPademuWirelessState) <= 256u) ? 1 : -1];
static volatile int g_wireless_rpc_pending;
static volatile int g_wireless_rpc_done;
static volatile int g_wireless_requested;
static TitanPademuWirelessRequest g_wireless_request;
static unsigned char g_bond_rpc_buf[384] __attribute__((aligned(64)));
static unsigned char g_bond_send_buf[384] __attribute__((aligned(64)));
typedef char TitanBondRpcReplyFitsBuffer[(sizeof(TitanPademuBondSync) <= 384u) ? 1 : -1];
static volatile int g_bond_rpc_pending;
static volatile int g_bond_rpc_done;
static uint32_t g_bond_rpc_command;
/* P10.6.3: deferred persisted Classic bond restore after BT RPC ready.
 * Health & Safety may request restore before the late-loaded BT RPC exists.
 * Do not consume this bit until g_bt_rpc.server is bound. */
static volatile int g_bond_restore_requested;
/* P10.5.3: pairing-time SIF QoS. Keep the diagnostic/control RPC lane
 * quiet enough that AUDSRV can meet its own IOP deadlines. */
static uint32_t g_wireless_diag_generation;
static uint32_t g_wireless_diag_flags;
static uint32_t g_wireless_diag_frame;

static int TitanPademuInputBridge_SnapshotHasConnected(
    const TitanPademuTelemetrySnapshot *snapshot, int valid)
{
    unsigned int i;
    if (!valid || !snapshot) return 0;
    for (i = 0u; i < TITAN_PADEMU_TELEMETRY_MAX_PADS; i++)
        if (snapshot->pads[i].connected) return 1;
    return 0;
}

static int TitanPademuInputBridge_WirelessHandshakeActive(void)
{
    return g_wireless_valid &&
        (g_wireless_state.flags &
         (TITAN_PADEMU_WIRELESS_SCANNING |
          TITAN_PADEMU_WIRELESS_CONNECT_IN_PROGRESS));
}

static void TitanPademuInputBridge_PairRpcDone(void *arg){(void)arg;g_pair_rpc_done=1;}
static void TitanPademuInputBridge_UsbSnapshotRpcDone(void *arg){(void)arg;g_usb_snapshot_rpc_done=1;}
static void TitanPademuInputBridge_BtSnapshotRpcDone(void *arg){(void)arg;g_bt_snapshot_rpc_done=1;}
static void TitanPademuInputBridge_TraceRpcDone(void *arg){(void)arg;g_trace_rpc_done=1;}
static void TitanPademuInputBridge_WirelessRpcDone(void *arg){(void)arg;g_wireless_rpc_done=1;}
static void TitanPademuInputBridge_BondRpcDone(void *arg){(void)arg;g_bond_rpc_done=1;}

static void TitanPademuInputBridge_SystemButtonHandler(void *packet, void *opt)
{
    TitanPademuSystemButtonEvent *event=(TitanPademuSystemButtonEvent *)packet;
    (void)opt;
    if(!event || !event->pressed || event->port>=TITAN_PADEMU_TELEMETRY_MAX_PADS)return;
    g_system_latched[event->port]=1u;
}
#endif

static TitanInputDeviceType device_type(uint8_t t){switch(t){case TITAN_PADEMU_DEVICE_DS3:return TITAN_INPUT_DEVICE_DUALSHOCK3;case TITAN_PADEMU_DEVICE_DS4:return TITAN_INPUT_DEVICE_DUALSHOCK4;case TITAN_PADEMU_DEVICE_DUALSENSE:return TITAN_INPUT_DEVICE_DUALSENSE;case TITAN_PADEMU_DEVICE_XBOX_ONE:return TITAN_INPUT_DEVICE_XBOX_ONE;case TITAN_PADEMU_DEVICE_XBOX_SERIES:return TITAN_INPUT_DEVICE_XBOX_SERIES;default:return TITAN_INPUT_DEVICE_NONE;}}
static TitanInputTransport transport_type(uint8_t t){return t==TITAN_PADEMU_TRANSPORT_BT?TITAN_INPUT_TRANSPORT_BLUETOOTH:(t==TITAN_PADEMU_TRANSPORT_USB?TITAN_INPUT_TRANSPORT_USB:TITAN_INPUT_TRANSPORT_NONE);}
static TitanInputBackend backend_type(uint8_t t){return t==TITAN_PADEMU_TRANSPORT_BT?TITAN_INPUT_BACKEND_PADEMU_BT:TITAN_INPUT_BACKEND_PADEMU_USB;}
static TitanInputBatteryState battery_state(uint8_t s){switch(s){case TITAN_PADEMU_BATTERY_UNKNOWN:return TITAN_INPUT_BATTERY_UNKNOWN;case TITAN_PADEMU_BATTERY_LEVEL:return TITAN_INPUT_BATTERY_LEVEL;case TITAN_PADEMU_BATTERY_CHARGING:return TITAN_INPUT_BATTERY_CHARGING;case TITAN_PADEMU_BATTERY_FULL:return TITAN_INPUT_BATTERY_FULL;default:return TITAN_INPUT_BATTERY_UNSUPPORTED;}}
static uint32_t caps_for(const TitanPademuPadTelemetry *p){uint32_t c=TITAN_INPUT_CAP_BUTTONS|TITAN_INPUT_CAP_DPAD|TITAN_INPUT_CAP_ANALOG_STICKS|TITAN_INPUT_CAP_ANALOG_TRIGGERS|TITAN_INPUT_CAP_RUMBLE|TITAN_INPUT_CAP_SYSTEM_BUTTON;if(p->transport==TITAN_PADEMU_TRANSPORT_BT)c|=TITAN_INPUT_CAP_WIRELESS;else c|=TITAN_INPUT_CAP_WIRED;if(p->battery_state!=TITAN_PADEMU_BATTERY_UNSUPPORTED)c|=TITAN_INPUT_CAP_BATTERY;if(p->device_type==TITAN_PADEMU_DEVICE_DS4||p->device_type==TITAN_PADEMU_DEVICE_DUALSENSE)c|=TITAN_INPUT_CAP_LIGHT;return c;}

void TitanPademuInputBridge_Initialize(void)
{
    memset(&g_usb_snapshot,0,sizeof(g_usb_snapshot));
    memset(&g_bt_snapshot,0,sizeof(g_bt_snapshot));
    memset(&g_metrics,0,sizeof(g_metrics));
    memset(&g_pairing,0,sizeof(g_pairing));
    memset(&g_wireless_state,0,sizeof(g_wireless_state));
    memset(&g_bond_cache,0,sizeof(g_bond_cache));
    g_bond_cache.version=TITAN_PADEMU_BOND_SYNC_VERSION;
    g_bond_storage_load_pending=1;g_bond_storage_write_pending=0;g_bond_import_requested=0;g_bond_ack_requested=0;g_bond_persist_generation=0u;
    memset(g_usb_device_ids,0,sizeof(g_usb_device_ids));
    memset(g_bt_device_ids,0,sizeof(g_bt_device_ids));
    memset((void*)g_system_latched,0,sizeof(g_system_latched));
    g_frame=0;g_usb_snapshot_valid=0;g_bt_snapshot_valid=0;g_wireless_valid=0;
    g_pairing.state=TITAN_PADEMU_PAIRING_IDLE;
#if defined(__EE__) || defined(_EE)
    memset(&g_usb_rpc,0,sizeof(g_usb_rpc));memset(&g_bt_rpc,0,sizeof(g_bt_rpc));
    memset(g_usb_rpc_buf,0,sizeof(g_usb_rpc_buf));memset(g_bt_snapshot_rpc_buf,0,sizeof(g_bt_snapshot_rpc_buf));
    memset(g_pair_rpc_buf,0,sizeof(g_pair_rpc_buf));memset(g_trace_rpc_buf,0,sizeof(g_trace_rpc_buf));
    memset(g_wireless_rpc_buf,0,sizeof(g_wireless_rpc_buf));memset(g_wireless_send_buf,0,sizeof(g_wireless_send_buf));
    memset(g_bond_rpc_buf,0,sizeof(g_bond_rpc_buf));memset(g_bond_send_buf,0,sizeof(g_bond_send_buf));
    memset(&g_wireless_request,0,sizeof(g_wireless_request));
    g_usb_snapshot_rpc_pending=0;g_usb_snapshot_rpc_done=0;g_bt_snapshot_rpc_pending=0;g_bt_snapshot_rpc_done=0;
    g_trace_rpc_pending=0;g_trace_rpc_done=0;g_trace_dropped_seen=0u;
    g_pair_rpc_pending=0;g_pair_rpc_done=0;g_pair_requested=0;g_pair_request_frame=0u;
    g_wireless_rpc_pending=0;g_wireless_rpc_done=0;g_wireless_requested=0;
    g_bond_rpc_pending=0;g_bond_rpc_done=0;g_bond_rpc_command=TITAN_PADEMU_BOND_SYNC_QUERY;g_bond_restore_requested=0;
    g_wireless_diag_generation=0xffffffffu;g_wireless_diag_flags=0xffffffffu;g_wireless_diag_frame=0u;
    if(!g_system_handler_installed){SifAddCmdHandler(TITAN_PADEMU_SYSTEM_CMD_ID,TitanPademuInputBridge_SystemButtonHandler,0);g_system_handler_installed=1;}
#endif
}

void TitanPademuInputBridge_Shutdown(void)
{
    unsigned i;
    for(i=0;i<TITAN_PADEMU_TELEMETRY_MAX_PADS;i++){
        if(g_usb_device_ids[i])TitanUnifiedInput_Disconnect(g_usb_device_ids[i]);
        if(g_bt_device_ids[i])TitanUnifiedInput_Disconnect(g_bt_device_ids[i]);
    }
    TitanPademuInputBridge_Initialize();
}

void TitanPademuInputBridge_BeginFrame(void)
{
    g_frame++;


#if defined(__EE__) || defined(_EE)
    /* P10.6.3: deferred persisted Classic bond restore after BT RPC ready. The original P10.5 startup LOAD can be
     * consumed before P10.6 late-loads bt_pademu. Keep the restore
     * intent latched until the dedicated BT RPC server is live, then
     * rearm the EXISTING P10.5 Storage Worker trigger exactly once.
     * Existing EE/RPC code remains responsible for BondSync IMPORT. */
    if (g_bond_restore_requested && g_bt_rpc.server &&
        !g_bond_rpc_pending && !g_bond_storage_write_pending &&
        !g_bond_ack_requested) {
        g_bond_storage_load_pending = 1;
        g_bond_restore_requested = 0;
        TitanDiagnostics_Event(TITAN_DIAG_INFO, TITAN_DIAG_INPUT, 0x0316u, 0,
            0x50313633u, g_frame, 1u, 0u); /* P163 */
    }
    if(g_wireless_rpc_done){
        g_wireless_rpc_done=0;g_wireless_rpc_pending=0;
        memset(&g_wireless_state,0,sizeof(g_wireless_state));
        memcpy(&g_wireless_state,(void*)UNCACHED_SEG(g_wireless_rpc_buf),sizeof(g_wireless_state));
        g_wireless_valid=g_wireless_state.version==TITAN_PADEMU_WIRELESS_VERSION;
        if(!g_wireless_valid)g_metrics.rpc_errors++;
        /* Normal QUERY completion used to emit four diagnostics per second,
         * crowding the flight recorder until the pairing breadcrumbs were lost.
         * Emit on state change/error, plus one 2-second heartbeat. */
        if (!g_wireless_valid || g_wireless_state.last_error ||
            g_wireless_state.generation != g_wireless_diag_generation ||
            g_wireless_state.flags != g_wireless_diag_flags ||
            (g_frame - g_wireless_diag_frame) >= 120u) {
            TitanDiagnostics_Event((g_wireless_valid && !g_wireless_state.last_error)?TITAN_DIAG_TRACE:TITAN_DIAG_WARN,
                TITAN_DIAG_INPUT,0x0315u,g_wireless_valid?g_wireless_state.last_error:-999,
                0x80000000u|g_wireless_state.flags,g_wireless_state.generation,
                g_wireless_state.count,g_wireless_state.last_hci_event);
            g_wireless_diag_generation=g_wireless_state.generation;
            g_wireless_diag_flags=g_wireless_state.flags;
            g_wireless_diag_frame=g_frame;
        }
    }
    if(g_bond_rpc_done){
        TitanPademuBondSync state;
        g_bond_rpc_done=0;g_bond_rpc_pending=0;memset(&state,0,sizeof(state));
        memcpy(&state,(void*)UNCACHED_SEG(g_bond_rpc_buf),sizeof(state));
        if(state.version==TITAN_PADEMU_BOND_SYNC_VERSION){
            if(g_bond_rpc_command==TITAN_PADEMU_BOND_SYNC_QUERY &&
               (state.flags&(TITAN_PADEMU_BOND_SYNC_CLASSIC_DIRTY|TITAN_PADEMU_BOND_SYNC_XBOX_DIRTY))){
                g_bond_cache=state;
                g_bond_persist_generation=state.generation;
                g_bond_storage_write_pending=1;
                TitanDiagnostics_Event(TITAN_DIAG_INFO,TITAN_DIAG_INPUT,0x0318u,0,state.generation,state.flags,state.classic.count,0u);
            }
        }else{g_metrics.rpc_errors++;TitanDiagnostics_Event(TITAN_DIAG_ERROR,TITAN_DIAG_INPUT,0x0318u,-1,state.version,g_bond_rpc_command,0u,0u);}
    }
    if(g_pair_rpc_done){
        TitanPademuBluetoothPairingStatus status;
        g_pair_rpc_done=0;g_pair_rpc_pending=0;memset(&status,0,sizeof(status));
        memcpy(&status,(void*)UNCACHED_SEG(g_pair_rpc_buf),sizeof(status));g_pairing.completions++;
        if(status.version!=TITAN_PADEMU_PAIRING_STATUS_VERSION){g_pairing.state=TITAN_PADEMU_PAIRING_ERROR;g_pairing.rpc_errors++;}
        else{
            g_pairing.available=status.available;g_pairing.started=status.started;
            g_pairing.transport_stage=status.transport.stage;g_pairing.transport_error=status.transport.last_error;
            g_pairing.usb_dev_id=status.transport.usb_dev_id;g_pairing.usb_status=status.transport.usb_status;
            g_pairing.endpoints_mask=status.transport.endpoints_mask;g_pairing.usb_identity=status.transport.usb_identity;
            g_pairing.last_hci_event=status.transport.last_hci_event;g_pairing.last_hci_opcode=status.transport.last_hci_opcode;
            g_pairing.last_hci_status=status.transport.last_hci_status;g_pairing.probe_count=status.transport.probe_count;
            g_pairing.connect_count=status.transport.connect_count;g_pairing.config_result=status.transport.config_result;
            g_pairing.reset_submit_result=status.transport.reset_submit_result;g_pairing.scan_submit_result=status.transport.scan_submit_result;
            g_pairing.state=status.started?TITAN_PADEMU_PAIRING_STARTED:(status.available?TITAN_PADEMU_PAIRING_PENDING:TITAN_PADEMU_PAIRING_UNAVAILABLE);
        }
        TitanDiagnostics_Event(g_pairing.state==TITAN_PADEMU_PAIRING_STARTED?TITAN_DIAG_INFO:TITAN_DIAG_TRACE,
            TITAN_DIAG_INPUT,0x0311u,(int32_t)g_pairing.transport_error,(uint32_t)g_pairing.state,
            (uint32_t)g_pairing.transport_stage,(uint32_t)g_pairing.usb_status,(uint32_t)g_pairing.last_hci_status);
    }
    if((g_pair_requested||g_pair_rpc_pending)&&g_pair_request_frame!=0u&&(g_frame-g_pair_request_frame)>600u&&g_pairing.state!=TITAN_PADEMU_PAIRING_TIMED_OUT){
        g_pair_requested=0;g_pairing.state=TITAN_PADEMU_PAIRING_TIMED_OUT;
        TitanDiagnostics_Event(TITAN_DIAG_ERROR,TITAN_DIAG_INPUT,0x0311u,-1,(uint32_t)g_pairing.state,0u,0u,g_pairing.rpc_errors);
    }
    if(g_trace_rpc_done){
        TitanPademuTraceBlock block;uint32_t i;
        g_trace_rpc_done=0;g_trace_rpc_pending=0;memset(&block,0,sizeof(block));
        memcpy(&block,(void*)UNCACHED_SEG(g_trace_rpc_buf),sizeof(block));
        if(block.version==TITAN_PADEMU_TRACE_VERSION&&block.build_id==TITAN_PADEMU_TRACE_BUILD_ID){
            for(i=0;i<block.count&&i<TITAN_PADEMU_TRACE_MAX_EVENTS;i++){
                const TitanPademuTraceEvent *e=&block.events[i];
                TitanDiagnostics_Event(e->result<0?TITAN_DIAG_ERROR:TITAN_DIAG_TRACE,TITAN_DIAG_INPUT,0x0313u,e->result,(uint32_t)e->code,e->a,e->b,e->c);
            }
            if(block.dropped!=g_trace_dropped_seen){TitanDiagnostics_Event(TITAN_DIAG_WARN,TITAN_DIAG_INPUT,0x0314u,0,block.dropped,g_trace_dropped_seen,block.build_id,block.count);g_trace_dropped_seen=block.dropped;}
        }else TitanDiagnostics_Event(TITAN_DIAG_ERROR,TITAN_DIAG_INPUT,0x0314u,-1,block.version,block.build_id,block.count,block.dropped);
    }
    if(g_usb_snapshot_rpc_done){g_usb_snapshot_rpc_done=0;g_usb_snapshot_rpc_pending=0;memcpy(&g_usb_snapshot,(void*)UNCACHED_SEG(g_usb_rpc_buf),sizeof(g_usb_snapshot));g_usb_snapshot_valid=g_usb_snapshot.version==TITAN_PADEMU_TELEMETRY_VERSION;}
    if(g_bt_snapshot_rpc_done){g_bt_snapshot_rpc_done=0;g_bt_snapshot_rpc_pending=0;memcpy(&g_bt_snapshot,(void*)UNCACHED_SEG(g_bt_snapshot_rpc_buf),sizeof(g_bt_snapshot));g_bt_snapshot_valid=g_bt_snapshot.version==TITAN_PADEMU_TELEMETRY_VERSION;}

    /* Legacy USB PADEMU is optional. Never let a missing USB server prevent the
     * independent BT client from progressing. */
    if(!g_usb_rpc.server && !g_usb_snapshot_rpc_pending && (g_frame%120u)==1u){
        g_metrics.bind_attempts++;
        if(sceSifBindRpc(&g_usb_rpc,TITAN_PADEMU_TELEMETRY_RPC_ID,0)<0||!g_usb_rpc.server)g_usb_snapshot_valid=0;
    }

    /* BEGIN_SCAN/Pair explicitly own activation of the Bluetooth runtime. */
    if(g_wireless_requested||g_pair_requested||g_bt_rpc.server){
        const TitanBluetoothRuntimeStatus *rt;
        (void)TitanBluetoothRuntime_Request();
        rt=TitanBluetoothRuntime_GetStatus();
        if(rt && rt->state==TITAN_BT_RUNTIME_READY && !g_bt_rpc.server &&
           !g_wireless_rpc_pending&&!g_pair_rpc_pending&&!g_trace_rpc_pending&&!g_bt_snapshot_rpc_pending&&!g_bond_rpc_pending &&
           (g_frame%15u)==1u){
            g_metrics.bind_attempts++;
            (void)sceSifBindRpc(&g_bt_rpc,TITAN_PADEMU_BT_TELEMETRY_RPC_ID,0);
        }
    }

    if(g_bt_rpc.server){
        int bt_busy=g_wireless_rpc_pending||g_pair_rpc_pending||g_trace_rpc_pending||g_bt_snapshot_rpc_pending||g_bond_rpc_pending;
        if(g_bond_import_requested&&!bt_busy){
            TitanPademuBondSync request=g_bond_cache;
            request.version=TITAN_PADEMU_BOND_SYNC_VERSION;request.command=TITAN_PADEMU_BOND_SYNC_IMPORT;
            memset(g_bond_rpc_buf,0,sizeof(g_bond_rpc_buf));memset(g_bond_send_buf,0,sizeof(g_bond_send_buf));
            memcpy(g_bond_send_buf,&request,sizeof(request));g_bond_import_requested=0;g_bond_rpc_pending=1;g_bond_rpc_command=request.command;
            if(sceSifCallRpc(&g_bt_rpc,TITAN_PADEMU_RPC_BOND_SYNC,SIF_RPC_M_NOWAIT,g_bond_send_buf,sizeof(request),g_bond_rpc_buf,sizeof(TitanPademuBondSync),TitanPademuInputBridge_BondRpcDone,0)<0){g_bond_rpc_pending=0;g_bond_import_requested=1;g_metrics.rpc_errors++;}
            return;
        }
        if(g_bond_ack_requested&&!bt_busy){
            TitanPademuBondSync request;memset(&request,0,sizeof(request));request.version=TITAN_PADEMU_BOND_SYNC_VERSION;request.command=TITAN_PADEMU_BOND_SYNC_ACK;request.generation=g_bond_persist_generation;
            memset(g_bond_rpc_buf,0,sizeof(g_bond_rpc_buf));memset(g_bond_send_buf,0,sizeof(g_bond_send_buf));memcpy(g_bond_send_buf,&request,sizeof(request));
            g_bond_ack_requested=0;g_bond_rpc_pending=1;g_bond_rpc_command=request.command;
            if(sceSifCallRpc(&g_bt_rpc,TITAN_PADEMU_RPC_BOND_SYNC,SIF_RPC_M_NOWAIT,g_bond_send_buf,sizeof(request),g_bond_rpc_buf,sizeof(TitanPademuBondSync),TitanPademuInputBridge_BondRpcDone,0)<0){g_bond_rpc_pending=0;g_bond_ack_requested=1;g_metrics.rpc_errors++;}
            return;
        }
        if(g_wireless_requested && !bt_busy &&
           !(g_wireless_request.command==TITAN_PADEMU_WIRELESS_QUERY && (g_frame%30u)==1u)){
            memset(g_wireless_rpc_buf,0,sizeof(g_wireless_rpc_buf));memset(g_wireless_send_buf,0,sizeof(g_wireless_send_buf));
    memset(g_bond_rpc_buf,0,sizeof(g_bond_rpc_buf));memset(g_bond_send_buf,0,sizeof(g_bond_send_buf));
            memcpy(g_wireless_send_buf,&g_wireless_request,sizeof(g_wireless_request));
            g_wireless_requested=0;g_wireless_rpc_pending=1;
            if(sceSifCallRpc(&g_bt_rpc,TITAN_PADEMU_RPC_WIRELESS_ACCESSORIES,SIF_RPC_M_NOWAIT,
                g_wireless_send_buf,sizeof(TitanPademuWirelessRequest),g_wireless_rpc_buf,sizeof(TitanPademuWirelessState),
                TitanPademuInputBridge_WirelessRpcDone,0)<0){g_wireless_rpc_pending=0;g_metrics.rpc_errors++;}
            return;
        }
        bt_busy=g_wireless_rpc_pending||g_pair_rpc_pending||g_trace_rpc_pending||g_bt_snapshot_rpc_pending||g_bond_rpc_pending;
        if(g_pair_requested&&!bt_busy){
            memset(g_pair_rpc_buf,0,sizeof(g_pair_rpc_buf));g_pair_requested=0;g_pair_rpc_pending=1;
            if(sceSifCallRpc(&g_bt_rpc,TITAN_PADEMU_RPC_BT_BEGIN_PAIRING,SIF_RPC_M_NOWAIT,g_pair_rpc_buf,0,g_pair_rpc_buf,sizeof(TitanPademuBluetoothPairingStatus),TitanPademuInputBridge_PairRpcDone,0)<0){g_pair_rpc_pending=0;g_pairing.state=TITAN_PADEMU_PAIRING_ERROR;g_pairing.rpc_errors++;g_metrics.rpc_errors++;}
            return;
        }
        bt_busy=g_wireless_rpc_pending||g_pair_rpc_pending||g_trace_rpc_pending||g_bt_snapshot_rpc_pending||g_bond_rpc_pending;
        /* Bond state is persistence metadata, not realtime input. During a
         * scan/connect handshake it must not compete with HCI/L2CAP/AUDSRV.
         * Once a dirty snapshot is cached, stop polling until storage commits
         * and generation-ACK completes. */
        if(!bt_busy && !g_bond_storage_write_pending && !g_bond_ack_requested &&
           !TitanPademuInputBridge_WirelessHandshakeActive() && (g_frame%120u)==7u){
            TitanPademuBondSync request;memset(&request,0,sizeof(request));request.version=TITAN_PADEMU_BOND_SYNC_VERSION;request.command=TITAN_PADEMU_BOND_SYNC_QUERY;
            memset(g_bond_rpc_buf,0,sizeof(g_bond_rpc_buf));memset(g_bond_send_buf,0,sizeof(g_bond_send_buf));memcpy(g_bond_send_buf,&request,sizeof(request));
            g_bond_rpc_pending=1;g_bond_rpc_command=request.command;
            if(sceSifCallRpc(&g_bt_rpc,TITAN_PADEMU_RPC_BOND_SYNC,SIF_RPC_M_NOWAIT,g_bond_send_buf,sizeof(request),g_bond_rpc_buf,sizeof(TitanPademuBondSync),TitanPademuInputBridge_BondRpcDone,0)<0){g_bond_rpc_pending=0;g_metrics.rpc_errors++;}
            return;
        }
        bt_busy=g_wireless_rpc_pending||g_pair_rpc_pending||g_trace_rpc_pending||g_bt_snapshot_rpc_pending||g_bond_rpc_pending;
        if(!bt_busy&&(g_frame%30u)==1u){
            memset(g_trace_rpc_buf,0,sizeof(g_trace_rpc_buf));g_trace_rpc_pending=1;
            if(sceSifCallRpc(&g_bt_rpc,TITAN_PADEMU_RPC_GET_TRACE,SIF_RPC_M_NOWAIT,g_trace_rpc_buf,0,g_trace_rpc_buf,sizeof(TitanPademuTraceBlock),TitanPademuInputBridge_TraceRpcDone,0)<0){g_trace_rpc_pending=0;g_metrics.rpc_errors++;}
            return;
        }
        bt_busy=g_wireless_rpc_pending||g_pair_rpc_pending||g_trace_rpc_pending||g_bt_snapshot_rpc_pending||g_bond_rpc_pending;
        {
            int bt_active=TitanPademuInputBridge_SnapshotHasConnected(&g_bt_snapshot,g_bt_snapshot_valid);
            int handshake=TitanPademuInputBridge_WirelessHandshakeActive();
            /* Before HID_RUNNING there is no controller state to sample. A
             * 4 Hz probe is enough; during the actual HCI/L2CAP handshake the
             * Wireless RPC is authoritative and snapshots are suppressed.
             * Once a BT pad is active retain the proven 30 Hz input cadence. */
            int bt_snapshot_due=bt_active ? ((g_frame&1u)!=0u) : (!handshake && (g_frame%15u)==3u);
            if(!bt_busy&&bt_snapshot_due){
                memset(g_bt_snapshot_rpc_buf,0,sizeof(g_bt_snapshot_rpc_buf));g_bt_snapshot_rpc_pending=1;g_metrics.rpc_calls++;
                if(sceSifCallRpc(&g_bt_rpc,TITAN_PADEMU_RPC_GET_SNAPSHOT,SIF_RPC_M_NOWAIT,g_bt_snapshot_rpc_buf,0,g_bt_snapshot_rpc_buf,sizeof(g_bt_snapshot),TitanPademuInputBridge_BtSnapshotRpcDone,0)<0){g_bt_snapshot_rpc_pending=0;g_metrics.rpc_errors++;}
            }
        }
    }

    if(g_usb_rpc.server&&!g_usb_snapshot_rpc_pending){
        int usb_active=TitanPademuInputBridge_SnapshotHasConnected(&g_usb_snapshot,g_usb_snapshot_valid);
        /* Do not spend half the SIF bandwidth polling an empty USB PADEMU.
         * Active wired pads keep 30 Hz; an empty transport is probed at 4 Hz. */
        int usb_snapshot_due=usb_active ? ((g_frame&1u)==0u) : ((g_frame%15u)==11u);
        if(usb_snapshot_due){
            memset(g_usb_rpc_buf,0,sizeof(g_usb_rpc_buf));g_usb_snapshot_rpc_pending=1;g_metrics.rpc_calls++;
            if(sceSifCallRpc(&g_usb_rpc,TITAN_PADEMU_RPC_GET_SNAPSHOT,SIF_RPC_M_NOWAIT,g_usb_rpc_buf,0,g_usb_rpc_buf,sizeof(g_usb_snapshot),TitanPademuInputBridge_UsbSnapshotRpcDone,0)<0){g_usb_snapshot_rpc_pending=0;g_metrics.rpc_errors++;}
        }
    }
#endif
}

static int submit_snapshot_port(const TitanPademuTelemetrySnapshot *snapshot,int valid,uint32_t *ids,unsigned int port,int use_system_latch)
{
    const TitanPademuPadTelemetry *p;TitanNormalizedInputState s;TitanInputDeviceType type;uint32_t id;
    if(!valid||port>=TITAN_PADEMU_TELEMETRY_MAX_PADS){if(port<TITAN_PADEMU_TELEMETRY_MAX_PADS&&ids[port]){TitanUnifiedInput_Disconnect(ids[port]);ids[port]=0;}return 0;}
    p=&snapshot->pads[port];
    if(!p->connected){if(ids[port]){TitanUnifiedInput_Disconnect(ids[port]);ids[port]=0;}return 0;}
    type=device_type(p->device_type);if(type==TITAN_INPUT_DEVICE_NONE)return 0;
    id=ids[port];if(!id){id=TitanUnifiedInput_ConnectEx(type,transport_type(p->transport),backend_type(p->transport),p->vendor_id,p->product_id,caps_for(p),p->stable_key?p->stable_key:((p->transport==TITAN_PADEMU_TRANSPORT_BT?0x50420000u:0x50410000u)+port));ids[port]=id;}
    if(!id)return 0;
    memset(&s,0,sizeof(s));s.left_x=s.left_y=s.right_x=s.right_y=128u;
    if(p->input_valid){s.buttons=(uint32_t)p->buttons;s.left_x=p->left_x;s.left_y=p->left_y;s.right_x=p->right_x;s.right_y=p->right_y;s.trigger_l=p->trigger_l;s.trigger_r=p->trigger_r;}
    if(p->system_button||(use_system_latch&&g_system_latched[port]))s.buttons|=TITAN_NBTN_SYSTEM;
    (void)TitanUnifiedInput_SubmitStateWithBattery(id,&s,battery_state(p->battery_state),p->battery_percent,p->battery_valid,p->battery_raw,0u);
    g_metrics.active_ports++;return 1;
}

int TitanPademuInputBridge_SubmitPort(unsigned int port,int connected,uint16_t buttons,uint8_t lx,uint8_t ly,uint8_t rx,uint8_t ry)
{
    int bt_active,usb_active;
    (void)connected;(void)buttons;(void)lx;(void)ly;(void)rx;(void)ry;
    if(port>=TITAN_PADEMU_TELEMETRY_MAX_PADS)return 0;
    /* BT first so a System/PS event emitted by the BT module is consumed by
     * the transport that generated it when USB and BT occupy the same slot. */
    bt_active=submit_snapshot_port(&g_bt_snapshot,g_bt_snapshot_valid,g_bt_device_ids,port,1);
    usb_active=submit_snapshot_port(&g_usb_snapshot,g_usb_snapshot_valid,g_usb_device_ids,port,!bt_active);
    if(bt_active||usb_active)g_system_latched[port]=0u;
    return bt_active||usb_active;
}

int TitanPademuInputBridge_StartBluetoothPairing(void)
{
#if defined(__EE__) || defined(_EE)
    if(g_pair_requested||g_pair_rpc_pending)return 1;
    if(!TitanBluetoothRuntime_Request())return 0;
    g_pairing.requests++;g_pairing.available=0u;g_pairing.started=0u;g_pairing.state=TITAN_PADEMU_PAIRING_PENDING;
    g_pair_requested=1;g_pair_request_frame=g_frame?g_frame:1u;return 1;
#else
    return 0;
#endif
}


void TitanPademuInputBridge_RequestBondRestore(void)
{
#if defined(__EE__) || defined(_EE)
    g_bond_restore_requested = 1;
#endif
}

int TitanPademuInputBridge_RequestWireless(unsigned int command,unsigned int index)
{
#if defined(__EE__) || defined(_EE)
    if(command>TITAN_PADEMU_WIRELESS_BOOTSTRAP)return 0;
    if(command!=TITAN_PADEMU_WIRELESS_QUERY && !TitanBluetoothRuntime_Request())return 0;
    if(command==TITAN_PADEMU_WIRELESS_QUERY){if(g_wireless_requested||g_wireless_rpc_pending)return 1;}
    else if(g_wireless_requested&&g_wireless_request.command!=TITAN_PADEMU_WIRELESS_QUERY)return 0;
    memset(&g_wireless_request,0,sizeof(g_wireless_request));g_wireless_request.version=TITAN_PADEMU_WIRELESS_VERSION;g_wireless_request.command=command;g_wireless_request.index=index;g_wireless_requested=1;
    if(command!=TITAN_PADEMU_WIRELESS_QUERY)TitanDiagnostics_Event(TITAN_DIAG_INFO,TITAN_DIAG_INPUT,0x0315u,0,command,index,g_frame,0u);
    return 1;
#else
    (void)command;(void)index;return 0;
#endif
}

#if defined(__EE__) || defined(_EE)
static int TitanPademuInputBridge_ReadFile(const char *path,void *data,unsigned int size)
{
    int fd=TitanStorageIo_Open(path,TITAN_STORAGE_IO_READ);
    int ok=0;if(fd<0)return 0;ok=TitanStorageIo_ReadExact(fd,data,size);TitanStorageIo_Close(fd);return ok;
}

static int TitanPademuInputBridge_WriteAtomic(const char *tmp,const char *path,const char *bak,const void *data,unsigned int size)
{
    int fd=TitanStorageIo_Open(tmp,TITAN_STORAGE_IO_WRITE|TITAN_STORAGE_IO_CREATE|TITAN_STORAGE_IO_TRUNCATE);
    int ok;if(fd<0)return 0;ok=TitanStorageIo_WriteExact(fd,data,size);if(ok)ok=TitanStorageIo_Flush(fd)>=0;TitanStorageIo_Close(fd);
    if(!ok){(void)TitanStorageIo_Remove(tmp);return 0;}
    if(TitanStorageIo_AtomicReplace(tmp,path,bak)<0){(void)TitanStorageIo_Remove(tmp);return 0;}
    (void)TitanStorageIo_Remove(bak);return 1;
}

int TitanPademuInputBridge_HasStorageWork(void)
{
    if(g_bond_storage_load_pending)return 1;
    if(g_bond_storage_write_pending && !(g_wireless_valid&&(g_wireless_state.flags&TITAN_PADEMU_WIRELESS_CONNECT_IN_PROGRESS)))return 1;
    return 0;
}

int TitanPademuInputBridge_StorageWorkerPump(void)
{
    if(g_bond_storage_load_pending){
        TitanPademuClassicBondFile classic;TitanPademuXboxBond xbox;
        memset(&g_bond_cache,0,sizeof(g_bond_cache));g_bond_cache.version=TITAN_PADEMU_BOND_SYNC_VERSION;
        memset(&classic,0,sizeof(classic));memset(&xbox,0,sizeof(xbox));
        if(TitanPademuInputBridge_ReadFile(TITAN_BT_CLASSIC_BOND_PATH,&classic,sizeof(classic)) && classic.magic==TITAN_PADEMU_CLASSIC_BOND_MAGIC && classic.version==TITAN_PADEMU_CLASSIC_BOND_VERSION && classic.count<=TITAN_PADEMU_WIRELESS_MAX_ACCESSORIES){g_bond_cache.classic=classic;g_bond_cache.flags|=TITAN_PADEMU_BOND_SYNC_CLASSIC_VALID;}
        else{g_bond_cache.classic.magic=TITAN_PADEMU_CLASSIC_BOND_MAGIC;g_bond_cache.classic.version=TITAN_PADEMU_CLASSIC_BOND_VERSION;g_bond_cache.flags|=TITAN_PADEMU_BOND_SYNC_CLASSIC_VALID;}
        if(TitanPademuInputBridge_ReadFile(TITAN_BT_XBOX_BOND_PATH,&xbox,sizeof(xbox)) && xbox.magic==TITAN_PADEMU_XBOX_BOND_MAGIC){g_bond_cache.xbox=xbox;g_bond_cache.flags|=TITAN_PADEMU_BOND_SYNC_XBOX_VALID;}
        g_bond_storage_load_pending=0;g_bond_import_requested=1;
        TitanDiagnostics_Event(TITAN_DIAG_INFO,TITAN_DIAG_INPUT,0x0318u,0,0x4C4F4144u,g_bond_cache.flags,g_bond_cache.classic.count,0u);
        return 1;
    }
    if(g_bond_storage_write_pending){
        int classic_ok=1,xbox_ok=1;
        if(g_wireless_valid&&(g_wireless_state.flags&TITAN_PADEMU_WIRELESS_CONNECT_IN_PROGRESS))return 0;
        if(g_bond_cache.flags&TITAN_PADEMU_BOND_SYNC_CLASSIC_VALID)classic_ok=TitanPademuInputBridge_WriteAtomic(TITAN_BT_CLASSIC_BOND_TMP,TITAN_BT_CLASSIC_BOND_PATH,TITAN_BT_CLASSIC_BOND_BAK,&g_bond_cache.classic,sizeof(g_bond_cache.classic));
        if(g_bond_cache.flags&TITAN_PADEMU_BOND_SYNC_XBOX_VALID)xbox_ok=TitanPademuInputBridge_WriteAtomic(TITAN_BT_XBOX_BOND_TMP,TITAN_BT_XBOX_BOND_PATH,TITAN_BT_XBOX_BOND_BAK,&g_bond_cache.xbox,sizeof(g_bond_cache.xbox));
        else{(void)TitanStorageIo_Remove(TITAN_BT_XBOX_BOND_PATH);(void)TitanStorageIo_Remove(TITAN_BT_XBOX_BOND_TMP);}
        TitanDiagnostics_Event((classic_ok&&xbox_ok)?TITAN_DIAG_INFO:TITAN_DIAG_ERROR,TITAN_DIAG_INPUT,0x0318u,(classic_ok&&xbox_ok)?0:-1,0x53415645u,g_bond_persist_generation,(uint32_t)classic_ok,(uint32_t)xbox_ok);
        if(classic_ok&&xbox_ok){g_bond_storage_write_pending=0;g_bond_ack_requested=1;return 1;}
    }
    return 0;
}
#else
int TitanPademuInputBridge_HasStorageWork(void){return 0;}
int TitanPademuInputBridge_StorageWorkerPump(void){return 0;}
#endif

const TitanPademuWirelessState *TitanPademuInputBridge_GetWirelessState(void){return g_wireless_valid?&g_wireless_state:0;}
int TitanPademuInputBridge_HasWirelessState(void){return g_wireless_valid;}
const TitanPademuPairingRuntime *TitanPademuInputBridge_GetBluetoothPairingRuntime(void){return &g_pairing;}
const TitanPademuBridgeMetrics *TitanPademuInputBridge_GetMetrics(void){return &g_metrics;}
#if defined(TITAN_PADEMU_BRIDGE_HOST_TEST)
void TitanPademuInputBridge_TestSetSnapshot(const TitanPademuTelemetrySnapshot *s){if(s){g_usb_snapshot=*s;g_usb_snapshot_valid=s->version==TITAN_PADEMU_TELEMETRY_VERSION;}else{memset(&g_usb_snapshot,0,sizeof(g_usb_snapshot));g_usb_snapshot_valid=0;}}
#endif
