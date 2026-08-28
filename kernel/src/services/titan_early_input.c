#include "titan_early_input.h"
#include "titan_iop_modules.h"
#include "titan_storage_io.h"
#include "titan_storage_paths.h"
#include "titan_early_input_wire.h"
#include "titan_diagnostics.h"
#include <kernel.h>
#include <loadfile.h>
#include <string.h>
#define TITAN_EARLY_INPUT_CONFIG_DIR TITAN_STORAGE_DEVICE_ROOT "/input"
#define TITAN_EARLY_INPUT_CONFIG_PATH TITAN_EARLY_INPUT_CONFIG_DIR "/early.bin"
#define TITAN_EARLY_INPUT_CONFIG_TEMP TITAN_EARLY_INPUT_CONFIG_DIR "/early.bin.tmp"
#define TITAN_EARLY_INPUT_CONFIG_BACKUP TITAN_EARLY_INPUT_CONFIG_DIR "/early.bin.bak"
#define TITAN_EARLY_INPUT_DEADLINE_MS 10000ull
#define TITAN_EARLY_INPUT_WORKER_STACK_BYTES (16u*1024u)
#define TITAN_EARLY_INPUT_WORKER_PRIORITY 62
static TitanEarlyInputStatus g_early;
static TitanEarlyInputConfigWire g_cfg;
static TitanEarlyInputState g_diag_last_state=TITAN_EARLY_INPUT_STATE_IDLE;
static void diag_state_if_changed(void)
{
    if(g_early.state==g_diag_last_state)return;
    g_diag_last_state=g_early.state;
    TitanDiagnostics_Event(
        g_early.state==TITAN_EARLY_INPUT_STATE_FAILED||g_early.state==TITAN_EARLY_INPUT_STATE_TIMED_OUT?TITAN_DIAG_ERROR:TITAN_DIAG_INFO,
        TITAN_DIAG_INPUT,0x0310u,(int32_t)g_early.last_module_result,
        (uint32_t)g_early.backend,(uint32_t)g_early.state,
        (uint32_t)g_early.smsutils_module_id,(uint32_t)g_early.pademu_module_id);
}
#if defined(__EE__) || defined(_EE)
extern void *_gp;
static unsigned char g_worker_stack[TITAN_EARLY_INPUT_WORKER_STACK_BYTES] __attribute__((aligned(16)));
static int g_worker_thread=-1;
static volatile int g_worker_done;
static volatile int g_worker_success;
#endif
static int read_config(TitanEarlyInputConfigWire *cfg)
{
    int fd;
    memset(cfg,0,sizeof(*cfg));
    fd=TitanStorageIo_Open(TITAN_EARLY_INPUT_CONFIG_PATH,TITAN_STORAGE_IO_READ);
    if(fd<0) return 0;
    if(!TitanStorageIo_ReadExact(fd,cfg,sizeof(*cfg))){TitanStorageIo_Close(fd);return 0;}
    TitanStorageIo_Close(fd);
    if(cfg->magic!=TITAN_EARLY_INPUT_CONFIG_MAGIC || cfg->version!=TITAN_EARLY_INPUT_CONFIG_VERSION) return 0;
    if(cfg->backend!=TITAN_EARLY_INPUT_BACKEND_USB && cfg->backend!=TITAN_EARLY_INPUT_BACKEND_BT) return 0;
    return 1;
}
static int exec_irx(const unsigned char *data,unsigned int size,int arg_len,const char *args,int *module_result)
{
    int result=-1;
    int id=SifExecModuleBuffer((void*)data,(int)size,arg_len,args,&result);
    if(module_result) *module_result=result;
    return (id>=0 && result>=0)?id:-1;
}
static unsigned int make_pademu_args(char *out,unsigned int cap,const TitanEarlyInputConfigWire *cfg)
{
    uint32_t settings=0,macro=0xabu;
    TitanPademuBootExtension ext;
    unsigned int n=8u+sizeof(ext);
    if(cap<n) return 0;
    memset(out,0,n);
    settings=(uint32_t)(cfg->pad_enable?cfg->pad_enable:3u) |
             ((uint32_t)(cfg->pad_vibration?cfg->pad_vibration:3u)<<8);
    settings|=TITAN_PADEMU_SETTINGS_TITAN_TRANSPORT_ONLY;
    memcpy(out,&settings,4);
    memcpy(out+4,&macro,4);
    memset(&ext,0,sizeof(ext));
    ext.magic=TITAN_PADEMU_BOOT_EXT_MAGIC;
    ext.xbox_bond=cfg->xbox_bond;
    memcpy(out+8,&ext,sizeof(ext));
    return n;
}
static int do_load(void)
{
    char args[96]; unsigned int arg_len; const unsigned char *image; unsigned int image_size;
    g_early.smsutils_module_id=exec_irx(titan_smsutils_irx,size_titan_smsutils_irx,0,0,&g_early.last_module_result);
    if(g_early.smsutils_module_id<0) return 0;
    arg_len=make_pademu_args(args,sizeof(args),&g_cfg); if(!arg_len) return 0;
    if(g_cfg.backend==TITAN_EARLY_INPUT_BACKEND_BT){image=titan_bt_pademu_irx;image_size=size_titan_bt_pademu_irx;}
    else {image=titan_usb_pademu_irx;image_size=size_titan_usb_pademu_irx;}
    g_early.pademu_module_id=exec_irx(image,image_size,(int)arg_len,args,&g_early.last_module_result);
    return g_early.pademu_module_id>=0;
}
#if defined(__EE__) || defined(_EE)
static void worker(void *arg){(void)arg;g_worker_success=do_load();g_worker_done=1;ExitDeleteThread();}
static int start_worker(void)
{
    ee_thread_t t; memset(&t,0,sizeof(t)); t.func=worker;t.stack=g_worker_stack;t.stack_size=sizeof(g_worker_stack);t.gp_reg=&_gp;t.initial_priority=TITAN_EARLY_INPUT_WORKER_PRIORITY;
    g_worker_thread=CreateThread(&t); if(g_worker_thread<0) return 0; if(StartThread(g_worker_thread,0)<0){DeleteThread(g_worker_thread);g_worker_thread=-1;return 0;} return 1;
}
#endif
TitanResult TitanEarlyInput_Initialize(void)
{
    memset(&g_early,0,sizeof(g_early));g_early.smsutils_module_id=-1;g_early.pademu_module_id=-1;g_early.deadline_ms=0u;g_early.state=TITAN_EARLY_INPUT_STATE_IDLE;g_diag_last_state=TITAN_EARLY_INPUT_STATE_IDLE;
    if(!read_config(&g_cfg)){g_early.initialized=1;return TITAN_RESULT_OK;}
    g_early.enabled=1;g_early.backend=(TitanEarlyInputBackend)g_cfg.backend;g_early.bond_loaded=(g_cfg.xbox_bond.magic==TITAN_EARLY_INPUT_BOND_VALID);g_early.state=TITAN_EARLY_INPUT_STATE_STARTING;
#if defined(__EE__) || defined(_EE)
    /* Do not issue LOADFILE RPCs from service initialization.  The worker is
     * armed here and started by the first runtime Pump, after kernel/service
     * bootstrap has completed. This keeps a bad BT adapter/IRX fail-open. */
    g_worker_done=0;g_worker_success=0;g_worker_thread=-1;
#else
    g_early.enabled=0;g_early.state=TITAN_EARLY_INPUT_STATE_FAILED;
#endif
    g_early.initialized=1; return TITAN_RESULT_OK;
}
void TitanEarlyInput_Pump(uint64_t now_ms)
{
    if(!g_early.initialized||!g_early.enabled) return;
    if(g_early.state==TITAN_EARLY_INPUT_STATE_STARTING){
#if defined(__EE__) || defined(_EE)
        if(!start_worker()){g_early.enabled=0;g_early.state=TITAN_EARLY_INPUT_STATE_FAILED;diag_state_if_changed();return;}
        g_early.state=TITAN_EARLY_INPUT_STATE_LOADING;
#else
        g_early.enabled=0;g_early.state=TITAN_EARLY_INPUT_STATE_FAILED;
#endif
        diag_state_if_changed();
    }
    if(g_early.state!=TITAN_EARLY_INPUT_STATE_LOADING) return;
    if(g_early.pump_count<0xffffffffu) g_early.pump_count++;
    if(g_early.deadline_ms==0u) g_early.deadline_ms=now_ms+TITAN_EARLY_INPUT_DEADLINE_MS;
#if defined(__EE__) || defined(_EE)
    if(g_worker_done){g_early.state=g_worker_success?TITAN_EARLY_INPUT_STATE_READY:TITAN_EARLY_INPUT_STATE_FAILED;if(!g_worker_success)g_early.enabled=0;diag_state_if_changed();return;}
#endif
    if(now_ms>=g_early.deadline_ms){g_early.state=TITAN_EARLY_INPUT_STATE_TIMED_OUT;g_early.enabled=0;diag_state_if_changed();}
}
void TitanEarlyInput_Shutdown(void)
{
#if defined(__EE__) || defined(_EE)
    /* Do not kill a loadfile RPC mid-call. A late worker is allowed to finish; it never owns shell state. */
    g_worker_thread=-1;g_worker_done=0;g_worker_success=0;
#endif
    memset(&g_early,0,sizeof(g_early));g_early.smsutils_module_id=-1;g_early.pademu_module_id=-1;
}
const TitanEarlyInputStatus *TitanEarlyInput_GetStatus(void){return &g_early;}

TitanEarlyInputBackend TitanEarlyInput_GetConfiguredBackend(void)
{
    TitanEarlyInputConfigWire cfg;
    if(read_config(&cfg)) return (TitanEarlyInputBackend)cfg.backend;
    return TITAN_EARLY_INPUT_NONE;
}

int TitanEarlyInput_SaveBackendPreference(TitanEarlyInputBackend backend)
{
    TitanEarlyInputConfigWire cfg;
    int fd;
    if(backend!=TITAN_EARLY_INPUT_NONE && backend!=TITAN_EARLY_INPUT_USB && backend!=TITAN_EARLY_INPUT_BT)return 0;
    if(!read_config(&cfg)){
        memset(&cfg,0,sizeof(cfg));
        cfg.magic=TITAN_EARLY_INPUT_CONFIG_MAGIC;cfg.version=TITAN_EARLY_INPUT_CONFIG_VERSION;
        cfg.pad_enable=3u;cfg.pad_vibration=3u;
    }
    cfg.backend=(uint8_t)backend;
    (void)TitanStorageIo_CreateDirectory(TITAN_STORAGE_DEVICE_ROOT);
    (void)TitanStorageIo_CreateDirectory(TITAN_EARLY_INPUT_CONFIG_DIR);
    fd=TitanStorageIo_Open(TITAN_EARLY_INPUT_CONFIG_TEMP,TITAN_STORAGE_IO_WRITE|TITAN_STORAGE_IO_CREATE|TITAN_STORAGE_IO_TRUNCATE);
    if(fd<0)return 0;
    if(!TitanStorageIo_WriteExact(fd,&cfg,sizeof(cfg)) || TitanStorageIo_Flush(fd)<0){TitanStorageIo_Close(fd);(void)TitanStorageIo_Remove(TITAN_EARLY_INPUT_CONFIG_TEMP);return 0;}
    TitanStorageIo_Close(fd);
    if(!TitanStorageIo_AtomicReplace(TITAN_EARLY_INPUT_CONFIG_TEMP,TITAN_EARLY_INPUT_CONFIG_PATH,TITAN_EARLY_INPUT_CONFIG_BACKUP)){(void)TitanStorageIo_Remove(TITAN_EARLY_INPUT_CONFIG_TEMP);return 0;}
    g_cfg=cfg;g_early.backend=backend;g_early.enabled=backend!=TITAN_EARLY_INPUT_NONE;
    return 1;
}
