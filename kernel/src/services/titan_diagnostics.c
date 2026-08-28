#include "titan_diagnostics.h"
#include "titan_time.h"
#include "titan_network.h"
#include "titan_asset_service.h"
#include "titan_storage_scheduler.h"
#include "titan_dex.h"
#include "titan_storage_io.h"
#include "titan_vlombiscore.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__GNUC__)
extern void TitanVlombisCore_RecordFrameBudget(uint32_t frame_us, uint32_t budget_us) __attribute__((weak));
#endif

#if defined(__EE__) || defined(_EE)
#include <kernel.h>
#endif

#define TITAN_DIAG_MAGIC 0x54444731u /* TDG1 */
#define TITAN_DIAG_EVENT_CAPACITY 2048u
#define TITAN_DIAG_FRAME_WINDOW 120u
#define TITAN_DIAG_FLUSH_FRAMES 120u
#define TITAN_DIAG_PATH "mass:/TITAN/logs/titan-current.bin"
#define TITAN_DIAG_PREVIOUS_PATH "mass:/TITAN/logs/titan-previous.bin"
#define TITAN_DIAG_PREVIOUS2_PATH "mass:/TITAN/logs/titan-previous-2.bin"
#define TITAN_DIAG_PREVIOUS3_PATH "mass:/TITAN/logs/titan-previous-3.bin"
#define TITAN_DIAG_PREVIOUS4_PATH "mass:/TITAN/logs/titan-previous-4.bin"
#define TITAN_DIAG_PREVIOUS5_PATH "mass:/TITAN/logs/titan-previous-5.bin"
#define TITAN_DIAG_TEXT_PATH "mass:/TITAN/logs/titan-last.txt"
#define TITAN_DIAG_PREVIOUS_TEXT_PATH "mass:/TITAN/logs/titan-previous.txt"
#define TITAN_DIAG_DIR "mass:/TITAN/logs"
#define TITAN_DIAG_SPIKE_US 25000u
#define TITAN_DIAG_STALL_US 250000u
#define TITAN_DIAG_PERCENTILE_SAMPLE_FRAMES 8u
#define TITAN_DIAG_SERVICE_SAMPLE_FRAMES 30u

typedef struct TitanDiagEvent {
    uint32_t timestamp_ms;
    uint16_t subsystem;
    uint16_t event;
    int32_t result;
    uint8_t level;
    uint8_t reserved[3];
    uint32_t a,b,c,d;
} TitanDiagEvent;

typedef struct TitanDiagDiskHeader {
    uint32_t magic, version, session_id, event_count, event_size;
    TitanDiagPerfSnapshot perf;
    uint32_t flags;
    uint32_t reserved[3];
} TitanDiagDiskHeader;

#define TITAN_DIAG_DISK_VERSION 2u
#define TITAN_DIAG_FLAG_CLEAN_SHUTDOWN 0x00000001u

typedef struct TitanDiagnosticsState {
    int initialized;
    int overlay_enabled;
    TitanDiagLevel maximum_level;
    uint32_t subsystem_mask;
    volatile unsigned int write_index;
    volatile unsigned int event_count;
    volatile unsigned int dirty;
    unsigned int flushed_index;
    unsigned int history_prepared;
    unsigned int clean_shutdown;
    unsigned int previous_present;
    uint32_t session_id;
    uint64_t frame_begin_us;
    uint64_t phase_begin_us[TITAN_DIAG_PHASE_COUNT];
    uint32_t frame_us[TITAN_DIAG_FRAME_WINDOW];
    unsigned int frame_pos, frame_count;
    uint64_t last_main_ms, last_storage_ms;
    uint32_t heartbeat_main, heartbeat_storage;
    uint32_t last_network_state;
    uint32_t last_asset_completed;
    TitanDiagPerfSnapshot perf;
    TitanDiagEvent events[TITAN_DIAG_EVENT_CAPACITY];
} TitanDiagnosticsState;

static TitanDiagnosticsState g_diag;

static uint32_t diag_now_ms(void){ return (uint32_t)TitanTime_NowMilliseconds(); }
static unsigned int diag_min_u(unsigned int a,unsigned int b){return a<b?a:b;}
static int diag_is_sample_count(uint32_t count){return count<=8u||((count&(count-1u))==0u);}

static void diag_mkdirs(void){ TitanStorageIo_CreateDirectory("mass:/TITAN"); TitanStorageIo_CreateDirectory(TITAN_DIAG_DIR); }

static int diag_path_exists(const char *path)
{
    int is_directory = 0;
    TitanStorageIoSize size = 0;
    return path != 0 && TitanStorageIo_PathInfo64(path, &is_directory, &size) == 0 && !is_directory;
}

static void diag_rotate_previous_sessions(void)
{
    if (g_diag.history_prepared)
        return;
    diag_mkdirs();
    if (diag_path_exists(TITAN_DIAG_PATH))
    {
        (void)TitanStorageIo_Remove(TITAN_DIAG_PREVIOUS5_PATH);
        if (diag_path_exists(TITAN_DIAG_PREVIOUS4_PATH))
            (void)TitanStorageIo_Rename(TITAN_DIAG_PREVIOUS4_PATH, TITAN_DIAG_PREVIOUS5_PATH);
        if (diag_path_exists(TITAN_DIAG_PREVIOUS3_PATH))
            (void)TitanStorageIo_Rename(TITAN_DIAG_PREVIOUS3_PATH, TITAN_DIAG_PREVIOUS4_PATH);
        if (diag_path_exists(TITAN_DIAG_PREVIOUS2_PATH))
            (void)TitanStorageIo_Rename(TITAN_DIAG_PREVIOUS2_PATH, TITAN_DIAG_PREVIOUS3_PATH);
        if (diag_path_exists(TITAN_DIAG_PREVIOUS_PATH))
            (void)TitanStorageIo_Rename(TITAN_DIAG_PREVIOUS_PATH, TITAN_DIAG_PREVIOUS2_PATH);
        (void)TitanStorageIo_Remove(TITAN_DIAG_PREVIOUS_PATH);
        if (TitanStorageIo_Rename(TITAN_DIAG_PATH, TITAN_DIAG_PREVIOUS_PATH) == 0)
            g_diag.previous_present = 1u;
    }
    else if (diag_path_exists(TITAN_DIAG_PREVIOUS_PATH))
        g_diag.previous_present = 1u;
    g_diag.history_prepared = 1u;
}

void TitanDiagnostics_Event(TitanDiagLevel level,TitanDiagSubsystem subsystem,uint16_t event,int32_t result,uint32_t a,uint32_t b,uint32_t c,uint32_t d){
    unsigned int slot;
    TitanDiagEvent *e;
    if(!g_diag.initialized) return;
    if (level < TITAN_DIAG_ERROR || level > g_diag.maximum_level) return;
    if ((unsigned int)subsystem >= 32u || (g_diag.subsystem_mask & (1u << (unsigned int)subsystem)) == 0u) return;
    slot=g_diag.write_index % TITAN_DIAG_EVENT_CAPACITY;
    e=&g_diag.events[slot];
    e->timestamp_ms=diag_now_ms(); e->subsystem=(uint16_t)subsystem; e->event=event; e->result=result; e->level=(uint8_t)level;
    e->a=a;e->b=b;e->c=c;e->d=d;
    g_diag.write_index=(g_diag.write_index+1u)%TITAN_DIAG_EVENT_CAPACITY;
    if(g_diag.event_count<TITAN_DIAG_EVENT_CAPACITY) g_diag.event_count++;
    g_diag.dirty=1u;
}

void TitanDiagnostics_Initialize(void){
    memset(&g_diag,0,sizeof(g_diag)); g_diag.initialized=1; g_diag.maximum_level=TITAN_DIAG_TRACE; g_diag.subsystem_mask=0xffffffffu; g_diag.session_id=diag_now_ms() ^ 0x5449544eu; g_diag.last_network_state=0xffffffffu;
    TitanDiagnostics_Event(TITAN_DIAG_INFO,TITAN_DIAG_BOOT,1,0,g_diag.session_id,0,0,0);
}
void TitanDiagnostics_Shutdown(void){ if(g_diag.initialized){TitanDiagnostics_MarkCleanShutdown();} }

void TitanDiagnostics_MarkCleanShutdown(void)
{
    if (!g_diag.initialized)
        return;
    g_diag.clean_shutdown = 1u;
    TitanDiagnostics_Event(TITAN_DIAG_INFO,TITAN_DIAG_BOOT,2,0,1u,0,0,0);
    g_diag.dirty = 1u;
    /* Power/restart paths call this only after storage has been quiesced.
     * Persist immediately so the next boot can distinguish a clean exit. */
    (void)TitanDiagnostics_StorageWorkerPump();
}
void TitanDiagnostics_BeginFrame(uint32_t frame_index)
{
    (void)frame_index;
    if (!g_diag.initialized)
    {
        return;
    }
    g_diag.frame_begin_us = TitanTime_NowMicroseconds();
}

void TitanDiagnostics_PhaseBegin(TitanDiagPhase phase)
{
    if (!g_diag.initialized || phase >= TITAN_DIAG_PHASE_COUNT)
    {
        return;
    }
    g_diag.phase_begin_us[phase] = TitanTime_NowMicroseconds();
}

void TitanDiagnostics_PhaseEnd(TitanDiagPhase phase)
{
    uint64_t now;
    uint32_t elapsed;
    if (!g_diag.initialized || phase >= TITAN_DIAG_PHASE_COUNT || g_diag.phase_begin_us[phase] == 0u)
    {
        return;
    }
    now = TitanTime_NowMicroseconds();
    elapsed = now > g_diag.phase_begin_us[phase] ? (uint32_t)(now - g_diag.phase_begin_us[phase]) : 0u;
    if (phase == TITAN_DIAG_PHASE_INPUT) g_diag.perf.phase_input_us = elapsed;
    else if (phase == TITAN_DIAG_PHASE_AUDIO) g_diag.perf.phase_audio_us = elapsed;
    else if (phase == TITAN_DIAG_PHASE_SERVICES) g_diag.perf.phase_services_us = elapsed;
    else if (phase == TITAN_DIAG_PHASE_UI) g_diag.perf.phase_ui_us = elapsed;
    else if (phase == TITAN_DIAG_PHASE_RENDER) g_diag.perf.phase_render_us = elapsed;
    g_diag.phase_begin_us[phase] = 0u;
}

static int cmp_u32(const void *aa,const void *bb){uint32_t a=*(const uint32_t*)aa,b=*(const uint32_t*)bb;return a<b?-1:a>b?1:0;}
static void diag_update_perf(uint32_t frame_index,uint32_t frame_us){
    uint64_t sum=0; uint32_t worst=0; unsigned int i,n; uint32_t tmp[TITAN_DIAG_FRAME_WINDOW];
    g_diag.frame_us[g_diag.frame_pos++ % TITAN_DIAG_FRAME_WINDOW]=frame_us; if(g_diag.frame_count<TITAN_DIAG_FRAME_WINDOW)g_diag.frame_count++;
    n=g_diag.frame_count; for(i=0;i<n;i++){tmp[i]=g_diag.frame_us[i];sum+=tmp[i];if(tmp[i]>worst)worst=tmp[i];}
    g_diag.perf.frame_index=frame_index; g_diag.perf.frame_us=frame_us; g_diag.perf.frame_avg_us=n?(uint32_t)(sum/n):0; g_diag.perf.frame_worst_us=worst;
    /* Percentile sorting is diagnostics-only and does not belong on every frame. */
    if (n != 0u && ((frame_index % TITAN_DIAG_PERCENTILE_SAMPLE_FRAMES) == 0u || g_diag.perf.frame_p95_us == 0u)) {
        qsort(tmp,n,sizeof(tmp[0]),cmp_u32);
        g_diag.perf.frame_p95_us=tmp[(n*95u/100u)>=n?n-1u:(n*95u/100u)];
    }
    g_diag.perf.fps_x100=g_diag.perf.frame_avg_us?100000000u/g_diag.perf.frame_avg_us:0;
    g_diag.perf.heartbeat_main=g_diag.heartbeat_main; g_diag.perf.heartbeat_storage=g_diag.heartbeat_storage;
}
void TitanDiagnostics_EndFrame(uint32_t frame_index){
    uint64_t now; uint32_t us;
    if (!g_diag.initialized)
    {
        return;
    }
    now = TitanTime_NowMicroseconds();
    us = now > g_diag.frame_begin_us ? (uint32_t)(now - g_diag.frame_begin_us) : 0u;
    if (us == 0u)
    {
        us = 1u;
    }
    g_diag.heartbeat_main++; g_diag.last_main_ms=now / 1000u; diag_update_perf(frame_index,us);
#if defined(__GNUC__)
    if (TitanVlombisCore_RecordFrameBudget != 0)
        TitanVlombisCore_RecordFrameBudget(us, 16667u);
#else
    TitanVlombisCore_RecordFrameBudget(us, 16667u);
#endif
    if(us>=TITAN_DIAG_SPIKE_US){
        uint32_t spike_count=++g_diag.perf.spikes;
        int stall=us>=TITAN_DIAG_STALL_US;
        /* Do not let ordinary 33 ms missed-vsync samples evict every network,
         * package, input and asset event from the 2048-entry flight recorder.
         * Counters remain exact; detailed events are sampled, while real
         * stalls are always retained. */
        if(stall||diag_is_sample_count(spike_count)){
            uint32_t service_ui=g_diag.perf.phase_services_us+g_diag.perf.phase_ui_us;
            TitanDiagnostics_Event(stall?TITAN_DIAG_ERROR:TITAN_DIAG_WARN,TITAN_DIAG_KERNEL,0x100,0,frame_index,us,g_diag.perf.frame_avg_us,g_diag.perf.frame_p95_us);
            TitanDiagnostics_Event(stall?TITAN_DIAG_ERROR:TITAN_DIAG_WARN,TITAN_DIAG_KERNEL,0x101,0,g_diag.perf.phase_input_us,g_diag.perf.phase_audio_us,service_ui,g_diag.perf.phase_render_us);
        }
    }
}
void TitanDiagnostics_StorageHeartbeat(void){if(!g_diag.initialized)return;g_diag.heartbeat_storage++;g_diag.last_storage_ms=TitanTime_NowMilliseconds();}

void TitanDiagnostics_UpdateMainThread(uint32_t frame_index){
    const TitanNetworkStatus *net; const TitanAssetServiceStatus *asset; uint64_t now;
    if (!g_diag.initialized)
    {
        return;
    }
    now = TitanTime_NowMilliseconds();
    /* Non-critical DEX telemetry is sampled at 2 Hz at 60 fps. Watchdog state
     * remains frame-current below. */
    if ((frame_index % TITAN_DIAG_SERVICE_SAMPLE_FRAMES) == 0u) {
        net = TitanNetwork_GetStatus();
        asset = TitanAssetService_GetStatus();
        if(net){g_diag.perf.network_state=(uint32_t)net->state;if(g_diag.last_network_state!=(uint32_t)net->state){TitanDiagnostics_Event(TITAN_DIAG_INFO,TITAN_DIAG_NETWORK,0x200,0,g_diag.last_network_state,(uint32_t)net->state,(uint32_t)net->wifi_online,(uint32_t)net->ethernet_online);g_diag.last_network_state=(uint32_t)net->state;}}
        if(asset){g_diag.perf.asset_queue_hint=asset->visible_game_count + (asset->network_busy?1u:0u);if(g_diag.last_asset_completed!=asset->downloads_completed){TitanDiagnostics_Event(TITAN_DIAG_INFO,TITAN_DIAG_ASSET,0x300,0,asset->downloads_completed,asset->cache_hits,asset->cache_misses,(uint32_t)asset->network_busy);g_diag.last_asset_completed=asset->downloads_completed;}}
        g_diag.perf.ee_ram_used_kb=TitanDex_GetRamUsedKilobytes();
    }
    g_diag.perf.storage_busy=(uint32_t)TitanStorageScheduler_IsForegroundBusy();
    if(g_diag.last_storage_ms!=0u && now-g_diag.last_storage_ms>5000u && g_diag.perf.storage_busy){TitanDiagnostics_Event(TITAN_DIAG_ERROR,TITAN_DIAG_WATCHDOG,0x401,-1,(uint32_t)(now-g_diag.last_storage_ms),g_diag.heartbeat_storage,0,0);g_diag.last_storage_ms=now;}
    if((frame_index % TITAN_DIAG_FLUSH_FRAMES)==0u)g_diag.dirty=1u;
}

int TitanDiagnostics_HasStorageWork(void){return g_diag.initialized && g_diag.dirty;}
int TitanDiagnostics_StorageWorkerPump(void){
    int fd; TitanDiagDiskHeader h; unsigned int count,start,i; TitanDiagEvent e;
    if (!TitanDiagnostics_HasStorageWork())
    {
        return 0;
    }
    diag_rotate_previous_sessions();
    fd=TitanStorageIo_Open(TITAN_DIAG_PATH,TITAN_STORAGE_IO_WRITE|TITAN_STORAGE_IO_CREATE|TITAN_STORAGE_IO_TRUNCATE); if(fd<0)return 0;
    memset(&h,0,sizeof(h));h.magic=TITAN_DIAG_MAGIC;h.version=TITAN_DIAG_DISK_VERSION;h.session_id=g_diag.session_id;h.event_count=g_diag.event_count;h.event_size=sizeof(TitanDiagEvent);h.perf=g_diag.perf;h.flags=g_diag.clean_shutdown?TITAN_DIAG_FLAG_CLEAN_SHUTDOWN:0u;
    if(!TitanStorageIo_WriteExact(fd,&h,sizeof(h))){TitanStorageIo_Close(fd);return 0;}
    count=g_diag.event_count; start=(g_diag.write_index + TITAN_DIAG_EVENT_CAPACITY - count)%TITAN_DIAG_EVENT_CAPACITY;
    for(i=0;i<count;i++){e=g_diag.events[(start+i)%TITAN_DIAG_EVENT_CAPACITY];if(!TitanStorageIo_WriteExact(fd,&e,sizeof(e))){TitanStorageIo_Close(fd);return 0;}}
    TitanStorageIo_Flush(fd);TitanStorageIo_Close(fd);g_diag.dirty=0u;g_diag.flushed_index=g_diag.write_index;return 1;
}
const TitanDiagPerfSnapshot *TitanDiagnostics_GetPerfSnapshot(void){return &g_diag.perf;}
int TitanDiagnostics_IsOverlayEnabled(void){return g_diag.overlay_enabled;}
void TitanDiagnostics_SetOverlayEnabled(int enabled){g_diag.overlay_enabled=enabled!=0;}
void TitanDiagnostics_SetMaximumLevel(TitanDiagLevel maximum_level)
{
    if (maximum_level < TITAN_DIAG_ERROR) maximum_level = TITAN_DIAG_ERROR;
    if (maximum_level > TITAN_DIAG_TRACE) maximum_level = TITAN_DIAG_TRACE;
    g_diag.maximum_level = maximum_level;
}
TitanDiagLevel TitanDiagnostics_GetMaximumLevel(void){return g_diag.maximum_level;}
void TitanDiagnostics_SetSubsystemEnabled(TitanDiagSubsystem subsystem, int enabled)
{
    uint32_t bit;
    if ((unsigned int)subsystem >= 32u) return;
    bit = 1u << (unsigned int)subsystem;
    if (enabled) g_diag.subsystem_mask |= bit; else g_diag.subsystem_mask &= ~bit;
}
int TitanDiagnostics_IsSubsystemEnabled(TitanDiagSubsystem subsystem)
{
    if ((unsigned int)subsystem >= 32u) return 0;
    return (g_diag.subsystem_mask & (1u << (unsigned int)subsystem)) != 0u;
}

static const char *diag_subsystem_name(unsigned int s){static const char *n[]={"?","BOOT","KERNEL","SHELL","INPUT","AUDIO","STORAGE","LIBRARY","PACKAGE","UPDATE","VMC","NETWORK","WIFI","NETMAN","PS2IP","ASSET","BROWSER","THEME","VIDEO","MEMORY","WATCHDOG","PIKAVR","SAS","BADGES","VLOMBIS","APP_RUNTIME"};return s<sizeof(n)/sizeof(n[0])?n[s]:"?";}
static const char *diag_event_name(unsigned int subsystem,unsigned int event)
{
    if (subsystem == TITAN_DIAG_APP_RUNTIME && event == 0x27F0u)
        return "TMOD_FLAGS_REJECT";
    if (subsystem == TITAN_DIAG_INPUT && event == 0x0310u) return "EARLY_INPUT_STATE";
    if (subsystem == TITAN_DIAG_INPUT && event == 0x0311u) return "BT_PAIRING_STATE";
    if (subsystem == TITAN_DIAG_INPUT && event == 0x0312u) return "INPUT_STEP_STALL";
    if (subsystem == TITAN_DIAG_INPUT && event == 0x0313u) return "PADEMU_IOP_TRACE";
    if (subsystem == TITAN_DIAG_INPUT && event == 0x0314u) return "PADEMU_TRACE_STATUS";
    if (subsystem == TITAN_DIAG_INPUT && event == 0x0315u) return "WIRELESS_RPC_ACTION";
    if (subsystem == TITAN_DIAG_INPUT && event == 0x0316u) return "BT_RUNTIME_STATE";
    if (subsystem == TITAN_DIAG_INPUT && event == 0x0317u) return "BT_RUNTIME_IMAGE";
    if (subsystem == TITAN_DIAG_INPUT && event == 0x0318u) return "BT_BOND_SYNC";
    if (subsystem == TITAN_DIAG_KERNEL && event == 0x0102u) return "SERVICE_STEP_STALL";
    if (subsystem != TITAN_DIAG_ASSET) return "";
    switch (event)
    {
        case 0x320: return "ASSET_SESSION_BEGIN";
        case 0x321: return "DNS_BEGIN";
        case 0x322: return "DNS_END";
        case 0x323: return "TCP_BEGIN";
        case 0x324: return "TCP_END";
        case 0x325: return "TLS_BEGIN";
        case 0x326: return "TLS_END";
        case 0x327: return "DOWNLOAD_BEGIN";
        case 0x328: return "DOWNLOAD_END";
        case 0x329: return "DECODE_BEGIN";
        case 0x32a: return "DECODE_END";
        case 0x32b: return "CACHE_BEGIN";
        case 0x32c: return "CACHE_END";
        case 0x32d: return "ASSET_SESSION_END";
        case 0x32e: return "AUTO_PAUSE";
        case 0x32f: return "AUTO_RESUME";
        case 0x330: return "ASSET_STACK_CANARY_FAIL";
        case 0x331: return "ASSET_STACK_MIN_FREE";
        case 0x332: return "ASSET_STORAGE_JOB_REJECT";
        case 0x333: return "TLS_ENGINE_ERROR";
        case 0x334: return "HTTP_PROTOCOL_ERROR";
        case 0x335: return "CACHE_WRITE_FAIL";
        case 0x336: return "CACHE_LOAD_FAIL";
        case 0x337: return "CATALOG_RACE_DROP";
        case 0x338: return "SOURCE_ALLOC_FAIL";
        default: return "";
    }
}
static const char *diag_pademu_trace_name(unsigned int code)
{
    switch(code)
    {
        case 1: return "MODULE_START"; case 2: return "ARGS"; case 3: return "EXPORT_REGISTER";
        case 4: return "SIO2_POLICY"; case 5: return "PAD_INIT_BEGIN"; case 6: return "PAD_INIT_END"; case 7: return "RPC_START";
        case 16: return "BT_INIT_BEGIN"; case 17: return "BT_MUTEX_CREATE"; case 18: return "BT_DRIVER_REGISTER"; case 19: return "BT_CHARGE_DRIVER";
        case 32: return "BT_USB_PROBE"; case 33: return "BT_USB_DEVICE"; case 34: return "BT_USB_INTERFACE"; case 35: return "BT_USB_PROBE_RESULT";
        case 36: return "BT_USB_CONNECT"; case 37: return "BT_USB_ENDPOINT"; case 38: return "BT_USB_SET_CONFIG"; case 39: return "BT_USB_CONFIG_CB";
        case 40: return "BT_USB_RX_ARM"; case 41: return "BT_USB_DISCONNECT"; case 48: return "BT_HCI_COMMAND"; case 49: return "BT_HCI_EVENT";
        case 50: return "BT_HCI_CONNECT"; case 64: return "BT_L2CAP_RX"; case 65: return "BT_HID_RUNNING"; case 66: return "BT_HID_INPUT";
        case 80: return "BT_PAIR_REQUEST"; default: return "UNKNOWN";
    }
}

static int diag_format_event_line(char *line,unsigned int cap,const TitanDiagEvent *e)
{
    if(e->subsystem==TITAN_DIAG_INPUT && e->event==0x0313u)
        return snprintf(line,cap,"%10u %-9s ev=%04X %-20s trace=%-20s lvl=%u rc=%d x=%08X y=%08X z=%08X\n",
                        e->timestamp_ms,diag_subsystem_name(e->subsystem),e->event,diag_event_name(e->subsystem,e->event),
                        diag_pademu_trace_name(e->a),e->level,e->result,e->b,e->c,e->d);
    return snprintf(line,cap,"%10u %-9s ev=%04X %-20s lvl=%u rc=%d a=%08X b=%08X c=%08X d=%08X\n",
                    e->timestamp_ms,diag_subsystem_name(e->subsystem),e->event,diag_event_name(e->subsystem,e->event),
                    e->level,e->result,e->a,e->b,e->c,e->d);
}

static int diag_export_file_to_text(const char *source_path,const char *text_path)
{
    int in_fd, out_fd;
    TitanDiagDiskHeader h;
    TitanDiagEvent e;
    char line[256];
    unsigned int i;
    int len;
    size_t legacy_header_size;
    if (source_path == 0 || text_path == 0)
        return 0;
    in_fd = TitanStorageIo_Open(source_path, TITAN_STORAGE_IO_READ);
    if (in_fd < 0)
        return 0;
    memset(&h, 0, sizeof(h));
    legacy_header_size = sizeof(h) - sizeof(h.flags) - sizeof(h.reserved);
    if (!TitanStorageIo_ReadExact(in_fd, &h, legacy_header_size) || h.magic != TITAN_DIAG_MAGIC)
    {
        TitanStorageIo_Close(in_fd);
        return 0;
    }
    if (h.version >= TITAN_DIAG_DISK_VERSION)
    {
        if (!TitanStorageIo_ReadExact(in_fd, &h.flags, sizeof(h.flags) + sizeof(h.reserved)))
        {
            TitanStorageIo_Close(in_fd);
            return 0;
        }
    }
    if (h.event_size != sizeof(TitanDiagEvent) || h.event_count > TITAN_DIAG_EVENT_CAPACITY)
    {
        TitanStorageIo_Close(in_fd);
        return 0;
    }
    out_fd = TitanStorageIo_Open(text_path,TITAN_STORAGE_IO_WRITE|TITAN_STORAGE_IO_CREATE|TITAN_STORAGE_IO_TRUNCATE);
    if (out_fd < 0)
    {
        TitanStorageIo_Close(in_fd);
        return 0;
    }
    len=snprintf(line,sizeof(line),"Project Titan Diagnostics session=%08X source=%s\nstatus=%s version=%u events=%u\nFPS=%u.%02u avg=%uus p95=%uus worst=%uus spikes=%u mainHB=%u storageHB=%u net=%u assets=%u storageBusy=%u ramUsed=%uKB\nPHASE input=%uus audio=%uus services=%uus ui=%uus render=%uus\n\n",h.session_id,source_path,(h.flags&TITAN_DIAG_FLAG_CLEAN_SHUTDOWN)?"CLEAN_SHUTDOWN":"UNCLEAN_OR_RESET",h.version,h.event_count,h.perf.fps_x100/100u,h.perf.fps_x100%100u,h.perf.frame_avg_us,h.perf.frame_p95_us,h.perf.frame_worst_us,h.perf.spikes,h.perf.heartbeat_main,h.perf.heartbeat_storage,h.perf.network_state,h.perf.asset_queue_hint,h.perf.storage_busy,h.perf.ee_ram_used_kb,h.perf.phase_input_us,h.perf.phase_audio_us,h.perf.phase_services_us,h.perf.phase_ui_us,h.perf.phase_render_us);
    if (len > 0)
        (void)TitanStorageIo_WriteExact(out_fd,line,(unsigned int)diag_min_u((unsigned int)len,sizeof(line)-1u));
    for (i=0;i<h.event_count;i++)
    {
        if (!TitanStorageIo_ReadExact(in_fd,&e,sizeof(e)))
            break;
        len=diag_format_event_line(line,sizeof(line),&e);
        if (len > 0)
            (void)TitanStorageIo_WriteExact(out_fd,line,(unsigned int)diag_min_u((unsigned int)len,sizeof(line)-1u));
    }
    (void)TitanStorageIo_Flush(out_fd);
    TitanStorageIo_Close(out_fd);
    TitanStorageIo_Close(in_fd);
    return i == h.event_count;
}

int TitanDiagnostics_HasPreviousSession(void)
{
    if (g_diag.previous_present)
        return 1;
    return diag_path_exists(TITAN_DIAG_PREVIOUS_PATH);
}

int TitanDiagnostics_ExportPreviousTextNow(void)
{
    diag_mkdirs();
    return diag_export_file_to_text(TITAN_DIAG_PREVIOUS_PATH,TITAN_DIAG_PREVIOUS_TEXT_PATH);
}

int TitanDiagnostics_ExportTextNow(void){
    int fd; char line[256]; unsigned int count,start,i; TitanDiagEvent *e; int len;
    if (!g_diag.initialized)
    {
        return 0;
    }
    diag_mkdirs();
    fd = TitanStorageIo_Open(
        TITAN_DIAG_TEXT_PATH,
        TITAN_STORAGE_IO_WRITE | TITAN_STORAGE_IO_CREATE | TITAN_STORAGE_IO_TRUNCATE
    );
    if (fd < 0)
    {
        return 0;
    }
    len=snprintf(line,sizeof(line),"Project Titan Diagnostics session=%08X\nFPS=%u.%02u avg=%uus p95=%uus worst=%uus spikes=%u mainHB=%u storageHB=%u net=%u assets=%u storageBusy=%u ramUsed=%uKB\nPHASE input=%uus audio=%uus services=%uus ui=%uus render=%uus\n\n",g_diag.session_id,g_diag.perf.fps_x100/100u,g_diag.perf.fps_x100%100u,g_diag.perf.frame_avg_us,g_diag.perf.frame_p95_us,g_diag.perf.frame_worst_us,g_diag.perf.spikes,g_diag.heartbeat_main,g_diag.heartbeat_storage,g_diag.perf.network_state,g_diag.perf.asset_queue_hint,g_diag.perf.storage_busy,g_diag.perf.ee_ram_used_kb,g_diag.perf.phase_input_us,g_diag.perf.phase_audio_us,g_diag.perf.phase_services_us,g_diag.perf.phase_ui_us,g_diag.perf.phase_render_us);TitanStorageIo_WriteExact(fd,line,(unsigned int)len);
    count=g_diag.event_count;start=(g_diag.write_index+TITAN_DIAG_EVENT_CAPACITY-count)%TITAN_DIAG_EVENT_CAPACITY;
    for(i=0;i<count;i++){e=&g_diag.events[(start+i)%TITAN_DIAG_EVENT_CAPACITY];len=diag_format_event_line(line,sizeof(line),e);if(len>0)TitanStorageIo_WriteExact(fd,line,(unsigned int)diag_min_u((unsigned int)len,sizeof(line)-1u));}
    TitanStorageIo_Flush(fd);TitanStorageIo_Close(fd);return 1;
}
