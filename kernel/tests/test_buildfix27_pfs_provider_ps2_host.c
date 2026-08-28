#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "titan_storage_mount.h"
#include "titan_storage_pfs_provider_ps2.h"
static int ready=1,mounts,unmounts;
int TitanPlatformPfs_Available(void){ return ready; }
int TitanPlatformPfs_Mount(const char *s,const char *r,uint32_t caps){ mounts++; return caps==(TITAN_STORAGE_MOUNT_CAP_READ|TITAN_STORAGE_MOUNT_CAP_WRITE) && strcmp(s,"hdd0:+TITAN")==0 && strcmp(r,"pfs0:/")==0; }
int TitanPlatformPfs_Unmount(const char *r){ unmounts++; return strcmp(r,"pfs0:/")==0; }
int main(void){ TitanStorageMountInfo m; TitanStorageMount_Initialize(); assert(TitanStoragePfsProviderPs2_RegisterIfAvailable()); assert(TitanStorageMount_PfsAvailable()); assert(TitanStorageMount_MountPfs("hdd0:+TITAN",0,TITAN_STORAGE_MOUNT_CAP_READ|TITAN_STORAGE_MOUNT_CAP_WRITE,&m)); assert(mounts==1 && m.state==TITAN_STORAGE_MOUNT_ONLINE); assert(TitanStorageMount_Unmount(0)); assert(unmounts==1); puts("buildfix27-pfs-provider-ps2-host: OK weak platform hook adapter"); return 0; }
