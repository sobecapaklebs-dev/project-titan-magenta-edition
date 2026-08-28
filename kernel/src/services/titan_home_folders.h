#ifndef PROJECT_TITAN_HOME_FOLDERS_H
#define PROJECT_TITAN_HOME_FOLDERS_H

#include "titan_neutrino.h"

#include <stdint.h>

#define TITAN_HOME_FOLDER_CAPACITY 16u
#define TITAN_HOME_FOLDER_ITEM_CAPACITY 128u
#define TITAN_HOME_FOLDER_NAME_CAPACITY 64u

typedef uint64_t TitanHomeGameKey;

typedef enum TitanHomeFoldersStatus
{
    TITAN_HOME_FOLDERS_NOT_INITIALIZED = 0,
    TITAN_HOME_FOLDERS_READY,
    TITAN_HOME_FOLDERS_READY_EMPTY,
    TITAN_HOME_FOLDERS_IO_ERROR,
    TITAN_HOME_FOLDERS_CORRUPT,
    TITAN_HOME_FOLDERS_FUTURE_VERSION
} TitanHomeFoldersStatus;

typedef struct TitanHomeFolder
{
    char name[TITAN_HOME_FOLDER_NAME_CAPACITY];
    unsigned int item_count;
    TitanHomeGameKey item_keys[TITAN_HOME_FOLDER_ITEM_CAPACITY];
} TitanHomeFolder;

void TitanHomeFolders_Initialize(void);
TitanHomeFoldersStatus TitanHomeFolders_GetStatus(void);
unsigned int TitanHomeFolders_GetCount(void);
const TitanHomeFolder *TitanHomeFolders_Get(unsigned int index);

TitanHomeGameKey TitanHomeFolders_MakeGameKey(const char *path);
TitanHomeGameKey TitanHomeFolders_MakeCatalogGameKey(
    const TitanNeutrinoGame *game
);
TitanHomeGameKey TitanHomeFolders_MakeShellKey(unsigned int page);
int TitanHomeFolders_FindForItemKey(TitanHomeGameKey key);
int TitanHomeFolders_ContainsItemKey(unsigned int folder_index, TitanHomeGameKey key);
int TitanHomeFolders_AddItemKey(unsigned int folder_index, TitanHomeGameKey key);
int TitanHomeFolders_RemoveItemKey(unsigned int folder_index, TitanHomeGameKey key);
int TitanHomeFolders_FindForGameKey(TitanHomeGameKey key);
int TitanHomeFolders_FindForGamePath(const char *path);
int TitanHomeFolders_ContainsGameKey(unsigned int folder_index, TitanHomeGameKey key);
int TitanHomeFolders_ContainsGamePath(unsigned int folder_index, const char *path);

int TitanHomeFolders_CreateKeys(
    const char *name,
    const TitanHomeGameKey *keys,
    unsigned int key_count,
    unsigned int *created_index
);
int TitanHomeFolders_UpdateKeys(
    unsigned int folder_index,
    const char *name,
    const TitanHomeGameKey *keys,
    unsigned int key_count
);
int TitanHomeFolders_Create(
    const char *name,
    const char *const *paths,
    unsigned int path_count,
    unsigned int *created_index
);
int TitanHomeFolders_Update(
    unsigned int folder_index,
    const char *name,
    const char *const *paths,
    unsigned int path_count
);
int TitanHomeFolders_AddGamePath(unsigned int folder_index, const char *path);
int TitanHomeFolders_RemoveGamePath(unsigned int folder_index, const char *path);
int TitanHomeFolders_Delete(unsigned int folder_index);
int TitanHomeFolders_Save(void);

#endif
