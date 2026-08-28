#include "titan_network.h"
#include "titan_iop_modules.h"
#include "titan_storage_io.h"
#include "titan_storage_paths.h"
#include "titan_diagnostics.h"
#include "titan_vlombiscore.h"
#include "../../iop/titan_ar9271/ar9271_scanner_protocol.h"

#include <kernel.h>
#include <loadfile.h>
#include <netman.h>
#include <ps2ip.h>
#include <ps2ips.h>
#include <tcpip.h>
#include <sifrpc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__)
extern uint32_t TitanVlombisCore_GetBackgroundCadence(uint32_t normal_frames, uint32_t pressured_frames) __attribute__((weak));
extern void TitanVlombisCore_RecordWork(uint32_t capability_mask, TitanVlombisEvent event, uint32_t work_units, uint32_t bytes_avoided) __attribute__((weak));
#endif

#define TITAN_NETWORK_SETTINGS_DIR TITAN_STORAGE_DEVICE_ROOT "/settings"
#define TITAN_NETWORK_SETTINGS_PATH TITAN_NETWORK_SETTINGS_DIR "/network.cfg"
#define TITAN_NETWORK_SETTINGS_TEMP TITAN_NETWORK_SETTINGS_DIR "/network.tmp"
#define TITAN_NETWORK_SETTINGS_BACKUP TITAN_NETWORK_SETTINGS_DIR "/network.bak"
#define TITAN_NETWORK_SETTINGS_LOAD_RETRY_FRAMES 30u
#define TITAN_NETWORK_SETTINGS_LOAD_MAX_ATTEMPTS 4u
#define TITAN_NETWORK_RPC_POLL_FRAMES 20u
#define TITAN_NETWORK_BOOT_GRACE_FRAMES 120u
#define TITAN_NETWORK_WIFI_HANDOFF_GRACE_FRAMES 30u

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
static volatile int g_wifi_info_rpc_pending;
static volatile int g_wifi_info_rpc_done;
static void TitanNetwork_InfoRpcDone(void *arg){(void)arg;g_wifi_info_rpc_done=1;}
static unsigned int g_poll_frame;
static int g_ps2ip_initialized;
static int g_netman_loaded;
static int g_smap_loaded;
static int g_wifi_bridge_enabled;
static int g_wifi_autoconnect_attempted;
static int g_network_bootstrap_done;
static int g_network_bootstrap_attempted;
static unsigned int g_wifi_online_since_frame;
static int g_wifi_handoff_pending;
static int g_diag_last_wifi_scan_state = -999;
static int g_network_settings_loaded;
static unsigned int g_network_settings_load_attempts;

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

static int TitanNetwork_LoadModule(const char *name, unsigned char *data, unsigned int size)
{
    int result=-1,id;if(!data||size==0u)return 0;id=SifExecModuleBuffer(data,size,0,NULL,&result);(void)name;return id>=0 && result>=0;
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

static void TitanNetwork_SetIpText(unsigned int addr)
{
    unsigned char *b=(unsigned char*)&addr;
    snprintf(g_network.ip,sizeof(g_network.ip),"%u.%u.%u.%u",b[0],b[1],b[2],b[3]);
}

static int TitanNetwork_HasUsableIp(unsigned int addr)
{
    return addr!=0u && addr!=0x0100fea9u;
}

static void TitanNetwork_RestartDhcp(void)
{
    t_ip_info info;
    if(!g_ps2ip_initialized)return;
    memset(&info,0,sizeof(info));
    if(!ps2ip_getconfig("sm0",&info))return;
    info.dhcp_enabled=0;
    info.ipaddr.s_addr=0u;info.netmask.s_addr=0u;info.gw.s_addr=0u;
    (void)ps2ip_setconfig(&info);
    memset(&info,0,sizeof(info));
    strcpy(info.netif_name,"sm0");
    IP4_ADDR((struct ip4_addr*)&info.ipaddr,169,254,0,1);
    IP4_ADDR((struct ip4_addr*)&info.netmask,255,255,0,0);
    IP4_ADDR((struct ip4_addr*)&info.gw,0,0,0,0);
    info.dhcp_enabled=1;
    (void)ps2ip_setconfig(&info);
}

static void TitanNetwork_UpdateLwipMac(void)
{
    struct netif *nif;
    unsigned char mac[6];
    if(!g_ps2ip_initialized)return;
    nif=netif_find("sm0");
    if(nif==0)return;
    if(NetManIoctl(NETMAN_NETIF_IOCTL_ETH_GET_MAC,0,0,mac,sizeof(mac))<0)return;
    memcpy(nif->hwaddr,mac,6u);
    nif->hwaddr_len=6u;
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
    if(!g_netman_loaded)return 0;
    name=preference==TITAN_NETWORK_PREFER_ETHERNET?"SMAP":"WFI";
    if(NetManSetMainIF(name)<0)return 0;
    if(g_ps2ip_initialized)TitanNetwork_UpdateLwipMac();
    if(preference==TITAN_NETWORK_PREFER_ETHERNET){
        if(g_network.wifi_rpc_bound)(void)TitanNetwork_WifiBridgeRpc(0);
        g_wifi_handoff_pending=0;
        if(restart_dhcp)TitanNetwork_RestartDhcp();
    }else if(g_network.wifi_online && g_network.wifi_rpc_bound){
        if(TitanNetwork_WifiBridgeRpc(1)){
            /* The IOP performs link-up on its control worker.  DHCP must not
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
    if(g_network.wifi_irx_loaded)return 1;
    g_network.wifi_irx_loaded=TitanNetwork_LoadModule(
        "TITAN_AR9271",titan_ar9271_irx,size_titan_ar9271_irx
    );
    return g_network.wifi_irx_loaded?1:0;
}

static int TitanNetwork_EnsureEthernetDriver(void)
{
    int dev9_ok;
    if(g_smap_loaded)return 1;
    /* HDD/PFS may have brought DEV9 online first. Reuse the resident module
     * rather than executing a second copy of the same IOP owner. */
    dev9_ok=SifSearchModuleByName("dev9")>=0;
    if(!dev9_ok)
        dev9_ok=TitanNetwork_LoadModule("PS2DEV9",titan_ps2dev9_irx,size_titan_ps2dev9_irx);
    if(!dev9_ok)return 0;
    g_smap_loaded=TitanNetwork_LoadModule("SMAP",titan_smap_irx,size_titan_smap_irx);
    g_network.ethernet_stack_loaded=g_smap_loaded?1:0;
    return g_smap_loaded?1:0;
}

static void TitanNetwork_InitInterfaces(void)
{
    struct ip4_addr ip,nm,gw;
    int selected_ok;

    g_network.ethernet_link_up=0;
    g_network.ethernet_online=0;
    if(!g_netman_loaded){
        g_netman_loaded=TitanNetwork_LoadModule(
            "NETMAN",titan_netman_irx,size_titan_netman_irx
        );
        if(!g_netman_loaded){
            g_network.state=TITAN_NETWORK_ERROR;
            TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),"NETMAN IOP nao iniciou");
            return;
        }
    }

    /* Keep the physical backends lazy: initial boot loads only the selected
     * interface. Re-enabling Network Settings is idempotent and never loads
     * a second copy of NETMAN/ps2ip or the already-resident backend. */
    if(g_network.preference==TITAN_NETWORK_PREFER_ETHERNET)
        selected_ok=TitanNetwork_EnsureEthernetDriver();
    else
        selected_ok=TitanNetwork_EnsureWifiDriver();

    if(!selected_ok){
        g_network.state=TITAN_NETWORK_ERROR;
        TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),
            g_network.preference==TITAN_NETWORK_PREFER_ETHERNET
                ? "SMAP/DEV9 nao iniciou"
                : "AR9271 IOP nao iniciou");
        return;
    }

    if(!g_ps2ip_initialized){
        IP4_ADDR(&ip,169,254,0,1);
        IP4_ADDR(&nm,255,255,0,0);
        IP4_ADDR(&gw,0,0,0,0);
        if(ps2ipInit(&ip,&nm,&gw)<0){
            g_network.state=TITAN_NETWORK_ERROR;
            TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),"ps2ipInit falhou");
            return;
        }
        g_ps2ip_initialized=1;
    }

    if(!TitanNetwork_SelectMainInterface(g_network.preference,0)){
        g_network.state=TITAN_NETWORK_ERROR;
        TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),"NetManSetMainIF falhou");
        return;
    }
    TitanNetwork_RestartDhcp();
}

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

void TitanNetwork_Initialize(void)
{
    memset(&g_network,0,sizeof(g_network));
    memset(g_wifi_pmk,0,sizeof(g_wifi_pmk));
    memset(g_wifi_aps,0,sizeof(g_wifi_aps));
    g_network.preference=TITAN_NETWORK_PREFER_WIFI;
    g_network.enabled=1;
    g_network.state=TITAN_NETWORK_INITIALIZING;
    TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),
        "Rede aguardando estabilizacao da shell");
    g_ps2ip_initialized=0;
    g_netman_loaded=0;
    g_smap_loaded=0;
    g_wifi_bridge_enabled=0;
    g_wifi_autoconnect_attempted=0;
    g_network_bootstrap_done=0;
    g_network_bootstrap_attempted=0;
    g_wifi_online_since_frame=0u;
    g_wifi_handoff_pending=0;
    g_diag_last_wifi_scan_state=-999;
    g_network_settings_loaded=0;
    g_network_settings_load_attempts=0u;
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
            TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),"Conexao a Internet desativada");
        }
        return;
    }

    if(!g_network_bootstrap_done){
        if(!g_network_bootstrap_attempted &&
           g_poll_frame>=TITAN_NETWORK_BOOT_GRACE_FRAMES){
            g_network_bootstrap_attempted=1;
            TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),
                "Inicializando interface de rede selecionada");
            TitanNetwork_InitInterfaces();
            g_network_bootstrap_done=1;
            if(g_network.state!=TITAN_NETWORK_ERROR){
                g_network.state=(g_network.ssid[0]&&g_wifi_pmk_valid)
                    ? TITAN_NETWORK_OFFLINE:TITAN_NETWORK_WAITING_CONFIG;
                TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),
                    g_network.state==TITAN_NETWORK_WAITING_CONFIG
                        ? "Configure Wi-Fi em Settings > Network"
                        : "Rede pronta para conectar");
            }
        }
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
                                ? "Wi-Fi seguro; transferindo link para ps2ip"
                                : "Wi-Fi validado; preparando handoff NetMan");
                    }
                }else if(info.state==TITAN_SCAN_ERROR){
                    g_wifi_online_since_frame=0u;g_wifi_handoff_pending=0;
                    if(g_wifi_bridge_enabled)(void)TitanNetwork_WifiBridgeRpc(0);
                    g_network.state=TITAN_NETWORK_ERROR;
                    TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),info.detail);
                }else if(info.state==TITAN_SCAN_SCANNING && !info.wifi_configured){
                    g_network.state=TITAN_NETWORK_WAITING_CONFIG;
                    TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),"Procurando redes Wi-Fi proximas");
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
        if (!g_network.wifi_rpc_bound)
            TitanNetwork_RecordBackgroundTick();
        int link=NetManIoctl(NETMAN_NETIF_IOCTL_GET_LINK_STATUS,0,0,0,0)==NETMAN_NETIF_ETH_LINK_STATE_UP;
        memset(&ipinfo,0,sizeof(ipinfo));
        if(g_network.preference==TITAN_NETWORK_PREFER_ETHERNET)
            g_network.ethernet_link_up=link;
        else if(g_network.wifi_online && g_wifi_bridge_enabled && g_wifi_handoff_pending && link){
            TitanDiagnostics_Event(TITAN_DIAG_INFO,TITAN_DIAG_PS2IP,0x230,0,g_poll_frame,1u,0u,0u);
            TitanNetwork_RestartDhcp();
            g_wifi_handoff_pending=0;
            TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),
                "Wi-Fi entregue ao NetMan; DHCP ps2ip iniciado");
        }
        if(ps2ip_getconfig("sm0",&ipinfo)){
            int online=link && TitanNetwork_HasUsableIp((unsigned int)ipinfo.ipaddr.s_addr);
            if(g_network.preference==TITAN_NETWORK_PREFER_ETHERNET){
                g_network.ethernet_online=online;
                if(online){
                    TitanNetwork_SetIpText((unsigned int)ipinfo.ipaddr.s_addr);
                    g_network.state=TITAN_NETWORK_ONLINE;
                    TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),"Ethernet RJ45 online / ps2ip");
                }else if(g_network.ethernet_stack_loaded){
                    g_network.state=TITAN_NETWORK_CONNECTING;
                    TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),link?"RJ45 link ativo; DHCP em andamento":"Aguardando link RJ45");
                }
            }else if(g_network.wifi_online && g_wifi_bridge_enabled){
                if(online){
                    TitanNetwork_SetIpText((unsigned int)ipinfo.ipaddr.s_addr);
                    g_network.state=TITAN_NETWORK_ONLINE;
                    TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),"Wi-Fi AR9271 online / NetMan + ps2ip");
                }else{
                    g_network.state=TITAN_NETWORK_CONNECTING;
                    TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),"Wi-Fi seguro; DHCP ps2ip em andamento");
                }
            }
        }
    }
}

void TitanNetwork_SetEnabled(int enabled)
{
    enabled=enabled?1:0;
    if(g_network.enabled==enabled)return;
    g_network.enabled=enabled;
    if(!enabled){
        if(g_wifi_bridge_enabled)(void)TitanNetwork_WifiBridgeRpc(0);
        g_wifi_online_since_frame=0u;g_wifi_handoff_pending=0;
        g_network.state=TITAN_NETWORK_OFFLINE;
        TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),"Conexao a Internet desativada");
    }else{
        g_network_bootstrap_done=0;
        g_network_bootstrap_attempted=0;
        g_poll_frame=TITAN_NETWORK_BOOT_GRACE_FRAMES;
        g_network.state=TITAN_NETWORK_INITIALIZING;
        TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),"Inicializando rede");
    }
    (void)TitanNetwork_Save();
}
int TitanNetwork_IsEnabled(void){return g_network.enabled?1:0;}

const TitanNetworkStatus *TitanNetwork_GetStatus(void){return &g_network;}
void TitanNetwork_SetPreference(TitanNetworkPreference p)
{
    int ok=1;
    if(p!=TITAN_NETWORK_PREFER_WIFI && p!=TITAN_NETWORK_PREFER_ETHERNET)return;

    g_network.preference=p;
    if(g_network_bootstrap_done && g_netman_loaded){
        if(p==TITAN_NETWORK_PREFER_ETHERNET)
            ok=TitanNetwork_EnsureEthernetDriver();
        else
            ok=TitanNetwork_EnsureWifiDriver();

        if(!ok){
            g_network.state=TITAN_NETWORK_ERROR;
            TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),
                p==TITAN_NETWORK_PREFER_ETHERNET
                    ? "Nao foi possivel carregar DEV9/SMAP"
                    : "Nao foi possivel carregar AR9271");
            return;
        }

        (void)TitanNetwork_SelectMainInterface(p,1);
        if(p==TITAN_NETWORK_PREFER_WIFI && g_network.wifi_rpc_bound &&
           g_network.ssid[0] && g_wifi_pmk_valid && !g_network.wifi_online){
            g_wifi_autoconnect_attempted=1;
            (void)TitanNetwork_ConnectWifi();
        }
    }
    (void)TitanNetwork_Save();
}
TitanNetworkPreference TitanNetwork_GetPreference(void){return g_network.preference;}
int TitanNetwork_SetWifiSsid(const char *ssid){unsigned int n;if(!ssid)return 0;n=(unsigned int)strlen(ssid);if(n<1u||n>32u)return 0;TitanNetwork_Copy(g_network.ssid,sizeof(g_network.ssid),ssid);memset(g_wifi_pmk,0,sizeof(g_wifi_pmk));g_wifi_pmk_valid=0;g_wifi_autoconnect_attempted=0;g_network.wifi_configured=0;g_network.wifi_online=0;g_network.state=TITAN_NETWORK_WAITING_CONFIG;return TitanNetwork_Save();}
int TitanNetwork_SetWifiPassword(const char *password){unsigned int n;if(!password||!g_network.ssid[0])return 0;n=(unsigned int)strlen(password);if(n<8u||n>63u)return 0;TitanNetwork_Pbkdf2(password,g_network.ssid,g_wifi_pmk);g_wifi_pmk_valid=1;g_wifi_autoconnect_attempted=0;return TitanNetwork_Save();}
int TitanNetwork_WifiHasKey(void){return g_wifi_pmk_valid;}
int TitanNetwork_RequestWifiScan(void)
{
    unsigned int ok=0;
    if(!g_network.enabled)return 0;
    if(!g_network_bootstrap_done || !g_netman_loaded)return 0;
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
        TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),"Procurando redes Wi-Fi proximas");
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
        case TITAN_NETWORK_WIFI_OPEN:return "Aberta";
        case TITAN_NETWORK_WIFI_WEP:return "WEP";
        case TITAN_NETWORK_WIFI_WPA:return "WPA";
        case TITAN_NETWORK_WIFI_WPA2:return "WPA2";
        case TITAN_NETWORK_WIFI_WPA3:return "WPA3";
        default:return "Desconhecida";
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

int TitanNetwork_ConnectWifi(void){TitanAr9271RuntimeConfig cfg;unsigned int ok=0;if(!g_network.wifi_rpc_bound||!g_network.ssid[0]||!g_wifi_pmk_valid)return 0;memset(&cfg,0,sizeof(cfg));cfg.version=TITAN_AR9271_RUNTIME_CONFIG_VERSION;cfg.ssid_len=(unsigned int)strlen(g_network.ssid);memcpy(cfg.ssid,g_network.ssid,cfg.ssid_len);memcpy(cfg.pmk,g_wifi_pmk,32u);memset(g_wifi_rpc_buffer,0,sizeof(g_wifi_rpc_buffer));memcpy(g_wifi_rpc_buffer,&cfg,sizeof(cfg));if(SifCallRpc(&g_wifi_rpc,TITAN_AR9271_SCAN_RPC_SET_CONFIG,0,g_wifi_rpc_buffer,sizeof(cfg),g_wifi_rpc_buffer,sizeof(ok),NULL,NULL)<0)return 0;memcpy(&ok,g_wifi_rpc_buffer,sizeof(ok));if(ok){g_network.state=TITAN_NETWORK_CONNECTING;TitanNetwork_Copy(g_network.detail,sizeof(g_network.detail),"Conectando ao Wi-Fi");}return ok?1:0;}
int TitanNetwork_Retry(void){unsigned int ok=0;if(g_network.preference==TITAN_NETWORK_PREFER_WIFI){if(!g_network.wifi_rpc_bound)return 0;memset(g_wifi_rpc_buffer,0,sizeof(g_wifi_rpc_buffer));if(SifCallRpc(&g_wifi_rpc,TITAN_AR9271_SCAN_RPC_RETRY,0,g_wifi_rpc_buffer,0,g_wifi_rpc_buffer,sizeof(ok),NULL,NULL)<0)return 0;memcpy(&ok,g_wifi_rpc_buffer,sizeof(ok));return ok?1:TitanNetwork_ConnectWifi();}
    if(g_network.ethernet_stack_loaded){(void)TitanNetwork_SelectMainInterface(TITAN_NETWORK_PREFER_ETHERNET,1);return 1;}
    return 0;
}
const char *TitanNetwork_GetStateName(void){switch(g_network.state){case TITAN_NETWORK_INITIALIZING:return "Inicializando";case TITAN_NETWORK_WAITING_CONFIG:return "Configurar";case TITAN_NETWORK_CONNECTING:return "Conectando";case TITAN_NETWORK_ONLINE:return "Online";case TITAN_NETWORK_ERROR:return "Erro";default:return "Offline";}}
const char *TitanNetwork_GetPreferenceName(void){return g_network.preference==TITAN_NETWORK_PREFER_ETHERNET?"RJ45 / Ethernet":"Wi-Fi / AR9271";}
