#include "titan_store_service.h"
#include "titan_download_service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static TitanDownloadRequest g_req; static unsigned int g_job=77u;
unsigned int TitanDownloadService_Enqueue(const TitanDownloadRequest *r){g_req=*r;return g_job;}
int TitanDownloadService_Find(unsigned int id,TitanDownloadSnapshot *s){if(id!=g_job)return 0;memset(s,0,sizeof(*s));s->job_id=id;s->state=TITAN_DOWNLOAD_DOWNLOADING;strcpy(s->title,"Demo App");s->bytes_downloaded=512;s->total_bytes=1024;s->progress_percent=50;s->speed_bytes_per_second=2048;return 1;}
void TitanNotifications_BeginDownloadActivity(const char*a,const char*b){(void)a;(void)b;}
void TitanNotifications_UpdateDownloadActivity(const char*a,unsigned int b,unsigned int c,unsigned int d,unsigned int e,unsigned int f,unsigned int g,unsigned int h){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;}
void TitanNotifications_CompleteDownloadActivity(const char*a,unsigned int b,unsigned int c){(void)a;(void)b;(void)c;}
void TitanNotifications_FailDownloadActivity(const char*a,unsigned int b,unsigned int c){(void)a;(void)b;(void)c;}

int main(void){
 const char repo[]="TITAN-REPOSITORY/1\nid=official\nname=Titan Official\n[package dep.core]\ntitle=Core\nversion=1.0\nplatform=ps2\narchitecture=mips-r5900\nurl=https://example.invalid/core.tpkg\nsha256=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\nsize=123\n[package demo.app]\ntitle=Demo App\nversion=1.2\nplatform=ps2\narchitecture=mips-r5900\nurl=https://example.invalid/demo.tpkg\nsha256=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\nsize=1024\ndependencies=dep.core\n";
 unsigned int id=0; const TitanRepositoryCatalog*c;
 TitanStoreService_Initialize();
 assert(TitanStoreService_AcceptRepository("https://repo.example/catalog.trepo",repo,sizeof(repo)-1)==TITAN_STORE_OK);
 c=TitanStoreService_GetCatalog();assert(c&&c->package_count==2);assert(strcmp(TitanStoreService_GetRepositoryUrl(),"https://repo.example/catalog.trepo")==0);
 assert(TitanStoreService_QueuePackage("demo.app",&id)==TITAN_STORE_OK); assert(id==g_job);
 assert(strcmp(g_req.package_id,"demo.app")==0);assert(g_req.expected_size==1024);assert(strcmp(g_req.expected_sha256,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb")==0);
 assert(strcmp(g_req.destination,"mass:/TITAN/packages/demo.app-1.2.tpkg")==0);
 TitanStoreService_Update(); TitanStoreService_Shutdown();
 puts("buildfix29-store-foundation-host: OK");return 0;
}
