#include "titan_network.h"
#include "titan_iop_modules.h"
#include "titan_storage_io.h"
#include "titan_storage_paths.h"
#include "titan_diagnostics.h"
#include "titan_health_trace.h"
#include "titan_vlombiscore.h"
#include "titan_boot_recovery.h"
#include "../../iop/titan_ar9271/ar9271_scanner_protocol.h"

#include <kernel.h>
#include <delaythread.h>
#include <loadfile.h>
#include <netman.h>
#include <netman_rpc.h>
#include <ps2ip.h>
#include <ps2ips.h>
#include <tcpip.h>
#include <sifrpc.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>

#if defined(__GNUC__)
extern uint32_t TitanVlombisCore_GetBackgroundCadence(uint32_t normal_frames, uint32_t pressured_frames) __attribute__((weak));
extern void TitanVlombisCore_RecordWork(uint32_t capability_mask, TitanVlombisEvent event, uint32_t work_units, uint32_t bytes_avoided) __attribute__((weak));
#endif

/* The frozen PS2SDK public compatibility headers used by Titan expose
 * err_t/netif through <tcpip.h>, but do not declare these lwIP symbols.
 * They are linked from libps2ip.a, so keep the exact ABI declarations local
 * instead of depending on non-installed lwIP internal headers. */
extern void netif_set_link_up(struct netif *netif);
extern void netif_set_link_down(struct netif *netif);
extern err_t tcpip_callback(void (*function)(void *ctx), void *ctx);

#define TITAN_NETWORK_SETTINGS_DIR TITAN_STORAGE_DEVICE_ROOT "/settings"
#define TITAN_NETWORK_SETTINGS_PATH TITAN_NETWORK_SETTINGS_DIR "/network.cfg"
#define TITAN_NETWORK_SETTINGS_TEMP TITAN_NETWORK_SETTINGS_DIR "/network.tmp"
#define TITAN_NETWORK_SETTINGS_BACKUP TITAN_NETWORK_SETTINGS_DIR "/network.bak"
#define TITAN_NETWORK_SETTINGS_LOAD_RETRY_FRAMES 30u
#define TITAN_NETWORK_SETTINGS_LOAD_MAX_ATTEMPTS 4u
#define TITAN_NETWORK_RPC_POLL_FRAMES 20u
#define TITAN_NETWORK_BOOT_GRACE_FRAMES 120u
#define TITAN_NETWORK_HOME_STABILIZE_FRAMES 45u
#define TITAN_NETWORK_NETMAN_RPC_PROBE_STEPS 150u
#define TITAN_NETWORK_NETMAN_RPC_PROBE_DELAY_US 10000u
#define TITAN_NETWORK_BOOTSTRAP_RETRY_FRAMES 180u
#define TITAN_NETWORK_BOOTSTRAP_MAX_RETRIES 3u
#define TITAN_NETWORK_WIFI_HANDOFF_GRACE_FRAMES 30u
#define TITAN_NETWORK_BOOTSTRAP_STACK_BYTES (16u * 1024u)
#define TITAN_NETWORK_BOOTSTRAP_PRIORITY 70
#define TITAN_NETWORK_TEST_STACK_BYTES (12u * 1024u)
#define TITAN_NETWORK_TEST_PRIORITY 71
#define TITAN_NETWORK_TEST_BOOTSTRAP_WAIT_STEPS 150u
#define TITAN_NETWORK_TEST_LINK_WAIT_STEPS 100u
#define TITAN_NETWORK_TEST_IP_WAIT_STEPS 300u
#define TITAN_NETWORK_TEST_WAIT_US 100000u
#define TITAN_NETWORK_TEST_DNS_TIMEOUT_MS 1500u

/* TITAN_RC5_4_ETHERNET_ASSURANCE_R1
 *
 * Connection testing is a real asynchronous operation.  The UI never calls
 * retry/module/bootstrap primitives directly.  Ethernet assurance verifies
 * NETMAN -> DEV9 -> SMAP -> ps2ip -> sm0 -> link ioctl in a worker and only
 * labels Internet successful after an external UDP/DNS reply is observed.
 */

/* TITAN_RC5_5_NETWORK_STORAGE_PERF_RECOVERY_R1
 *
 * Hardware recovery pass: keep network bring-up entirely on the background
 * worker, make every cold Ethernet boundary observable, use the exact PS2SDK
 * ps2ip_getconfig() success contract, and avoid starving Library discovery
 * behind diagnostic USB snapshots.
 */

/* TITAN_RC5_6_NETMAN_SCAN_PERF_RECOVERY_R1
 *
 * Hardware evidence showed the RC5.5 image was deployed correctly but stopped
 * at the EE NETMAN bind.  Do not enter PS2SDK's unbounded NetManInit() until
 * the IOP NETMAN RPC server is actually bindable.  Bootstrap begins only after
 * HOME is ready, retries a missing server with bounded backoff, and keeps every
 * failure named/observable instead of leaving a worker stuck forever.
 */

typedef enum TitanNetworkBootstrapStage
{
    TITAN_NETWORK_BOOTSTRAP_IDLE = 0,
    TITAN_NETWORK_BOOTSTRAP_DEV9,
    TITAN_NETWORK_BOOTSTRAP_NETMAN,
    TITAN_NETWORK_BOOTSTRAP_BACKEND,
    TITAN_NETWORK_BOOTSTRAP_NETMAN_RPC,
    TITAN_NETWORK_BOOTSTRAP_NETMAN_EE,
    TITAN_NETWORK_BOOTSTRAP_LINK_MODE,
    TITAN_NETWORK_BOOTSTRAP_PS2IP,
    TITAN_NETWORK_BOOTSTRAP_MAIN_IF,
    TITAN_NETWORK_BOOTSTRAP_READY,
    TITAN_NETWORK_BOOTSTRAP_ERROR
} TitanNetworkBootstrapStage;

/* TITAN_RC4_IOP_ETHERNET_FIX_R1
 *
 * SifExecModuleBuffer's module result is a module residency status, not a
 * generic errno.  0 = resident, 1 = did not stay resident, 2 = resident and
 * removable.  Never treat result==1 as a successful IOP service start.
 *
 * Ethernet DHCP recovery is deliberately bounded: a live PHY that never
 * receives a lease must not remain in "Connecting" forever.
 */
#define TITAN_IOP_MODULE_RESIDENT_END 0
#define TITAN_IOP_MODULE_NO_RESIDENT_END 1
#define TITAN_IOP_MODULE_REMOVABLE_END 2
#define TITAN_NETWORK_ETH_DHCP_RETRY_POLLS 30u
#define TITAN_NETWORK_ETH_DHCP_MAX_RETRIES 3u

/* TITAN_RC5_ETHERNET_LINK_DHCP_LIFECYCLE_R1
 *
 * NetManSetMainIF() does not replay the selected backend's current link state
 * to the EE TCP/IP stack.  Mirror PS2SDK's own ps2ip link callbacks through
 * tcpip_callback(), and never start/restart Ethernet DHCP while the PHY is
 * down.  A link-up transition arms DHCP; the following network poll starts it
 * after the lwIP callback has had a chance to run.
 */

typedef struct TitanSha1Context
{
    unsigned int h[5];
    unsigned long long bytes;
    unsigned char block[64];
    unsigned int used;
} TitanSha1Context;

static TitanNetworkStatus g_network;
static TitanNetworkWifiAp g_wifi_aps[TITAN_NETWORK_WIFI_AP_CAPACITY];
static unsigned char g_wifi_pmk[32];
static int g_wifi_pmk_valid;
static SifRpcClientData_t g_wifi_rpc __attribute__((aligned(64)));
static unsigned char g_wifi_rpc_buffer[1024] __attribute__((aligned(64)));
/* Status telemetry is a slow-path RPC and may take hundreds of milliseconds
 * while AR9271/HTC is probing or recovering. Use a second client so periodic
 * GET_INFO never blocks the UI thread or collides with explicit connect/scan
 * control RPCs. */
static SifRpcClientData_t g_wifi_info_rpc __attribute__((aligned(64)));
static unsigned char g_wifi_info_rpc_buffer[1024] __attribute__((aligned(64)));
static SifRpcClientData_t g_netman_probe_rpc __attribute__((aligned(64)));
static volatile int g_wifi_info_rpc_pending;
static volatile int g_wifi_info_rpc_done;
static void TitanNetwork_InfoRpcDone(void *arg){(void)arg;g_wifi_info_rpc_done=1;}
static unsigned int g_poll_frame;
static int g_ps2ip_initialized;
static int g_netman_ee_initialized;
static int g_netman_loaded;
static int g_dev9_loaded;
static int g_smap_loaded;
static int g_wifi_bridge_enabled;
static int g_wifi_autoconnect_attempted;
static int g_wifi_scan_pending;
static volatile int g_network_bootstrap_done;
static int g_network_bootstrap_attempted;
static TitanNetworkBootstrapStage g_network_bootstrap_stage;
static TitanNetworkBootstrapStage g_network_bootstrap_error_stage;
static unsigned int g_network_home_ready_frame;
static unsigned int g_network_bootstrap_retry_count;
static unsigned int g_network_bootstrap_next_retry_frame;
#if defined(__EE__) || defined(_EE)
static unsigned char g_network_bootstrap_stack[TITAN_NETWORK_BOOTSTRAP_STACK_BYTES] __attribute__((aligned(16)));
static volatile int g_network_bootstrap_worker_active;
#endif
#if defined(__EE__) || defined(_EE)
static unsigned char g_network_test_stack[TITAN_NETWORK_TEST_STACK_BYTES] __attribute__((aligned(16)));
static volatile int g_network_test_worker_active;
#endif
static TitanNetworkConnectionTestStatus g_network_test;
static unsigned int g_network_test_generation;
static unsigned int g_wifi_online_since_frame;
static int g_wifi_handoff_pending;
static int g_diag_last_wifi_scan_state = -999;
static int g_network_settings_loaded;
static unsigned int g_network_settings_load_attempts;
static unsigned int g_ethernet_dhcp_wait_polls;
static unsigned int g_ethernet_dhcp_retry_count;
static int g_ethernet_dhcp_pending;
static int g_ethernet_dhcp_started;
static int g_ethernet_last_link_state;

/* Targeted Ethernet flight-recorder state. These values are diagnostics only;
 * they do not own networking or add a second logger/transport path. */
static int g_diag_last_ethernet_link = -1;
static int g_diag_last_ethernet_link_ioctl = -999;
static int g_diag_last_ethernet_dhcp_status = -999;
static int g_diag_last_ethernet_online = -1;

static const char *TitanNetwork_BootstrapStageName(TitanNetworkBootstrapStage stage)
{
    switch(stage){
        case TITAN_NETWORK_BOOTSTRAP_DEV9:return "DEV9";
        case TITAN_NETWORK_BOOTSTRAP_NETMAN:return "NETMAN IOP";
        case TITAN_NETWORK_BOOTSTRAP_BACKEND:return "Network backend";
        case TITAN_NETWORK_BOOTSTRAP_NETMAN_RPC:return "NETMAN IOP RPC server";
        case TITAN_NETWORK_BOOTSTRAP_NETMAN_EE:return "NETMAN EE RPC";
        case TITAN_NETWORK_BOOTSTRAP_LINK_MODE:return "SMAP link mode";
        case TITAN_NETWORK_BOOTSTRAP_PS2IP:return "ps2ip / lwIP";
        case TITAN_NETWORK_BOOTSTRAP_MAIN_IF:return "Main interface";
        case TITAN_NETWORK_BOOTSTRAP_READY:return "Ready";
        case TITAN_NETWORK_BOOTSTRAP_ERROR:return "Error";
        default:return "Idle";
    }
}

static void TitanNetwork_SetBootstrapStage(TitanNetworkBootstrapStage stage)
{
    if(stage==TITAN_NETWORK_BOOTSTRAP_ERROR &&
       g_network_bootstrap_stage!=TITAN_NETWORK_BOOTSTRAP_ERROR)
        g_network_bootstrap_error_stage=g_network_bootstrap_stage;
    else if(stage!=TITAN_NETWORK_BOOTSTRAP_ERROR)
        g_network_bootstrap_error_stage=TITAN_NETWORK_BOOTSTRAP_IDLE;
    g_network_bootstrap_stage=stage;
    TitanDiagnostics_Event(
        stage==TITAN_NETWORK_BOOTSTRAP_ERROR?TITAN_DIAG_ERROR:TITAN_DIAG_INFO,
        TITAN_DIAG_NETWORK,0x254,0,(uint32_t)stage,
        (uint32_t)(stage==TITAN_NETWORK_BOOTSTRAP_ERROR
            ? g_network_bootstrap_error_stage:g_network.preference),
        (uint32_t)g_network_bootstrap_done,g_poll_frame
    );
}

int TitanNetwork_IsBootstrapComplete(void)
{
    return g_network_bootstrap_done && g_network_bootstrap_stage==TITAN_NETWORK_BOOTSTRAP_READY;
}

const char *TitanNetwork_GetBootstrapStageName(void)
{
    if(g_network_bootstrap_stage==TITAN_NETWORK_BOOTSTRAP_ERROR &&
       g_network_bootstrap_error_stage!=TITAN_NETWORK_BOOTSTRAP_IDLE)
        return TitanNetwork_BootstrapStageName(g_network_bootstrap_error_stage);
    return TitanNetwork_BootstrapStageName(g_network_bootstrap_stage);
}

static unsigned int TitanNetwork_Rol(unsigned int v, unsigned int n)
{
    return (v << n) | (v >> (32u - n));
}

static void TitanNetwork_Sha1Block(TitanSha1Context *c, const unsigned char *p)
{
    unsigned int w[80], a,b,d,e,f,k,t,i,cc;
    for(i=0;i<16;i++)w[i]=((unsigned int)p[i*4]<<24)|((unsigned int)p[i*4+1]<<16)|((unsigned int)p[i*4+2]<<8)|p[i*4+3];
    for(i=16;i<80;i++)w[i]=TitanNetwork_Rol(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
    a=c->h[0];b=c->h[1];cc=c->h[2];d=c->h[3];e=c->h[4];
    for(i=0;i<80;i++){
        if(i<20){f=(b&cc)|((~b)&d);k=0x5a827999u;}
        else if(i<40){f=b^cc^d;k=0x6ed9eba1u;}
        else if(i<60){f=(b&cc)|(b&d)|(cc&d);k=0x8f1bbcdcu;}
        else{f=b^cc^d;k=0xca62c1d6u;}
        t=TitanNetwork_Rol(a,5)+f+e+k+w[i];e=d;d=cc;cc=TitanNetwork_Rol(b,30);b=a;a=t;
    }
    c->h[0]+=a;c->h[1]+=b;c->h[2]+=cc;c->h[3]+=d;c->h[4]+=e;
}

static void TitanNetwork_Sha1Init(TitanSha1Context *c)
{
    memset(c,0,sizeof(*c));c->h[0]=0x67452301u;c->h[1]=0xefcdab89u;c->h[2]=0x98badcfeu;c->h[3]=0x10325476u;c->h[4]=0xc3d2e1f0u;
}

static void TitanNetwork_Sha1Update(TitanSha1Context *c,const void *data,unsigned int len)
{
    const unsigned char *p=(const unsigned char*)data;unsigned int n;
    c->bytes+=len;
    while(len){n=64u-c->used;if(n>len)n=len;memcpy(c->block+c->used,p,n);c->used+=n;p+=n;len-=n;if(c->used==64u){TitanNetwork_Sha1Block(c,c->block);c->used=0u;}}
}

static void TitanNetwork_Sha1Final(TitanSha1Context *c,unsigned char out[20])
{
    unsigned long long bits=c->bytes*8ull;unsigned int i;
    c->block[c->used++]=0x80u;
    if(c->used>56u){while(c->used<64u)c->block[c->used++]=0;TitanNetwork_Sha1Block(c,c->block);c->used=0u;}
    while(c->used<56u)c->block[c->used++]=0;
    for(i=0;i<8;i++)c->block[63u-i]=(unsigned char)(bits>>(i*8));
    TitanNetwork_Sha1Block(c,c->block);
    for(i=0;i<5;i++){out[i*4]=(unsigned char)(c->h[i]>>24);out[i*4+1]=(unsigned char)(c->h[i]>>16);out[i*4+2]=(unsigned char)(c->h[i]>>8);out[i*4+3]=(unsigned char)c->h[i];}
    memset(c,0,sizeof(*c));
}

static void TitanNetwork_HmacSha1(const unsigned char *key,unsigned int keylen,const unsigned char *data,unsigned int len,unsigned char out[20])
{
    unsigned char k[64],ipad[64],opad[64],inner[20];TitanSha1Context c;unsigned int i;
    memset(k,0,sizeof(k));
    if(keylen>64u){TitanNetwork_Sha1Init(&c);TitanNetwork_Sha1Update(&c,key,keylen);TitanNetwork_Sha1Final(&c,k);keylen=20u;}else memcpy(k,key,keylen);
    for(i=0;i<64;i++){ipad[i]=(unsigned char)(k[i]^0x36u);opad[i]=(unsigned char)(k[i]^0x5cu);}
    TitanNetwork_Sha1Init(&c);TitanNetwork_Sha1Update(&c,ipad,64);TitanNetwork_Sha1Update(&c,data,len);TitanNetwork_Sha1Final(&c,inner);
    TitanNetwork_Sha1Init(&c);TitanNetwork_Sha1Update(&c,opad,64);TitanNetwork_Sha1Update(&c,inner,20);TitanNetwork_Sha1Final(&c,out);
    memset(k,0,sizeof(k));memset(ipad,0,sizeof(ipad));memset(opad,0,sizeof(opad));memset(inner,0,sizeof(inner));
}

static void TitanNetwork_Pbkdf2(const char *password,const char *ssid,unsigned char out[32])
{
    unsigned char salt[37],u[20],t[20];unsigned int sl=(unsigned int)strlen(ssid),pl=(unsigned int)strlen(password),block,iter,i,off=0u,n;
    if(sl>32u)
        sl=32u;
    memcpy(salt,ssid,sl);
    for(block=1u;block<=2u;block++){
        salt[sl]=(unsigned char)(block>>24);salt[sl+1]=(unsigned char)(block>>16);salt[sl+2]=(unsigned char)(block>>8);salt[sl+3]=(unsigned char)block;
        TitanNetwork_HmacSha1((const unsigned char*)password,pl,salt,sl+4u,u);memcpy(t,u,20);
        for(iter=1u;iter<4096u;iter++){TitanNetwork_HmacSha1((const unsigned char*)password,pl,u,20,u);for(i=0;i<20;i++)t[i]^=u[i];}
        n=(32u-off)<20u?(32u-off):20u;memcpy(out+off,t,n);off+=n;
    }
    memset(salt,0,sizeof(salt));memset(u,0,sizeof(u));memset(t,0,sizeof(t));
}

static void TitanNetwork_Copy(char *dst,unsigned int cap,const char *src)
{
    unsigned int i=0u;if(!dst||cap==0u)return;while(src&&src[i]&&i+1u<cap){dst[i]=src[i];i++;}dst[i]='\0';
}

static uint32_t TitanNetwork_ModuleTraceId(const char *name)
{
    if(!name) return 0u;
    if(strcmp(name,"NETMAN")==0) return 1u;
    if(strcmp(name,"PS2DEV9")==0) return 2u;
    if(strcmp(name,"SMAP")==0) return 3u;
    if(strcmp(name,"TITAN_AR9271")==0) return 4u;
    return 255u;
}

static int TitanNetwork_LoadModule(const char *name, unsigned char *data, unsigned int size)
{
    int result=-1;
    int id;
    int resident;
    uint32_t trace_id=TitanNetwork_ModuleTraceId(name);

    if(!data || size==0u) return 0;
    TitanHealthTrace_Critical(
        TITAN_HT_NETWORK_MODULE_EXEC_BEFORE,g_poll_frame,trace_id,size,0u
    );
    id=SifExecModuleBuffer(data,size,0,NULL,&result);
    TitanHealthTrace_Mark(
        TITAN_HT_NETWORK_MODULE_EXEC_AFTER,g_poll_frame,trace_id,
        (uint32_t)id,(uint32_t)result
    );
    resident=result==TITAN_IOP_MODULE_RESIDENT_END ||
             result==TITAN_IOP_MODULE_REMOVABLE_END;

    TitanDiagnostics_Event(
        id>=0 && resident ? TITAN_DIAG_INFO : TITAN_DIAG_ERROR,
        TITAN_DIAG_NETWORK,0x24c,
        id<0?id:result,
        (uint32_t)id,(uint32_t)result,size,0u
    );

    return id>=0 && resident;
}

static int TitanNetwork_HexNibble(char c)
{
    if(c>='0'&&c<='9') return c-'0';
    if(c>='a'&&c<='f') return c-'a'+10;
    if(c>='A'&&c<='F') return c-'A'+10;
    return -1;
}

static int TitanNetwork_Save(void)
{
    char text[320],hex[65];
    unsigned int i;
    int fd,n,ok=0;

    /* network.cfg is user state, not part of a build.  Keep it under the
     * persistent Titan storage root and publish it transactionally.  BF16
     * passed NULL as AtomicReplace's backup path; once network.cfg existed,
     * every later save was rejected with EEXIST.  The first SSID write thus
     * survived while the following PMK write was silently lost. */
    if(TitanStorageIo_CreateDirectory(TITAN_STORAGE_DEVICE_ROOT)<0 &&
       !TitanStorageIo_PathInfo64(TITAN_STORAGE_DEVICE_ROOT,0,0))
        goto done;
    if(TitanStorageIo_CreateDirectory(TITAN_NETWORK_SETTINGS_DIR)<0 &&
       !TitanStorageIo_PathInfo64(TITAN_NETWORK_SETTINGS_DIR,0,0))
        goto done;

    for(i=0;i<32u;i++)
        sprintf(hex+i*2u,"%02x",g_wifi_pmk[i]);
    hex[64]='\0';
    n=snprintf(text,sizeof(text),
        "version=3\nenabled=%d\npreference=%s\nssid=%s\npmk=%s\npmk_valid=%d\n",
        g_network.enabled?1:0,
        g_network.preference==TITAN_NETWORK_PREFER_ETHERNET?"ethernet":"wifi",
        g_network.ssid,hex,g_wifi_pmk_valid?1:0);
    if(n<=0||(unsigned int)n>=sizeof(text))goto done;

    (void)TitanStorageIo_Remove(TITAN_NETWORK_SETTINGS_TEMP);
    fd=TitanStorageIo_Open(TITAN_NETWORK_SETTINGS_TEMP,
        TITAN_STORAGE_IO_WRITE|TITAN_STORAGE_IO_CREATE|TITAN_STORAGE_IO_TRUNCATE);
    if(fd<0)goto done;
    if(!TitanStorageIo_WriteExact(fd,text,(unsigned int)n) ||
       TitanStorageIo_Flush(fd)<0)
    {
        (void)TitanStorageIo_Close(fd);
        (void)TitanStorageIo_Remove(TITAN_NETWORK_SETTINGS_TEMP);
        goto done;
    }
    if(TitanStorageIo_Close(fd)<0)
    {
        (void)TitanStorageIo_Remove(TITAN_NETWORK_SETTINGS_TEMP);
        goto done;
    }
    if(!TitanStorageIo_AtomicReplace(
        TITAN_NETWORK_SETTINGS_TEMP,
        TITAN_NETWORK_SETTINGS_PATH,
        TITAN_NETWORK_SETTINGS_BACKUP))
    {
        (void)TitanStorageIo_Remove(TITAN_NETWORK_SETTINGS_TEMP);
        goto done;
    }
    g_network_settings_loaded=1;
    ok=1;

done:
    TitanDiagnostics_Event(
        ok?TITAN_DIAG_INFO:TITAN_DIAG_WARN,
        TITAN_DIAG_NETWORK,0x230,ok?0:-1,
        (uint32_t)g_wifi_pmk_valid,
        (uint32_t)g_network.preference,
        (uint32_t)g_network.enabled,
        (uint32_t)strlen(g_network.ssid));
    return ok;
}

static int TitanNetwork_LoadPath(const char *path)
{
    char text[384],ssid[TITAN_NETWORK_SSID_CAPACITY],*line,*save;
    unsigned char pmk[32];
    int fd,n,version=0,enabled=1,preference=TITAN_NETWORK_PREFER_WIFI;
    int pmk_valid=0,pmk_parsed=0,saw_pmk_valid=0;

    memset(ssid,0,sizeof(ssid));
    memset(pmk,0,sizeof(pmk));
    fd=TitanStorageIo_Open(path,TITAN_STORAGE_IO_READ);
    if(fd<0)return 0;
    n=TitanStorageIo_Read(fd,text,sizeof(text)-1u);
    (void)TitanStorageIo_Close(fd);
    if(n<=0)return 0;
    text[n]='\0';

    line=strtok_r(text,"\n",&save);
    while(line){
        if(strncmp(line,"version=",8)==0)version=atoi(line+8);
        else if(strncmp(line,"enabled=",8)==0)enabled=(line[8]=='1')?1:0;
        else if(strncmp(line,"preference=",11)==0)
            preference=strcmp(line+11,"ethernet")==0?TITAN_NETWORK_PREFER_ETHERNET:TITAN_NETWORK_PREFER_WIFI;
        else if(strncmp(line,"ssid=",5)==0)
            TitanNetwork_Copy(ssid,sizeof(ssid),line+5);
        else if(strncmp(line,"pmk=",4)==0&&strlen(line+4)>=64u){
            unsigned int i;int valid=1;
            for(i=0;i<32u;i++){
                int a=TitanNetwork_HexNibble(line[4+i*2u]);
                int b=TitanNetwork_HexNibble(line[5+i*2u]);
                if(a<0||b<0){valid=0;break;}
                pmk[i]=(unsigned char)((a<<4)|b);
            }
            pmk_parsed=valid;
        }
        else if(strncmp(line,"pmk_valid=",10)==0){
            saw_pmk_valid=1;
            pmk_valid=(line[10]=='1')?1:0;
        }
        line=strtok_r(NULL,"\n",&save);
    }

    if(version<2||version>3)return 0;
    if(strlen(ssid)>32u)return 0;
    if(!saw_pmk_valid)return 0;
    if(pmk_valid&&!pmk_parsed)return 0;

    g_network.enabled=enabled;
    g_network.preference=(TitanNetworkPreference)preference;
    TitanNetwork_Copy(g_network.ssid,sizeof(g_network.ssid),ssid);
    memset(g_wifi_pmk,0,sizeof(g_wifi_pmk));
    if(pmk_valid)memcpy(g_wifi_pmk,pmk,sizeof(g_wifi_pmk));
    g_wifi_pmk_valid=pmk_valid;
    return 1;
}

static int TitanNetwork_Load(void)
{
    int source=0;
    g_network_settings_load_attempts++;
    if(TitanNetwork_LoadPath(TITAN_NETWORK_SETTINGS_PATH))source=1;
    else if(TitanNetwork_LoadPath(TITAN_NETWORK_SETTINGS_BACKUP))source=2;
    if(source){
        g_network_settings_loaded=1;
        TitanDiagnostics_Event(
            TITAN_DIAG_INFO,TITAN_DIAG_NETWORK,0x231,0,
            (uint32_t)source,(uint32_t)g_wifi_pmk_valid,
            (uint32_t)g_network.preference,(uint32_t)strlen(g_network.ssid));
        /* Recover a damaged/missing primary from the last known-good backup. */
        if(source==2)(void)TitanNetwork_Save();
        return 1;
    }
    return 0;
}

static void TitanNetwork_FormatIpv4(char *output,unsigned int capacity,unsigned int addr)
{
    const unsigned char *b=(const unsigned char*)&addr;
    if(output==0 || capacity==0u)return;
    snprintf(output,capacity,"%u.%u.%u.%u",b[0],b[1],b[2],b[3]);
}

static void TitanNetwork_SetIpText(unsigned int addr)
{
    TitanNetwork_FormatIpv4(g_network.ip,sizeof(g_network.ip),addr);
}

static void TitanNetwork_ClearLiveAddress(void)
{
    g_network.ip[0]='\0';
    g_network.gateway[0]='\0';
    g_network.dns[0]='\0';
}

static int TitanNetwork_HasUsableIp(unsigned int addr)
{
    return addr!=0u && addr!=0x0100fea9u;
}

static int TitanNetwork_RestartDhcp(void)
{
    t_ip_info info;
    int get_ok;
    int disable_result;
    int enable_result;

    if(!g_ps2ip_initialized){
        TitanDiagnostics_Event(
            TITAN_DIAG_WARN,TITAN_DIAG_PS2IP,0x245,-1,0u,0u,0u,0u
        );
        return 0;
    }

    memset(&info,0,sizeof(info));
    get_ok=ps2ip_getconfig("sm0",&info);
    TitanDiagnostics_Event(
        get_ok==1?TITAN_DIAG_INFO:TITAN_DIAG_ERROR,
        TITAN_DIAG_PS2IP,0x245,get_ok,
        (uint32_t)info.dhcp_enabled,(uint32_t)info.dhcp_status,
        (uint32_t)info.ipaddr.s_addr,(uint32_t)info.gw.s_addr
    );
    /* PS2SDK returns 1 when the named netif exists and 0 when it does not. */
    if(get_ok!=1) return 0;

    info.dhcp_enabled=0;
    info.ipaddr.s_addr=0u;
    info.netmask.s_addr=0u;
    info.gw.s_addr=0u;
    disable_result=ps2ip_setconfig(&info);

    memset(&info,0,sizeof(info));
    strcpy(info.netif_name,"sm0");
    /* t_ip_info uses struct in_addr while PS2SDK's IP4_ADDR macro expects
     * struct ip4_addr.  Avoid strict-aliasing violations on R5900: build the
     * values in correctly typed temporaries, then copy the scalar address. */
    {
        struct ip4_addr ip;
        struct ip4_addr mask;
        struct ip4_addr gateway;
        IP4_ADDR(&ip,169,254,0,1);
        IP4_ADDR(&mask,255,255,0,0);
        IP4_ADDR(&gateway,0,0,0,0);
        info.ipaddr.s_addr=ip.addr;
        info.netmask.s_addr=mask.addr;
        info.gw.s_addr=gateway.addr;
    }
    info.dhcp_enabled=1;
    enable_result=ps2ip_setconfig(&info);

    /* ps2ip_setconfig returns 1 on success and 0 when the netif was not
     * found.  >=0 is therefore not a valid success test. */
    TitanDiagnostics_Event(
        disable_result==1 && enable_result==1 ? TITAN_DIAG_INFO:TITAN_DIAG_ERROR,
        TITAN_DIAG_PS2IP,0x246,
        enable_result==1?0:-3,
        (uint32_t)disable_result,(uint32_t)enable_result,
        (uint32_t)info.ipaddr.s_addr,(uint32_t)info.netmask.s_addr
    );

    return disable_result==1 && enable_result==1;
}

static void TitanNetwork_UpdateLwipMac(void)
{
    struct netif *nif;
    unsigned char mac[6];
    int result;
    if(!g_ps2ip_initialized)return;
    nif=netif_find("sm0");
    if(nif==0){
        TitanDiagnostics_Event(TITAN_DIAG_WARN,TITAN_DIAG_PS2IP,0x24b,-1,0u,0u,0u,0u);
        return;
    }
    result=NetManIoctl(NETMAN_NETIF_IOCTL_ETH_GET_MAC,0,0,mac,sizeof(mac));
    if(result<0){
        TitanDiagnostics_Event(TITAN_DIAG_ERROR,TITAN_DIAG_NETMAN,0x24b,result,0u,0u,0u,0u);
        return;
    }
    memcpy(nif->hwaddr,mac,6u);
    nif->hwaddr_len=6u;
    TitanDiagnostics_Event(
        TITAN_DIAG_INFO,TITAN_DIAG_NETMAN,0x24b,result,
        ((uint32_t)mac[0]<<24)|((uint32_t)mac[1]<<16)|((uint32_t)mac[2]<<8)|mac[3],
        ((uint32_t)mac[4]<<8)|mac[5],0u,0u
    );
}

static void TitanNetwork_LwipLinkUpCallback(void *ctx)
{
    struct netif *nif=(struct netif*)ctx;
    if(nif) netif_set_link_up(nif);
}

static void TitanNetwork_LwipLinkDownCallback(void *ctx)
{
    struct netif *nif=(struct netif*)ctx;
    if(nif) netif_set_link_down(nif);
}

static int TitanNetwork_SyncLwipLinkState(void)
{
    struct netif *nif;
    int link_raw;
    int link_up;
    err_t callback_result;

    if(!g_ps2ip_initialized) return 0;
    nif=netif_find("sm0");
    if(nif==0) return 0;

    link_raw=NetManIoctl(
        NETMAN_NETIF_IOCTL_GET_LINK_STATUS,0,0,0,0
    );
    if(link_raw<0){
        TitanDiagnostics_Event(
            TITAN_DIAG_ERROR,TITAN_DIAG_NETMAN,0x24d,
            link_raw,0u,0u,0u,0u
        );
        return 0;
    }

    link_up=link_raw==NETMAN_NETIF_ETH_LINK_STATE_UP;
    callback_result=tcpip_callback(
        link_up ? TitanNetwork_LwipLinkUpCallback
                : TitanNetwork_LwipLinkDownCallback,
        nif
    );

    TitanDiagnostics_Event(
        callback_result==0?TITAN_DIAG_INFO:TITAN_DIAG_ERROR,
        TITAN_DIAG_NETMAN,0x24d,
        callback_result==0?link_raw:(int)callback_result,
        (uint32_t)link_up,(uint32_t)callback_result,
        (uint32_t)g_network.preference,0u
    );
    /* lwIP ERR_OK is numerically zero.  The frozen public PS2SDK header does
     * not export the ERR_OK macro, so compare the err_t result directly. */
    return callback_result==0;
}

static int TitanNetwork_WifiBridgeRpc(int enabled)
{
    unsigned int request=enabled?1u:0u,ok=0u;
    if(!g_network.wifi_rpc_bound)return 0;
    memset(g_wifi_rpc_buffer,0,sizeof(g_wifi_rpc_buffer));
    memcpy(g_wifi_rpc_buffer,&request,sizeof(request));
    if(SifCallRpc(&g_wifi_rpc,TITAN_AR9271_SCAN_RPC_NETMAN_ACTIVE,0,
                  g_wifi_rpc_buffer,sizeof(request),g_wifi_rpc_buffer,sizeof(ok),0,0)<0)
        return 0;
    memcpy(&ok,g_wifi_rpc_buffer,sizeof(ok));
    g_wifi_bridge_enabled=enabled && ok;
    return ok?1:0;
}

static int TitanNetwork_SelectMainInterface(TitanNetworkPreference preference,int restart_dhcp)
{
    const char *name;

    if(!g_netman_loaded) return 0;
    name=preference==TITAN_NETWORK_PREFER_ETHERNET?"SMA":"WFI";
    TitanHealthTrace_Critical(
        TITAN_HT_NETWORK_SELECT_IF_BEFORE,g_poll_frame,
        (uint32_t)preference,restart_dhcp?1u:0u,0u
    );
    {
        int mainif_result=NetManSetMainIF(name);
        TitanHealthTrace_Mark(
            TITAN_HT_NETWORK_SELECT_IF_AFTER,g_poll_frame,
            (uint32_t)preference,(uint32_t)mainif_result,0u
        );
        if(mainif_result<0) return 0;
    }

    /* NetManSetMainIF only changes MainNetIF in the frozen PS2SDK. It does
     * not itself call UpdateNetIFStatus(), so synchronize the lwIP link state
     * immediately instead of inheriting the previous WFI/SMAP state. */
    if(g_ps2ip_initialized){
        TitanNetwork_UpdateLwipMac();
        (void)TitanNetwork_SyncLwipLinkState();
    }

    if(preference==TITAN_NETWORK_PREFER_ETHERNET){
        if(g_network.wifi_rpc_bound) (void)TitanNetwork_WifiBridgeRpc(0);
        g_wifi_handoff_pending=0;
        if(restart_dhcp){
            /* Do not race SMAP auto-negotiation.  The next physical link-up
             * observation will synchronize lwIP, and the following poll will
             * start DHCP. */
            g_ethernet_dhcp_pending=1;
            g_ethernet_dhcp_started=0;
            g_ethernet_dhcp_wait_polls=0u;
            g_ethernet_dhcp_retry_count=0u;
        }
    }else if(g_network.wifi_online && g_network.wifi_rpc_bound){
        if(TitanNetwork_WifiBridgeRpc(1)){
            /* The IOP performs link-up on its control worker. DHCP must not
             * race that transition; Update() starts it only after NetMan
             * reports the WFI link up. */
            g_wifi_handoff_pending=restart_dhcp?1:0;
        }
    }else if(g_network.wifi_rpc_bound){
        (void)TitanNetwork_WifiBridgeRpc(0);
        g_wifi_handoff_pending=0;
    }
    return 1;
}


static int TitanNetwork_EnsureWifiDriver(void)
{
    if(g_network.wifi_irx_loaded){
        int wifi_resident;
        TitanHealthTrace_Critical(
            TITAN_HT_NETWORK_MODULE_SEARCH_BEFORE,g_poll_frame,4u,1u,0u
        );
        wifi_resident=SifSearchModuleByName("titan_ar9271");
        TitanHealthTrace_Mark(
            TITAN_HT_NETWORK_MODULE_SEARCH_AFTER,g_poll_frame,4u,(uint32_t)wifi_resident,1u
        );
        if(wifi_resident>=0) return 1;
    }

    g_network.wifi_irx_loaded=0;
    TitanHealthTrace_Critical(
        TITAN_HT_NETWORK_MODULE_SEARCH_BEFORE,g_poll_frame,4u,2u,0u
    );
    {
        int wifi_resident=SifSearchModuleByName("titan_ar9271");
        TitanHealthTrace_Mark(
            TITAN_HT_NETWORK_MODULE_SEARCH_AFTER,g_poll_frame,4u,(uint32_t)wifi_resident,2u
        );
        if(wifi_resident>=0){
            g_network.wifi_irx_loaded=1;
            return 1;
        }
    }

    g_network.wifi_irx_loaded=TitanNetwork_LoadModule(
        "TITAN_AR9271",titan_ar9271_irx,size_titan_ar9271_irx
    );
    if(g_network.wifi_irx_loaded){
        int wifi_verify;
        TitanHealthTrace_Critical(
            TITAN_HT_NETWORK_MODULE_SEARCH_BEFORE,g_poll_frame,4u,3u,0u
        );
        wifi_verify=SifSearchModuleByName("titan_ar9271");
        TitanHealthTrace_Mark(
            TITAN_HT_NETWORK_MODULE_SEARCH_AFTER,g_poll_frame,4u,(uint32_t)wifi_verify,3u
        );
        if(wifi_verify<0) g_network.wifi_irx_loaded=0;
    }

    return g_network.wifi_irx_loaded?1:0;
}

static int TitanNetwork_EnsureDev9Driver(void)
{
    /*
     * RC5.6-R8 DEV9 authority:
     * zero from SifSearchModuleByName("dev9") is not enough to skip the
     * initial embedded DEV9 load. Once SifExecModuleBuffer succeeds, that
     * execution remains authoritative even if the post-load name search is 0.
     */
    int dev9_search_id;
    int dev9_from_exec=0;

    if(g_dev9_loaded)
        return 1;

    TitanHealthTrace_Critical(
        TITAN_HT_NETWORK_MODULE_SEARCH_BEFORE,g_poll_frame,2u,1u,0u
    );
    dev9_search_id=SifSearchModuleByName("dev9");
    TitanHealthTrace_Mark(
        TITAN_HT_NETWORK_MODULE_SEARCH_AFTER,g_poll_frame,2u,
        (uint32_t)dev9_search_id,1u
    );
    TitanDiagnostics_Event(
        TITAN_DIAG_DEBUG,TITAN_DIAG_NETWORK,0x25e,dev9_search_id,
        (uint32_t)(dev9_search_id>0),0u,g_poll_frame,0u
    );

    if(dev9_search_id>0)
        g_dev9_loaded=1;
    else{
        g_dev9_loaded=TitanNetwork_LoadModule(
            "PS2DEV9",titan_ps2dev9_irx,size_titan_ps2dev9_irx
        );
        dev9_from_exec=g_dev9_loaded?1:0;
    }

    TitanDiagnostics_Event(
        g_dev9_loaded?TITAN_DIAG_INFO:TITAN_DIAG_ERROR,
        TITAN_DIAG_NETWORK,0x241,g_dev9_loaded?0:-1,
        (uint32_t)dev9_search_id,(uint32_t)dev9_from_exec,
        (uint32_t)g_dev9_loaded,g_poll_frame
    );
    if(!g_dev9_loaded) return 0;

    if(dev9_from_exec){
        int dev9_verify;
        TitanHealthTrace_Critical(
            TITAN_HT_NETWORK_MODULE_SEARCH_BEFORE,g_poll_frame,2u,2u,0u
        );
        dev9_verify=SifSearchModuleByName("dev9");
        TitanHealthTrace_Mark(
            TITAN_HT_NETWORK_MODULE_SEARCH_AFTER,g_poll_frame,2u,
            (uint32_t)dev9_verify,2u
        );
        TitanDiagnostics_Event(
            TITAN_DIAG_DEBUG,TITAN_DIAG_NETWORK,0x261,dev9_verify,
            2u,(uint32_t)g_dev9_loaded,g_poll_frame,0u
        );
    }
    return 1;
}

static int TitanNetwork_EnsureEthernetDriver(void)
{
    int smap_search_hint;

    /* Successful SMAP execution is the lifetime authority for this kernel.
     * SifSearchModuleByName() is telemetry only: hardware showed that its
     * result can disagree with the already-running module, so it must never
     * invalidate or trigger duplicate execution of a proven SMAP instance. */
    if(g_smap_loaded){
        TitanHealthTrace_Critical(
            TITAN_HT_NETWORK_MODULE_SEARCH_BEFORE,g_poll_frame,3u,1u,0u
        );
        smap_search_hint=SifSearchModuleByName("SMAP_driver");
        TitanHealthTrace_Mark(
            TITAN_HT_NETWORK_MODULE_SEARCH_AFTER,g_poll_frame,3u,
            (uint32_t)smap_search_hint,1u
        );
        TitanDiagnostics_Event(
            TITAN_DIAG_DEBUG,TITAN_DIAG_NETWORK,0x263,smap_search_hint,
            1u,1u,g_poll_frame,0u
        );
        g_network.ethernet_stack_loaded=1;
        return 1;
    }

    g_ethernet_dhcp_wait_polls=0u;
    g_ethernet_dhcp_retry_count=0u;
    g_network.ethernet_stack_loaded=0;

    if(!TitanNetwork_EnsureDev9Driver()) return 0;

    /*
     * RC5.6-R7 SMAP rebind:
     * R5 proved that finding "SMAP_driver" by name does NOT prove that the
     * driver is registered in the NETMAN instance Titan just loaded:
     * the public EE NetManSetMainIF("SMAP") helper returned -ENXIO because the public ABI only carries three characters plus NUL.
     *
     * Therefore the first SMAP bring-up for this kernel always executes
     * Titan's embedded SMAP after the current NETMAN is in place. Its _start()
     * calls NetManRegisterNetIF("SMAP"), which is the registration we need.
     * The pre-existing module-name result is kept only as telemetry.
     */
    TitanHealthTrace_Critical(
        TITAN_HT_NETWORK_MODULE_SEARCH_BEFORE,g_poll_frame,3u,2u,0u
    );
    smap_search_hint=SifSearchModuleByName("SMAP_driver");
    TitanHealthTrace_Mark(
        TITAN_HT_NETWORK_MODULE_SEARCH_AFTER,g_poll_frame,3u,
        (uint32_t)smap_search_hint,2u
    );
    TitanDiagnostics_Event(
        TITAN_DIAG_DEBUG,TITAN_DIAG_NETWORK,0x25f,smap_search_hint,
        (uint32_t)(smap_search_hint>=0),0u,g_poll_frame,0u
    );

    g_smap_loaded=TitanNetwork_LoadModule(
        "SMAP",titan_smap_irx,size_titan_smap_irx
    );

    if(g_smap_loaded){
        int smap_verify=SifSearchModuleByName("SMAP_driver");
        TitanHealthTrace_Mark(
            TITAN_HT_NETWORK_MODULE_SEARCH_AFTER,g_poll_frame,3u,
            (uint32_t)smap_verify,3u
        );
        TitanDiagnostics_Event(
            TITAN_DIAG_DEBUG,TITAN_DIAG_NETWORK,0x262,smap_verify,
            (uint32_t)g_smap_loaded,
            (uint32_t)smap_search_hint,g_poll_frame,0u
        );
        /* Search result is diagnostic only after successful execution.
         * Functional proof follows at NetManSetMainIF("SMA"), the Titan-safe SMAP alias. */
    }

    g_network.ethernet_stack_loaded=g_smap_loaded?1:0;
    TitanDiagnostics_Event(
        g_smap_loaded?TITAN_DIAG_INFO:TITAN_DIAG_ERROR,
        TITAN_DIAG_NETWORK,0x242,g_smap_loaded?0:-1,
        (uint32_t)g_smap_loaded,(uint32_t)smap_search_hint,0u,g_poll_frame
    );
    return g_smap_loaded?1:0;
}

static int TitanNetwork_WaitForNetmanRpcServer(void)
{
#if defined(__EE__) || defined(_EE)
    unsigned int attempt;
    int bind_result=-1;

    memset(&g_netman_probe_rpc,0,sizeof(g_netman_probe_rpc));
    for(attempt=1u;attempt<=TITAN_NETWORK_NETMAN_RPC_PROBE_STEPS;attempt++){
        bind_result=SifBindRpc(&g_netman_probe_rpc,NETMAN_RPC_NUMBER,0);
        if(bind_result>=0 && g_netman_probe_rpc.server!=0){
            TitanDiagnostics_Event(
                TITAN_DIAG_INFO,TITAN_DIAG_NETMAN,0x258,bind_result,
                attempt,1u,g_poll_frame,0u
            );
            return 1;
        }
        if(attempt==1u || attempt==30u || attempt==75u ||
           attempt==TITAN_NETWORK_NETMAN_RPC_PROBE_STEPS){
            TitanDiagnostics_Event(
                TITAN_DIAG_DEBUG,TITAN_DIAG_NETMAN,0x258,bind_result,
                attempt,0u,g_poll_frame,0u
            );
        }
        DelayThread(TITAN_NETWORK_NETMAN_RPC_PROBE_DELAY_US);
    }
    TitanDiagnostics_Event(
        TITAN_DIAG_ERROR,TITAN_DIAG_NETMAN,0x259,bind_result,
        TITAN_NETWORK_NETMAN_RPC_PROBE_STEPS,0u,g_poll_frame,0u
    );
    return 0;
#else
    return 1;
#endif
}

static void TitanNetwork_InitInterfaces(void)
{
    struct ip4_addr ip,nm,gw;
    int selected_ok;

    TitanDiagnostics_Event(
        TITAN_DIAG_INFO,TITAN_DIAG_NETWORK,0x240,0,
        (uint32_t)g_network.preference,(uint32_t)g_netman_loaded,
        (uint32_t)g_ps2ip_initialized,(uint32_t)g_smap_loaded
    );
    g_network.ethernet_link_known=0;
    g_network.ethernet_link_up=0;
    g_network.ethernet_online=0;
    g_network.main_interface_ready=0;

    /* Cold Ethernet order follows the dependency chain: DEV9 first, then
     * NETMAN IOP, then SMAP, then the EE NETMAN client/link mode, and only
     * after that lwIP/ps2ip. Wi-Fi preserves its validated backend path. */
    if(g_network.preference==TITAN_NETWORK_PREFER_ETHERNET){
        TitanNetwork_SetBootstrapStage(TITAN_NETWORK_BOOTSTRAP_DEV9);
        if(!TitanNetwork_EnsureDev9Driver()){
            g_network.dev9_ready=0;
            g_network.state=TITAN_NETWORK_ERROR;
            TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),"DEV9 failed to initialize");
            TitanNetwork_SetBootstrapStage(TITAN_NETWORK_BOOTSTRAP_ERROR);
            return;
        }
        g_network.dev9_ready=1;
    }

    TitanNetwork_SetBootstrapStage(TITAN_NETWORK_BOOTSTRAP_NETMAN);
    {
        int netman_search_hint;

        /*
         * RC5.6-R4: module-name lookup is diagnostic only.
         *
         * Hardware trace showed that SifSearchModuleByName("Network_Manager")
         * could make the old path skip Titan's embedded NETMAN even though
         * NETMAN_RPC_NUMBER never became bindable.  The cold path now follows
         * the PS2SDK ordering: DEV9 -> embedded NETMAN -> SMAP -> RPC -> EE.
         * The bounded RPC bind below is the authoritative readiness proof.
         */
        TitanHealthTrace_Critical(
            TITAN_HT_NETWORK_MODULE_SEARCH_BEFORE,g_poll_frame,1u,4u,0u
        );
        netman_search_hint=SifSearchModuleByName("Network_Manager");
        TitanHealthTrace_Mark(
            TITAN_HT_NETWORK_MODULE_SEARCH_AFTER,g_poll_frame,1u,
            (uint32_t)netman_search_hint,4u
        );
        TitanDiagnostics_Event(
            TITAN_DIAG_DEBUG,TITAN_DIAG_NETMAN,0x25b,netman_search_hint,
            (uint32_t)g_netman_loaded,0u,g_poll_frame,0u
        );

        if(!g_netman_loaded){
            g_netman_loaded=TitanNetwork_LoadModule(
                "NETMAN",titan_netman_irx,size_titan_netman_irx
            );
            TitanDiagnostics_Event(
                g_netman_loaded?TITAN_DIAG_INFO:TITAN_DIAG_WARN,
                TITAN_DIAG_NETMAN,0x25c,g_netman_loaded?0:-1,
                (uint32_t)g_netman_loaded,
                (uint32_t)netman_search_hint,
                size_titan_netman_irx,g_poll_frame
            );
        }

        /*
         * Do not abort on the name-search/load hint. A compatible resident
         * NETMAN may make a duplicate load fail, while a false-positive name
         * lookup must never skip the embedded image again. RPC is authoritative.
         */
    }

    g_network.netman_iop_ready=g_netman_loaded?1:0;

    TitanNetwork_SetBootstrapStage(TITAN_NETWORK_BOOTSTRAP_BACKEND);
    if(g_network.preference==TITAN_NETWORK_PREFER_ETHERNET)
        selected_ok=TitanNetwork_EnsureEthernetDriver();
    else
        selected_ok=TitanNetwork_EnsureWifiDriver();

    g_network.main_interface_ready=selected_ok?1:0;
    if(!selected_ok){
        g_network.state=TITAN_NETWORK_ERROR;
        TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),
            g_network.preference==TITAN_NETWORK_PREFER_ETHERNET
                ? "Ethernet backend failed during DEV9 / SMAP initialization"
                : "AR9271 IOP backend failed to initialize");
        TitanNetwork_SetBootstrapStage(TITAN_NETWORK_BOOTSTRAP_ERROR);
        return;
    }

    TitanNetwork_SetBootstrapStage(TITAN_NETWORK_BOOTSTRAP_NETMAN_RPC);
    if(!TitanNetwork_WaitForNetmanRpcServer()){
        g_network.netman_rpc_ready=0;
        g_network.state=TITAN_NETWORK_ERROR;
        TitanNetwork_Copy(
            g_network.detail,sizeof(g_network.detail),
            "NETMAN IOP RPC server unavailable; bounded retry scheduled"
        );
        TitanNetwork_SetBootstrapStage(TITAN_NETWORK_BOOTSTRAP_ERROR);
        return;
    }

    /* Successful bind to NETMAN_RPC_NUMBER is the authoritative health proof. */
    g_netman_loaded=1;
    g_network.netman_iop_ready=1;
    g_network.netman_rpc_ready=1;

    TitanNetwork_SetBootstrapStage(TITAN_NETWORK_BOOTSTRAP_NETMAN_EE);
    if(!g_netman_ee_initialized){
        int netman_init_result;
        TitanHealthTrace_Critical(
            TITAN_HT_NETWORK_NETMAN_INIT_BEFORE,g_poll_frame,0u,0u,0u
        );
        netman_init_result=NetManInit();
        TitanHealthTrace_Mark(
            TITAN_HT_NETWORK_NETMAN_INIT_AFTER,g_poll_frame,
            (uint32_t)netman_init_result,0u,0u
        );
        TitanDiagnostics_Event(
            netman_init_result==0?TITAN_DIAG_INFO:TITAN_DIAG_ERROR,
            TITAN_DIAG_NETMAN,0x256,netman_init_result,0u,0u,0u,0u
        );
        if(netman_init_result!=0){
            g_network.netman_ee_ready=0;
            g_network.state=TITAN_NETWORK_ERROR;
            TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),"NetManInit EE RPC failed");
            TitanNetwork_SetBootstrapStage(TITAN_NETWORK_BOOTSTRAP_ERROR);
            return;
        }
        g_netman_ee_initialized=1;
    }
    g_network.netman_ee_ready=g_netman_ee_initialized?1:0;

    if(g_network.preference==TITAN_NETWORK_PREFER_ETHERNET){
        int smap_select_result;

        /*
         * RC5.6 hardware-trace closure (2026-08-30):
         *
         * The Titan-safe SMA alias is selected before ps2ip so that ps2ip's
         * initial GET_MAC ioctl is routed to SMAP even when WFI also exists.
         *
         * DO NOT call NetManSetLinkMode(AUTO) here. The frozen SMAP driver is
         * already built with AUTO negotiation as its default. Its
         * SMAPSetLinkMode() deliberately returns -ENXIO while
         * SmapIsInitialized is still false. Hardware trace proved that the old
         * bootstrap called it during that asynchronous start window and then
         * converted the expected -ENXIO into a fatal bootstrap error.
         *
         * The protected Titan UL/OPL Ethernet path follows the same rule for
         * AUTO: CurrentMode starts at AUTO and NetManSetLinkMode() is only
         * invoked when the requested mode actually differs. Physical link is
         * observed later through NETMAN_NETIF_IOCTL_GET_LINK_STATUS.
         */
        smap_select_result=NetManSetMainIF("SMA");
        TitanDiagnostics_Event(
            smap_select_result==0?TITAN_DIAG_INFO:TITAN_DIAG_ERROR,
            TITAN_DIAG_NETMAN,0x25d,smap_select_result,
            (uint32_t)g_smap_loaded,(uint32_t)g_netman_loaded,
            (uint32_t)g_netman_ee_initialized,g_poll_frame
        );
        if(smap_select_result!=0){
            g_network.state=TITAN_NETWORK_ERROR;
            TitanNetwork_Copy(
                g_network.detail,sizeof(g_network.detail),
                "SMAP main-interface selection failed"
            );
            TitanNetwork_SetBootstrapStage(TITAN_NETWORK_BOOTSTRAP_ERROR);
            return;
        }

        /* 0x264: AUTO link policy is owned by SMAP's validated default.
         * a=1 means the early fatal SetLinkMode(AUTO) call was intentionally
         * skipped; b/c preserve the proven backend lifetime state. */
        TitanDiagnostics_Event(
            TITAN_DIAG_INFO,TITAN_DIAG_NETMAN,0x264,0,
            1u,(uint32_t)g_smap_loaded,
            (uint32_t)g_netman_ee_initialized,g_poll_frame
        );
    }

    TitanNetwork_SetBootstrapStage(TITAN_NETWORK_BOOTSTRAP_PS2IP);
    if(!g_ps2ip_initialized){
        IP4_ADDR(&ip,169,254,0,1);
        IP4_ADDR(&nm,255,255,0,0);
        IP4_ADDR(&gw,0,0,0,0);
        {
            int ps2ip_result;
            TitanHealthTrace_Critical(
                TITAN_HT_NETWORK_PS2IP_INIT_BEFORE,g_poll_frame,0u,0u,0u
            );
            ps2ip_result=ps2ipInit(&ip,&nm,&gw);
            TitanHealthTrace_Mark(
                TITAN_HT_NETWORK_PS2IP_INIT_AFTER,g_poll_frame,
                (uint32_t)ps2ip_result,0u,0u
            );
            TitanDiagnostics_Event(
                ps2ip_result>=0?TITAN_DIAG_INFO:TITAN_DIAG_ERROR,
                TITAN_DIAG_PS2IP,0x243,ps2ip_result,0u,0u,0u,0u
            );
            if(ps2ip_result<0){
                g_network.ps2ip_ready=0;
                g_network.state=TITAN_NETWORK_ERROR;
                TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),"ps2ipInit failed");
                TitanNetwork_SetBootstrapStage(TITAN_NETWORK_BOOTSTRAP_ERROR);
                return;
            }
        }
        g_ps2ip_initialized=1;
    }
    g_network.ps2ip_ready=g_ps2ip_initialized?1:0;

    TitanNetwork_SetBootstrapStage(TITAN_NETWORK_BOOTSTRAP_MAIN_IF);
    selected_ok=TitanNetwork_SelectMainInterface(
        g_network.preference,
        g_network.preference==TITAN_NETWORK_PREFER_ETHERNET ? 1 : 0
    );
    TitanDiagnostics_Event(
        selected_ok?TITAN_DIAG_INFO:TITAN_DIAG_ERROR,TITAN_DIAG_NETMAN,0x244,selected_ok?0:-1,
        (uint32_t)g_network.preference,0u,0u,0u
    );
    if(!selected_ok){
        g_network.state=TITAN_NETWORK_ERROR;
        TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),"Main interface selection failed");
        TitanNetwork_SetBootstrapStage(TITAN_NETWORK_BOOTSTRAP_ERROR);
        return;
    }

    if(g_network.preference!=TITAN_NETWORK_PREFER_ETHERNET &&
       !TitanNetwork_RestartDhcp()){
        g_network.state=TITAN_NETWORK_ERROR;
        TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),"ps2ip DHCP failed to start");
        TitanNetwork_SetBootstrapStage(TITAN_NETWORK_BOOTSTRAP_ERROR);
        return;
    }

    TitanNetwork_SetBootstrapStage(TITAN_NETWORK_BOOTSTRAP_READY);
}

#if defined(__EE__) || defined(_EE)
static void TitanNetwork_BootstrapWorker(void *arg)
{
    (void)arg;
    TitanHealthTrace_Mark(
        TITAN_HT_NETWORK_BOOT_WORKER_ENTER,g_poll_frame,
        (uint32_t)g_network.preference,0u,0u
    );

    TitanNetwork_InitInterfaces();

    if(g_network.enabled && g_network.state!=TITAN_NETWORK_ERROR){
        if(g_network.preference==TITAN_NETWORK_PREFER_ETHERNET){
            g_network.state=TITAN_NETWORK_CONNECTING;
            TitanNetwork_Copy(
                g_network.detail,sizeof(g_network.detail),
                "Ethernet initialized; waiting for RJ45 link / DHCP"
            );
        }else{
            g_network.state=(g_network.ssid[0]&&g_wifi_pmk_valid)
                ? TITAN_NETWORK_OFFLINE:TITAN_NETWORK_WAITING_CONFIG;
            TitanNetwork_Copy(
                g_network.detail,sizeof(g_network.detail),
                g_network.state==TITAN_NETWORK_WAITING_CONFIG
                    ? "Configure Wi-Fi in Settings > Network"
                    : "Network ready to connect"
            );
        }
    }else if(!g_network.enabled){
        g_network.state=TITAN_NETWORK_OFFLINE;
        TitanNetwork_Copy(
            g_network.detail,sizeof(g_network.detail),
            "Internet connection disabled"
        );
    }

    TitanHealthTrace_Critical(
        TITAN_HT_NETWORK_BOOT_WORKER_EXIT,g_poll_frame,
        (uint32_t)g_network.state,(uint32_t)g_network.preference,0u
    );
    if(g_network_bootstrap_stage==TITAN_NETWORK_BOOTSTRAP_READY){
        g_network_bootstrap_retry_count=0u;
        g_network_bootstrap_next_retry_frame=0u;
    }else if(g_network.state==TITAN_NETWORK_ERROR &&
             (g_network_bootstrap_error_stage==TITAN_NETWORK_BOOTSTRAP_NETMAN_RPC ||
              g_network_bootstrap_error_stage==TITAN_NETWORK_BOOTSTRAP_NETMAN_EE)){
        g_network_bootstrap_next_retry_frame=
            g_poll_frame+TITAN_NETWORK_BOOTSTRAP_RETRY_FRAMES;
    }
    g_network_bootstrap_done=1;
    g_network_bootstrap_worker_active=0;
    ExitDeleteThread();
}

static int TitanNetwork_StartBootstrapWorker(void)
{
    ee_thread_t thread;
    int id;

    if(g_network_bootstrap_worker_active)
        return 1;

    memset(&thread,0,sizeof(thread));
    thread.func=TitanNetwork_BootstrapWorker;
    thread.stack=g_network_bootstrap_stack;
    thread.stack_size=sizeof(g_network_bootstrap_stack);
    thread.gp_reg=&_gp;
    thread.initial_priority=TITAN_NETWORK_BOOTSTRAP_PRIORITY;

    TitanHealthTrace_Mark(
        TITAN_HT_NETWORK_BOOT_WORKER_CREATE_BEFORE,g_poll_frame,0u,0u,0u
    );
    id=CreateThread(&thread);
    TitanHealthTrace_Mark(
        TITAN_HT_NETWORK_BOOT_WORKER_CREATE_AFTER,g_poll_frame,(uint32_t)id,0u,0u
    );
    if(id<0)
        return 0;

    g_network_bootstrap_worker_active=1;
    if(StartThread(id,0)<0){
        g_network_bootstrap_worker_active=0;
        DeleteThread(id);
        return 0;
    }
    return 1;
}
#else
static int TitanNetwork_StartBootstrapWorker(void)
{
    TitanNetwork_InitInterfaces();
    g_network_bootstrap_done=1;
    return 1;
}
#endif

static int TitanNetwork_BindWifiRpc(void)
{
    if(!g_network.wifi_rpc_bound){
        memset(&g_wifi_rpc,0,sizeof(g_wifi_rpc));
        if(SifBindRpc(&g_wifi_rpc,TITAN_AR9271_SCAN_RPC_ID,0)>=0 && g_wifi_rpc.server)
            g_network.wifi_rpc_bound=1;
    }
    if(g_network.wifi_rpc_bound && !g_wifi_info_rpc.server){
        memset(&g_wifi_info_rpc,0,sizeof(g_wifi_info_rpc));
        (void)SifBindRpc(&g_wifi_info_rpc,TITAN_AR9271_SCAN_RPC_ID,0);
    }
    return g_network.wifi_rpc_bound?1:0;
}

static unsigned int TitanNetwork_BackgroundPollFrames(void)
{
    unsigned int frames = TITAN_NETWORK_RPC_POLL_FRAMES;
    /* Connection establishment stays at the proven cadence. Only steady-state
     * polling yields frame time when the shell is under pressure. */
    if (g_network.state != TITAN_NETWORK_ONLINE)
        return frames;
#if defined(__GNUC__)
    if (TitanVlombisCore_GetBackgroundCadence != 0)
        frames = TitanVlombisCore_GetBackgroundCadence(
            TITAN_NETWORK_RPC_POLL_FRAMES, TITAN_NETWORK_RPC_POLL_FRAMES * 2u);
#else
    frames = TitanVlombisCore_GetBackgroundCadence(
        TITAN_NETWORK_RPC_POLL_FRAMES, TITAN_NETWORK_RPC_POLL_FRAMES * 2u);
#endif
    return frames == 0u ? TITAN_NETWORK_RPC_POLL_FRAMES : frames;
}

static void TitanNetwork_RecordBackgroundTick(void)
{
#if defined(__GNUC__)
    if (TitanVlombisCore_RecordWork == 0)
        return;
#endif
    TitanVlombisCore_RecordWork(
        TITAN_VLOMBIS_CAP_NETWORK_BACKGROUND_QOS | TITAN_VLOMBIS_CAP_BACKGROUND_QOS |
            TITAN_VLOMBIS_CAP_FRAME_BUDGETING,
        TITAN_VLOMBIS_EVENT_NETWORK_BACKGROUND_TICK, 1u, 0u);
}

static void TitanNetwork_TestSetDetail(char *target,unsigned int capacity,const char *text)
{
    TitanNetwork_Copy(target,capacity,text!=0?text:"");
}

static int TitanNetwork_ProbeDnsServer(unsigned int address_host_order,unsigned short transaction_id)
{
#if defined(__EE__) || defined(_EE)
    static const unsigned char qname[] = {
        7u,'e','x','a','m','p','l','e',3u,'c','o','m',0u
    };
    struct sockaddr_in remote;
    fd_set read_set;
    struct timeval timeout;
    unsigned char query[32];
    unsigned char response[192];
    int fd;
    int sent;
    int ready;
    int received;
    unsigned int offset=0u;

    memset(query,0,sizeof(query));
    query[offset++]=(unsigned char)(transaction_id>>8);
    query[offset++]=(unsigned char)(transaction_id&0xffu);
    query[offset++]=0x01u; /* recursion desired */
    query[offset++]=0x00u;
    query[offset++]=0x00u;
    query[offset++]=0x01u; /* one question */
    offset+=6u;
    memcpy(query+offset,qname,sizeof(qname));
    offset+=(unsigned int)sizeof(qname);
    query[offset++]=0x00u; query[offset++]=0x01u; /* A */
    query[offset++]=0x00u; query[offset++]=0x01u; /* IN */

    fd=socket(PF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(fd<0) return 0;

    memset(&remote,0,sizeof(remote));
    remote.sin_family=AF_INET;
    remote.sin_port=htons(53u);
    remote.sin_addr.s_addr=htonl(address_host_order);

    sent=sendto(
        fd,query,(int)offset,0,
        (struct sockaddr*)&remote,sizeof(remote)
    );
    if(sent!=(int)offset){
        lwip_close(fd);
        return 0;
    }

    FD_ZERO(&read_set);
    FD_SET(fd,&read_set);
    timeout.tv_sec=(long)(TITAN_NETWORK_TEST_DNS_TIMEOUT_MS/1000u);
    timeout.tv_usec=(long)((TITAN_NETWORK_TEST_DNS_TIMEOUT_MS%1000u)*1000u);
    ready=select(fd+1,&read_set,0,0,&timeout);
    if(ready<=0){
        lwip_close(fd);
        return 0;
    }

    received=recvfrom(fd,response,sizeof(response),0,0,0);
    lwip_close(fd);
    if(received<12) return 0;
    if(response[0]!=(unsigned char)(transaction_id>>8) ||
       response[1]!=(unsigned char)(transaction_id&0xffu)) return 0;
    if((response[2]&0x80u)==0u) return 0; /* DNS response bit */
    return 1;
#else
    (void)address_host_order;
    (void)transaction_id;
    return 0;
#endif
}

static int TitanNetwork_ProbeInternet(void)
{
    unsigned short transaction_id=(unsigned short)(0x5400u | (g_network_test.generation&0xffu));
    /* Use two independent public recursive resolvers.  A valid DNS reply proves
     * routing beyond the LAN without depending on Titan's configured DNS text. */
    if(TitanNetwork_ProbeDnsServer(0x01010101u,transaction_id)) return 1;
    return TitanNetwork_ProbeDnsServer(0x08080808u,(unsigned short)(transaction_id+1u));
}

static int TitanNetwork_CaptureEthernetAssurance(int main_if_ok)
{
    t_ip_info ipinfo;
    int link_raw;

    memset(&ipinfo,0,sizeof(ipinfo));
    g_network_test.netman_ok=g_netman_loaded?1:0;
    g_network_test.dev9_ok=g_dev9_loaded?1:0;
    g_network_test.smap_ok=g_smap_loaded?1:0;
    g_network_test.ps2ip_ok=g_ps2ip_initialized?1:0;
    g_network_test.sm0_ok=(g_ps2ip_initialized &&
        ps2ip_getconfig("sm0",&ipinfo)==1)?1:0;
    g_network_test.main_if_ok=main_if_ok?1:0;

    link_raw=NetManIoctl(NETMAN_NETIF_IOCTL_GET_LINK_STATUS,0,0,0,0);
    g_network_test.link_ioctl_ok=
        (link_raw==NETMAN_NETIF_ETH_LINK_STATE_UP ||
         link_raw==NETMAN_NETIF_ETH_LINK_STATE_DOWN)?1:0;
    g_network_test.link_mode=g_network_test.link_ioctl_ok &&
        link_raw==NETMAN_NETIF_ETH_LINK_STATE_UP
        ? NetManIoctl(NETMAN_NETIF_IOCTL_ETH_GET_LINK_MODE,0,0,0,0)
        : -1;

    if(g_network_test.netman_ok && g_network_test.dev9_ok &&
       g_network_test.smap_ok && g_network_test.ps2ip_ok &&
       g_network_test.sm0_ok && g_network_test.link_ioctl_ok &&
       g_network_test.main_if_ok){
        g_network_test.software_result=TITAN_NETWORK_TEST_SUCCESS;
        TitanNetwork_TestSetDetail(
            g_network_test.software_detail,sizeof(g_network_test.software_detail),
            "NETMAN, DEV9, SMAP, ps2ip, sm0 and link-status ioctl verified"
        );
        return 1;
    }

    g_network_test.software_result=TITAN_NETWORK_TEST_FAILED;
    TitanNetwork_TestSetDetail(
        g_network_test.software_detail,sizeof(g_network_test.software_detail),
        "Ethernet software path failed; see Network Services Status."
    );
    TitanDiagnostics_Event(
        TITAN_DIAG_ERROR,TITAN_DIAG_NETWORK,0x253,-1,
        ((uint32_t)g_network_test.netman_ok<<24) |
        ((uint32_t)g_network_test.dev9_ok<<16) |
        ((uint32_t)g_network_test.smap_ok<<8) |
        (uint32_t)g_network_test.ps2ip_ok,
        ((uint32_t)g_network_test.sm0_ok<<16) |
        ((uint32_t)g_network_test.link_ioctl_ok<<8) |
        (uint32_t)g_network_test.main_if_ok,
        (uint32_t)link_raw,(uint32_t)g_network_test.link_mode
    );
    return 0;
}

#if defined(__EE__) || defined(_EE)
static int TitanNetwork_TestWaitForBootstrap(void)
{
    unsigned int step;
    if(g_network_bootstrap_done) return g_network.state!=TITAN_NETWORK_ERROR;
    for(step=0u;step<TITAN_NETWORK_TEST_BOOTSTRAP_WAIT_STEPS;step++){
        if(g_network_bootstrap_done)
            return g_network.state!=TITAN_NETWORK_ERROR;
        DelayThread(TITAN_NETWORK_TEST_WAIT_US);
    }
    return 0;
}

static void TitanNetwork_ConnectionTestWorker(void *arg)
{
    unsigned int step;
    int retry_ok=0;
    int software_ok=1;
    int physical_ok=0;
    int ip_ok=0;
    unsigned int generation=(unsigned int)(uintptr_t)arg;

    if(generation!=g_network_test.generation) goto finished;

    if(!g_network.enabled){
        g_network_test.software_result=TITAN_NETWORK_TEST_FAILED;
        g_network_test.physical_result=TITAN_NETWORK_TEST_NOT_STARTED;
        g_network_test.ip_result=TITAN_NETWORK_TEST_NOT_STARTED;
        g_network_test.internet_result=TITAN_NETWORK_TEST_NOT_STARTED;
        TitanNetwork_TestSetDetail(
            g_network_test.software_detail,sizeof(g_network_test.software_detail),
            "Network services are disabled"
        );
        goto finished;
    }

    if(!TitanNetwork_TestWaitForBootstrap()){
        g_network_test.software_result=TITAN_NETWORK_TEST_FAILED;
        g_network_test.physical_result=TITAN_NETWORK_TEST_NOT_STARTED;
        g_network_test.ip_result=TITAN_NETWORK_TEST_NOT_STARTED;
        g_network_test.internet_result=TITAN_NETWORK_TEST_NOT_STARTED;
        TitanNetwork_TestSetDetail(
            g_network_test.software_detail,sizeof(g_network_test.software_detail),
            "Network bootstrap did not complete"
        );
        TitanDiagnostics_Event(
            TITAN_DIAG_ERROR,TITAN_DIAG_NETWORK,0x255,-1,
            (uint32_t)(g_network_bootstrap_stage==TITAN_NETWORK_BOOTSTRAP_ERROR
                ? g_network_bootstrap_error_stage:g_network_bootstrap_stage),
            (uint32_t)g_network_bootstrap_done,
            (uint32_t)g_network.state,(uint32_t)g_network.preference
        );
        goto finished;
    }

    retry_ok=TitanNetwork_Retry();

    if(g_network_test.preference==TITAN_NETWORK_PREFER_ETHERNET){
        software_ok=TitanNetwork_CaptureEthernetAssurance(retry_ok);
        if(!software_ok){
            g_network_test.physical_result=TITAN_NETWORK_TEST_NOT_STARTED;
            g_network_test.ip_result=TITAN_NETWORK_TEST_NOT_STARTED;
            g_network_test.internet_result=TITAN_NETWORK_TEST_NOT_STARTED;
            goto finished;
        }

        {
            int physical_known=0;
            for(step=0u;step<TITAN_NETWORK_TEST_LINK_WAIT_STEPS;step++){
                int link_raw=NetManIoctl(
                    NETMAN_NETIF_IOCTL_GET_LINK_STATUS,0,0,0,0
                );
                if(link_raw==NETMAN_NETIF_ETH_LINK_STATE_UP ||
                   link_raw==NETMAN_NETIF_ETH_LINK_STATE_DOWN){
                    physical_known=1;
                    g_network.ethernet_link_known=1;
                    g_network.ethernet_link_up=
                        link_raw==NETMAN_NETIF_ETH_LINK_STATE_UP;
                    if(g_network.ethernet_link_up){
                        physical_ok=1;
                        break;
                    }
                }else{
                    g_network.ethernet_link_known=0;
                }
                DelayThread(TITAN_NETWORK_TEST_WAIT_US);
            }
            if(!physical_ok){
                g_network_test.physical_result=TITAN_NETWORK_TEST_FAILED;
                TitanNetwork_TestSetDetail(
                    g_network_test.physical_detail,sizeof(g_network_test.physical_detail),
                    physical_known
                        ? "Cable disconnected"
                        : "Ethernet physical link status unavailable"
                );
                g_network_test.ip_result=TITAN_NETWORK_TEST_NOT_STARTED;
                TitanNetwork_TestSetDetail(
                    g_network_test.ip_detail,sizeof(g_network_test.ip_detail),
                    physical_known
                        ? "Waiting for an Ethernet link partner"
                        : "Physical link was not verified"
                );
                g_network_test.internet_result=TITAN_NETWORK_TEST_NOT_STARTED;
                TitanNetwork_TestSetDetail(
                    g_network_test.internet_detail,sizeof(g_network_test.internet_detail),
                    physical_known
                        ? "Requires a physical Ethernet link"
                        : "Requires a verified physical link"
                );
                goto finished;
            }
        }
        g_network_test.physical_result=TITAN_NETWORK_TEST_SUCCESS;
        TitanNetwork_TestSetDetail(
            g_network_test.physical_detail,sizeof(g_network_test.physical_detail),
            "Cable connected / SMAP link up"
        );
    }else{
        g_network_test.software_result=retry_ok
            ? TITAN_NETWORK_TEST_SUCCESS:TITAN_NETWORK_TEST_FAILED;
        TitanNetwork_TestSetDetail(
            g_network_test.software_detail,sizeof(g_network_test.software_detail),
            retry_ok?"Wi-Fi retry accepted by the AR9271 service":"Wi-Fi service is not ready"
        );
        if(!retry_ok) goto finished;
        for(step=0u;step<TITAN_NETWORK_TEST_LINK_WAIT_STEPS*2u;step++){
            if(g_network.wifi_online){physical_ok=1;break;}
            DelayThread(TITAN_NETWORK_TEST_WAIT_US);
        }
        if(!physical_ok){
            g_network_test.physical_result=TITAN_NETWORK_TEST_FAILED;
            TitanNetwork_TestSetDetail(
                g_network_test.physical_detail,sizeof(g_network_test.physical_detail),
                "Wi-Fi is not associated"
            );
            goto finished;
        }
        g_network_test.physical_result=TITAN_NETWORK_TEST_SUCCESS;
        TitanNetwork_TestSetDetail(
            g_network_test.physical_detail,sizeof(g_network_test.physical_detail),
            "Wi-Fi associated"
        );
    }

    g_network_test.ip_result=TITAN_NETWORK_TEST_CHECKING;
    TitanNetwork_TestSetDetail(
        g_network_test.ip_detail,sizeof(g_network_test.ip_detail),
        "Waiting for DHCP / IPv4"
    );
    for(step=0u;step<TITAN_NETWORK_TEST_IP_WAIT_STEPS;step++){
        if(g_network.ip[0] && g_network.state==TITAN_NETWORK_ONLINE){
            ip_ok=1;
            break;
        }
        DelayThread(TITAN_NETWORK_TEST_WAIT_US);
    }
    if(!ip_ok){
        g_network_test.ip_result=TITAN_NETWORK_TEST_FAILED;
        TitanNetwork_TestSetDetail(
            g_network_test.ip_detail,sizeof(g_network_test.ip_detail),
            "DHCP did not produce a usable IPv4 address"
        );
        g_network_test.internet_result=TITAN_NETWORK_TEST_NOT_STARTED;
        TitanNetwork_TestSetDetail(
            g_network_test.internet_detail,sizeof(g_network_test.internet_detail),
            "Internet probe requires an IPv4 address"
        );
        goto finished;
    }

    g_network_test.ip_result=TITAN_NETWORK_TEST_SUCCESS;
    TitanNetwork_TestSetDetail(
        g_network_test.ip_detail,sizeof(g_network_test.ip_detail),g_network.ip
    );

    g_network_test.internet_result=TITAN_NETWORK_TEST_CHECKING;
    TitanNetwork_TestSetDetail(
        g_network_test.internet_detail,sizeof(g_network_test.internet_detail),
        "Probing external DNS reachability"
    );
    if(TitanNetwork_ProbeInternet()){
        g_network_test.internet_result=TITAN_NETWORK_TEST_SUCCESS;
        TitanNetwork_TestSetDetail(
            g_network_test.internet_detail,sizeof(g_network_test.internet_detail),
            "External network reply received"
        );
    }else{
        g_network_test.internet_result=TITAN_NETWORK_TEST_FAILED;
        TitanNetwork_TestSetDetail(
            g_network_test.internet_detail,sizeof(g_network_test.internet_detail),
            "No external DNS reply received"
        );
    }

finished:
    if(generation==g_network_test.generation){
        g_network_test.running=0;
        g_network_test.complete=1;
        TitanDiagnostics_Event(
            g_network_test.software_result==TITAN_NETWORK_TEST_FAILED
                ? TITAN_DIAG_ERROR:TITAN_DIAG_INFO,
            TITAN_DIAG_NETWORK,0x252,0,
            (uint32_t)g_network_test.software_result,
            (uint32_t)g_network_test.physical_result,
            (uint32_t)g_network_test.ip_result,
            (uint32_t)g_network_test.internet_result
        );
    }
    g_network_test_worker_active=0;
    ExitDeleteThread();
}
#endif

void TitanNetwork_Initialize(void)
{
    memset(&g_network,0,sizeof(g_network));
    memset(g_wifi_pmk,0,sizeof(g_wifi_pmk));
    memset(g_wifi_aps,0,sizeof(g_wifi_aps));
    g_network.preference=TITAN_NETWORK_PREFER_WIFI;
    g_network.enabled=1;
    g_network.state=TITAN_NETWORK_INITIALIZING;
    TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),
        "Network waiting for shell stabilization");
    g_ps2ip_initialized=0;
    g_netman_ee_initialized=0;
    g_netman_loaded=0;
    g_dev9_loaded=0;
    g_smap_loaded=0;
    g_wifi_bridge_enabled=0;
    g_wifi_autoconnect_attempted=0;
    g_wifi_scan_pending=0;
    g_network_bootstrap_done=0;
    g_network_bootstrap_attempted=0;
    g_network_bootstrap_stage=TITAN_NETWORK_BOOTSTRAP_IDLE;
    g_network_bootstrap_error_stage=TITAN_NETWORK_BOOTSTRAP_IDLE;
    g_network_home_ready_frame=0u;
    g_network_bootstrap_retry_count=0u;
    g_network_bootstrap_next_retry_frame=0u;
    memset(&g_netman_probe_rpc,0,sizeof(g_netman_probe_rpc));
#if defined(__EE__) || defined(_EE)
    g_network_bootstrap_worker_active=0;
    g_network_test_worker_active=0;
#endif
    memset(&g_network_test,0,sizeof(g_network_test));
    g_network_test_generation=0u;
    g_wifi_online_since_frame=0u;
    g_wifi_handoff_pending=0;
    g_diag_last_wifi_scan_state=-999;
    g_network_settings_loaded=0;
    g_network_settings_load_attempts=0u;
    g_diag_last_ethernet_link=-1;
    g_diag_last_ethernet_link_ioctl=-999;
    g_diag_last_ethernet_dhcp_status=-999;
    g_diag_last_ethernet_online=-1;
    g_ethernet_dhcp_wait_polls=0u;
    g_ethernet_dhcp_retry_count=0u;
    g_ethernet_dhcp_pending=0;
    g_ethernet_dhcp_started=0;
    g_ethernet_last_link_state=-1;
    memset(&g_wifi_info_rpc,0,sizeof(g_wifi_info_rpc));
    memset(g_wifi_info_rpc_buffer,0,sizeof(g_wifi_info_rpc_buffer));
    g_wifi_info_rpc_pending=0;
    g_wifi_info_rpc_done=0;
    (void)TitanNetwork_Load();
    g_poll_frame=0u;
}

void TitanNetwork_Update(void)
{
    TitanAr9271ScanInfo info;
    t_ip_info ipinfo;
    unsigned int rpc_poll_frames;
    g_poll_frame++;
    rpc_poll_frames = TitanNetwork_BackgroundPollFrames();

    /* A boot-time read can race the USB/fileXio mount on some hardware.  Retry
     * a few times before network bootstrap; a successful runtime save marks the
     * settings as loaded and suppresses these retries. */
    if(!g_network_settings_loaded &&
       g_network_settings_load_attempts<TITAN_NETWORK_SETTINGS_LOAD_MAX_ATTEMPTS &&
       (g_poll_frame%TITAN_NETWORK_SETTINGS_LOAD_RETRY_FRAMES)==0u)
        (void)TitanNetwork_Load();

    /*
     * Never put SifExecModuleBuffer/ps2ip bootstrap on the kernel's critical
     * startup path. BF6 first reaches a stable rendered shell; only then does
     * the optional network stack claim IOP memory.
     */
    if(!g_network.enabled){
        if(g_network.state!=TITAN_NETWORK_OFFLINE){
            g_network.state=TITAN_NETWORK_OFFLINE;
            TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),"Internet connection disabled");
        }
        return;
    }

    /* RC5.6: the old 120-frame counter could start NETMAN while PADEMU was
     * still completing its IOP-side initialization.  HOME readiness is the
     * actual boot boundary.  Give it a short post-HOME stabilization window,
     * then perform network bring-up on the existing worker only. */
    if(!TitanBootRecovery_GetState()->home_ready)
        return;
    if(g_network_home_ready_frame==0u){
        g_network_home_ready_frame=g_poll_frame;
        return;
    }

    if(g_network_bootstrap_done && g_network.state==TITAN_NETWORK_ERROR &&
       (g_network_bootstrap_error_stage==TITAN_NETWORK_BOOTSTRAP_NETMAN_RPC ||
        g_network_bootstrap_error_stage==TITAN_NETWORK_BOOTSTRAP_NETMAN_EE) &&
       g_network_bootstrap_retry_count<TITAN_NETWORK_BOOTSTRAP_MAX_RETRIES &&
       !g_network_bootstrap_worker_active &&
       g_poll_frame>=g_network_bootstrap_next_retry_frame){
        g_network_bootstrap_retry_count++;
        g_network_bootstrap_done=0;
        g_network_bootstrap_attempted=0;
        g_network_bootstrap_stage=TITAN_NETWORK_BOOTSTRAP_IDLE;
        g_network_bootstrap_error_stage=TITAN_NETWORK_BOOTSTRAP_IDLE;
        TitanNetwork_Copy(
            g_network.detail,sizeof(g_network.detail),
            "Retrying NETMAN RPC bootstrap in background"
        );
        TitanDiagnostics_Event(
            TITAN_DIAG_INFO,TITAN_DIAG_NETMAN,0x25a,0,
            g_network_bootstrap_retry_count,
            TITAN_NETWORK_BOOTSTRAP_MAX_RETRIES,g_poll_frame,0u
        );
    }

    if(!g_network_bootstrap_done){
        if(g_poll_frame-g_network_home_ready_frame<TITAN_NETWORK_HOME_STABILIZE_FRAMES)
            return;
        if(!g_network_bootstrap_attempted &&
           g_poll_frame>=TITAN_NETWORK_BOOT_GRACE_FRAMES){
            g_network_bootstrap_attempted=1;
            TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),
                "Initializing selected network interface in background");
            if(!TitanNetwork_StartBootstrapWorker()){
                g_network_bootstrap_done=1;
                g_network.state=TITAN_NETWORK_ERROR;
                TitanNetwork_Copy(
                    g_network.detail,sizeof(g_network.detail),
                    "Failed to start network worker"
                );
            }
        }
        /* Never execute SifExecModuleBuffer(), ps2ipInit() or NetManSetMainIF()
         * from the frame loop.  The dedicated worker owns the boot sequence,
         * so a stuck IOP/RPC call can fail networking without freezing UI. */
        if(!g_network_bootstrap_done)return;
    }
    if(g_network.wifi_irx_loaded && !g_network.wifi_rpc_bound){
        if(TitanNetwork_BindWifiRpc() &&
           g_network.preference==TITAN_NETWORK_PREFER_WIFI &&
           g_network.ssid[0] && g_wifi_pmk_valid && !g_wifi_autoconnect_attempted){
            g_wifi_autoconnect_attempted=1;
            (void)TitanNetwork_ConnectWifi();
        }
    }
    if(g_wifi_scan_pending && g_network.wifi_rpc_bound &&
       g_network.preference==TITAN_NETWORK_PREFER_WIFI){
        g_wifi_scan_pending=0;
        (void)TitanNetwork_RequestWifiScan();
    }
    if(g_network.wifi_rpc_bound && !g_wifi_info_rpc.server &&
       (g_poll_frame%rpc_poll_frames)==0u){
        memset(&g_wifi_info_rpc,0,sizeof(g_wifi_info_rpc));
        (void)SifBindRpc(&g_wifi_info_rpc,TITAN_AR9271_SCAN_RPC_ID,0);
    }

    /* Consume a completed status snapshot without waiting in the frame loop. */
    if(g_wifi_info_rpc_done){
        g_wifi_info_rpc_done=0;
        g_wifi_info_rpc_pending=0;
        memcpy(&info,(void*)UNCACHED_SEG(g_wifi_info_rpc_buffer),sizeof(info));
            if(info.magic==TITAN_AR9271_SCAN_MAGIC){
                unsigned int i;
                int was_online=g_network.wifi_online;
                if(g_diag_last_wifi_scan_state!=info.state){
                    TitanDiagnostics_Event(
                        info.state==TITAN_SCAN_ERROR?TITAN_DIAG_ERROR:TITAN_DIAG_INFO,
                        TITAN_DIAG_WIFI,0x210,(int32_t)info.error_code,
                        (uint32_t)info.state,info.rx_usb_packets,info.rx_80211_frames,info.hif_packets
                    );
                    TitanDiagnostics_Event(
                        TITAN_DIAG_DEBUG,TITAN_DIAG_WIFI,0x211,0,
                        ((uint32_t)info.auth_status<<16)|(uint32_t)info.assoc_status,
                        ((uint32_t)info.wifi_configured<<24)|((uint32_t)info.target_security<<16)|((uint32_t)(unsigned char)info.target_rssi_dbm<<8)|info.current_channel,
                        info.rx_bytes,info.rx_processed_completions
                    );
                    g_diag_last_wifi_scan_state=info.state;
                }
                g_network.wifi_adapter_present=info.dev_id>=0?1:0;
                g_network.wifi_scanning=info.state==TITAN_SCAN_SCANNING?1:0;
                g_network.wifi_configured=info.wifi_configured?1:0;
                g_network.wifi_online=info.state==TITAN_SCAN_ONLINE_PASS;
                g_network.wifi_ap_count=info.ap_count>TITAN_NETWORK_WIFI_AP_CAPACITY
                    ? TITAN_NETWORK_WIFI_AP_CAPACITY:(unsigned int)info.ap_count;
                memset(g_wifi_aps,0,sizeof(g_wifi_aps));
                for(i=0u;i<g_network.wifi_ap_count;i++){
                    TitanNetwork_Copy(g_wifi_aps[i].ssid,sizeof(g_wifi_aps[i].ssid),info.aps[i].ssid);
                    memcpy(g_wifi_aps[i].bssid,info.aps[i].bssid,6u);
                    g_wifi_aps[i].rssi_dbm=info.aps[i].rssi_dbm;
                    g_wifi_aps[i].channel=info.aps[i].channel;
                    g_wifi_aps[i].security=info.aps[i].security;
                    g_wifi_aps[i].seen_count=info.aps[i].seen_count;
                }
                if(info.gateway[0]||info.gateway[1]||info.gateway[2]||info.gateway[3])
                    snprintf(g_network.gateway,sizeof(g_network.gateway),"%u.%u.%u.%u",info.gateway[0],info.gateway[1],info.gateway[2],info.gateway[3]);
                if(info.dns[0]||info.dns[1]||info.dns[2]||info.dns[3])
                    snprintf(g_network.dns,sizeof(g_network.dns),"%u.%u.%u.%u",info.dns[0],info.dns[1],info.dns[2],info.dns[3]);
                if(info.target_ssid[0])TitanNetwork_Copy(g_network.ssid,sizeof(g_network.ssid),info.target_ssid);
                if(g_network.wifi_online){
                    if(!was_online)g_wifi_online_since_frame=g_poll_frame;
                    if(g_network.preference==TITAN_NETWORK_PREFER_WIFI){
                        if(!g_wifi_bridge_enabled &&
                           g_poll_frame-g_wifi_online_since_frame>=TITAN_NETWORK_WIFI_HANDOFF_GRACE_FRAMES){
                            TitanDiagnostics_Event(TITAN_DIAG_INFO,TITAN_DIAG_NETMAN,0x220,0,g_poll_frame,info.rx_usb_packets,info.hif_packets,0);
                            (void)TitanNetwork_SelectMainInterface(TITAN_NETWORK_PREFER_WIFI,1);
                        }
                        g_network.state=TITAN_NETWORK_CONNECTING;
                        TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),
                            g_wifi_bridge_enabled
                                ? "Wi-Fi secured; handing link to ps2ip"
                                : "Wi-Fi validado; preparando handoff NetMan");
                    }
                }else if(info.state==TITAN_SCAN_ERROR){
                    g_wifi_online_since_frame=0u;g_wifi_handoff_pending=0;
                    if(g_wifi_bridge_enabled)(void)TitanNetwork_WifiBridgeRpc(0);
                    g_network.state=TITAN_NETWORK_ERROR;
                    TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),info.detail);
                }else if(info.state==TITAN_SCAN_SCANNING && !info.wifi_configured){
                    g_network.state=TITAN_NETWORK_WAITING_CONFIG;
                    TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),"Searching for nearby Wi-Fi networks");
                }else if(info.state==TITAN_SCAN_WAITING && !info.wifi_configured){
                    g_network.state=TITAN_NETWORK_WAITING_CONFIG;
                    TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),info.detail);
                }else if(info.state>TITAN_SCAN_USB_DETECTED && info.state<TITAN_SCAN_ONLINE_PASS){
                    g_network.state=TITAN_NETWORK_CONNECTING;
                    TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),info.detail);
                }
            }
    }

    /* Start at most one non-blocking GET_INFO transaction. Explicit network
     * actions keep using g_wifi_rpc and therefore cannot be starved by
     * telemetry polling. */
    if(g_network.wifi_rpc_bound && g_wifi_info_rpc.server &&
       !g_wifi_info_rpc_pending && (g_poll_frame%rpc_poll_frames)==0u){
        TitanNetwork_RecordBackgroundTick();
        memset(g_wifi_info_rpc_buffer,0,sizeof(g_wifi_info_rpc_buffer));
        g_wifi_info_rpc_pending=1;
        if(SifCallRpc(&g_wifi_info_rpc,TITAN_AR9271_SCAN_RPC_GET_INFO,SIF_RPC_M_NOWAIT,
                      g_wifi_info_rpc_buffer,sizeof(info),
                      g_wifi_info_rpc_buffer,sizeof(info),
                      TitanNetwork_InfoRpcDone,0)<0)
            g_wifi_info_rpc_pending=0;
    }

    if(g_ps2ip_initialized && (g_poll_frame%rpc_poll_frames)==0u){
        int link_ioctl,link,link_valid;
        if (!g_network.wifi_rpc_bound)
            TitanNetwork_RecordBackgroundTick();
        link_ioctl=NetManIoctl(NETMAN_NETIF_IOCTL_GET_LINK_STATUS,0,0,0,0);
        link_valid=(link_ioctl==NETMAN_NETIF_ETH_LINK_STATE_UP ||
                    link_ioctl==NETMAN_NETIF_ETH_LINK_STATE_DOWN);
        link=link_valid && link_ioctl==NETMAN_NETIF_ETH_LINK_STATE_UP;
        memset(&ipinfo,0,sizeof(ipinfo));
        if(g_network.preference==TITAN_NETWORK_PREFER_ETHERNET){
            g_network.ethernet_link_known=link_valid?1:0;
            if(g_diag_last_ethernet_link!=link || g_diag_last_ethernet_link_ioctl!=link_ioctl){
                TitanDiagnostics_Event(
                    link_valid?TITAN_DIAG_INFO:TITAN_DIAG_ERROR,
                    TITAN_DIAG_NETMAN,0x247,link_ioctl,
                    (uint32_t)link,(uint32_t)g_network.ethernet_stack_loaded,
                    (uint32_t)g_network.preference,g_poll_frame
                );
                g_diag_last_ethernet_link=link;
                g_diag_last_ethernet_link_ioctl=link_ioctl;
            }

            if(!link_valid){
                /* Unknown is not DOWN. Preserve that distinction all the way
                 * to Settings and do not start/reset DHCP on an invalid PHY
                 * ioctl result. */
                g_network.ethernet_link_up=0;
                g_ethernet_last_link_state=-1;
                g_network.state=TITAN_NETWORK_CONNECTING;
                TitanNetwork_Copy(
                    g_network.detail,sizeof(g_network.detail),
                    "Ethernet physical link status unavailable"
                );
            }else{
                int link_changed=g_ethernet_last_link_state!=link;
                g_network.ethernet_link_up=link;
                if(link_changed){
                    g_ethernet_last_link_state=link;
                    g_ethernet_dhcp_pending=1;
                    g_ethernet_dhcp_started=0;
                    g_ethernet_dhcp_wait_polls=0u;
                    g_ethernet_dhcp_retry_count=0u;
                    if(!link) TitanNetwork_ClearLiveAddress();
                    (void)TitanNetwork_SyncLwipLinkState();
                    TitanDiagnostics_Event(
                        TITAN_DIAG_INFO,TITAN_DIAG_NETMAN,0x250,link_ioctl,
                        (uint32_t)link,(uint32_t)g_ethernet_dhcp_pending,
                        (uint32_t)g_ethernet_dhcp_started,g_poll_frame
                    );
                }else if(link && g_ethernet_dhcp_pending && !g_ethernet_dhcp_started){
                    if(TitanNetwork_RestartDhcp()){
                        g_ethernet_dhcp_pending=0;
                        g_ethernet_dhcp_started=1;
                        g_ethernet_dhcp_wait_polls=0u;
                        TitanDiagnostics_Event(
                            TITAN_DIAG_INFO,TITAN_DIAG_PS2IP,0x251,0,
                            g_ethernet_dhcp_retry_count,1u,0u,g_poll_frame
                        );
                        TitanNetwork_Copy(
                            g_network.detail,sizeof(g_network.detail),
                            "RJ45 link active; DHCP started"
                        );
                    }else{
                        g_ethernet_dhcp_retry_count++;
                        TitanDiagnostics_Event(
                            TITAN_DIAG_WARN,TITAN_DIAG_PS2IP,0x251,-1,
                            g_ethernet_dhcp_retry_count,0u,0u,g_poll_frame
                        );
                        if(g_ethernet_dhcp_retry_count>=TITAN_NETWORK_ETH_DHCP_MAX_RETRIES){
                            g_network.state=TITAN_NETWORK_ERROR;
                            TitanNetwork_Copy(
                                g_network.detail,sizeof(g_network.detail),
                                "RJ45 link active; DHCP start failed"
                            );
                        }else{
                            g_network.state=TITAN_NETWORK_CONNECTING;
                            TitanNetwork_Copy(
                                g_network.detail,sizeof(g_network.detail),
                                "RJ45 link active; preparing DHCP"
                            );
                        }
                    }
                }
            }
        }
        else if(g_network.wifi_online && g_wifi_bridge_enabled && g_wifi_handoff_pending && link){
            TitanDiagnostics_Event(TITAN_DIAG_INFO,TITAN_DIAG_PS2IP,0x230,0,g_poll_frame,1u,0u,0u);
            TitanNetwork_RestartDhcp();
            g_wifi_handoff_pending=0;
            TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),
                "Wi-Fi handed to NetMan; ps2ip DHCP started");
        }
        if(ps2ip_getconfig("sm0",&ipinfo)==1){
            int online=(g_network.preference==TITAN_NETWORK_PREFER_ETHERNET ? link_valid : link) && TitanNetwork_HasUsableIp((unsigned int)ipinfo.ipaddr.s_addr);
            if(g_network.preference==TITAN_NETWORK_PREFER_ETHERNET){
                if(g_diag_last_ethernet_dhcp_status!=(int)ipinfo.dhcp_status){
                    TitanDiagnostics_Event(
                        TITAN_DIAG_INFO,TITAN_DIAG_PS2IP,0x248,0,
                        (uint32_t)ipinfo.dhcp_enabled,(uint32_t)ipinfo.dhcp_status,
                        (uint32_t)ipinfo.ipaddr.s_addr,(uint32_t)ipinfo.gw.s_addr
                    );
                    g_diag_last_ethernet_dhcp_status=(int)ipinfo.dhcp_status;
                }
                if(g_diag_last_ethernet_online!=online){
                    TitanDiagnostics_Event(
                        online?TITAN_DIAG_INFO:TITAN_DIAG_DEBUG,
                        TITAN_DIAG_NETWORK,0x249,online?0:1,
                        (uint32_t)link,(uint32_t)ipinfo.ipaddr.s_addr,
                        (uint32_t)ipinfo.netmask.s_addr,(uint32_t)ipinfo.gw.s_addr
                    );
                    g_diag_last_ethernet_online=online;
                }
                g_network.ethernet_online=online;
                if(online){
                    g_ethernet_dhcp_wait_polls=0u;
                    g_ethernet_dhcp_retry_count=0u;
                    g_ethernet_dhcp_pending=0;
                    g_ethernet_dhcp_started=1;
                    TitanNetwork_SetIpText((unsigned int)ipinfo.ipaddr.s_addr);
                    if(ipinfo.gw.s_addr!=0u)
                        TitanNetwork_FormatIpv4(
                            g_network.gateway,sizeof(g_network.gateway),
                            (unsigned int)ipinfo.gw.s_addr
                        );
                    else
                        g_network.gateway[0]='\0';
                    g_network.state=TITAN_NETWORK_ONLINE;
                    TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),"Ethernet RJ45 online / ps2ip");
                }else if(g_network.ethernet_stack_loaded){
                    g_network.state=TITAN_NETWORK_CONNECTING;
                    if(!g_network.ethernet_link_known){
                        TitanNetwork_ClearLiveAddress();
                        TitanNetwork_Copy(
                            g_network.detail,sizeof(g_network.detail),
                            "Ethernet physical link status unavailable"
                        );
                    }else if(!link){
                        g_ethernet_dhcp_wait_polls=0u;
                        g_ethernet_dhcp_retry_count=0u;
                        g_ethernet_dhcp_started=0;
                        g_ethernet_dhcp_pending=1;
                        TitanNetwork_ClearLiveAddress();
                        TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),"Waiting for RJ45 link");
                    }else if(g_ethernet_dhcp_pending || !g_ethernet_dhcp_started){
                        /* Link is valid but DHCP is deliberately gated behind
                         * the lwIP link-state callback.  Do not burn timeout
                         * budget before the client has actually started. */
                        g_ethernet_dhcp_wait_polls=0u;
                        TitanNetwork_Copy(
                            g_network.detail,sizeof(g_network.detail),
                            "RJ45 link active; preparing DHCP"
                        );
                    }else{
                        g_ethernet_dhcp_wait_polls++;
                        if(g_ethernet_dhcp_wait_polls>=TITAN_NETWORK_ETH_DHCP_RETRY_POLLS){
                            if(g_ethernet_dhcp_retry_count<TITAN_NETWORK_ETH_DHCP_MAX_RETRIES){
                                g_ethernet_dhcp_retry_count++;
                                g_ethernet_dhcp_wait_polls=0u;
                                if(TitanNetwork_RestartDhcp()){
                                    g_ethernet_dhcp_started=1;
                                    g_ethernet_dhcp_pending=0;
                                    TitanDiagnostics_Event(
                                        TITAN_DIAG_WARN,TITAN_DIAG_PS2IP,0x24e,0,
                                        g_ethernet_dhcp_retry_count,
                                        (uint32_t)ipinfo.dhcp_status,
                                        (uint32_t)ipinfo.ipaddr.s_addr,
                                        g_poll_frame
                                    );
                                    TitanNetwork_Copy(
                                        g_network.detail,sizeof(g_network.detail),
                                        "RJ45 link active; restarting DHCP"
                                    );
                                }else if(g_ethernet_dhcp_retry_count>=TITAN_NETWORK_ETH_DHCP_MAX_RETRIES){
                                    g_network.state=TITAN_NETWORK_ERROR;
                                    TitanDiagnostics_Event(
                                        TITAN_DIAG_ERROR,TITAN_DIAG_PS2IP,0x24f,-2,
                                        g_ethernet_dhcp_retry_count,
                                        (uint32_t)ipinfo.dhcp_status,
                                        (uint32_t)ipinfo.ipaddr.s_addr,
                                        g_poll_frame
                                    );
                                    TitanNetwork_Copy(
                                        g_network.detail,sizeof(g_network.detail),
                                        "Failed to restart Ethernet DHCP"
                                    );
                                }
                            }else{
                                g_network.state=TITAN_NETWORK_ERROR;
                                TitanDiagnostics_Event(
                                    TITAN_DIAG_ERROR,TITAN_DIAG_PS2IP,0x24f,-1,
                                    g_ethernet_dhcp_retry_count,
                                    (uint32_t)ipinfo.dhcp_status,
                                    (uint32_t)ipinfo.ipaddr.s_addr,
                                    g_poll_frame
                                );
                                TitanNetwork_Copy(
                                    g_network.detail,sizeof(g_network.detail),
                                    "RJ45 link active; DHCP did not respond"
                                );
                            }
                        }else{
                            TitanNetwork_Copy(
                                g_network.detail,sizeof(g_network.detail),
                                "RJ45 link active; DHCP in progress"
                            );
                        }
                    }
                }
            }else if(g_network.wifi_online && g_wifi_bridge_enabled){
                if(online){
                    TitanNetwork_SetIpText((unsigned int)ipinfo.ipaddr.s_addr);
                    if(ipinfo.gw.s_addr!=0u)
                        TitanNetwork_FormatIpv4(
                            g_network.gateway,sizeof(g_network.gateway),
                            (unsigned int)ipinfo.gw.s_addr
                        );
                    else
                        g_network.gateway[0]='\0';
                    g_network.state=TITAN_NETWORK_ONLINE;
                    TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),"Wi-Fi AR9271 online / NetMan + ps2ip");
                }else{
                    g_network.state=TITAN_NETWORK_CONNECTING;
                    TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),"Wi-Fi secured; ps2ip DHCP in progress");
                }
            }
        }
    }
}

static void TitanNetwork_InvalidateConnectionTest(void)
{
    g_network_test_generation++;
    if(g_network_test_generation==0u) g_network_test_generation=1u;
    memset(&g_network_test,0,sizeof(g_network_test));
    g_network_test.generation=g_network_test_generation;
    g_network_test.valid=0;
    g_network_test.preference=g_network.preference;
    g_network_test.software_result=TITAN_NETWORK_TEST_NOT_STARTED;
    g_network_test.physical_result=TITAN_NETWORK_TEST_NOT_STARTED;
    g_network_test.ip_result=TITAN_NETWORK_TEST_NOT_STARTED;
    g_network_test.internet_result=TITAN_NETWORK_TEST_NOT_STARTED;
}

void TitanNetwork_SetEnabled(int enabled)
{
    enabled=enabled?1:0;
    if(g_network.enabled==enabled)return;
    TitanNetwork_InvalidateConnectionTest();
    g_network.enabled=enabled;
    if(!enabled){
        if(g_wifi_bridge_enabled)(void)TitanNetwork_WifiBridgeRpc(0);
        g_wifi_online_since_frame=0u;g_wifi_handoff_pending=0;
        g_network.state=TITAN_NETWORK_OFFLINE;
        TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),"Internet connection disabled");
    }else{
        g_network_bootstrap_done=0;
        g_network_bootstrap_attempted=0;
        g_network_bootstrap_stage=TITAN_NETWORK_BOOTSTRAP_IDLE;
        g_network_bootstrap_error_stage=TITAN_NETWORK_BOOTSTRAP_IDLE;
        g_network_home_ready_frame=g_poll_frame;
        g_network_bootstrap_retry_count=0u;
        g_network_bootstrap_next_retry_frame=0u;
        g_network.state=TITAN_NETWORK_INITIALIZING;
        TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),"Initializing network");
    }
    (void)TitanNetwork_Save();
}
int TitanNetwork_IsEnabled(void){return g_network.enabled?1:0;}

const TitanNetworkStatus *TitanNetwork_GetStatus(void){return &g_network;}
void TitanNetwork_SetPreference(TitanNetworkPreference p)
{
    TitanNetwork_RequestPreference(p);
}

void TitanNetwork_RequestPreference(TitanNetworkPreference p)
{
    int changed;
    if(p!=TITAN_NETWORK_PREFER_WIFI && p!=TITAN_NETWORK_PREFER_ETHERNET)return;
    changed=g_network.preference!=p;
    g_network.preference=p;
    if(changed){
        TitanNetwork_InvalidateConnectionTest();
        TitanNetwork_ClearLiveAddress();
        g_network.ethernet_online=0;
        g_network.ethernet_link_known=0;
        g_network.ethernet_link_up=0;
        g_network.main_interface_ready=0;
        g_network.wifi_online=0;
        g_wifi_scan_pending=0;
        g_wifi_online_since_frame=0u;
        g_wifi_handoff_pending=0;
        g_ethernet_dhcp_wait_polls=0u;
        g_ethernet_dhcp_retry_count=0u;
        g_ethernet_dhcp_pending=p==TITAN_NETWORK_PREFER_ETHERNET?1:0;
        g_ethernet_dhcp_started=0;
        g_ethernet_last_link_state=-1;
        if(g_network.enabled){
            g_network_bootstrap_done=0;
            g_network_bootstrap_attempted=0;
            g_network_bootstrap_stage=TITAN_NETWORK_BOOTSTRAP_IDLE;
            g_network_bootstrap_error_stage=TITAN_NETWORK_BOOTSTRAP_IDLE;
            g_network_home_ready_frame=g_poll_frame;
            g_network_bootstrap_retry_count=0u;
            g_network_bootstrap_next_retry_frame=0u;
            g_network.state=TITAN_NETWORK_INITIALIZING;
            TitanNetwork_Copy(
                g_network.detail,sizeof(g_network.detail),
                p==TITAN_NETWORK_PREFER_ETHERNET
                    ? "Preparing Ethernet in background"
                    : "Preparing Wi-Fi in background"
            );
        }
    }
    (void)TitanNetwork_Save();
}

TitanNetworkPreference TitanNetwork_GetPreference(void){return g_network.preference;}
int TitanNetwork_SetWifiSsid(const char *ssid){unsigned int n;if(!ssid)return 0;n=(unsigned int)strlen(ssid);if(n<1u||n>32u)return 0;TitanNetwork_InvalidateConnectionTest();TitanNetwork_Copy(g_network.ssid,sizeof(g_network.ssid),ssid);memset(g_wifi_pmk,0,sizeof(g_wifi_pmk));g_wifi_pmk_valid=0;g_wifi_autoconnect_attempted=0;g_network.wifi_configured=0;g_network.wifi_online=0;g_network.main_interface_ready=0;g_network.state=TITAN_NETWORK_WAITING_CONFIG;return TitanNetwork_Save();}
int TitanNetwork_SetWifiPassword(const char *password){unsigned int n;if(!password||!g_network.ssid[0])return 0;n=(unsigned int)strlen(password);if(n<8u||n>63u)return 0;TitanNetwork_InvalidateConnectionTest();TitanNetwork_Pbkdf2(password,g_network.ssid,g_wifi_pmk);g_wifi_pmk_valid=1;g_wifi_autoconnect_attempted=0;return TitanNetwork_Save();}
int TitanNetwork_WifiHasKey(void){return g_wifi_pmk_valid;}
int TitanNetwork_RequestWifiScan(void)
{
    unsigned int ok=0;
    if(!g_network.enabled)return 0;
    if(!g_network_bootstrap_done || !g_netman_loaded){
        g_wifi_scan_pending=1;
        return 1;
    }
    if(!TitanNetwork_EnsureWifiDriver())return 0;
    if(!g_network.wifi_rpc_bound && !TitanNetwork_BindWifiRpc())return 0;
    if(g_network.wifi_online)return 1; /* keep cached list while associated */
    memset(g_wifi_rpc_buffer,0,sizeof(g_wifi_rpc_buffer));
    if(SifCallRpc(&g_wifi_rpc,TITAN_AR9271_SCAN_RPC_START_SCAN,0,
                  g_wifi_rpc_buffer,0,g_wifi_rpc_buffer,sizeof(ok),0,0)<0)return 0;
    memcpy(&ok,g_wifi_rpc_buffer,sizeof(ok));
    if(ok){
        g_network.wifi_scanning=1;
        g_network.state=TITAN_NETWORK_WAITING_CONFIG;
        TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),"Searching for nearby Wi-Fi networks");
    }
    return ok?1:0;
}
unsigned int TitanNetwork_GetWifiNetworkCount(void){return g_network.wifi_ap_count;}
const TitanNetworkWifiAp *TitanNetwork_GetWifiNetwork(unsigned int index)
{
    return index<g_network.wifi_ap_count?&g_wifi_aps[index]:0;
}
const char *TitanNetwork_GetWifiSecurityName(unsigned int security)
{
    switch(security){
        case TITAN_NETWORK_WIFI_OPEN:return "Open";
        case TITAN_NETWORK_WIFI_WEP:return "WEP";
        case TITAN_NETWORK_WIFI_WPA:return "WPA";
        case TITAN_NETWORK_WIFI_WPA2:return "WPA2";
        case TITAN_NETWORK_WIFI_WPA3:return "WPA3";
        default:return "Unknown";
    }
}
unsigned int TitanNetwork_GetWifiSignalPercent(const TitanNetworkWifiAp *ap)
{
    int rssi;
    if(!ap)return 0u;
    rssi=(int)ap->rssi_dbm;
    if(rssi>=-45)return 100u;
    if(rssi<=-95)return 0u;
    return (unsigned int)((rssi+95)*2);
}

int TitanNetwork_ConnectWifi(void){TitanAr9271RuntimeConfig cfg;unsigned int ok=0;if(!g_network.wifi_rpc_bound||!g_network.ssid[0]||!g_wifi_pmk_valid)return 0;memset(&cfg,0,sizeof(cfg));cfg.version=TITAN_AR9271_RUNTIME_CONFIG_VERSION;cfg.ssid_len=(unsigned int)strlen(g_network.ssid);memcpy(cfg.ssid,g_network.ssid,cfg.ssid_len);memcpy(cfg.pmk,g_wifi_pmk,32u);memset(g_wifi_rpc_buffer,0,sizeof(g_wifi_rpc_buffer));memcpy(g_wifi_rpc_buffer,&cfg,sizeof(cfg));if(SifCallRpc(&g_wifi_rpc,TITAN_AR9271_SCAN_RPC_SET_CONFIG,0,g_wifi_rpc_buffer,sizeof(cfg),g_wifi_rpc_buffer,sizeof(ok),NULL,NULL)<0)return 0;memcpy(&ok,g_wifi_rpc_buffer,sizeof(ok));if(ok){g_network.state=TITAN_NETWORK_CONNECTING;TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),"Connecting to Wi-Fi");}return ok?1:0;}
int TitanNetwork_Retry(void)
{
    unsigned int ok=0;

    if(g_network.preference==TITAN_NETWORK_PREFER_WIFI){
        if(!g_network.wifi_rpc_bound) return 0;
        memset(g_wifi_rpc_buffer,0,sizeof(g_wifi_rpc_buffer));
        if(SifCallRpc(
            &g_wifi_rpc,TITAN_AR9271_SCAN_RPC_RETRY,0,
            g_wifi_rpc_buffer,0,g_wifi_rpc_buffer,sizeof(ok),NULL,NULL
        )<0) return 0;
        memcpy(&ok,g_wifi_rpc_buffer,sizeof(ok));
        return ok?1:TitanNetwork_ConnectWifi();
    }

    {
        int smap_retry_hint=SifSearchModuleByName("SMAP_driver");
        /* Diagnostic only. A failed name lookup must never invalidate a SMAP
         * whose _start() execution already succeeded in this kernel. */
        TitanDiagnostics_Event(
            TITAN_DIAG_DEBUG,TITAN_DIAG_NETWORK,0x263,smap_retry_hint,
            (uint32_t)g_smap_loaded,1u,g_poll_frame,0u
        );
    }
    g_network.ethernet_stack_loaded=g_smap_loaded?1:0;
    g_ethernet_dhcp_wait_polls=0u;
    g_ethernet_dhcp_retry_count=0u;
    g_ethernet_dhcp_pending=1;
    g_ethernet_dhcp_started=0;
    g_ethernet_last_link_state=-1;

    if(!TitanNetwork_EnsureEthernetDriver()) return 0;
    if(!TitanNetwork_SelectMainInterface(
        TITAN_NETWORK_PREFER_ETHERNET,1
    )) return 0;

    g_network.state=TITAN_NETWORK_CONNECTING;
    TitanNetwork_Copy(
        g_network.detail,sizeof(g_network.detail),
        "Retrying Ethernet; waiting for link/DHCP"
    );
    return 1;
}

int TitanNetwork_StartConnectionTest(void)
{
#if defined(__EE__) || defined(_EE)
    ee_thread_t thread;
    int id;

    if(g_network_test_worker_active) return 1;

    memset(&g_network_test,0,sizeof(g_network_test));
    g_network_test_generation++;
    if(g_network_test_generation==0u) g_network_test_generation=1u;
    g_network_test.generation=g_network_test_generation;
    g_network_test.valid=1;
    g_network_test.running=1;
    g_network_test.complete=0;
    g_network_test.preference=g_network.preference;
    g_network_test.software_result=TITAN_NETWORK_TEST_CHECKING;
    g_network_test.physical_result=TITAN_NETWORK_TEST_CHECKING;
    g_network_test.ip_result=TITAN_NETWORK_TEST_CHECKING;
    g_network_test.internet_result=TITAN_NETWORK_TEST_NOT_STARTED;
    g_network_test.link_mode=-1;
    TitanNetwork_TestSetDetail(
        g_network_test.software_detail,sizeof(g_network_test.software_detail),
        "Checking network software path"
    );
    TitanNetwork_TestSetDetail(
        g_network_test.physical_detail,sizeof(g_network_test.physical_detail),
        g_network.preference==TITAN_NETWORK_PREFER_ETHERNET
            ? "Checking Ethernet link":"Checking Wi-Fi association"
    );
    TitanNetwork_TestSetDetail(
        g_network_test.ip_detail,sizeof(g_network_test.ip_detail),
        "Checking IPv4 configuration"
    );
    TitanNetwork_TestSetDetail(
        g_network_test.internet_detail,sizeof(g_network_test.internet_detail),
        "Waiting for IP address"
    );

    memset(&thread,0,sizeof(thread));
    thread.func=TitanNetwork_ConnectionTestWorker;
    thread.stack=g_network_test_stack;
    thread.stack_size=sizeof(g_network_test_stack);
    thread.gp_reg=&_gp;
    thread.initial_priority=TITAN_NETWORK_TEST_PRIORITY;
    id=CreateThread(&thread);
    if(id<0){
        g_network_test.running=0;
        g_network_test.complete=1;
        g_network_test.software_result=TITAN_NETWORK_TEST_FAILED;
        TitanNetwork_TestSetDetail(
            g_network_test.software_detail,sizeof(g_network_test.software_detail),
            "Could not create the network test worker"
        );
        return 0;
    }
    g_network_test_worker_active=1;
    if(StartThread(id,(void*)(uintptr_t)g_network_test.generation)<0){
        g_network_test_worker_active=0;
        DeleteThread(id);
        g_network_test.running=0;
        g_network_test.complete=1;
        g_network_test.software_result=TITAN_NETWORK_TEST_FAILED;
        TitanNetwork_TestSetDetail(
            g_network_test.software_detail,sizeof(g_network_test.software_detail),
            "Could not start the network test worker"
        );
        return 0;
    }
    return 1;
#else
    memset(&g_network_test,0,sizeof(g_network_test));
    g_network_test.complete=1;
    g_network_test.software_result=TITAN_NETWORK_TEST_FAILED;
    return 0;
#endif
}

const TitanNetworkConnectionTestStatus *TitanNetwork_GetConnectionTestStatus(void)
{
    return &g_network_test;
}

const char *TitanNetwork_GetTestResultName(TitanNetworkTestResult result)
{
    switch(result){
        case TITAN_NETWORK_TEST_CHECKING:return "Checking...";
        case TITAN_NETWORK_TEST_SUCCESS:return "Successful";
        case TITAN_NETWORK_TEST_FAILED:return "Failed";
        case TITAN_NETWORK_TEST_NOT_APPLICABLE:return "Not applicable";
        default:return "Not started";
    }
}

const char *TitanNetwork_GetStateName(void){switch(g_network.state){case TITAN_NETWORK_INITIALIZING:return "Initializing";case TITAN_NETWORK_WAITING_CONFIG:return "Configure";case TITAN_NETWORK_CONNECTING:return "Connecting";case TITAN_NETWORK_ONLINE:return "Online";case TITAN_NETWORK_ERROR:return "Error";default:return "Offline";}}
const char *TitanNetwork_GetPreferenceName(void){return g_network.preference==TITAN_NETWORK_PREFER_ETHERNET?"RJ45 / Ethernet":"Wi-Fi / AR9271";}
