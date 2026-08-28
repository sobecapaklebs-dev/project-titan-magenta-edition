/*
 * Project Titan AR9271 flight recorder TT1424.
 *
 * Design rule: absolutely no filesystem/storage I/O while the Wi-Fi path is
 * executing. Events are appended to static IOP RAM and exported over RPC only
 * after the recorder is frozen. This keeps diagnostics from becoming a new
 * timing variable in the association experiment.
 */
#include <types.h>
#include <sysclib.h>
#include <thbase.h>
#include <thsemap.h>
#include "titan_flightrec.h"

static TitanFlightRecRecord g_rec[TITAN_FR_RECORD_CAP] __attribute__((aligned(16)));
static u8 g_blob[TITAN_FR_BLOB_CAP] __attribute__((aligned(16)));
static TitanFlightRecMeta g_meta;
static int g_fr_lock=-1;

static void fr_lock_init(void)
{
    if(g_fr_lock<0){
        iop_sema_t s; memset(&s,0,sizeof(s)); s.initial=1; s.max=1;
        g_fr_lock=CreateSema(&s);
    }
}
static void fr_lock(void){if(g_fr_lock>=0)WaitSema(g_fr_lock);}
static void fr_unlock(void){if(g_fr_lock>=0)SignalSema(g_fr_lock);}

int titan_fr_lock_ready(void){return g_fr_lock>=0?1:0;}

static u32 fr_clock_us32(void)
{
    iop_sys_clock_t t; u32 sec=0,usec=0;
    memset(&t,0,sizeof(t));
    GetSystemTime(&t); SysClock2USec(&t,&sec,&usec);
    return sec*1000000u+usec;
}

static u32 rec_guard(const TitanFlightRecRecord *r)
{
    return 0xA9271419u ^ r->seq ^ r->timestamp_us ^ ((u32)r->type<<16) ^
           (u32)r->flags ^ (u32)r->rc ^ r->a ^ r->b ^ r->c ^ r->d ^
           r->blob_off ^ ((u32)r->blob_len<<16) ^ r->aux;
}

static void fr_record_unlocked(u16 type,u16 flags,s32 rc,u32 a,u32 b,u32 c,u32 d,
                               const void *blob,u16 blob_len,u16 aux)
{
    TitanFlightRecRecord *r; u32 boff=0; u16 keep=blob_len;
    if(g_meta.magic!=TITAN_FR_MAGIC || g_meta.frozen)return;
    if(keep>TITAN_FR_BLOB_MAX){g_meta.dropped_blob_bytes+=(u32)(keep-TITAN_FR_BLOB_MAX);keep=TITAN_FR_BLOB_MAX;flags|=TITAN_FR_F_BLOB_TRUNCATED;}
    if(!blob)keep=0;
    if(g_meta.record_count>=TITAN_FR_RECORD_CAP){++g_meta.dropped_records;return;}
    if(keep){
        if(g_meta.blob_used+(u32)keep>TITAN_FR_BLOB_CAP){
            g_meta.dropped_blob_bytes+=(u32)keep; keep=0; flags|=TITAN_FR_F_BLOB_DROPPED;
        }else{
            boff=g_meta.blob_used;
            memcpy(g_blob+boff,blob,keep);
            g_meta.blob_used+=(u32)keep;
        }
    }
    r=&g_rec[g_meta.record_count];
    memset(r,0,sizeof(*r));
    r->seq=g_meta.record_count;
    r->timestamp_us=fr_clock_us32()-g_meta.start_us;
    r->type=type;r->flags=flags;r->rc=rc;r->a=a;r->b=b;r->c=c;r->d=d;
    r->blob_off=boff;r->blob_len=keep;r->aux=aux;
    r->guard=rec_guard(r);
    ++g_meta.record_count;
}

void titan_fr_reset(void)
{
    fr_lock_init(); fr_lock();
    memset(&g_meta,0,sizeof(g_meta));
    g_meta.magic=TITAN_FR_MAGIC;
    g_meta.version=TITAN_FR_VERSION;
    g_meta.build_id=TITAN_FR_BUILD_ID;
    g_meta.record_size=sizeof(TitanFlightRecRecord);
    g_meta.record_capacity=TITAN_FR_RECORD_CAP;
    g_meta.blob_capacity=TITAN_FR_BLOB_CAP;
    g_meta.start_us=fr_clock_us32();
    /* TT1424 may run through WPA2 and the online proof. The scanner redacts
     * key-cache WMI values and never records raw EAPOL payloads, so this flag
     * remains truthful even after M3/PTK/GTK installation. */
    g_meta.secrets_redacted=1;
    fr_record_unlocked(TITAN_FR_EV_RUN_BEGIN,0,0,TITAN_FR_BUILD_ID,0,0,0,NULL,0,0);
    fr_unlock();
}

void titan_fr_record(u16 type,u16 flags,s32 rc,u32 a,u32 b,u32 c,u32 d,
                     const void *blob,u16 blob_len,u16 aux)
{
    fr_lock(); fr_record_unlocked(type,flags,rc,a,b,c,d,blob,blob_len,aux); fr_unlock();
}

void titan_fr_mark(u32 tag,u32 a,u32 b,u32 c)
{ titan_fr_record(TITAN_FR_EV_MARK,0,0,tag,a,b,c,NULL,0,0); }

void titan_fr_freeze(s32 reason)
{
    fr_lock();
    if(g_meta.magic==TITAN_FR_MAGIC && !g_meta.frozen){
        fr_record_unlocked(TITAN_FR_EV_FREEZE,0,reason,g_meta.record_count,g_meta.blob_used,0,0,NULL,0,0);
        g_meta.freeze_reason=reason;
        g_meta.freeze_us=fr_clock_us32()-g_meta.start_us;
        g_meta.frozen=1;
    }
    fr_unlock();
}

void titan_fr_get_meta(TitanFlightRecMeta *out)
{ if(out){fr_lock();memcpy(out,&g_meta,sizeof(*out));fr_unlock();} }

int titan_fr_copy_records(u32 byte_off,void *dst,u32 cap)
{
    u32 total,n; int ret=0; fr_lock();
    total=g_meta.record_count*(u32)sizeof(TitanFlightRecRecord);
    if(dst&&g_meta.frozen&&byte_off<total){n=total-byte_off;if(n>cap)n=cap;memcpy(dst,((u8*)g_rec)+byte_off,n);ret=(int)n;}
    fr_unlock(); return ret;
}
int titan_fr_copy_blob(u32 byte_off,void *dst,u32 cap)
{
    u32 n; int ret=0; fr_lock();
    if(dst&&g_meta.frozen&&byte_off<g_meta.blob_used){n=g_meta.blob_used-byte_off;if(n>cap)n=cap;memcpy(dst,g_blob+byte_off,n);ret=(int)n;}
    fr_unlock(); return ret;
}
int titan_fr_is_frozen(void){int r;fr_lock();r=g_meta.frozen?1:0;fr_unlock();return r;}
