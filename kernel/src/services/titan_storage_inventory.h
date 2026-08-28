#ifndef PROJECT_TITAN_STORAGE_INVENTORY_H
#define PROJECT_TITAN_STORAGE_INVENTORY_H

#include <stdint.h>

#define TITAN_STORAGE_INVENTORY_MAX_ENTRIES 80u
#define TITAN_STORAGE_INVENTORY_NAME_CAPACITY 80u
#define TITAN_STORAGE_INVENTORY_PATH_CAPACITY 256u

typedef enum TitanStorageInventoryKind
{
    TITAN_STORAGE_INVENTORY_APPLICATION = 0,
    TITAN_STORAGE_INVENTORY_NATIVE_GAME,
    TITAN_STORAGE_INVENTORY_THEME
} TitanStorageInventoryKind;

typedef struct TitanStorageInventoryEntry
{
    TitanStorageInventoryKind kind;
    unsigned int source_index;
    uint64_t bytes;
    int bytes_valid;
    char name[TITAN_STORAGE_INVENTORY_NAME_CAPACITY];
    char path[TITAN_STORAGE_INVENTORY_PATH_CAPACITY];
} TitanStorageInventoryEntry;

typedef struct TitanStorageInventorySnapshot
{
    unsigned int generation;
    unsigned int count;
    unsigned int application_count;
    unsigned int native_game_count;
    unsigned int theme_count;
    uint64_t titan_bytes;
    uint64_t application_bytes;
    uint64_t native_game_bytes;
    uint64_t theme_bytes;
    int titan_bytes_valid;
    int scanning;
    unsigned int scan_index;
    unsigned int scan_errors;
} TitanStorageInventorySnapshot;

void TitanStorageInventory_Initialize(void);
void TitanStorageInventory_RequestRefresh(void);
const TitanStorageInventorySnapshot *TitanStorageInventory_GetSnapshot(void);
const TitanStorageInventoryEntry *TitanStorageInventory_GetEntry(unsigned int index);
unsigned int TitanStorageInventory_GetKindCount(TitanStorageInventoryKind kind);
const TitanStorageInventoryEntry *TitanStorageInventory_GetKindEntry(
    TitanStorageInventoryKind kind,
    unsigned int visible_index
);
int TitanStorageInventory_HasStorageWork(void);
int TitanStorageInventory_StorageWorkerPump(void);

#endif
