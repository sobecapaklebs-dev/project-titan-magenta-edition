#include "titan_home_folders.h"

#include "titan_storage_io.h"
#include "titan_storage_paths.h"

#include <ctype.h>
#include <stdint.h>
#include <string.h>

#define TITAN_HOME_FOLDERS_PATH TITAN_STORAGE_DEVICE_ROOT "/HOMEFOLD.DAT"
#define TITAN_HOME_FOLDERS_TEMP_PATH TITAN_STORAGE_DEVICE_ROOT "/HOMEFOLD.TMP"
#define TITAN_HOME_FOLDERS_BACKUP_PATH TITAN_STORAGE_DEVICE_ROOT "/HOMEFOLD.BAK"

#define TITAN_HOME_FOLDERS_VERSION 1u
#define TITAN_HOME_FOLDERS_HEADER_SIZE 20u
#define TITAN_HOME_FOLDERS_RECORD_SIZE \
    (TITAN_HOME_FOLDER_NAME_CAPACITY + 4u + \
     (TITAN_HOME_FOLDER_ITEM_CAPACITY * 8u))

static const unsigned char g_folder_magic[4] = {'T', 'H', 'F', '1'};
static TitanHomeFolder g_folders[TITAN_HOME_FOLDER_CAPACITY];
static TitanHomeFolder g_load_folders[TITAN_HOME_FOLDER_CAPACITY];
static unsigned char g_record_buffer[TITAN_HOME_FOLDERS_RECORD_SIZE];
static unsigned int g_folder_count;
static TitanHomeFoldersStatus g_status = TITAN_HOME_FOLDERS_NOT_INITIALIZED;

static void TitanHomeFolders_WriteU32Le(unsigned char *destination, uint32_t value)
{
    destination[0] = (unsigned char)(value & 0xffu);
    destination[1] = (unsigned char)((value >> 8) & 0xffu);
    destination[2] = (unsigned char)((value >> 16) & 0xffu);
    destination[3] = (unsigned char)((value >> 24) & 0xffu);
}

static uint32_t TitanHomeFolders_ReadU32Le(const unsigned char *source)
{
    return (uint32_t)source[0] |
        ((uint32_t)source[1] << 8) |
        ((uint32_t)source[2] << 16) |
        ((uint32_t)source[3] << 24);
}

static void TitanHomeFolders_WriteU64Le(unsigned char *destination, uint64_t value)
{
    unsigned int index;

    for (index = 0u; index < 8u; index++)
    {
        destination[index] = (unsigned char)((value >> (index * 8u)) & 0xffu);
    }
}

static uint64_t TitanHomeFolders_ReadU64Le(const unsigned char *source)
{
    uint64_t value;
    unsigned int index;

    value = 0u;
    for (index = 0u; index < 8u; index++)
    {
        value |= ((uint64_t)source[index]) << (index * 8u);
    }
    return value;
}

static uint32_t TitanHomeFolders_ChecksumUpdate(
    uint32_t checksum,
    const void *data,
    unsigned int size
)
{
    const unsigned char *bytes;
    unsigned int index;

    bytes = (const unsigned char *)data;
    for (index = 0u; index < size; index++)
    {
        checksum ^= bytes[index];
        checksum *= 16777619u;
    }
    return checksum;
}

static void TitanHomeFolders_CopyString(
    char *destination,
    unsigned int capacity,
    const char *source
)
{
    unsigned int index;

    if (destination == 0 || capacity == 0u)
    {
        return;
    }

    destination[0] = '\0';
    if (source == 0)
    {
        return;
    }

    index = 0u;
    while (index + 1u < capacity && source[index] != '\0')
    {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static int TitanHomeFolders_StringValid(const char *text, unsigned int capacity)
{
    return text != 0 && memchr(text, '\0', capacity) != 0;
}

TitanHomeGameKey TitanHomeFolders_MakeGameKey(const char *path)
{
    uint64_t hash;

    if (path == 0 || path[0] == '\0')
    {
        return 0u;
    }

    hash = UINT64_C(14695981039346656037);
    while (*path != '\0')
    {
        unsigned char byte;

        byte = (unsigned char)tolower((unsigned char)*path);
        hash ^= (uint64_t)byte;
        hash *= UINT64_C(1099511628211);
        path++;
    }

    return hash != 0u ? hash : UINT64_C(1);
}

TitanHomeGameKey TitanHomeFolders_MakeCatalogGameKey(
    const TitanNeutrinoGame *game
)
{
    static const char ul_prefix[] = "titan:ul:";
    uint64_t hash;
    const char *cursor;

    if (game == 0)
    {
        return 0u;
    }

    if (game->format != TITAN_NEUTRINO_GAME_USBEXTREME)
    {
        return TitanHomeFolders_MakeGameKey(game->path);
    }

    if (game->startup[0] == '\0')
    {
        return 0u;
    }

    /* Every USBExtreme record is physically stored in the same ul.cfg.
     * The startup ID is the stable per-game identity used by the Titan UL
     * Engine and remains valid when the same device is exposed as mass: or
     * mass0:. */
    hash = UINT64_C(14695981039346656037);
    cursor = ul_prefix;
    while (*cursor != '\0')
    {
        hash ^= (uint64_t)(unsigned char)*cursor++;
        hash *= UINT64_C(1099511628211);
    }
    cursor = game->startup;
    while (*cursor != '\0')
    {
        hash ^= (uint64_t)(unsigned char)tolower((unsigned char)*cursor++);
        hash *= UINT64_C(1099511628211);
    }

    return hash != 0u ? hash : UINT64_C(1);
}

TitanHomeGameKey TitanHomeFolders_MakeShellKey(unsigned int page)
{
    static const unsigned char prefix[] =
    {
        't', 'i', 't', 'a', 'n', ':', 's', 'h', 'e', 'l', 'l', ':'
    };
    uint64_t hash;
    unsigned int index;

    hash = UINT64_C(14695981039346656037);
    for (index = 0u; index < sizeof(prefix); index++)
    {
        hash ^= (uint64_t)prefix[index];
        hash *= UINT64_C(1099511628211);
    }

    for (index = 0u; index < 4u; index++)
    {
        hash ^= (uint64_t)((page >> (index * 8u)) & 0xffu);
        hash *= UINT64_C(1099511628211);
    }

    return hash != 0u ? hash : UINT64_C(1);
}

static int TitanHomeFolders_KeyAlreadyUsedExcept(
    TitanHomeGameKey key,
    int except_folder
)
{
    unsigned int folder_index;
    unsigned int item_index;

    if (key == 0u)
    {
        return 0;
    }

    for (folder_index = 0u; folder_index < g_folder_count; folder_index++)
    {
        if ((int)folder_index == except_folder)
        {
            continue;
        }

        for (item_index = 0u; item_index < g_folders[folder_index].item_count; item_index++)
        {
            if (g_folders[folder_index].item_keys[item_index] == key)
            {
                return 1;
            }
        }
    }
    return 0;
}

static int TitanHomeFolders_ValidateCandidate(
    const char *name,
    const TitanHomeGameKey *keys,
    unsigned int key_count,
    int except_folder
)
{
    unsigned int left;
    unsigned int right;

    if (
        name == 0 || name[0] == '\0' ||
        strlen(name) >= TITAN_HOME_FOLDER_NAME_CAPACITY ||
        keys == 0 || key_count == 0u ||
        key_count > TITAN_HOME_FOLDER_ITEM_CAPACITY
    )
    {
        return 0;
    }

    for (left = 0u; left < key_count; left++)
    {
        if (
            keys[left] == 0u ||
            TitanHomeFolders_KeyAlreadyUsedExcept(keys[left], except_folder)
        )
        {
            return 0;
        }

        for (right = left + 1u; right < key_count; right++)
        {
            if (keys[left] == keys[right])
            {
                return 0;
            }
        }
    }
    return 1;
}

static void TitanHomeFolders_Assign(
    TitanHomeFolder *folder,
    const char *name,
    const TitanHomeGameKey *keys,
    unsigned int key_count
)
{
    memset(folder, 0, sizeof(*folder));
    TitanHomeFolders_CopyString(folder->name, sizeof(folder->name), name);
    folder->item_count = key_count;
    memcpy(folder->item_keys, keys, key_count * sizeof(keys[0]));
}

static void TitanHomeFolders_BuildRecord(
    unsigned char record[TITAN_HOME_FOLDERS_RECORD_SIZE],
    const TitanHomeFolder *folder
)
{
    unsigned int item_index;
    unsigned int offset;

    memset(record, 0, TITAN_HOME_FOLDERS_RECORD_SIZE);
    memcpy(record, folder->name, sizeof(folder->name));
    TitanHomeFolders_WriteU32Le(
        record + TITAN_HOME_FOLDER_NAME_CAPACITY,
        folder->item_count
    );

    offset = TITAN_HOME_FOLDER_NAME_CAPACITY + 4u;
    for (item_index = 0u; item_index < TITAN_HOME_FOLDER_ITEM_CAPACITY; item_index++)
    {
        TitanHomeFolders_WriteU64Le(
            record + offset + (item_index * 8u),
            folder->item_keys[item_index]
        );
    }
}

static int TitanHomeFolders_ReadFile(
    const char *path,
    TitanHomeFoldersStatus *status
)
{
    unsigned char header[TITAN_HOME_FOLDERS_HEADER_SIZE];
    TitanHomeFolder *loaded;
    TitanStorageIoSize file_size;
    uint32_t version;
    uint32_t count;
    uint32_t expected_checksum;
    uint32_t checksum;
    uint32_t expected_size;
    unsigned int folder_index;
    int descriptor;

    loaded = g_load_folders;

    if (!TitanStorageIo_PathInfo64(path, 0, &file_size))
    {
        *status = TITAN_HOME_FOLDERS_READY_EMPTY;
        return 0;
    }

    descriptor = TitanStorageIo_Open(path, TITAN_STORAGE_IO_READ);
    if (descriptor < 0)
    {
        *status = TITAN_HOME_FOLDERS_IO_ERROR;
        return -1;
    }

    if (!TitanStorageIo_ReadExact(descriptor, header, sizeof(header)))
    {
        TitanStorageIo_Close(descriptor);
        *status = TITAN_HOME_FOLDERS_CORRUPT;
        return -1;
    }

    if (memcmp(header, g_folder_magic, sizeof(g_folder_magic)) != 0)
    {
        TitanStorageIo_Close(descriptor);
        *status = TITAN_HOME_FOLDERS_CORRUPT;
        return -1;
    }

    version = TitanHomeFolders_ReadU32Le(header + 4u);
    count = TitanHomeFolders_ReadU32Le(header + 8u);
    expected_checksum = TitanHomeFolders_ReadU32Le(header + 12u);
    expected_size = TitanHomeFolders_ReadU32Le(header + 16u);

    if (version > TITAN_HOME_FOLDERS_VERSION)
    {
        TitanStorageIo_Close(descriptor);
        *status = TITAN_HOME_FOLDERS_FUTURE_VERSION;
        return -1;
    }

    if (
        version != TITAN_HOME_FOLDERS_VERSION ||
        count > TITAN_HOME_FOLDER_CAPACITY ||
        expected_size != TITAN_HOME_FOLDERS_HEADER_SIZE +
            (count * TITAN_HOME_FOLDERS_RECORD_SIZE) ||
        file_size != (TitanStorageIoSize)expected_size
    )
    {
        TitanStorageIo_Close(descriptor);
        *status = TITAN_HOME_FOLDERS_CORRUPT;
        return -1;
    }

    memset(loaded, 0, sizeof(g_load_folders));
    checksum = 2166136261u;

    for (folder_index = 0u; folder_index < count; folder_index++)
    {
        unsigned char *record;
        unsigned int item_index;
        unsigned int item_count;
        unsigned int offset;

        record = g_record_buffer;
        if (!TitanStorageIo_ReadExact(
            descriptor,
            record,
            TITAN_HOME_FOLDERS_RECORD_SIZE
        ))
        {
            TitanStorageIo_Close(descriptor);
            *status = TITAN_HOME_FOLDERS_CORRUPT;
            return -1;
        }

        checksum = TitanHomeFolders_ChecksumUpdate(
            checksum,
            record,
            TITAN_HOME_FOLDERS_RECORD_SIZE
        );
        memcpy(loaded[folder_index].name, record, TITAN_HOME_FOLDER_NAME_CAPACITY);
        item_count = TitanHomeFolders_ReadU32Le(
            record + TITAN_HOME_FOLDER_NAME_CAPACITY
        );
        loaded[folder_index].item_count = item_count;

        if (
            !TitanHomeFolders_StringValid(
                loaded[folder_index].name,
                sizeof(loaded[folder_index].name)
            ) ||
            loaded[folder_index].name[0] == '\0' ||
            item_count == 0u ||
            item_count > TITAN_HOME_FOLDER_ITEM_CAPACITY
        )
        {
            TitanStorageIo_Close(descriptor);
            *status = TITAN_HOME_FOLDERS_CORRUPT;
            return -1;
        }

        offset = TITAN_HOME_FOLDER_NAME_CAPACITY + 4u;
        for (item_index = 0u; item_index < TITAN_HOME_FOLDER_ITEM_CAPACITY; item_index++)
        {
            loaded[folder_index].item_keys[item_index] =
                TitanHomeFolders_ReadU64Le(
                    record + offset + (item_index * 8u)
                );

            if (
                item_index < item_count &&
                loaded[folder_index].item_keys[item_index] == 0u
            )
            {
                TitanStorageIo_Close(descriptor);
                *status = TITAN_HOME_FOLDERS_CORRUPT;
                return -1;
            }
        }
    }

    if (TitanStorageIo_Close(descriptor) < 0)
    {
        *status = TITAN_HOME_FOLDERS_IO_ERROR;
        return -1;
    }

    if (checksum != expected_checksum)
    {
        *status = TITAN_HOME_FOLDERS_CORRUPT;
        return -1;
    }

    memset(g_folders, 0, sizeof(g_folders));
    memcpy(g_folders, loaded, sizeof(g_folders));
    g_folder_count = count;
    *status = count == 0u
        ? TITAN_HOME_FOLDERS_READY_EMPTY
        : TITAN_HOME_FOLDERS_READY;
    return 1;
}

void TitanHomeFolders_Initialize(void)
{
    TitanHomeFoldersStatus primary_status;
    TitanHomeFoldersStatus backup_status;
    int result;

    memset(g_folders, 0, sizeof(g_folders));
    memset(g_load_folders, 0, sizeof(g_load_folders));
    g_folder_count = 0u;

    result = TitanHomeFolders_ReadFile(
        TITAN_HOME_FOLDERS_PATH,
        &primary_status
    );
    if (result > 0)
    {
        g_status = primary_status;
        return;
    }

    if (result == 0)
    {
        int backup_result;

        backup_result = TitanHomeFolders_ReadFile(
            TITAN_HOME_FOLDERS_BACKUP_PATH,
            &backup_status
        );
        if (backup_result > 0)
        {
            g_status = backup_status;
            (void)TitanHomeFolders_Save();
            return;
        }

        g_status = TITAN_HOME_FOLDERS_READY_EMPTY;
        return;
    }

    result = TitanHomeFolders_ReadFile(
        TITAN_HOME_FOLDERS_BACKUP_PATH,
        &backup_status
    );
    if (result > 0)
    {
        g_status = backup_status;
        (void)TitanHomeFolders_Save();
        return;
    }

    g_status = primary_status;
}

TitanHomeFoldersStatus TitanHomeFolders_GetStatus(void)
{
    return g_status;
}

unsigned int TitanHomeFolders_GetCount(void)
{
    return g_folder_count;
}

const TitanHomeFolder *TitanHomeFolders_Get(unsigned int index)
{
    if (index >= g_folder_count)
    {
        return 0;
    }
    return &g_folders[index];
}

int TitanHomeFolders_ContainsItemKey(
    unsigned int folder_index,
    TitanHomeGameKey key
)
{
    unsigned int item_index;

    if (folder_index >= g_folder_count || key == 0u)
    {
        return 0;
    }

    for (item_index = 0u; item_index < g_folders[folder_index].item_count; item_index++)
    {
        if (g_folders[folder_index].item_keys[item_index] == key)
        {
            return 1;
        }
    }
    return 0;
}

int TitanHomeFolders_ContainsGameKey(
    unsigned int folder_index,
    TitanHomeGameKey key
)
{
    return TitanHomeFolders_ContainsItemKey(folder_index, key);
}

int TitanHomeFolders_ContainsGamePath(unsigned int folder_index, const char *path)
{
    return TitanHomeFolders_ContainsItemKey(
        folder_index,
        TitanHomeFolders_MakeGameKey(path)
    );
}

int TitanHomeFolders_FindForItemKey(TitanHomeGameKey key)
{
    unsigned int folder_index;

    for (folder_index = 0u; folder_index < g_folder_count; folder_index++)
    {
        if (TitanHomeFolders_ContainsItemKey(folder_index, key))
        {
            return (int)folder_index;
        }
    }
    return -1;
}

int TitanHomeFolders_FindForGameKey(TitanHomeGameKey key)
{
    return TitanHomeFolders_FindForItemKey(key);
}

int TitanHomeFolders_FindForGamePath(const char *path)
{
    return TitanHomeFolders_FindForItemKey(
        TitanHomeFolders_MakeGameKey(path)
    );
}

int TitanHomeFolders_Save(void)
{
    unsigned char header[TITAN_HOME_FOLDERS_HEADER_SIZE];
    uint32_t checksum;
    uint32_t file_size;
    unsigned int folder_index;
    int descriptor;

    checksum = 2166136261u;
    for (folder_index = 0u; folder_index < g_folder_count; folder_index++)
    {
        TitanHomeFolders_BuildRecord(
            g_record_buffer,
            &g_folders[folder_index]
        );
        checksum = TitanHomeFolders_ChecksumUpdate(
            checksum,
            g_record_buffer,
            TITAN_HOME_FOLDERS_RECORD_SIZE
        );
    }

    file_size = TITAN_HOME_FOLDERS_HEADER_SIZE +
        (g_folder_count * TITAN_HOME_FOLDERS_RECORD_SIZE);
    memset(header, 0, sizeof(header));
    memcpy(header, g_folder_magic, sizeof(g_folder_magic));
    TitanHomeFolders_WriteU32Le(header + 4u, TITAN_HOME_FOLDERS_VERSION);
    TitanHomeFolders_WriteU32Le(header + 8u, g_folder_count);
    TitanHomeFolders_WriteU32Le(header + 12u, checksum);
    TitanHomeFolders_WriteU32Le(header + 16u, file_size);

    descriptor = TitanStorageIo_Open(
        TITAN_HOME_FOLDERS_TEMP_PATH,
        TITAN_STORAGE_IO_WRITE |
        TITAN_STORAGE_IO_CREATE |
        TITAN_STORAGE_IO_TRUNCATE
    );
    if (descriptor < 0)
    {
        g_status = TITAN_HOME_FOLDERS_IO_ERROR;
        return 0;
    }

    if (!TitanStorageIo_WriteExact(descriptor, header, sizeof(header)))
    {
        TitanStorageIo_Close(descriptor);
        TitanStorageIo_Remove(TITAN_HOME_FOLDERS_TEMP_PATH);
        g_status = TITAN_HOME_FOLDERS_IO_ERROR;
        return 0;
    }

    for (folder_index = 0u; folder_index < g_folder_count; folder_index++)
    {
        TitanHomeFolders_BuildRecord(
            g_record_buffer,
            &g_folders[folder_index]
        );
        if (!TitanStorageIo_WriteExact(
            descriptor,
            g_record_buffer,
            TITAN_HOME_FOLDERS_RECORD_SIZE
        ))
        {
            TitanStorageIo_Close(descriptor);
            TitanStorageIo_Remove(TITAN_HOME_FOLDERS_TEMP_PATH);
            g_status = TITAN_HOME_FOLDERS_IO_ERROR;
            return 0;
        }
    }

    if (
        TitanStorageIo_Flush(descriptor) < 0 ||
        TitanStorageIo_Close(descriptor) < 0
    )
    {
        TitanStorageIo_Remove(TITAN_HOME_FOLDERS_TEMP_PATH);
        g_status = TITAN_HOME_FOLDERS_IO_ERROR;
        return 0;
    }

    if (!TitanStorageIo_AtomicReplace(
        TITAN_HOME_FOLDERS_TEMP_PATH,
        TITAN_HOME_FOLDERS_PATH,
        TITAN_HOME_FOLDERS_BACKUP_PATH
    ))
    {
        TitanStorageIo_Remove(TITAN_HOME_FOLDERS_TEMP_PATH);
        g_status = TITAN_HOME_FOLDERS_IO_ERROR;
        return 0;
    }

    g_status = g_folder_count == 0u
        ? TITAN_HOME_FOLDERS_READY_EMPTY
        : TITAN_HOME_FOLDERS_READY;
    return 1;
}

int TitanHomeFolders_CreateKeys(
    const char *name,
    const TitanHomeGameKey *keys,
    unsigned int key_count,
    unsigned int *created_index
)
{
    unsigned int index;

    if (
        g_folder_count >= TITAN_HOME_FOLDER_CAPACITY ||
        !TitanHomeFolders_ValidateCandidate(name, keys, key_count, -1)
    )
    {
        return 0;
    }

    index = g_folder_count;
    TitanHomeFolders_Assign(&g_folders[index], name, keys, key_count);
    g_folder_count++;

    if (!TitanHomeFolders_Save())
    {
        g_folder_count--;
        memset(&g_folders[index], 0, sizeof(g_folders[index]));
        return 0;
    }

    if (created_index != 0)
    {
        *created_index = index;
    }
    return 1;
}

int TitanHomeFolders_UpdateKeys(
    unsigned int folder_index,
    const char *name,
    const TitanHomeGameKey *keys,
    unsigned int key_count
)
{
    TitanHomeFolder previous;

    if (
        folder_index >= g_folder_count ||
        !TitanHomeFolders_ValidateCandidate(
            name,
            keys,
            key_count,
            (int)folder_index
        )
    )
    {
        return 0;
    }

    previous = g_folders[folder_index];
    TitanHomeFolders_Assign(
        &g_folders[folder_index],
        name,
        keys,
        key_count
    );

    if (!TitanHomeFolders_Save())
    {
        g_folders[folder_index] = previous;
        return 0;
    }
    return 1;
}

static int TitanHomeFolders_PathsToKeys(
    const char *const *paths,
    unsigned int path_count,
    TitanHomeGameKey keys[TITAN_HOME_FOLDER_ITEM_CAPACITY]
)
{
    unsigned int index;

    if (
        paths == 0 || path_count == 0u ||
        path_count > TITAN_HOME_FOLDER_ITEM_CAPACITY
    )
    {
        return 0;
    }

    for (index = 0u; index < path_count; index++)
    {
        keys[index] = TitanHomeFolders_MakeGameKey(paths[index]);
        if (keys[index] == 0u)
        {
            return 0;
        }
    }
    return 1;
}

int TitanHomeFolders_Create(
    const char *name,
    const char *const *paths,
    unsigned int path_count,
    unsigned int *created_index
)
{
    TitanHomeGameKey keys[TITAN_HOME_FOLDER_ITEM_CAPACITY];

    if (!TitanHomeFolders_PathsToKeys(paths, path_count, keys))
    {
        return 0;
    }
    return TitanHomeFolders_CreateKeys(
        name,
        keys,
        path_count,
        created_index
    );
}

int TitanHomeFolders_Update(
    unsigned int folder_index,
    const char *name,
    const char *const *paths,
    unsigned int path_count
)
{
    TitanHomeGameKey keys[TITAN_HOME_FOLDER_ITEM_CAPACITY];

    if (!TitanHomeFolders_PathsToKeys(paths, path_count, keys))
    {
        return 0;
    }
    return TitanHomeFolders_UpdateKeys(
        folder_index,
        name,
        keys,
        path_count
    );
}

int TitanHomeFolders_AddItemKey(unsigned int folder_index, TitanHomeGameKey key)
{
    TitanHomeFolder previous;
    TitanHomeFolder *folder;

    if (
        folder_index >= g_folder_count ||
        key == 0u ||
        TitanHomeFolders_FindForItemKey(key) >= 0
    )
    {
        return 0;
    }

    folder = &g_folders[folder_index];
    if (folder->item_count >= TITAN_HOME_FOLDER_ITEM_CAPACITY)
    {
        return 0;
    }

    previous = *folder;
    folder->item_keys[folder->item_count] = key;
    folder->item_count++;

    if (!TitanHomeFolders_Save())
    {
        *folder = previous;
        return 0;
    }
    return 1;
}

int TitanHomeFolders_AddGamePath(unsigned int folder_index, const char *path)
{
    return TitanHomeFolders_AddItemKey(
        folder_index,
        TitanHomeFolders_MakeGameKey(path)
    );
}

int TitanHomeFolders_RemoveItemKey(unsigned int folder_index, TitanHomeGameKey key)
{
    TitanHomeFolder previous;
    TitanHomeFolder *folder;
    unsigned int item_index;

    if (folder_index >= g_folder_count || key == 0u)
    {
        return 0;
    }

    folder = &g_folders[folder_index];
    if (folder->item_count <= 1u)
    {
        return 0;
    }

    for (item_index = 0u; item_index < folder->item_count; item_index++)
    {
        if (folder->item_keys[item_index] == key)
        {
            previous = *folder;
            if (item_index + 1u < folder->item_count)
            {
                memmove(
                    &folder->item_keys[item_index],
                    &folder->item_keys[item_index + 1u],
                    (folder->item_count - item_index - 1u) *
                        sizeof(folder->item_keys[0])
                );
            }
            folder->item_count--;
            folder->item_keys[folder->item_count] = 0u;

            if (!TitanHomeFolders_Save())
            {
                *folder = previous;
                return 0;
            }
            return 1;
        }
    }
    return 0;
}

int TitanHomeFolders_RemoveGamePath(unsigned int folder_index, const char *path)
{
    return TitanHomeFolders_RemoveItemKey(
        folder_index,
        TitanHomeFolders_MakeGameKey(path)
    );
}

int TitanHomeFolders_Delete(unsigned int folder_index)
{
    TitanHomeFolder backup[TITAN_HOME_FOLDER_CAPACITY];
    unsigned int old_count;

    if (folder_index >= g_folder_count)
    {
        return 0;
    }

    memcpy(backup, g_folders, sizeof(g_folders));
    old_count = g_folder_count;

    if (folder_index + 1u < g_folder_count)
    {
        memmove(
            &g_folders[folder_index],
            &g_folders[folder_index + 1u],
            (g_folder_count - folder_index - 1u) * sizeof(g_folders[0])
        );
    }
    g_folder_count--;
    memset(&g_folders[g_folder_count], 0, sizeof(g_folders[g_folder_count]));

    if (!TitanHomeFolders_Save())
    {
        memcpy(g_folders, backup, sizeof(g_folders));
        g_folder_count = old_count;
        return 0;
    }
    return 1;
}
