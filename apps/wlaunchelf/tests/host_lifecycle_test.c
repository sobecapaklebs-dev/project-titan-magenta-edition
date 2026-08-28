#include "titan_app_sdk.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int titan_app_init(const TitanAppApiV1 *, const char *);
void titan_app_update(const TitanAppInput *, uint32_t);
void titan_app_draw(void);
void titan_app_shutdown(void);
const char *wle_test_path(void);
uint32_t wle_test_entry_count(void);
uint32_t wle_test_viewer_active(void);
uint32_t wle_test_image_viewer_active(void);
uint32_t wle_test_viewer_size(void);
uint32_t wle_test_vmc_view_active(void);
const char *wle_test_viewer_path(void);
const char *wle_test_vmc_path(void);
uint32_t wle_test_vmc_export_active(void);
const char *wle_test_vmc_export_destination(void);

static int exit_requested;
static unsigned int text_draw_calls;
static unsigned int panel_draw_calls;
static int text_input_mode;
static int write_calls;
static int commit_calls;
static int direct_final_write;
static char committed_from[256];
static char committed_to[256];
static int pfs_mounted;
static int pfs_mount_calls;
static int pfs_unmount_calls;
static void request_exit(void){ exit_requested=1; }
static uint64_t uptime_ms(void){ return 0; }
static void *fake_alloc(uint32_t n){ static unsigned char b[16384]; return n<=sizeof(b)?b:0; }
static void fake_release(void *p){ (void)p; }
static void draw_text(float a,float b,float c,const char*d,uint8_t e,uint8_t f,uint8_t g,uint8_t h){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;text_draw_calls++;}
static void draw_rect(float a,float b,float c,float d,uint8_t e,uint8_t f,uint8_t g,uint8_t h){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;}
static void draw_panel(float a,float b,float c,float d,uint8_t e){(void)a;(void)b;(void)c;(void)d;(void)e;panel_draw_calls++;}
static void draw_progress(float a,float b,float c,float d,uint32_t e,uint8_t f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;}
static int notify(TitanAppNotificationKind k,const char*t,const char*m){(void)k;(void)t;(void)m;return 1;}
static void media_draw(float x,float y,float w,float h,uint8_t a){(void)x;(void)y;(void)w;(void)h;(void)a;}
static int media_present_image_file(const char*p){return strcmp(p,"mass:/PHOTO.JPG")==0;}
static int vmc_list_active_root(uint32_t index,TitanAppVmcEntry *e){
    if(e==0) return -1;
    memset(e,0,sizeof(*e));
    if(index==0u){ strcpy(e->name,"BASLUS-TESTSAVE"); e->size=2u; e->flags=TITAN_APP_VMC_ENTRY_DIRECTORY|TITAN_APP_VMC_ENTRY_READABLE; return 1; }
    if(index==1u){ strcpy(e->name,"icon.sys"); e->size=12345u; e->flags=TITAN_APP_VMC_ENTRY_READABLE; return 1; }
    return 0;
}
static int vmc_list_active_path(const char *path,uint32_t index,TitanAppVmcEntry *e){
    if(path==0 || e==0) return -1;
    memset(e,0,sizeof(*e));
    if(path[0]=='\0') return vmc_list_active_root(index,e);
    if(strcmp(path,"BASLUS-TESTSAVE")!=0) return -1;
    if(index==0u){ strcpy(e->name,"DATA.BIN"); e->size=6u; e->flags=TITAN_APP_VMC_ENTRY_READABLE; return 1; }
    return 0;
}
static int vmc_read_active_file(const char *path,uint32_t off,void *buf,uint32_t cap){
    static const unsigned char data[6]={'A','B','C','D','E','F'};
    uint32_t n;
    if(path==0 || buf==0 || strcmp(path,"BASLUS-TESTSAVE/DATA.BIN")!=0) return -1;
    if(off>=6u) return 0;
    n=6u-off; if(n>cap) n=cap; memcpy(buf,data+off,n); return (int)n;
}
static int storage_list(const char *p,uint32_t start,TitanAppStorageEntry *e,uint32_t cap){
    (void)start;
    if(strcmp(p,"mass:/")!=0 || cap<4) return -1;
    memset(e,0,sizeof(*e)*4);
    strcpy(e[0].name,"APPS"); e[0].flags=TITAN_APP_STORAGE_ENTRY_DIRECTORY;
    strcpy(e[1].name,"BOOT.ELF"); e[1].size=1234;
    strcpy(e[2].name,"README.TXT"); e[2].size=18;
    strcpy(e[3].name,"PHOTO.JPG"); e[3].size=4096;
    return 4;
}
static int storage_stat(const char*p,TitanAppStorageEntry*e){(void)p;(void)e;return 1;}
static int storage_read(const char*p,uint64_t o,void*b,uint32_t c){
    static const char sample[]="Titan text\nLine 2\n";
    uint32_t n=(uint32_t)(sizeof(sample)-1u);
    if(strcmp(p,"mass:/README.TXT")!=0 || o!=0u || c<n) return -1;
    memcpy(b,sample,n); return (int)n;
}
static int storage_write(const char*p,uint64_t o,const void*b,uint32_t s,uint32_t f){
    (void)o;(void)b;(void)f; write_calls++;
    if(strcmp(p,"mass:/README.TXT")==0) direct_final_write=1;
    return (int)s;
}
static int storage_mkdir(const char*p){(void)p;return 1;}
static int storage_remove(const char*p){(void)p;return 1;}
static int storage_rename(const char*a,const char*b){(void)a;(void)b;return 1;}
static int launch_elf(const char*p){(void)p;return -3;}

static int storage_list_volumes(uint32_t start,TitanAppStorageVolume *v,uint32_t cap){
    static const char *roots[6]={"mass:/","mc0:/","mc1:/","cdfs:/","hdd0:","pfs0:/"};
    uint32_t total=pfs_mounted?6u:5u,i,n=0;
    for(i=start;i<total && n<cap;i++,n++){ memset(&v[n],0,sizeof(v[n])); strcpy(v[n].root,roots[i]); v[n].flags=TITAN_APP_STORAGE_VOLUME_ONLINE|TITAN_APP_STORAGE_VOLUME_READ|((i==3u||i==4u)?TITAN_APP_STORAGE_VOLUME_READ_ONLY:TITAN_APP_STORAGE_VOLUME_WRITE); }
    return (int)n;
}
static int storage_commit_replace(const char*a,const char*b){
    commit_calls++; strncpy(committed_from,a,sizeof(committed_from)-1u); strncpy(committed_to,b,sizeof(committed_to)-1u); return 1;
}
static int pfs_available(void){ return 1; }
static int pfs_mount(const char *source,uint32_t slot,uint32_t caps,TitanAppPfsMountStatus *st){
    if(!source||strcmp(source,"hdd0:+TITAN")!=0||slot!=0u||caps!=(TITAN_APP_PFS_CAP_READ|TITAN_APP_PFS_CAP_WRITE)||!st) return 0;
    pfs_mounted=1; pfs_mount_calls++; memset(st,0,sizeof(*st)); st->slot=0; st->state=TITAN_APP_PFS_STATE_ONLINE; st->capabilities=caps; strcpy(st->root,"pfs0:/"); return 1;
}
static int pfs_unmount(uint32_t slot){ if(slot!=0u||!pfs_mounted)return 0; pfs_mounted=0; pfs_unmount_calls++; return 1; }
static int pfs_status(uint32_t slot,TitanAppPfsMountStatus *st){ if(slot>3u||!st)return 0; if(slot!=0u||!pfs_mounted)return 0; memset(st,0,sizeof(*st)); st->slot=0; st->state=TITAN_APP_PFS_STATE_ONLINE; st->capabilities=TITAN_APP_PFS_CAP_READ|TITAN_APP_PFS_CAP_WRITE; strcpy(st->root,"pfs0:/"); return 1; }
static int text_input_open(const char*t,const char*i,uint32_t m){(void)i;(void)m;text_input_mode=(t&&strstr(t,"Montar PFS"))?3:1;return 1;}
static int text_input_poll(char*t,uint32_t c){
    if(text_input_mode==1){ const char *v="Edited safely\n"; strncpy(t,v,c-1u); t[c-1u]='\0'; text_input_mode=2; return TITAN_APP_TEXT_INPUT_CONFIRMED; }
    if(text_input_mode==3){ const char *v="hdd0:+TITAN"; strncpy(t,v,c-1u); t[c-1u]='\0'; text_input_mode=2; return TITAN_APP_TEXT_INPUT_CONFIRMED; }
    if(c) t[0]='\0';
    return TITAN_APP_TEXT_INPUT_CANCELLED;
}

int main(void){
    TitanAppApiV1 api;
    TitanAppInput in;
    memset(&api,0,sizeof(api)); memset(&in,0,sizeof(in));
    api.abi_version=TITAN_APP_API_VERSION; api.struct_size=sizeof(api);
    api.request_exit=request_exit; api.uptime_ms=uptime_ms; api.alloc=fake_alloc; api.release=fake_release;
    api.draw_text=draw_text; api.draw_text_shadow=draw_text; api.draw_rect=draw_rect; api.draw_panel=draw_panel; api.draw_progress=draw_progress; api.notify=notify; api.media_draw=media_draw;
    api.storage_list=storage_list; api.storage_stat=storage_stat; api.storage_read=storage_read; api.storage_write=storage_write;
    api.storage_mkdir=storage_mkdir; api.storage_remove=storage_remove; api.storage_rename=storage_rename; api.launch_elf=launch_elf;
    api.storage_list_volumes=storage_list_volumes; api.storage_commit_replace=storage_commit_replace;
    api.text_input_open=text_input_open; api.text_input_poll=text_input_poll; api.media_present_image_file=media_present_image_file;
    api.vmc_list_active_root=vmc_list_active_root; api.vmc_list_active_path=vmc_list_active_path; api.vmc_read_active_file=vmc_read_active_file;
    api.pfs_available=pfs_available; api.pfs_mount=pfs_mount; api.pfs_unmount=pfs_unmount; api.pfs_status=pfs_status;
    if(titan_app_init(&api,"mass:/TITAN/APPS/TITNWLE01")!=0) return 1;
    titan_app_update(&in,16);
    if(wle_test_entry_count()!=5 || strcmp(wle_test_path(),"")!=0) return 2;
    in.pressed=TITAN_APP_BUTTON_R2; titan_app_update(&in,16); in.pressed=0; titan_app_update(&in,16);
    if(!wle_test_vmc_view_active() || wle_test_entry_count()!=2u || strcmp(wle_test_path(),"")!=0 || strcmp(wle_test_vmc_path(),"")!=0) return 12;
    in.pressed=TITAN_APP_BUTTON_CROSS; titan_app_update(&in,16); in.pressed=0; titan_app_update(&in,16);
    if(strcmp(wle_test_vmc_path(),"BASLUS-TESTSAVE")!=0 || wle_test_entry_count()!=1u) return 14;
    in.pressed=TITAN_APP_BUTTON_CIRCLE; titan_app_update(&in,16); in.pressed=0; titan_app_update(&in,16);
    if(strcmp(wle_test_vmc_path(),"")!=0 || wle_test_entry_count()!=2u) return 15;
    in.pressed=TITAN_APP_BUTTON_R2; titan_app_update(&in,16); in.pressed=0; titan_app_update(&in,16);
    if(wle_test_vmc_view_active() || wle_test_entry_count()!=5u) return 13;
    in.pressed=TITAN_APP_BUTTON_CROSS; titan_app_update(&in,16); in.pressed=0; titan_app_update(&in,16);
    if(strcmp(wle_test_path(),"mass:/")!=0 || wle_test_entry_count()!=4) return 3;
    in.pressed=TITAN_APP_BUTTON_DOWN; titan_app_update(&in,16);
    in.pressed=TITAN_APP_BUTTON_DOWN; titan_app_update(&in,16);
    in.pressed=TITAN_APP_BUTTON_CROSS; titan_app_update(&in,16); in.pressed=0; titan_app_update(&in,16);
    if(!wle_test_viewer_active() || wle_test_viewer_size()!=18u || strcmp(wle_test_viewer_path(),"mass:/README.TXT")!=0) return 7;
    titan_app_draw();
    in.pressed=TITAN_APP_BUTTON_SQUARE; titan_app_update(&in,16); in.pressed=0; titan_app_update(&in,16);
    if(write_calls!=1 || commit_calls!=1 || direct_final_write || strcmp(committed_from,"mass:/README.TXT.titantmp")!=0 || strcmp(committed_to,"mass:/README.TXT")!=0) return 8;
    in.pressed=TITAN_APP_BUTTON_CIRCLE; titan_app_update(&in,16); in.pressed=0; titan_app_update(&in,16);
    if(wle_test_viewer_active()) return 9;
    in.pressed=TITAN_APP_BUTTON_DOWN; titan_app_update(&in,16);
    in.pressed=TITAN_APP_BUTTON_CROSS; titan_app_update(&in,16); in.pressed=0; titan_app_update(&in,16);
    if(!wle_test_image_viewer_active()) return 10;
    titan_app_draw();
    in.pressed=TITAN_APP_BUTTON_CIRCLE; titan_app_update(&in,16); in.pressed=0; titan_app_update(&in,16);
    if(wle_test_image_viewer_active()) return 11;
    /* Export nested VMC file into the currently open mass:/ directory. */
    in.pressed=TITAN_APP_BUTTON_R2; titan_app_update(&in,16); in.pressed=0; titan_app_update(&in,16);
    if(!wle_test_vmc_view_active() || wle_test_entry_count()!=2u) return 16;
    in.pressed=TITAN_APP_BUTTON_CROSS; titan_app_update(&in,16); in.pressed=0; titan_app_update(&in,16);
    if(strcmp(wle_test_vmc_path(),"BASLUS-TESTSAVE")!=0 || wle_test_entry_count()!=1u) return 17;
    write_calls=0; commit_calls=0; direct_final_write=0; committed_from[0]='\0'; committed_to[0]='\0';
    in.pressed=TITAN_APP_BUTTON_SQUARE; titan_app_update(&in,16); in.pressed=0;
    if(!wle_test_vmc_export_active()) return 18;
    titan_app_update(&in,16);
    titan_app_update(&in,16);
    if(wle_test_vmc_export_active() || write_calls!=1 || commit_calls!=1 || direct_final_write) return 19;
    if(strcmp(committed_from,"mass:/DATA.BIN.titantmp")!=0 || strcmp(committed_to,"mass:/DATA.BIN")!=0) return 20;
    if(strcmp(wle_test_vmc_export_destination(),"mass:/DATA.BIN")!=0) return 21;
    in.pressed=TITAN_APP_BUTTON_R2; titan_app_update(&in,16); in.pressed=0; titan_app_update(&in,16);
    in.pressed=TITAN_APP_BUTTON_CIRCLE; titan_app_update(&in,16); in.pressed=0; titan_app_update(&in,16);
    if(strcmp(wle_test_path(),"")!=0) return 4;
    /* R1 mounts via Titan PFS ABI and root refresh advertises pfs0 only after success. */
    in.pressed=TITAN_APP_BUTTON_R1; titan_app_update(&in,16); in.pressed=0; titan_app_update(&in,16); titan_app_update(&in,16);
    if(pfs_mount_calls!=1 || !pfs_mounted || wle_test_entry_count()!=6u) return 22;
    /* Select appended pfs0:/ and R1 unmounts only the session-owned slot. */
    in.pressed=TITAN_APP_BUTTON_UP; titan_app_update(&in,16); in.pressed=0;
    in.pressed=TITAN_APP_BUTTON_R1; titan_app_update(&in,16); in.pressed=0; titan_app_update(&in,16);
    if(pfs_unmount_calls!=1 || pfs_mounted || wle_test_entry_count()!=5u) return 23;
    in.pressed=TITAN_APP_BUTTON_CIRCLE; titan_app_update(&in,16);
    if(!exit_requested) return 5;
    titan_app_draw();
    if(panel_draw_calls == 0u || text_draw_calls < 3u) return 6;
    titan_app_shutdown();
    puts("wlaunchelf-titan-host-lifecycle: OK draw-visible");
    return 0;
}
