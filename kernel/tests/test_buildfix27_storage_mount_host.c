#include "titan_storage_mount.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct FakeProvider { int ready, mount_ok, unmount_ok, mounts, unmounts; } FakeProvider;
static int avail(void *c){return ((FakeProvider*)c)->ready;}
static int mount_pfs(void *c,const char*s,const char*r,uint32_t caps){FakeProvider*f=c;f->mounts++;assert(strcmp(s,"hdd0:+TITAN")==0);assert(strcmp(r,"pfs1:/")==0);assert((caps&TITAN_STORAGE_MOUNT_CAP_READ)!=0u);return f->mount_ok;}
static int unmount_pfs(void *c,const char*r){FakeProvider*f=c;f->unmounts++;assert(strcmp(r,"pfs1:/")==0);return f->unmount_ok;}
int main(void){
 FakeProvider f={0,1,1,0,0}; TitanStoragePfsProvider p={avail,mount_pfs,unmount_pfs}; TitanStorageMountInfo a,b;
 TitanStorageMount_Initialize(); assert(!TitanStorageMount_PfsAvailable());
 assert(!TitanStorageMount_MountPfs("hdd0:+TITAN",1,TITAN_STORAGE_MOUNT_CAP_READ|TITAN_STORAGE_MOUNT_CAP_WRITE,&a));
 assert(TitanStorageMount_RegisterPfsProvider(&p,&f)); assert(!TitanStorageMount_PfsAvailable());
 f.ready=1; assert(TitanStorageMount_PfsAvailable());
 assert(!TitanStorageMount_MountPfs("hdd0:../BAD",1,TITAN_STORAGE_MOUNT_CAP_READ,&a));
 assert(TitanStorageMount_MountPfs("hdd0:+TITAN",1,TITAN_STORAGE_MOUNT_CAP_READ|TITAN_STORAGE_MOUNT_CAP_WRITE,&a));
 assert(a.state==TITAN_STORAGE_MOUNT_ONLINE && a.slot==1u && strcmp(a.root,"pfs1:/")==0);
 assert((a.capabilities&TITAN_STORAGE_MOUNT_CAP_WRITE)!=0u && f.mounts==1);
 assert(!TitanStorageMount_MountPfs("hdd0:+TITAN",1,TITAN_STORAGE_MOUNT_CAP_READ,&b));
 assert(TitanStorageMount_Get(1,&b) && b.generation==a.generation);
 assert(TitanStorageMount_List(0,&b) && b.slot==1u);
 assert(TitanStorageMount_Unmount(1) && f.unmounts==1);
 assert(TitanStorageMount_Get(1,&b) && b.state==TITAN_STORAGE_MOUNT_OFFLINE && b.generation>a.generation);
 assert(!TitanStorageMount_List(0,&b));
 f.mount_ok=0; assert(!TitanStorageMount_MountPfs("hdd0:+TITAN",1,TITAN_STORAGE_MOUNT_CAP_READ,&b));
 assert(TitanStorageMount_Get(1,&b) && b.state==TITAN_STORAGE_MOUNT_ERROR);
 puts("buildfix27-storage-mount-host: OK provider-gated PFS lifecycle/generation/rollback"); return 0;
}
