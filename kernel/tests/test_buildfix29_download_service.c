#define TITAN_DOWNLOAD_SERVICE_HOST_TEST 1
#include "titan_download_service.h"
#include "titan_http_client.h"
#include "titan_network_client.h"
#include "titan_package_installer.h"
#include "titan_storage_io.h"
#include "titan_storage_scheduler.h"
#include "titan_time.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const unsigned char k_payload[] = {
'T','P','K','G','-','B','F','2','9','-','d','o','w','n','l','o','a','d','-','s','e','r','v','i','c','e','-','f','i','x','t','u','r','e','-','v','1','\n',
0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,
32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63
};
static unsigned long long g_now_ms;

uint64_t TitanTime_NowMilliseconds(void) { g_now_ms += 125u; return g_now_ms; }
void TitanStorageScheduler_NotifyBackgroundWork(void) {}

int TitanNetworkClient_GetEx(TitanNetworkClientOwner owner,const char *url,unsigned char *destination,unsigned int capacity,unsigned int *out_size,int *out_status,const TitanHttpClientOptions *options,TitanHttpResultInfo *info)
{
    unsigned int first=37u;
    (void)destination;(void)capacity;
    assert(owner==TITAN_NETWORK_CLIENT_OWNER_DOWNLOAD_SERVICE);
    assert(strncmp(url,"https://",8)==0);
    if (out_status) *out_status=200;
    if (out_size) *out_size=(unsigned int)sizeof(k_payload);
    if (info) { memset(info,0,sizeof(*info)); info->content_length=(unsigned int)sizeof(k_payload); }
    assert(options && options->body_write && options->progress);
    assert(options->body_write(options->user_data,k_payload,first));
    options->progress(options->user_data,first,(unsigned int)sizeof(k_payload));
    assert(options->body_write(options->user_data,k_payload+first,(unsigned int)sizeof(k_payload)-first));
    options->progress(options->user_data,(unsigned int)sizeof(k_payload),(unsigned int)sizeof(k_payload));
    return 1;
}
void TitanNetworkClient_ResetSession(void) {}
void TitanNetworkClient_GetSnapshot(TitanNetworkClientSnapshot *snapshot){ if(snapshot) memset(snapshot,0,sizeof(*snapshot)); }

int TitanStorageIo_Open(const char *path,int flags)
{
    FILE *f;
    (void)flags;
    f=fopen(path,"wb+");
    if(!f) return -1;
    return fileno(f);
}
int TitanStorageIo_Close(int fd){ return close(fd)==0?0:-1; }
int TitanStorageIo_WriteExact(int fd,const void *buf,size_t size)
{
    const unsigned char *p=(const unsigned char*)buf; size_t done=0;
    while(done<size){ ssize_t n=write(fd,p+done,size-done); if(n<=0)return 0; done+=(size_t)n; }
    return 1;
}
int TitanStorageIo_Flush(int fd){ return fsync(fd)==0; }
int TitanStorageIo_Remove(const char *path){ return unlink(path); }
int TitanStorageIo_PathInfo64(const char *path,int *is_dir,TitanStorageIoSize *size)
{
    FILE *f=fopen(path,"rb"); long n; if(!f)return 0; fseek(f,0,SEEK_END); n=ftell(f); fclose(f); if(is_dir)*is_dir=0; if(size)*size=(TitanStorageIoSize)n; return 1;
}
int TitanStorageIo_AtomicReplace(const char *tmp,const char *dst,const char *bak)
{
    (void)bak; unlink(dst); return rename(tmp,dst)==0;
}
int TitanPackageInstaller_ValidatePackageFile(const char *path,TitanPackageInfo *info)
{
    unsigned char magic[4]; FILE *f=fopen(path,"rb"); if(!f)return 0; if(fread(magic,1,4,f)!=4){fclose(f);return 0;} fclose(f); if(info) memset(info,0,sizeof(*info)); return memcmp(magic,"TPKG",4)==0;
}

int main(void)
{
    TitanDownloadRequest r;
    TitanDownloadSnapshot s;
    unsigned int id;
    char dir[]="/tmp/titan-bf29-dl-XXXXXX";
    char dst[256];
    FILE *f; unsigned char got[sizeof(k_payload)];
    assert(mkdtemp(dir)!=NULL);
    snprintf(dst,sizeof(dst),"%s/pkg.tpkg",dir);
    memset(&r,0,sizeof(r));
    strcpy(r.package_id,"com.titan.test"); strcpy(r.title,"Fixture");
    strcpy(r.url,"https://example.invalid/pkg.tpkg"); strcpy(r.destination,dst);
    strcpy(r.expected_sha256,"3be25097d50a1ce813672fe7f2e9f54faf5eccebf6afc9968c3e85dd85fe5f18");
    r.expected_size=sizeof(k_payload); r.retry_limit=1;
    assert(TitanDownloadService_Initialize());
    id=TitanDownloadService_Enqueue(&r); assert(id!=0);
    assert(TitanDownloadService_TestRunNext());
    assert(TitanDownloadService_Find(id,&s));
    assert(s.state==TITAN_DOWNLOAD_COMPLETE); assert(s.failure==TITAN_DOWNLOAD_FAILURE_NONE);
    assert(s.bytes_downloaded==sizeof(k_payload)); assert(s.total_bytes==sizeof(k_payload)); assert(s.progress_percent==100);
    assert(s.speed_bytes_per_second>0);
    f=fopen(dst,"rb"); assert(f); assert(fread(got,1,sizeof(got),f)==sizeof(got)); fclose(f); assert(memcmp(got,k_payload,sizeof(got))==0);

    /* Honest pause semantics: service supports pausing, but does not claim byte-range resume. */
    id=TitanDownloadService_Enqueue(&r); assert(id!=0); assert(TitanDownloadService_Pause(id)); assert(TitanDownloadService_Find(id,&s)); assert(s.state==TITAN_DOWNLOAD_PAUSED); assert(s.pause_supported==1); assert(s.resume_supported==0); assert(TitanDownloadService_Resume(id));
    assert(TitanDownloadService_Cancel(id)); assert(TitanDownloadService_Find(id,&s)); assert(s.state==TITAN_DOWNLOAD_CANCELLED);

    TitanDownloadService_Shutdown();
    unlink(dst); rmdir(dir);
    puts("buildfix29-download-service-host: OK");
    return 0;
}
