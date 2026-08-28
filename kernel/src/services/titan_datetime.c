#include "titan_datetime.h"
#include "titan_diagnostics.h"
#include "titan_network.h"
#include "titan_storage_io.h"
#include "titan_storage_paths.h"
#include "titan_time.h"

#include <stdio.h>
#include <string.h>

#if defined(__EE__) || defined(_EE)
#include <kernel.h>
#include <libcdvd.h>
#include <netdb.h>
#include <osd_config.h>
#include <ps2ip.h>
#include <sys/select.h>
#include <unistd.h>
#endif

#define TITAN_DATETIME_SETTINGS_DIR TITAN_STORAGE_DEVICE_ROOT "/system"
#define TITAN_DATETIME_SETTINGS_PATH TITAN_DATETIME_SETTINGS_DIR "/time.cfg"
#define TITAN_DATETIME_SETTINGS_TEMP TITAN_DATETIME_SETTINGS_DIR "/time.cfg.tmp"
#define TITAN_DATETIME_SETTINGS_BACKUP TITAN_DATETIME_SETTINGS_DIR "/time.cfg.bak"
#define TITAN_DATETIME_DEFAULT_SERVER "pool.ntp.org"
#define TITAN_DATETIME_NTP_UNIX_DELTA 2208988800ull
#define TITAN_DATETIME_SYNC_RETRY_MS (5ull * 60ull * 1000ull)
#define TITAN_DATETIME_SYNC_REFRESH_MS (6ull * 60ull * 60ull * 1000ull)
#define TITAN_DATETIME_WORKER_STACK_BYTES (16u * 1024u)
#define TITAN_DATETIME_WORKER_PRIORITY 67
#define TITAN_DATETIME_SNTP_TIMEOUT_MS 2500u

typedef enum TitanDateTimeWorkerJob {
    TITAN_DATETIME_JOB_NONE = 0,
    TITAN_DATETIME_JOB_SNTP,
    TITAN_DATETIME_JOB_SAVE
} TitanDateTimeWorkerJob;

typedef struct TitanDateTimeState {
    TitanDateTimeSnapshot snapshot;
    uint64_t anchor_utc;
    uint64_t anchor_monotonic_ms;
    uint64_t next_auto_sync_ms;
    int settings_dirty;
    int worker_job;
    int worker_active;
    int worker_done;
    int worker_result;
    uint64_t worker_epoch;
    uint32_t worker_nonce;
    char timezone_text[16];
} TitanDateTimeState;

static TitanDateTimeState g_datetime;

#if defined(__EE__) || defined(_EE)
extern void *_gp;
static unsigned char g_datetime_worker_stack[TITAN_DATETIME_WORKER_STACK_BYTES] __attribute__((aligned(16)));
static int g_datetime_worker_thread = -1;
#endif

static int TitanDateTime_IsLeap(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int TitanDateTime_DaysInMonth(int year, int month)
{
    static const unsigned char days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1 || month > 12) return 0;
    if (month == 2 && TitanDateTime_IsLeap(year)) return 29;
    return days[month - 1];
}

int TitanDateTime_ValidateCivil(int year, int month, int day, int hour, int minute, int second)
{
    int dim;
    if (year < 2000 || year > 2099 || month < 1 || month > 12 || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59)
        return 0;
    dim = TitanDateTime_DaysInMonth(year, month);
    return day >= 1 && day <= dim;
}

static int64_t TitanDateTime_DaysFromCivil(int year, unsigned int month, unsigned int day)
{
    int era;
    unsigned int yoe, doy, doe;
    unsigned int mp;
    year -= month <= 2u;
    era = (year >= 0 ? year : year - 399) / 400;
    yoe = (unsigned int)(year - era * 400);
    mp = month > 2u ? month - 3u : month + 9u;
    doy = (153u * mp + 2u) / 5u + day - 1u;
    doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

uint64_t TitanDateTime_CivilToUnix(int year, int month, int day, int hour, int minute, int second)
{
    int64_t days;
    if (!TitanDateTime_ValidateCivil(year, month, day, hour, minute, second)) return 0u;
    days = TitanDateTime_DaysFromCivil(year, (unsigned int)month, (unsigned int)day);
    if (days < 0) return 0u;
    return (uint64_t)days * 86400ull + (uint64_t)hour * 3600ull + (uint64_t)minute * 60ull + (uint64_t)second;
}

int TitanDateTime_UnixToCivil(uint64_t epoch, int *year, int *month, int *day, int *hour, int *minute, int *second)
{
    int64_t z, era;
    unsigned int doe, yoe, doy, mp;
    int y, m, d;
    uint64_t sod;
    if (!year || !month || !day || !hour || !minute || !second) return 0;
    z = (int64_t)(epoch / 86400ull) + 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = (unsigned int)(z - era * 146097);
    yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    y = (int)yoe + (int)era * 400;
    doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    mp = (5u * doy + 2u) / 153u;
    d = (int)(doy - (153u * mp + 2u) / 5u + 1u);
    m = (int)mp + (mp < 10u ? 3 : -9);
    y += m <= 2;
    sod = epoch % 86400ull;
    *year = y; *month = m; *day = d;
    *hour = (int)(sod / 3600ull);
    *minute = (int)((sod / 60ull) % 60ull);
    *second = (int)(sod % 60ull);
    return y >= 1970 && y <= 2400;
}

static uint32_t TitanDateTime_ReadU32Be(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

int TitanDateTime_ParseSntpResponse(const unsigned char *packet, unsigned int size, uint32_t request_nonce, uint64_t *unix_seconds)
{
    unsigned int li, mode, stratum;
    uint32_t origin_fraction, tx_seconds;
    if (!packet || size < 48u || !unix_seconds) return 0;
    li = (packet[0] >> 6) & 3u;
    mode = packet[0] & 7u;
    stratum = packet[1];
    if (li == 3u || (mode != 4u && mode != 5u) || stratum == 0u || stratum > 15u) return 0;
    origin_fraction = TitanDateTime_ReadU32Be(packet + 28u);
    if (request_nonce != 0u && origin_fraction != request_nonce) return 0;
    tx_seconds = TitanDateTime_ReadU32Be(packet + 40u);
    if ((uint64_t)tx_seconds <= TITAN_DATETIME_NTP_UNIX_DELTA) return 0;
    *unix_seconds = (uint64_t)tx_seconds - TITAN_DATETIME_NTP_UNIX_DELTA;
    return *unix_seconds >= 946684800ull; /* 2000-01-01 */
}

static void TitanDateTime_RefreshText(void)
{
    int y,m,d,hh,mm,ss;
    uint64_t local;
    int display_hour;
    const char *suffix = "";
    int tz = g_datetime.snapshot.timezone_minutes;
    int64_t shifted = (int64_t)g_datetime.snapshot.utc_epoch_seconds + (int64_t)tz * 60ll;
    if (!g_datetime.snapshot.valid || shifted < 0) {
        strcpy(g_datetime.snapshot.date_text, "----/--/--");
        strcpy(g_datetime.snapshot.time_text, "--:--");
        strcpy(g_datetime.timezone_text, "UTC");
        return;
    }
    local = (uint64_t)shifted;
    g_datetime.snapshot.local_epoch_seconds = local;
    if (!TitanDateTime_UnixToCivil(local,&y,&m,&d,&hh,&mm,&ss)) return;
    if (g_datetime.snapshot.date_format == TITAN_DATE_FORMAT_DMY)
        snprintf(g_datetime.snapshot.date_text,sizeof(g_datetime.snapshot.date_text),"%02d/%02d/%04d",d,m,y);
    else if (g_datetime.snapshot.date_format == TITAN_DATE_FORMAT_MDY)
        snprintf(g_datetime.snapshot.date_text,sizeof(g_datetime.snapshot.date_text),"%02d/%02d/%04d",m,d,y);
    else
        snprintf(g_datetime.snapshot.date_text,sizeof(g_datetime.snapshot.date_text),"%04d-%02d-%02d",y,m,d);
    if (g_datetime.snapshot.clock_format == TITAN_CLOCK_FORMAT_12H) {
        suffix = hh >= 12 ? "PM" : "AM";
        display_hour = hh % 12; if (display_hour == 0) display_hour = 12;
        snprintf(g_datetime.snapshot.time_text,sizeof(g_datetime.snapshot.time_text),"%02d:%02d %s",display_hour,mm,suffix);
    } else {
        snprintf(g_datetime.snapshot.time_text,sizeof(g_datetime.snapshot.time_text),"%02d:%02d",hh,mm);
    }
    if (tz == 0) snprintf(g_datetime.timezone_text,sizeof(g_datetime.timezone_text),"UTC");
    else snprintf(g_datetime.timezone_text,sizeof(g_datetime.timezone_text),"UTC%c%02d:%02d",tz < 0 ? '-' : '+',(tz < 0 ? -tz : tz)/60,(tz < 0 ? -tz : tz)%60);
}

static void TitanDateTime_SetAnchor(uint64_t utc, TitanDateTimeSource source, uint64_t now_ms)
{
    if (utc < 946684800ull) return;
    g_datetime.anchor_utc = utc;
    g_datetime.anchor_monotonic_ms = now_ms;
    g_datetime.snapshot.utc_epoch_seconds = utc;
    g_datetime.snapshot.source = source;
    g_datetime.snapshot.valid = 1;
    TitanDateTime_RefreshText();
}

#if defined(__EE__) || defined(_EE)
static int TitanDateTime_BcdToInt(unsigned char value) { return ((value >> 4) & 0x0f) * 10 + (value & 0x0f); }

static int TitanDateTime_ReadRtc(uint64_t *utc, int *osd_offset_minutes)
{
    sceCdCLOCK raw, local;
    int ry,rm,rd,rh,rmin,rs,ly,lm,ld,lh,lmin,ls;
    uint64_t re, le;
    if (!utc || !sceCdInit(SCECdINoD) || !sceCdReadClock(&raw)) return 0;
    local = raw;
    configConvertToLocalTime(&local);
    ry=2000+TitanDateTime_BcdToInt(raw.year); rm=TitanDateTime_BcdToInt(raw.month); rd=TitanDateTime_BcdToInt(raw.day);
    rh=TitanDateTime_BcdToInt(raw.hour); rmin=TitanDateTime_BcdToInt(raw.minute); rs=TitanDateTime_BcdToInt(raw.second);
    ly=2000+TitanDateTime_BcdToInt(local.year); lm=TitanDateTime_BcdToInt(local.month); ld=TitanDateTime_BcdToInt(local.day);
    lh=TitanDateTime_BcdToInt(local.hour); lmin=TitanDateTime_BcdToInt(local.minute); ls=TitanDateTime_BcdToInt(local.second);
    re=TitanDateTime_CivilToUnix(ry,rm,rd,rh,rmin,rs); le=TitanDateTime_CivilToUnix(ly,lm,ld,lh,lmin,ls);
    if (!re) return 0;
    *utc=re;
    if (osd_offset_minutes && le) {
        int64_t diff=(int64_t)le-(int64_t)re;
        if(diff>=-12*3600ll && diff<=14*3600ll) *osd_offset_minutes=(int)(diff/60ll);
    }
    return 1;
}

static int TitanDateTime_SntpFetch(uint64_t *epoch, uint32_t nonce)
{
    unsigned char request[48], response[48];
    struct hostent *host;
    struct sockaddr_in address;
    fd_set readfds;
    struct timeval timeout;
    int fd, amount;
    memset(request,0,sizeof(request)); request[0]=0x23u;
    request[44]=(unsigned char)(nonce>>24); request[45]=(unsigned char)(nonce>>16); request[46]=(unsigned char)(nonce>>8); request[47]=(unsigned char)nonce;
    host=gethostbyname(g_datetime.snapshot.server); if(!host || !host->h_addr_list || !host->h_addr_list[0]) return -1;
    fd=socket(PF_INET,SOCK_DGRAM,IPPROTO_UDP); if(fd<0) return -2;
    memset(&address,0,sizeof(address)); address.sin_family=AF_INET; address.sin_port=htons(123); memcpy(&address.sin_addr,host->h_addr_list[0],sizeof(address.sin_addr));
    if(sendto(fd,request,sizeof(request),0,(struct sockaddr*)&address,sizeof(address))!=(int)sizeof(request)){close(fd);return -2;}
    FD_ZERO(&readfds); FD_SET(fd,&readfds); timeout.tv_sec=TITAN_DATETIME_SNTP_TIMEOUT_MS/1000u; timeout.tv_usec=(TITAN_DATETIME_SNTP_TIMEOUT_MS%1000u)*1000u;
    amount=select(fd+1,&readfds,0,0,&timeout); if(amount<=0){close(fd);return amount==0?-3:-2;}
    amount=recvfrom(fd,response,sizeof(response),0,0,0); close(fd);
    if(amount<48 || !TitanDateTime_ParseSntpResponse(response,(unsigned int)amount,nonce,epoch)) return -4;
    return 1;
}

static int TitanDateTime_SaveSettings(void)
{
    char text[256]; int fd,n;
    (void)TitanStorageIo_CreateDirectory(TITAN_STORAGE_DEVICE_ROOT);
    (void)TitanStorageIo_CreateDirectory(TITAN_DATETIME_SETTINGS_DIR);
    n=snprintf(text,sizeof(text),"version=1\nautomatic=%d\ntimezone_minutes=%d\nclock_format=%d\ndate_format=%d\nserver=%s\n",
        g_datetime.snapshot.automatic,g_datetime.snapshot.timezone_minutes,(int)g_datetime.snapshot.clock_format,(int)g_datetime.snapshot.date_format,g_datetime.snapshot.server);
    if(n<=0 || (unsigned int)n>=sizeof(text)) return 0;
    fd=TitanStorageIo_Open(TITAN_DATETIME_SETTINGS_TEMP,TITAN_STORAGE_IO_WRITE|TITAN_STORAGE_IO_CREATE|TITAN_STORAGE_IO_TRUNCATE);
    if(fd<0) return 0;
    if(!TitanStorageIo_WriteExact(fd,text,(size_t)n) || TitanStorageIo_Flush(fd)<0 || TitanStorageIo_Close(fd)<0) return 0;
    return TitanStorageIo_AtomicReplace(TITAN_DATETIME_SETTINGS_TEMP,TITAN_DATETIME_SETTINGS_PATH,TITAN_DATETIME_SETTINGS_BACKUP);
}

static void TitanDateTime_Worker(void *arg)
{
    int result=0; (void)arg;
    if(g_datetime.worker_job==TITAN_DATETIME_JOB_SNTP) result=TitanDateTime_SntpFetch(&g_datetime.worker_epoch,g_datetime.worker_nonce);
    else if(g_datetime.worker_job==TITAN_DATETIME_JOB_SAVE) result=TitanDateTime_SaveSettings()?1:-5;
    g_datetime.worker_result=result; g_datetime.worker_done=1; ExitDeleteThread();
}

static int TitanDateTime_StartWorker(int job)
{
    ee_thread_t t;
    if(g_datetime.worker_active) return 0;
    memset(&t,0,sizeof(t)); t.func=TitanDateTime_Worker; t.stack=g_datetime_worker_stack; t.stack_size=sizeof(g_datetime_worker_stack); t.gp_reg=&_gp; t.initial_priority=TITAN_DATETIME_WORKER_PRIORITY;
    g_datetime.worker_job=job; g_datetime.worker_done=0; g_datetime.worker_result=0; g_datetime.worker_active=1;
    g_datetime_worker_thread=CreateThread(&t); if(g_datetime_worker_thread<0){g_datetime.worker_active=0;return 0;}
    if(StartThread(g_datetime_worker_thread,0)<0){DeleteThread(g_datetime_worker_thread);g_datetime_worker_thread=-1;g_datetime.worker_active=0;return 0;}
    return 1;
}
#else
static int TitanDateTime_StartWorker(int job) { (void)job; return 0; }
#endif

static void TitanDateTime_LoadSettings(void)
{
    char text[256],*line,*save; int fd; TitanStorageIoResult got;
    fd=TitanStorageIo_Open(TITAN_DATETIME_SETTINGS_PATH,TITAN_STORAGE_IO_READ); if(fd<0) return;
    got=TitanStorageIo_ReadSome(fd,text,sizeof(text)-1u); TitanStorageIo_Close(fd); if(got<=0) return; text[(size_t)got]='\0';
    line=strtok_r(text,"\n",&save);
    while(line){int value;
        if(sscanf(line,"automatic=%d",&value)==1) g_datetime.snapshot.automatic=value?1:0;
        else if(sscanf(line,"timezone_minutes=%d",&value)==1 && value>=-720 && value<=840) g_datetime.snapshot.timezone_minutes=value;
        else if(sscanf(line,"clock_format=%d",&value)==1 && value>=0 && value<=1) g_datetime.snapshot.clock_format=(TitanClockFormat)value;
        else if(sscanf(line,"date_format=%d",&value)==1 && value>=0 && value<=2) g_datetime.snapshot.date_format=(TitanDateFormat)value;
        else if(strncmp(line,"server=",7)==0 && strlen(line+7)>0u && strlen(line+7)<sizeof(g_datetime.snapshot.server)) strcpy(g_datetime.snapshot.server,line+7);
        line=strtok_r(0,"\n",&save);
    }
}

void TitanDateTime_Initialize(void)
{
#if defined(__EE__) || defined(_EE)
    uint64_t rtc=0u; int offset=0;
#endif
    memset(&g_datetime,0,sizeof(g_datetime));
    g_datetime.snapshot.automatic=1; g_datetime.snapshot.clock_format=TITAN_CLOCK_FORMAT_24H; g_datetime.snapshot.date_format=TITAN_DATE_FORMAT_DMY;
    strcpy(g_datetime.snapshot.server,TITAN_DATETIME_DEFAULT_SERVER);
#if defined(__EE__) || defined(_EE)
    if(TitanDateTime_ReadRtc(&rtc,&offset)){g_datetime.snapshot.timezone_minutes=offset;TitanDateTime_SetAnchor(rtc,TITAN_DATETIME_SOURCE_RTC,TitanTime_NowMilliseconds());}
#endif
    TitanDateTime_LoadSettings();
    TitanDateTime_RefreshText();
    g_datetime.snapshot.sync_state=g_datetime.snapshot.automatic?TITAN_DATETIME_SYNC_WAIT_NETWORK:TITAN_DATETIME_SYNC_IDLE;
}

void TitanDateTime_Shutdown(void)
{
    /* Never kill DNS/socket/storage calls mid-flight. Worker owns only its fixed
     * scratch and result slots; late completion cannot mutate shell resources. */
    g_datetime.worker_active=0; g_datetime.worker_done=0;
#if defined(__EE__) || defined(_EE)
    g_datetime_worker_thread=-1;
#endif
}

void TitanDateTime_Update(int network_online, uint64_t now_ms)
{
    if(g_datetime.snapshot.valid){uint64_t elapsed=now_ms>=g_datetime.anchor_monotonic_ms?now_ms-g_datetime.anchor_monotonic_ms:0u;g_datetime.snapshot.utc_epoch_seconds=g_datetime.anchor_utc+elapsed/1000ull;TitanDateTime_RefreshText();}
    if(g_datetime.worker_active && g_datetime.worker_done){
        int job=g_datetime.worker_job,result=g_datetime.worker_result; g_datetime.worker_active=0;g_datetime.worker_done=0;g_datetime.worker_job=TITAN_DATETIME_JOB_NONE;
        if(job==TITAN_DATETIME_JOB_SNTP){
            if(result>0){TitanDateTime_SetAnchor(g_datetime.worker_epoch,TITAN_DATETIME_SOURCE_SNTP,now_ms);g_datetime.snapshot.last_sync_epoch_seconds=g_datetime.worker_epoch;g_datetime.snapshot.sync_successes++;g_datetime.snapshot.sync_state=TITAN_DATETIME_SYNC_OK;g_datetime.next_auto_sync_ms=now_ms+TITAN_DATETIME_SYNC_REFRESH_MS;TitanDiagnostics_Event(TITAN_DIAG_INFO,TITAN_DIAG_NETWORK,0x330,0,(uint32_t)g_datetime.worker_epoch,0,0,0);}
            else {g_datetime.snapshot.last_error=result;g_datetime.snapshot.sync_state=result==-1?TITAN_DATETIME_SYNC_DNS_ERROR:(result==-3?TITAN_DATETIME_SYNC_TIMEOUT:(result==-4?TITAN_DATETIME_SYNC_INVALID_RESPONSE:TITAN_DATETIME_SYNC_SOCKET_ERROR));g_datetime.next_auto_sync_ms=now_ms+TITAN_DATETIME_SYNC_RETRY_MS;TitanDiagnostics_Event(TITAN_DIAG_WARN,TITAN_DIAG_NETWORK,0x331,result,0,0,0,0);}
        } else if(job==TITAN_DATETIME_JOB_SAVE && result<=0) TitanDiagnostics_Event(TITAN_DIAG_WARN,TITAN_DIAG_STORAGE,0x332,result,0,0,0,0);
    }
    if(g_datetime.settings_dirty && !g_datetime.worker_active){if(TitanDateTime_StartWorker(TITAN_DATETIME_JOB_SAVE))g_datetime.settings_dirty=0;}
    if(g_datetime.snapshot.automatic && !g_datetime.worker_active){
        if(!network_online){if(g_datetime.snapshot.sync_state==TITAN_DATETIME_SYNC_PENDING || g_datetime.snapshot.sync_state==TITAN_DATETIME_SYNC_WAIT_NETWORK)g_datetime.snapshot.sync_state=TITAN_DATETIME_SYNC_WAIT_NETWORK;}
        else if(g_datetime.snapshot.sync_state==TITAN_DATETIME_SYNC_PENDING || g_datetime.snapshot.sync_state==TITAN_DATETIME_SYNC_WAIT_NETWORK || now_ms>=g_datetime.next_auto_sync_ms){
            g_datetime.worker_nonce=(uint32_t)(now_ms ^ (now_ms>>32) ^ 0x5449544eu); if(g_datetime.worker_nonce==0u)g_datetime.worker_nonce=1u;
            if(TitanDateTime_StartWorker(TITAN_DATETIME_JOB_SNTP)){g_datetime.snapshot.sync_attempts++;g_datetime.snapshot.sync_state=TITAN_DATETIME_SYNC_RUNNING;}
        }
    }
}

void TitanDateTime_RequestSync(void){g_datetime.snapshot.sync_state=TITAN_DATETIME_SYNC_PENDING;g_datetime.next_auto_sync_ms=0u;}
void TitanDateTime_SetAutomatic(int enabled){g_datetime.snapshot.automatic=enabled?1:0;g_datetime.snapshot.sync_state=enabled?TITAN_DATETIME_SYNC_WAIT_NETWORK:TITAN_DATETIME_SYNC_IDLE;g_datetime.settings_dirty=1;}
void TitanDateTime_SetTimezoneMinutes(int minutes){if(minutes<-720)minutes=-720;if(minutes>840)minutes=840;g_datetime.snapshot.timezone_minutes=minutes;g_datetime.settings_dirty=1;TitanDateTime_RefreshText();}
void TitanDateTime_CycleTimezone(int direction){int tz=g_datetime.snapshot.timezone_minutes+(direction<0?-30:30);if(tz<-720)tz=840;if(tz>840)tz=-720;TitanDateTime_SetTimezoneMinutes(tz);}
void TitanDateTime_SetClockFormat(TitanClockFormat format){if(format>TITAN_CLOCK_FORMAT_12H)return;g_datetime.snapshot.clock_format=format;g_datetime.settings_dirty=1;TitanDateTime_RefreshText();}
void TitanDateTime_ToggleClockFormat(void){TitanDateTime_SetClockFormat(g_datetime.snapshot.clock_format==TITAN_CLOCK_FORMAT_24H?TITAN_CLOCK_FORMAT_12H:TITAN_CLOCK_FORMAT_24H);}
void TitanDateTime_SetDateFormat(TitanDateFormat format){if(format>TITAN_DATE_FORMAT_MDY)return;g_datetime.snapshot.date_format=format;g_datetime.settings_dirty=1;TitanDateTime_RefreshText();}
void TitanDateTime_CycleDateFormat(int direction){int f=(int)g_datetime.snapshot.date_format+(direction<0?-1:1);if(f<0)f=2;if(f>2)f=0;TitanDateTime_SetDateFormat((TitanDateFormat)f);}
int TitanDateTime_AdjustManualSeconds(int64_t delta_seconds){int64_t next;if(g_datetime.snapshot.automatic)return 0;next=(int64_t)g_datetime.snapshot.utc_epoch_seconds+delta_seconds;if(next<(int64_t)946684800ull)return 0;TitanDateTime_SetAnchor((uint64_t)next,TITAN_DATETIME_SOURCE_MANUAL,TitanTime_NowMilliseconds());return 1;}
int TitanDateTime_AdjustManualDays(int delta_days){return TitanDateTime_AdjustManualSeconds((int64_t)delta_days*86400ll);}
const TitanDateTimeSnapshot *TitanDateTime_GetSnapshot(void){return &g_datetime.snapshot;}
const char *TitanDateTime_GetTimezoneText(void){return g_datetime.timezone_text;}
const char *TitanDateTime_GetDateFormatName(void){return g_datetime.snapshot.date_format==TITAN_DATE_FORMAT_DMY?"DD/MM/YYYY":(g_datetime.snapshot.date_format==TITAN_DATE_FORMAT_MDY?"MM/DD/YYYY":"YYYY-MM-DD");}
const char *TitanDateTime_GetClockFormatName(void){return g_datetime.snapshot.clock_format==TITAN_CLOCK_FORMAT_12H?"12-hour":"24-hour";}
const char *TitanDateTime_GetSourceName(void){switch(g_datetime.snapshot.source){case TITAN_DATETIME_SOURCE_RTC:return "RTC";case TITAN_DATETIME_SOURCE_SNTP:return "Internet (SNTP)";case TITAN_DATETIME_SOURCE_MANUAL:return "Manual";default:return "Unavailable";}}
const char *TitanDateTime_GetSyncStateName(void){switch(g_datetime.snapshot.sync_state){case TITAN_DATETIME_SYNC_WAIT_NETWORK:return "Waiting for network";case TITAN_DATETIME_SYNC_PENDING:return "Pending";case TITAN_DATETIME_SYNC_RUNNING:return "Synchronizing";case TITAN_DATETIME_SYNC_OK:return "Synchronized";case TITAN_DATETIME_SYNC_DNS_ERROR:return "DNS error";case TITAN_DATETIME_SYNC_SOCKET_ERROR:return "Network error";case TITAN_DATETIME_SYNC_TIMEOUT:return "Timed out";case TITAN_DATETIME_SYNC_INVALID_RESPONSE:return "Invalid response";default:return "Idle";}}
