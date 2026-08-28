#ifndef PROJECT_TITAN_STORAGE_MOUNT_H
#define PROJECT_TITAN_STORAGE_MOUNT_H

#include <stdint.h>

#define TITAN_STORAGE_MOUNT_MAX_SLOTS 4u
#define TITAN_STORAGE_MOUNT_SOURCE_CAPACITY 128u
#define TITAN_STORAGE_MOUNT_ROOT_CAPACITY 16u

#define TITAN_STORAGE_MOUNT_CAP_READ  0x00000001u
#define TITAN_STORAGE_MOUNT_CAP_WRITE 0x00000002u

typedef enum TitanStorageMountState
{
    TITAN_STORAGE_MOUNT_OFFLINE = 0,
    TITAN_STORAGE_MOUNT_MOUNTING,
    TITAN_STORAGE_MOUNT_ONLINE,
    TITAN_STORAGE_MOUNT_UNMOUNTING,
    TITAN_STORAGE_MOUNT_ERROR
} TitanStorageMountState;

typedef struct TitanStorageMountInfo
{
    uint32_t slot;
    uint32_t generation;
    uint32_t capabilities;
    TitanStorageMountState state;
    char source[TITAN_STORAGE_MOUNT_SOURCE_CAPACITY];
    char root[TITAN_STORAGE_MOUNT_ROOT_CAPACITY];
} TitanStorageMountInfo;

typedef struct TitanStoragePfsProvider
{
    int (*available)(void *context);
    int (*mount)(void *context, const char *source, const char *root, uint32_t capabilities);
    int (*unmount)(void *context, const char *root);
} TitanStoragePfsProvider;

void TitanStorageMount_Initialize(void);
int TitanStorageMount_RegisterPfsProvider(const TitanStoragePfsProvider *provider, void *context);
int TitanStorageMount_PfsAvailable(void);
int TitanStorageMount_MountPfs(const char *source, uint32_t requested_slot, uint32_t capabilities, TitanStorageMountInfo *out);
int TitanStorageMount_Unmount(uint32_t slot);
int TitanStorageMount_Get(uint32_t slot, TitanStorageMountInfo *out);
int TitanStorageMount_List(uint32_t index, TitanStorageMountInfo *out);

#endif
