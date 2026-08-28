#include "titan_app_sdk.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int titan_app_init(const TitanAppApiV1 *, const char *);
void titan_app_update(const TitanAppInput *, uint32_t);
void titan_app_shutdown(void);
uint32_t wle_test_recursive_active(void);
uint32_t wle_test_recursive_delete_phase(void);
int wle_test_start_recursive(const char *, const char *, int);

static unsigned char heap[16384];
static unsigned commits, mkdir_calls, source_removes, delete_before_copy_complete;
static void request_exit(void){}
static uint64_t uptime_ms(void){return 0;}
static void *fake_alloc(uint32_t n){return n<=sizeof(heap)?heap:0;}
static void fake_release(void*p){(void)p;}
static void draw_text(float a,float b,float c,const char*d,uint8_t e,uint8_t f,uint8_t g,uint8_t h){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;}
static void draw_rect(float a,float b,float c,float d,uint8_t e,uint8_t f,uint8_t g,uint8_t h){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;}
static void draw_panel(float a,float b,float c,float d,uint8_t e){(void)a;(void)b;(void)c;(void)d;(void)e;}
static void draw_progress(float a,float b,float c,float d,uint32_t e,uint8_t f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;}
static int notify(TitanAppNotificationKind k,const char*t,const char*m){(void)k;(void)t;(void)m;return 1;}
static int media_present_image_file(const char*p){(void)p;return 0;}
static int fill(TitanAppStorageEntry *e,const char*n,uint32_t flags,uint64_t size){memset(e,0,sizeof(*e));strncpy(e->name,n,sizeof(e->name)-1u);e->flags=flags;e->size=size;return 1;}
static int storage_list(const char*p,uint32_t start,TitanAppStorageEntry*e,uint32_t cap){
    if(cap==0u)return 0;
    if(strcmp(p,"mass:/TREE")==0){if(start==0u)return fill(e,"A.TXT",0u,3u);if(start==1u)return fill(e,"SUB",TITAN_APP_STORAGE_ENTRY_DIRECTORY,0u);return 0;}
    if(strcmp(p,"mass:/TREE/SUB")==0){if(start==0u)return fill(e,"B.TXT",0u,4u);return 0;}
    return -1;
}
static int storage_stat(const char*p,TitanAppStorageEntry*e){(void)p;(void)e;return 0;}
static int storage_read(const char*p,uint64_t o,void*b,uint32_t c){
    const char *v=strcmp(p,"mass:/TREE/A.TXT")==0?"abc":strcmp(p,"mass:/TREE/SUB/B.TXT")==0?"WXYZ":0;
    uint32_t n=v?(uint32_t)strlen(v):0u;
    if(!v) return -1;
    if(o>=n) return 0;
    if(c>n-(uint32_t)o) c=n-(uint32_t)o;
    memcpy(b,v+(uint32_t)o,c);
    return (int)c;
}
static int storage_write(const char*p,uint64_t o,const void*b,uint32_t s,uint32_t f){(void)o;(void)b;(void)f;return strstr(p,".titantmp")?(int)s:-1;}
static int storage_mkdir(const char*p){(void)p;mkdir_calls++;return 1;}
static int storage_remove(const char*p){
    size_t n=strlen(p);if(n>=9u && strcmp(p+n-9u,".titantmp")==0)return 1;
    if(strncmp(p,"mass:/TREE",10u)==0){source_removes++;if(commits<2u)delete_before_copy_complete=1u;}
    return 1;
}
static int storage_rename(const char*a,const char*b){(void)a;(void)b;return 1;}
static int launch_elf(const char*p){(void)p;return -1;}
static int storage_list_volumes(uint32_t s,TitanAppStorageVolume*v,uint32_t c){(void)s;(void)v;(void)c;return 0;}
static int storage_commit_replace(const char*a,const char*b){(void)a;(void)b;commits++;return 1;}
static int text_input_open(const char*t,const char*i,uint32_t m){(void)t;(void)i;(void)m;return 0;}
static int text_input_poll(char*t,uint32_t c){(void)t;(void)c;return TITAN_APP_TEXT_INPUT_CANCELLED;}

static int vmc_list_active_root(uint32_t i,TitanAppVmcEntry*e){(void)i;(void)e;return 0;}
static int vmc_list_active_path(const char*p,uint32_t i,TitanAppVmcEntry*e){(void)p;(void)i;(void)e;return 0;}
static int vmc_read_active_file(const char*p,uint32_t o,void*b,uint32_t c){(void)p;(void)o;(void)b;(void)c;return -1;}

static void build_api(TitanAppApiV1 *api){
    memset(api,0,sizeof(*api));api->abi_version=TITAN_APP_API_VERSION;api->struct_size=sizeof(*api);
    api->request_exit=request_exit;api->uptime_ms=uptime_ms;api->alloc=fake_alloc;api->release=fake_release;
    api->draw_text=draw_text;api->draw_text_shadow=draw_text;api->draw_rect=draw_rect;api->draw_panel=draw_panel;api->draw_progress=draw_progress;api->notify=notify;
    api->storage_list=storage_list;api->storage_stat=storage_stat;api->storage_read=storage_read;api->storage_write=storage_write;api->storage_mkdir=storage_mkdir;api->storage_remove=storage_remove;api->storage_rename=storage_rename;api->launch_elf=launch_elf;
    api->storage_list_volumes=storage_list_volumes;api->storage_commit_replace=storage_commit_replace;api->text_input_open=text_input_open;api->text_input_poll=text_input_poll;api->media_present_image_file=media_present_image_file;api->vmc_list_active_root=vmc_list_active_root;api->vmc_list_active_path=vmc_list_active_path;api->vmc_read_active_file=vmc_read_active_file;
}
static int run_case(int move){
    TitanAppApiV1 api;TitanAppInput in;unsigned steps=0;memset(&in,0,sizeof(in));commits=mkdir_calls=source_removes=delete_before_copy_complete=0u;build_api(&api);
    if(titan_app_init(&api,"mass:/TITAN/APPS/TITNWLE01")!=0)return 10;
    if(!wle_test_start_recursive("mass:/TREE","mass:/DEST",move))return 11;
    while(wle_test_recursive_active() && steps++<64u)titan_app_update(&in,16u);
    if(wle_test_recursive_active() || commits!=2u || mkdir_calls!=2u)return 12;
    if(!move && source_removes!=0u)return 13;
    if(move && (source_removes!=4u || delete_before_copy_complete))return 14;
    if(move && wle_test_recursive_delete_phase()==0u)return 15;
    titan_app_shutdown();return 0;
}
int main(void){int r=run_case(0);if(r)return r;r=run_case(1);if(r)return r;puts("wlaunchelf-recursive-host: OK bounded DFS copy + two-phase move");return 0;}
