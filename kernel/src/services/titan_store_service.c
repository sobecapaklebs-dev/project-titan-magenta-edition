#include "titan_store_service.h"

#include "titan_download_service.h"
#if defined(TITAN_STORE_SERVICE_HOST_TEST)
void TitanNotifications_BeginDownloadActivity(const char *, const char *);
void TitanNotifications_UpdateDownloadActivity(const char *, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int);
void TitanNotifications_CompleteDownloadActivity(const char *, unsigned int, unsigned int);
void TitanNotifications_FailDownloadActivity(const char *, unsigned int, unsigned int);
#else
#include "titan_notifications.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TITAN_STORE_REPOSITORY_URL_CAPACITY 768u
#define TITAN_STORE_TRACKED_DOWNLOADS 6u

typedef struct TitanStoreTrackedDownload
{
    unsigned int job_id;
    int notified_terminal;
} TitanStoreTrackedDownload;

typedef struct TitanStoreRuntime
{
    TitanRepositoryCatalog *catalog;
    char repository_url[TITAN_STORE_REPOSITORY_URL_CAPACITY];
    int repository_loaded;
    TitanStoreTrackedDownload tracked[TITAN_STORE_TRACKED_DOWNLOADS];
    unsigned int last_visible_job;
    TitanDownloadState last_visible_state;
} TitanStoreRuntime;

static TitanStoreRuntime g_store;

static void copy_text(char *dst, size_t cap, const char *src)
{
    size_t i=0u; if(!dst||cap==0u)return;
    while(src&&src[i]&&i+1u<cap){dst[i]=src[i];i++;} dst[i]='\0';
}

static int dependency_available(const char *token)
{
    return token && token[0] && TitanRepository_FindPackage(g_store.catalog,token)!=0;
}

static int dependencies_satisfied(const char *text)
{
    char token[TITAN_REPOSITORY_PACKAGE_ID_CAPACITY];
    unsigned int used=0u;
    const char *p=text;
    if(!text||!text[0])return 1;
    while(1)
    {
        char c=*p++;
        if(c==','||c=='\0')
        {
            while(used&&token[used-1]==' ')used--;
            token[used]='\0';
            { unsigned int start=0u; while(token[start]==' ')start++; if(start)memmove(token,token+start,used-start+1u); }
            if(token[0]&&!dependency_available(token))return 0;
            used=0u;
            if(c=='\0')break;
            continue;
        }
        if(used+1u>=sizeof(token))return 0;
        token[used++]=c;
    }
    return 1;
}

void TitanStoreService_Initialize(void){memset(&g_store,0,sizeof(g_store));}

TitanStoreResult TitanStoreService_AcceptRepository(const char *source_url,const char *text,unsigned int size)
{
    TitanRepositoryCatalog parsed;
    if(!source_url||!source_url[0]||!text||!size)return TITAN_STORE_BAD_REPOSITORY;
    if(TitanRepository_Parse(text,size,&parsed)!=TITAN_REPOSITORY_PARSE_OK)return TITAN_STORE_BAD_REPOSITORY;
    if(g_store.catalog==0){g_store.catalog=(TitanRepositoryCatalog*)malloc(sizeof(TitanRepositoryCatalog));if(g_store.catalog==0)return TITAN_STORE_BAD_REPOSITORY;}
    *g_store.catalog=parsed;
    copy_text(g_store.repository_url,sizeof(g_store.repository_url),source_url);
    g_store.repository_loaded=1;
    return TITAN_STORE_OK;
}

const TitanRepositoryCatalog *TitanStoreService_GetCatalog(void){return g_store.repository_loaded?g_store.catalog:0;}
const char *TitanStoreService_GetRepositoryUrl(void){return g_store.repository_loaded?g_store.repository_url:"";}

TitanStoreResult TitanStoreService_QueuePackage(const char *package_id,unsigned int *out_job_id)
{
    const TitanRepositoryPackage *pkg;
    TitanDownloadRequest req;
    unsigned int job_id;
    unsigned int i;
    int result;
    if(out_job_id)*out_job_id=0u;
    if(!g_store.repository_loaded)return TITAN_STORE_NO_REPOSITORY;
    pkg=TitanRepository_FindPackage(g_store.catalog,package_id);
    if(!pkg)return TITAN_STORE_PACKAGE_NOT_FOUND;
    if(!dependencies_satisfied(pkg->dependencies))return TITAN_STORE_DEPENDENCY_MISSING;
    memset(&req,0,sizeof(req));
    copy_text(req.package_id,sizeof(req.package_id),pkg->package_id);
    copy_text(req.title,sizeof(req.title),pkg->title);
    copy_text(req.url,sizeof(req.url),pkg->download_url);
    copy_text(req.expected_sha256,sizeof(req.expected_sha256),pkg->sha256);
    req.expected_size=pkg->package_size;
    req.retry_limit=2u;
    result=snprintf(req.destination,sizeof(req.destination),"mass:/TITAN/packages/%s-%s.tpkg",pkg->package_id,pkg->version);
    if(result<=0||(size_t)result>=sizeof(req.destination))return TITAN_STORE_DOWNLOAD_REJECTED;
    job_id=TitanDownloadService_Enqueue(&req);
    if(!job_id)return TITAN_STORE_DOWNLOAD_REJECTED;
    for(i=0u;i<TITAN_STORE_TRACKED_DOWNLOADS;i++)
    {
        if(g_store.tracked[i].job_id==0u||g_store.tracked[i].notified_terminal)
        {g_store.tracked[i].job_id=job_id;g_store.tracked[i].notified_terminal=0;break;}
    }
    if(out_job_id)*out_job_id=job_id;
    return TITAN_STORE_OK;
}

void TitanStoreService_Update(void)
{
    unsigned int i;
    TitanDownloadSnapshot s;
    for(i=0u;i<TITAN_STORE_TRACKED_DOWNLOADS;i++)
    {
        TitanStoreTrackedDownload *t=&g_store.tracked[i];
        char msg[112];
        unsigned int received,total;
        if(!t->job_id||t->notified_terminal)continue;
        if(!TitanDownloadService_Find(t->job_id,&s))continue;
        if(s.state==TITAN_DOWNLOAD_QUEUED||s.state==TITAN_DOWNLOAD_PREPARING||s.state==TITAN_DOWNLOAD_DOWNLOADING||s.state==TITAN_DOWNLOAD_VERIFYING||s.state==TITAN_DOWNLOAD_PUBLISHING||s.state==TITAN_DOWNLOAD_PAUSED)
        {
            if(g_store.last_visible_job!=s.job_id)
            {
                TitanNotifications_BeginDownloadActivity(s.title,"Preparando download");
                g_store.last_visible_job=s.job_id;
            }
            if(s.state==TITAN_DOWNLOAD_VERIFYING) copy_text(msg,sizeof(msg),"Verificando integridade");
            else if(s.state==TITAN_DOWNLOAD_PUBLISHING) copy_text(msg,sizeof(msg),"Publicando pacote validado");
            else if(s.state==TITAN_DOWNLOAD_PAUSED) copy_text(msg,sizeof(msg),"Download pausado");
            else copy_text(msg,sizeof(msg),"Baixando pacote");
            received=s.bytes_downloaded>0xffffffffu?0xffffffffu:(unsigned int)s.bytes_downloaded;
            total=s.total_bytes>0xffffffffu?0xffffffffu:(unsigned int)s.total_bytes;
            TitanNotifications_UpdateDownloadActivity(msg,s.progress_percent,0u,0u,s.progress_percent,received,total,s.speed_bytes_per_second);
            return; /* one visible transfer card; queue remains available in service snapshots */
        }
        if(s.state==TITAN_DOWNLOAD_COMPLETE)
        {
            snprintf(msg,sizeof(msg),"%s pronto para instalar",s.title);
            TitanNotifications_CompleteDownloadActivity(msg,1u,1u);
            t->notified_terminal=1;
            if(g_store.last_visible_job==s.job_id)g_store.last_visible_job=0u;
            continue;
        }
        if(s.state==TITAN_DOWNLOAD_FAILED||s.state==TITAN_DOWNLOAD_CANCELLED)
        {
            snprintf(msg,sizeof(msg),"Falha no download de %s",s.title);
            TitanNotifications_FailDownloadActivity(msg,0u,1u);
            t->notified_terminal=1;
            if(g_store.last_visible_job==s.job_id)g_store.last_visible_job=0u;
        }
    }
}

void TitanStoreService_Shutdown(void){free(g_store.catalog);memset(&g_store,0,sizeof(g_store));}
