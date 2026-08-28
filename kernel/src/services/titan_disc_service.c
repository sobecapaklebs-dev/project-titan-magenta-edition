#include "titan_disc_service.h"
#include "titan_time.h"
#if !defined(TITAN_DISC_HOST_TEST)
#include "titan_notifications.h"
#include "titan_diagnostics.h"
#else
#define TITAN_NOTIFICATION_INFO 1
#define TITAN_NOTIFICATION_WARNING 2
#define TITAN_NOTIFICATION_ERROR 3
#define TITAN_DIAG_INFO 1
#define TITAN_DIAG_KERNEL 2
extern int TitanNotifications_Push(int kind,const char *title,const char *message);
extern void TitanDiagnostics_Event(int level,int subsystem,uint16_t event,int32_t result,uint32_t a,uint32_t b,uint32_t c,uint32_t d);
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#if defined(__EE__) || defined(_EE)
#include <libcdvd.h>
#endif

#define TITAN_DISC_POLL_INTERVAL_MS 750u

typedef enum TitanDiscProbeResultKind {
    TITAN_DISC_PROBE_NONE = 0,
    TITAN_DISC_PROBE_DETECTING,
    TITAN_DISC_PROBE_READY,
    TITAN_DISC_PROBE_UNSUPPORTED,
    TITAN_DISC_PROBE_ERROR
} TitanDiscProbeResultKind;

typedef struct TitanDiscWorkerResult {
    volatile unsigned int ready;
    TitanDiscProbeResultKind kind;
    TitanDiscInfo info;
} TitanDiscWorkerResult;

typedef struct TitanDiscServiceState {
    TitanDiscInfo info;
    TitanDiscPlatformOps ops;
    void *ops_context;
    uint64_t next_poll_ms;
    volatile unsigned int work_pending;
    volatile unsigned int worker_active;
    TitanDiscWorkerResult worker;
    unsigned int initialized;
} TitanDiscServiceState;

static TitanDiscServiceState g_disc;

static void CopyText(char *dst, size_t cap, const char *src, size_t n)
{
    if (!dst || cap == 0u) return;
    if (!src) { dst[0]='\0'; return; }
    if (n >= cap) n=cap-1u;
    memcpy(dst,src,n); dst[n]='\0';
}

static void Trim(char *s)
{
    char *p,*e;
    if (!s) return;
    p=s; while (*p && isspace((unsigned char)*p)) p++;
    if (p!=s) memmove(s,p,strlen(p)+1u);
    e=s+strlen(s); while (e>s && isspace((unsigned char)e[-1])) *--e='\0';
}

int TitanDisc_NormalizeBootPath(const char *input, char *output, size_t capacity)
{
    char temp[TITAN_DISC_BOOT_PATH_CAPACITY];
    const char *p;
    size_t n=0u;
    if (!input || !output || capacity<16u) return 0;
    while (*input && isspace((unsigned char)*input)) input++;
    if (strncasecmp(input,"cdrom0:",7u)==0) input+=7u;
    while (*input=='\\') input++;
    if (*input=='/' || strstr(input,"..") || strchr(input,':')) return 0;
    for (p=input; *p && !isspace((unsigned char)*p); ++p)
    {
        unsigned char c=(unsigned char)*p;
        if (c=='/') return 0;
        if (!(isalnum(c)||c=='_'||c=='.'||c=='-'||c==';'||c=='\\')) return 0;
        if (n+1u>=sizeof(temp)) return 0;
        temp[n++]=(char)c;
    }
    temp[n]='\0';
    while (n>0u && temp[n-1u]=='\\') temp[--n]='\0';
    if (n==0u) return 0;
    if (!strchr(temp,';')) {
        if (n+2u>=sizeof(temp)) return 0;
        temp[n++]=';'; temp[n++]='1'; temp[n]='\0';
    }
    if (snprintf(output,capacity,"cdrom0:\\%s",temp)<0 || strlen(output)>=capacity) return 0;
    return 1;
}

int TitanDisc_ExtractTitleId(const char *boot_path, char *output, size_t capacity)
{
    const char *p,*end,*semi;
    size_t n;
    if (!boot_path || !output || capacity==0u) return 0;
    p=strrchr(boot_path,'\\'); p=p?p+1:boot_path;
    semi=strchr(p,';'); end=semi?semi:p+strlen(p);
    n=(size_t)(end-p);
    if (n<8u || n>=capacity) return 0;
    CopyText(output,capacity,p,n);
    for (n=0u; output[n]; ++n) output[n]=(char)toupper((unsigned char)output[n]);
    /* PS2 IDs normally contain a four-letter prefix, underscore and dotted number. */
    if (!isalpha((unsigned char)output[0]) || !isalpha((unsigned char)output[1]) ||
        !isalpha((unsigned char)output[2]) || !isalpha((unsigned char)output[3]) ||
        output[4]!='_' || !strchr(output,'.')) { output[0]='\0'; return 0; }
    return 1;
}

TitanDiscRegion TitanDisc_InferRegion(const char *id)
{
    if (!id || strlen(id)<4u) return TITAN_DISC_REGION_UNKNOWN;
    if (!strncmp(id,"SLUS",4)||!strncmp(id,"SCUS",4)) return TITAN_DISC_REGION_NTSC_U;
    if (!strncmp(id,"SLES",4)||!strncmp(id,"SCES",4)||!strncmp(id,"SCED",4)) return TITAN_DISC_REGION_PAL;
    if (!strncmp(id,"SLPM",4)||!strncmp(id,"SLPS",4)||!strncmp(id,"SCPS",4)||!strncmp(id,"SCAJ",4)) return TITAN_DISC_REGION_NTSC_J;
    return TITAN_DISC_REGION_UNKNOWN;
}

static int KeyEq(const char *a,const char *b){return strcasecmp(a,b)==0;}

int TitanDisc_ParseSystemCnf(const char *data, size_t length, TitanDiscInfo *info)
{
    size_t off=0u;
    char boot_raw[TITAN_DISC_BOOT_PATH_CAPACITY]={0};
    if (!data || !info || length==0u || length>TITAN_DISC_SYSTEM_CNF_CAPACITY) return 0;
    while (off<length)
    {
        size_t start=off,end; char line[256],*eq,*key,*value;
        while (off<length && data[off]!='\n' && data[off]!='\r') off++;
        end=off; while (off<length && (data[off]=='\n'||data[off]=='\r')) off++;
        if (end<=start) continue;
        if (end-start>=sizeof(line)) continue;
        CopyText(line,sizeof(line),data+start,end-start); Trim(line);
        if (!line[0]||line[0]=='#'||line[0]==';') continue;
        eq=strchr(line,'='); if (!eq) continue; *eq='\0'; key=line; value=eq+1; Trim(key); Trim(value);
        if ((KeyEq(key,"BOOT2")||KeyEq(key,"BOOT")) && !boot_raw[0]) CopyText(boot_raw,sizeof(boot_raw),value,strlen(value));
        else if (KeyEq(key,"VER")) CopyText(info->version,sizeof(info->version),value,strlen(value));
        else if (KeyEq(key,"VMODE")) CopyText(info->vmode,sizeof(info->vmode),value,strlen(value));
    }
    if (!boot_raw[0]) { info->last_error=TITAN_DISC_ERROR_SYSTEM_CNF_INVALID; return 0; }
    if (!TitanDisc_NormalizeBootPath(boot_raw,info->boot_path,sizeof(info->boot_path))) { info->last_error=TITAN_DISC_ERROR_BOOT_PATH_INVALID; return 0; }
    if (!TitanDisc_ExtractTitleId(info->boot_path,info->title_id,sizeof(info->title_id))) { info->last_error=TITAN_DISC_ERROR_BOOT_PATH_INVALID; return 0; }
    info->region=TitanDisc_InferRegion(info->title_id);
    info->system_cnf_valid=1;
    snprintf(info->title,sizeof(info->title),"PlayStation 2 Disc — %s",info->title_id);
    info->metadata_state=TITAN_DISC_METADATA_FALLBACK;
    return 1;
}

int TitanDisc_ParseMetadata(const char *data, size_t length, TitanDiscInfo *info)
{
    size_t off=0u; int found=0;
    if (!data || !info || length==0u || length>TITAN_DISC_METADATA_CAPACITY) return 0;
    while (off<length) {
        size_t start=off,end; char line[256],*eq,*key,*value;
        while (off<length && data[off]!='\n' && data[off]!='\r') off++;
        end=off; while (off<length && (data[off]=='\n'||data[off]=='\r')) off++;
        if (end<=start || end-start>=sizeof(line)) continue;
        CopyText(line,sizeof(line),data+start,end-start); Trim(line);
        if (!line[0] || line[0]=='#' || line[0]==';') continue;
        eq=strchr(line,'='); if (!eq) continue; *eq='\0'; key=line; value=eq+1; Trim(key); Trim(value);
        if (!value[0] || strstr(value,"..")) continue;
        if (KeyEq(key,"title")) { CopyText(info->title,sizeof(info->title),value,strlen(value)); found=1; }
        else if (KeyEq(key,"icon")) { CopyText(info->icon_path,sizeof(info->icon_path),value,strlen(value)); found=1; }
        else if (KeyEq(key,"cover")) { CopyText(info->cover_path,sizeof(info->cover_path),value,strlen(value)); found=1; }
        else if (KeyEq(key,"background")) { CopyText(info->background_path,sizeof(info->background_path),value,strlen(value)); found=1; }
        else if (KeyEq(key,"igr_profile")) {
            unsigned long v=strtoul(value,0,10);
            if (v>=100u && v<=102u) { info->compatibility_profile_hint=(uint32_t)v; found=1; }
        }
    }
    if (found) info->metadata_state=TITAN_DISC_METADATA_LOCAL_CACHE;
    return found;
}

#if defined(__EE__) || defined(_EE)
static int Ps2Probe(void *ctx,TitanDiscMediaType *media)
{
    int t; (void)ctx;
    if (!media) return -1;
    t=sceCdGetDiskType();
    if (t==SCECdNODISC) { *media=TITAN_DISC_MEDIA_NONE; return 0; }
    if (t==SCECdDETCT) return 2;
    if (sceCdDiskReady(1)==0) return 2;
    if (t==SCECdPS2DVD) { *media=TITAN_DISC_MEDIA_PS2_DVD; return 1; }
    if (t==SCECdPS2CD || t==SCECdPS2CDDA) { *media=TITAN_DISC_MEDIA_PS2_CD; return 1; }
    *media=TITAN_DISC_MEDIA_UNSUPPORTED; return 1;
}
static int Ps2ReadCnf(void *ctx,char *buffer,size_t capacity)
{
    FILE *f; size_t n; (void)ctx;
    if (!buffer||capacity<2u) return -1;
    f=fopen("cdrom0:\\SYSTEM.CNF;1","rb");
    if (!f) return 0;
    n=fread(buffer,1,capacity-1u,f); fclose(f); buffer[n]='\0';
    return n>0u?(int)n:-1;
}
static int Ps2ReadMetadata(void *ctx,const char *title_id,char *buffer,size_t capacity)
{
    FILE *f; size_t n; char path[256]; (void)ctx;
    if (!title_id || !title_id[0] || !buffer || capacity<2u) return -1;
    snprintf(path,sizeof(path),"mass:/TITAN/cache/disc/%s.cfg",title_id);
    f=fopen(path,"rb"); if (!f) return 0;
    n=fread(buffer,1,capacity-1u,f); fclose(f); buffer[n]='\0';
    return n>0u?(int)n:-1;
}
#endif

void TitanDiscService_SetPlatformOps(const TitanDiscPlatformOps *ops, void *context)
{
    if (ops) g_disc.ops=*ops; else memset(&g_disc.ops,0,sizeof(g_disc.ops));
    g_disc.ops_context=context;
}

void TitanDiscService_Initialize(void)
{
    TitanDiscPlatformOps saved=g_disc.ops; void *ctx=g_disc.ops_context;
    memset(&g_disc,0,sizeof(g_disc)); g_disc.info.state=TITAN_DISC_NO_MEDIA; g_disc.initialized=1u;
    g_disc.ops=saved; g_disc.ops_context=ctx;
#if defined(__EE__) || defined(_EE)
    if (!g_disc.ops.probe_media) { (void)sceCdInit(SCECdINoD); g_disc.ops.probe_media=Ps2Probe; g_disc.ops.read_system_cnf=Ps2ReadCnf; g_disc.ops.read_metadata=Ps2ReadMetadata; }
#endif
}

void TitanDiscService_Shutdown(void){g_disc.work_pending=0u; g_disc.worker_active=0u; g_disc.initialized=0u;}

static void NotifyTransition(TitanDiscState old,TitanDiscState now)
{
    if (old==now) return;
    if (now==TITAN_DISC_READY) TitanNotifications_Push(TITAN_NOTIFICATION_INFO,"Disc","PlayStation 2 disc ready");
    else if (now==TITAN_DISC_UNSUPPORTED) TitanNotifications_Push(TITAN_NOTIFICATION_WARNING,"Disc","Unsupported disc");
    else if (now==TITAN_DISC_ERROR) TitanNotifications_Push(TITAN_NOTIFICATION_ERROR,"Disc","Disc read/identification error");
    TitanDiagnostics_Event(TITAN_DIAG_INFO,TITAN_DIAG_KERNEL,0xD100u,(int32_t)now,(uint32_t)old,g_disc.info.media_type,g_disc.info.last_error,g_disc.info.media_generation);
}

void TitanDiscService_UpdateMainThread(uint64_t now_ms)
{
    TitanDiscState old;
    if (!g_disc.initialized) return;
    if (g_disc.worker.ready)
    {
        TitanDiscWorkerResult r=g_disc.worker;
        unsigned int old_generation=g_disc.info.media_generation;
        unsigned int old_probe_count=g_disc.info.probe_count;
        g_disc.worker.ready=0u;
        old=g_disc.info.state;
        if (r.kind==TITAN_DISC_PROBE_NONE)
        {
            int had_media=g_disc.info.media_present || old==TITAN_DISC_READY || old==TITAN_DISC_UNSUPPORTED;
            memset(&g_disc.info,0,sizeof(g_disc.info));
            g_disc.info.state=had_media?TITAN_DISC_EJECTED:TITAN_DISC_NO_MEDIA;
            g_disc.info.media_generation=old_generation+(had_media?1u:0u);
            g_disc.info.probe_count=old_probe_count+1u;
            if (had_media) NotifyTransition(old,TITAN_DISC_EJECTED);
        }
        else if (r.kind==TITAN_DISC_PROBE_DETECTING)
        {
            g_disc.info.probe_count=old_probe_count+1u;
            if (old==TITAN_DISC_NO_MEDIA || old==TITAN_DISC_EJECTED)
            {
                g_disc.info.state=TITAN_DISC_PROBING;
                NotifyTransition(old,g_disc.info.state);
            }
        }
        else
        {
            int same_media = g_disc.info.media_present && r.info.media_present &&
                g_disc.info.media_type==r.info.media_type &&
                strcmp(g_disc.info.title_id,r.info.title_id)==0 &&
                strcmp(g_disc.info.boot_path,r.info.boot_path)==0 &&
                old==r.info.state;
            if (same_media)
            {
                g_disc.info.probe_count=old_probe_count+1u;
                g_disc.info.read_time_ms=r.info.read_time_ms;
            }
            else
            {
                unsigned int gen=old_generation+1u;
                if (r.kind==TITAN_DISC_PROBE_READY)
                {
                    /* Explicit identification state sequence. These are event
                     * transitions, not frame delays; expensive reads already
                     * completed on the worker. */
                    NotifyTransition(old,TITAN_DISC_INSERT_DETECTED);
                    NotifyTransition(TITAN_DISC_INSERT_DETECTED,TITAN_DISC_PROBING);
                    NotifyTransition(TITAN_DISC_PROBING,TITAN_DISC_MEDIA_PRESENT);
                    NotifyTransition(TITAN_DISC_MEDIA_PRESENT,TITAN_DISC_IDENTIFYING);
                }
                g_disc.info=r.info;
                g_disc.info.media_generation=gen;
                g_disc.info.probe_count=old_probe_count+1u;
                NotifyTransition(r.kind==TITAN_DISC_PROBE_READY?TITAN_DISC_IDENTIFYING:old,g_disc.info.state);
            }
        }
    }
    if (!g_disc.work_pending && !g_disc.worker_active && now_ms>=g_disc.next_poll_ms &&
        g_disc.info.state!=TITAN_DISC_LAUNCHING && g_disc.info.state!=TITAN_DISC_RUNNING)
    {
        g_disc.work_pending=1u;
        g_disc.next_poll_ms=now_ms+TITAN_DISC_POLL_INTERVAL_MS;
    }
}

int TitanDiscService_HasStorageWork(void){return g_disc.work_pending && !g_disc.worker_active;}
int TitanDiscService_StorageWorkerPump(void)
{
    TitanDiscMediaType media=TITAN_DISC_MEDIA_NONE; TitanDiscWorkerResult r; int p,n; uint64_t start;
    char cnf[TITAN_DISC_SYSTEM_CNF_CAPACITY];
    if (!TitanDiscService_HasStorageWork()) return 0;
    g_disc.work_pending=0u; g_disc.worker_active=1u; memset(&r,0,sizeof(r)); start=TitanTime_NowMilliseconds();
    if (!g_disc.ops.probe_media) { r.kind=TITAN_DISC_PROBE_NONE; goto done; }
    p=g_disc.ops.probe_media(g_disc.ops_context,&media); r.info.probe_count=g_disc.info.probe_count+1u;
    if (p==0) { r.kind=TITAN_DISC_PROBE_NONE; goto done; }
    if (p==2) { r.kind=TITAN_DISC_PROBE_DETECTING; goto done; }
    if (p<0) { r.kind=TITAN_DISC_PROBE_ERROR; r.info.state=TITAN_DISC_ERROR; r.info.last_error=TITAN_DISC_ERROR_DISC_READ; goto done; }
    r.info.media_present=1; r.info.media_type=media; r.info.state=TITAN_DISC_MEDIA_PRESENT;
    if (media!=TITAN_DISC_MEDIA_PS2_CD && media!=TITAN_DISC_MEDIA_PS2_DVD) { r.kind=TITAN_DISC_PROBE_UNSUPPORTED; r.info.state=TITAN_DISC_UNSUPPORTED; r.info.last_error=TITAN_DISC_ERROR_UNSUPPORTED_MEDIA; goto done; }
    r.info.state=TITAN_DISC_IDENTIFYING;
    if (!g_disc.ops.read_system_cnf) { r.kind=TITAN_DISC_PROBE_ERROR; r.info.state=TITAN_DISC_ERROR; r.info.last_error=TITAN_DISC_ERROR_SYSTEM_CNF_NOT_FOUND; goto done; }
    n=g_disc.ops.read_system_cnf(g_disc.ops_context,cnf,sizeof(cnf));
    if (n==0) { r.kind=TITAN_DISC_PROBE_ERROR; r.info.state=TITAN_DISC_ERROR; r.info.last_error=TITAN_DISC_ERROR_SYSTEM_CNF_NOT_FOUND; goto done; }
    if (n<0 || !TitanDisc_ParseSystemCnf(cnf,(size_t)n,&r.info)) { r.kind=TITAN_DISC_PROBE_ERROR; r.info.state=TITAN_DISC_ERROR; if (r.info.last_error==TITAN_DISC_ERROR_NONE) r.info.last_error=TITAN_DISC_ERROR_SYSTEM_CNF_INVALID; goto done; }
    if (g_disc.ops.read_metadata) {
        char metadata[TITAN_DISC_METADATA_CAPACITY];
        int mn=g_disc.ops.read_metadata(g_disc.ops_context,r.info.title_id,metadata,sizeof(metadata));
        if (mn>0) (void)TitanDisc_ParseMetadata(metadata,(size_t)mn,&r.info);
    }
    r.kind=TITAN_DISC_PROBE_READY; r.info.state=TITAN_DISC_READY; r.info.last_error=TITAN_DISC_ERROR_NONE;
done:
    r.info.read_time_ms=(uint32_t)(TitanTime_NowMilliseconds()-start); g_disc.worker=r; g_disc.worker.ready=1u; g_disc.worker_active=0u; return 1;
}

const TitanDiscInfo *TitanDiscService_GetInfo(void){return &g_disc.info;}
int TitanDiscService_IsReady(void){return g_disc.info.state==TITAN_DISC_READY && g_disc.info.media_present;}
void TitanDiscService_MarkLaunching(void){if (TitanDiscService_IsReady()) g_disc.info.state=TITAN_DISC_LAUNCHING;}
void TitanDiscService_MarkRunning(void){if (g_disc.info.state==TITAN_DISC_LAUNCHING) g_disc.info.state=TITAN_DISC_RUNNING;}
void TitanDiscService_MarkLaunchFailed(void){g_disc.info.state=TITAN_DISC_ERROR; g_disc.info.last_error=TITAN_DISC_ERROR_LAUNCH_FAILED;}
const char *TitanDiscState_Name(TitanDiscState s){static const char*n[]={"NO_MEDIA","INSERT_DETECTED","PROBING","MEDIA_PRESENT","IDENTIFYING","READY","UNSUPPORTED","ERROR","LAUNCHING","RUNNING","EJECTED"};return (unsigned)s<sizeof(n)/sizeof(n[0])?n[s]:"UNKNOWN";}
const char *TitanDiscError_Name(TitanDiscError e){static const char*n[]={"NONE","NO_MEDIA","UNSUPPORTED_MEDIA","SYSTEM_CNF_NOT_FOUND","SYSTEM_CNF_INVALID","BOOT_PATH_INVALID","DISC_READ_ERROR","LAUNCH_FAILED","SESSION_FAILED","IGR_UNAVAILABLE"};return (unsigned)e<sizeof(n)/sizeof(n[0])?n[e]:"UNKNOWN";}
