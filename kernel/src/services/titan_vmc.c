#include "titan_vmc.h"

#include "titan_storage_io.h"
#include "titan_storage_paths.h"
#include "titan_shell_busy.h"

#include <string.h>

#define TITAN_VMC_WRITE_CHUNK_SIZE (8u * 1024u)
#define TITAN_VMC_PAGE_SIZE 512u
#define TITAN_VMC_PAGES_PER_CLUSTER 2u
#define TITAN_VMC_CLUSTER_SIZE 1024u
#define TITAN_VMC_PAGES_PER_BLOCK 16u
#define TITAN_VMC_CLUSTERS_PER_BLOCK 8u
#define TITAN_VMC_CLUSTERS_PER_CARD 8192u
#define TITAN_VMC_IFC_CLUSTER 8u
#define TITAN_VMC_FAT_FIRST_CLUSTER 9u
#define TITAN_VMC_FAT_CLUSTER_COUNT 32u
#define TITAN_VMC_ALLOC_OFFSET 41u
#define TITAN_VMC_ALLOC_END 8135u
#define TITAN_VMC_BACKUP_BLOCK_1 1023u
#define TITAN_VMC_BACKUP_BLOCK_2 1022u
#define TITAN_VMC_MAX_ALLOCATABLE_CLUSTERS 8001u
#define TITAN_VMC_SUPERBLOCK_SIZE 512u
#define TITAN_VMC_DEVICE_INFO_SIZE 384u
#define TITAN_VMC_DIRECTORY_ENTRY_SIZE 512u
#define TITAN_VMC_MAGIC_SIZE 28u

#define TITAN_VMC_FAT_END 0xFFFFFFFFu
#define TITAN_VMC_FAT_FREE 0x7FFFFFFFu
#define TITAN_VMC_FAT_ALLOCATED_BIT 0x80000000u
#define TITAN_VMC_FAT_CLUSTER_MASK 0x7FFFFFFFu

#define TITAN_VMC_MODE_READ 0x0001u
#define TITAN_VMC_MODE_WRITE 0x0002u
#define TITAN_VMC_MODE_EXECUTE 0x0004u
#define TITAN_VMC_MODE_DIRECTORY 0x0020u
#define TITAN_VMC_MODE_0400 0x0400u
#define TITAN_VMC_MODE_HIDDEN 0x2000u
#define TITAN_VMC_MODE_EXISTS 0x8000u

/*
 * The on-disk VMC signature is exactly 28 bytes.  Keep the C literal
 * NUL-terminated, then copy/compare only TITAN_VMC_MAGIC_SIZE bytes.
 * This avoids GCC 16's -Wunterminated-string-initialization diagnostic
 * without changing the bytes written to the virtual memory card.
 */
static const unsigned char g_titan_vmc_magic[] =
    "Sony PS2 Memory Card Format ";

typedef char TitanVmcMagicSizeMustMatch[
    ((sizeof(g_titan_vmc_magic) - 1u) == TITAN_VMC_MAGIC_SIZE) ? 1 : -1
];

static const unsigned char g_titan_vmc_version[12] =
    "1.2.0.0";

static unsigned char g_titan_vmc_write_chunk[
    TITAN_VMC_WRITE_CHUNK_SIZE
] __attribute__((aligned(64)));

static unsigned char g_titan_vmc_cluster[
    TITAN_VMC_CLUSTER_SIZE
] __attribute__((aligned(64)));

typedef struct TitanVmcStatusCacheEntry
{
    int valid;
    unsigned int profile_identifier;
    TitanVmcStatus status;
    char path[TITAN_USER_PROFILE_VMC_PATH_CAPACITY];
} TitanVmcStatusCacheEntry;

static TitanVmcStatusCacheEntry g_titan_vmc_status_cache[
    TITAN_USER_PROFILE_CAPACITY
];

static void TitanVmc_Write16(
    unsigned char *buffer,
    unsigned int offset,
    unsigned int value
)
{
    buffer[offset + 0u] = (unsigned char)(value & 0xFFu);
    buffer[offset + 1u] = (unsigned char)((value >> 8u) & 0xFFu);
}

static void TitanVmc_Write32(
    unsigned char *buffer,
    unsigned int offset,
    unsigned int value
)
{
    buffer[offset + 0u] = (unsigned char)(value & 0xFFu);
    buffer[offset + 1u] = (unsigned char)((value >> 8u) & 0xFFu);
    buffer[offset + 2u] = (unsigned char)((value >> 16u) & 0xFFu);
    buffer[offset + 3u] = (unsigned char)((value >> 24u) & 0xFFu);
}

static unsigned int TitanVmc_Read16(
    const unsigned char *buffer,
    unsigned int offset
)
{
    return
        (unsigned int)buffer[offset + 0u] |
        ((unsigned int)buffer[offset + 1u] << 8u);
}

static unsigned int TitanVmc_Read32(
    const unsigned char *buffer,
    unsigned int offset
)
{
    return
        (unsigned int)buffer[offset + 0u] |
        ((unsigned int)buffer[offset + 1u] << 8u) |
        ((unsigned int)buffer[offset + 2u] << 16u) |
        ((unsigned int)buffer[offset + 3u] << 24u);
}

static TitanVmcStatusCacheEntry *TitanVmc_FindCacheEntry(
    const TitanUserProfile *profile
)
{
    unsigned int index;

    if (profile == 0 || profile->vmc_path[0] == '\0')
    {
        return 0;
    }

    for (index = 0u; index < TITAN_USER_PROFILE_CAPACITY; index++)
    {
        TitanVmcStatusCacheEntry *entry = &g_titan_vmc_status_cache[index];
        if (entry->valid &&
            entry->profile_identifier == profile->identifier &&
            strcmp(entry->path, profile->vmc_path) == 0)
        {
            return entry;
        }
    }

    return 0;
}

static void TitanVmc_StoreCachedStatus(
    const TitanUserProfile *profile,
    TitanVmcStatus status
)
{
    TitanVmcStatusCacheEntry *entry;
    unsigned int index;
    unsigned int slot;

    if (profile == 0 || profile->vmc_path[0] == '\0')
    {
        return;
    }

    entry = TitanVmc_FindCacheEntry(profile);
    if (entry == 0)
    {
        slot = TITAN_USER_PROFILE_CAPACITY;
        for (index = 0u; index < TITAN_USER_PROFILE_CAPACITY; index++)
        {
            if (!g_titan_vmc_status_cache[index].valid)
            {
                slot = index;
                break;
            }
        }
        if (slot == TITAN_USER_PROFILE_CAPACITY)
        {
            slot = profile->identifier % TITAN_USER_PROFILE_CAPACITY;
        }
        entry = &g_titan_vmc_status_cache[slot];
    }

    memset(entry, 0, sizeof(*entry));
    entry->valid = 1;
    entry->profile_identifier = profile->identifier;
    entry->status = status;
    {
        unsigned int path_index;

        path_index = 0u;
        while (
            profile->vmc_path[path_index] != '\0' &&
            path_index + 1u < sizeof(entry->path)
        )
        {
            entry->path[path_index] = profile->vmc_path[path_index];
            path_index++;
        }
        entry->path[path_index] = '\0';
    }
}

void TitanVmc_InvalidateCachedStatus(const TitanUserProfile *profile)
{
    TitanVmcStatusCacheEntry *entry;

    if (profile == 0)
    {
        memset(g_titan_vmc_status_cache, 0, sizeof(g_titan_vmc_status_cache));
        return;
    }

    entry = TitanVmc_FindCacheEntry(profile);
    if (entry != 0)
    {
        memset(entry, 0, sizeof(*entry));
    }
}

TitanVmcStatus TitanVmc_GetCachedStatus(const TitanUserProfile *profile)
{
    TitanVmcStatusCacheEntry *entry;

    entry = TitanVmc_FindCacheEntry(profile);
    return entry != 0 ? entry->status : TITAN_VMC_STATUS_CHECKING;
}

static int TitanVmc_EnsureDirectory(
    const char *path
)
{
    int is_directory;

    if (path == 0 || path[0] == '\0')
    {
        return 0;
    }

    if (TitanStorageIo_PathInfo(path, &is_directory, 0))
    {
        return is_directory;
    }

    if (TitanStorageIo_CreateDirectory(path) >= 0)
    {
        return 1;
    }

    return
        TitanStorageIo_PathInfo(path, &is_directory, 0) &&
        is_directory;
}

static int TitanVmc_BuildUserDirectory(
    const TitanUserProfile *profile,
    char *destination,
    unsigned int capacity
)
{
    const char *suffix;
    unsigned int length;
    unsigned int suffix_length;

    if (
        profile == 0 ||
        destination == 0 ||
        capacity == 0u ||
        profile->vmc_path[0] == '\0'
    )
    {
        return 0;
    }

    suffix = "/profile.vmc";
    suffix_length = (unsigned int)strlen(suffix);
    length = (unsigned int)strlen(profile->vmc_path);

    if (
        length <= suffix_length ||
        length >= capacity ||
        strcmp(&profile->vmc_path[length - suffix_length], suffix) != 0
    )
    {
        return 0;
    }

    memcpy(destination, profile->vmc_path, length + 1u);
    destination[length - suffix_length] = '\0';
    return 1;
}

static int TitanVmc_EnsureProfileDirectories(
    const TitanUserProfile *profile
)
{
    char user_directory[TITAN_USER_PROFILE_VMC_PATH_CAPACITY];

    return
        TitanVmc_EnsureDirectory(TITAN_STORAGE_DEVICE_ROOT) &&
        TitanVmc_EnsureDirectory(TITAN_PROFILE_USERS_PATH) &&
        TitanVmc_BuildUserDirectory(
            profile,
            user_directory,
            sizeof(user_directory)
        ) &&
        TitanVmc_EnsureDirectory(user_directory);
}

static int TitanVmc_BuildDerivedPath(
    const char *source,
    const char *suffix,
    char *destination,
    unsigned int capacity
)
{
    unsigned int source_length;
    unsigned int suffix_length;

    if (
        source == 0 ||
        suffix == 0 ||
        destination == 0 ||
        capacity == 0u
    )
    {
        return 0;
    }

    source_length = (unsigned int)strlen(source);
    suffix_length = (unsigned int)strlen(suffix);

    if (source_length + suffix_length + 1u > capacity)
    {
        return 0;
    }

    memcpy(destination, source, source_length);
    memcpy(destination + source_length, suffix, suffix_length + 1u);
    return 1;
}

static int TitanVmc_BufferIsBlank(
    const unsigned char *buffer,
    unsigned int size
)
{
    unsigned int index;
    unsigned char expected;

    if (buffer == 0 || size == 0u)
    {
        return 0;
    }

    expected = buffer[0];
    if (expected != 0u && expected != 0xFFu)
    {
        return 0;
    }

    for (index = 1u; index < size; index++)
    {
        if (buffer[index] != expected)
        {
            return 0;
        }
    }

    return 1;
}

static int TitanVmc_ReadAt(
    int descriptor,
    TitanStorageIoOffset offset,
    void *buffer,
    size_t size
)
{
    return TitanStorageIo_Seek64(
            descriptor,
            offset,
            TITAN_STORAGE_IO_SEEK_SET
        ) == offset &&
        TitanStorageIo_ReadExact(descriptor, buffer, size);
}

static int TitanVmc_IsDirectoryEntry(
    const unsigned char *entry,
    const char *expected_name
)
{
    unsigned int mode;

    if (entry == 0 || expected_name == 0)
        return 0;

    mode = TitanVmc_Read16(entry, 0u);
    return
        (mode & TITAN_VMC_MODE_EXISTS) != 0u &&
        (mode & TITAN_VMC_MODE_DIRECTORY) != 0u &&
        strncmp(
            (const char *)(entry + 0x40u),
            expected_name,
            32u
        ) == 0;
}

static TitanVmcStatus TitanVmc_ValidateSuperblock(
    const unsigned char *superblock
)
{
    if (TitanVmc_BufferIsBlank(superblock, TITAN_VMC_SUPERBLOCK_SIZE))
    {
        return TITAN_VMC_STATUS_NEEDS_FORMAT;
    }

    if (
        memcmp(superblock, g_titan_vmc_magic, TITAN_VMC_MAGIC_SIZE) != 0 ||
        TitanVmc_Read16(superblock, 0x28u) != TITAN_VMC_PAGE_SIZE ||
        TitanVmc_Read16(superblock, 0x2Au) != TITAN_VMC_PAGES_PER_CLUSTER ||
        TitanVmc_Read16(superblock, 0x2Cu) != TITAN_VMC_PAGES_PER_BLOCK ||
        TitanVmc_Read32(superblock, 0x30u) != TITAN_VMC_CLUSTERS_PER_CARD ||
        TitanVmc_Read32(superblock, 0x34u) != TITAN_VMC_ALLOC_OFFSET ||
        TitanVmc_Read32(superblock, 0x38u) != TITAN_VMC_ALLOC_END ||
        TitanVmc_Read32(superblock, 0x3Cu) != 0u ||
        TitanVmc_Read32(superblock, 0x40u) != TITAN_VMC_BACKUP_BLOCK_1 ||
        TitanVmc_Read32(superblock, 0x44u) != TITAN_VMC_BACKUP_BLOCK_2 ||
        TitanVmc_Read32(superblock, 0x50u) != TITAN_VMC_IFC_CLUSTER ||
        superblock[0x150u] != 2u ||
        TitanVmc_Read32(superblock, 0x160u) != 1u
    )
    {
        return TITAN_VMC_STATUS_CORRUPT;
    }

    return TITAN_VMC_STATUS_READY;
}

/*
 * Runtime readiness probe: deliberately bounded to two 1 KiB reads.
 * Games legitimately modify/extend the FAT and root directory chain, so the
 * shell must not rescan all 32 FAT clusters just to draw a menu or launch.
 */
static TitanVmcStatus TitanVmc_GetPathStatusFast(const char *path)
{
    unsigned char superblock[TITAN_VMC_SUPERBLOCK_SIZE];
    TitanStorageIoSize file_size;
    TitanVmcStatus status;
    int descriptor;

    if (path == 0 || path[0] == '\0')
        return TITAN_VMC_STATUS_IO_ERROR;

    if (!TitanStorageIo_PathInfo64(path, 0, &file_size))
        return TITAN_VMC_STATUS_MISSING;
    if (file_size != (TitanStorageIoSize)TITAN_VMC_SIZE_BYTES)
        return TITAN_VMC_STATUS_INVALID_SIZE;

    descriptor = TitanStorageIo_Open(path, TITAN_STORAGE_IO_READ);
    if (descriptor < 0)
        return TITAN_VMC_STATUS_IO_ERROR;

    if (!TitanVmc_ReadAt(descriptor, 0, superblock, sizeof(superblock)))
    {
        TitanStorageIo_Close(descriptor);
        return TITAN_VMC_STATUS_IO_ERROR;
    }

    status = TitanVmc_ValidateSuperblock(superblock);
    if (status != TITAN_VMC_STATUS_READY)
    {
        TitanStorageIo_Close(descriptor);
        return status;
    }

    if (!TitanVmc_ReadAt(
            descriptor,
            (TitanStorageIoOffset)TITAN_VMC_ALLOC_OFFSET *
                TITAN_VMC_CLUSTER_SIZE,
            g_titan_vmc_cluster,
            sizeof(g_titan_vmc_cluster)))
    {
        TitanStorageIo_Close(descriptor);
        return TITAN_VMC_STATUS_IO_ERROR;
    }

    if (!TitanVmc_IsDirectoryEntry(g_titan_vmc_cluster, ".") ||
        !TitanVmc_IsDirectoryEntry(
            g_titan_vmc_cluster + TITAN_VMC_DIRECTORY_ENTRY_SIZE,
            ".."))
    {
        TitanStorageIo_Close(descriptor);
        return TITAN_VMC_STATUS_CORRUPT;
    }

    if (TitanStorageIo_Close(descriptor) < 0)
        return TITAN_VMC_STATUS_IO_ERROR;
    return TITAN_VMC_STATUS_READY;
}

static TitanVmcStatus TitanVmc_VerifyPathRaw(
    const char *path
)
{
    unsigned char superblock[TITAN_VMC_SUPERBLOCK_SIZE];
    TitanStorageIoSize file_size;
    int descriptor;
    unsigned int index;

    if (path == 0 || path[0] == '\0')
        return TITAN_VMC_STATUS_IO_ERROR;

    if (!TitanStorageIo_PathInfo64(path, 0, &file_size))
        return TITAN_VMC_STATUS_MISSING;
    if (file_size != (TitanStorageIoSize)TITAN_VMC_SIZE_BYTES)
        return TITAN_VMC_STATUS_INVALID_SIZE;

    descriptor = TitanStorageIo_Open(path, TITAN_STORAGE_IO_READ);
    if (descriptor < 0)
        return TITAN_VMC_STATUS_IO_ERROR;

    if (!TitanVmc_ReadAt(descriptor, 0, superblock, sizeof(superblock))) {
        TitanStorageIo_Close(descriptor);
        return TITAN_VMC_STATUS_IO_ERROR;
    }

    {
        TitanVmcStatus superblock_status;
        superblock_status = TitanVmc_ValidateSuperblock(superblock);
        if (superblock_status != TITAN_VMC_STATUS_READY)
        {
            TitanStorageIo_Close(descriptor);
            return superblock_status;
        }
    }

    /* Validate the indirect FAT map instead of trusting only the superblock. */
    if (!TitanVmc_ReadAt(
            descriptor,
            (TitanStorageIoOffset)TITAN_VMC_IFC_CLUSTER *
                TITAN_VMC_CLUSTER_SIZE,
            g_titan_vmc_cluster,
            sizeof(g_titan_vmc_cluster))) {
        TitanStorageIo_Close(descriptor);
        return TITAN_VMC_STATUS_IO_ERROR;
    }
    for (index = 0u; index < TITAN_VMC_FAT_CLUSTER_COUNT; index++) {
        if (TitanVmc_Read32(g_titan_vmc_cluster, index * 4u) !=
            TITAN_VMC_FAT_FIRST_CLUSTER + index) {
            TitanStorageIo_Close(descriptor);
            return TITAN_VMC_STATUS_CORRUPT;
        }
    }
    TitanShellBusy_Pulse();

    /* Every FAT entry must be free, end-of-chain, or an in-range link. */
    for (index = 0u; index < TITAN_VMC_FAT_CLUSTER_COUNT; index++) {
        unsigned int entry;
        if (!TitanVmc_ReadAt(
                descriptor,
                (TitanStorageIoOffset)(TITAN_VMC_FAT_FIRST_CLUSTER + index) *
                    TITAN_VMC_CLUSTER_SIZE,
                g_titan_vmc_cluster,
                sizeof(g_titan_vmc_cluster))) {
            TitanStorageIo_Close(descriptor);
            return TITAN_VMC_STATUS_IO_ERROR;
        }
        for (entry = 0u; entry < 256u; entry++) {
            unsigned int allocation_index = index * 256u + entry;
            unsigned int value;

            if (allocation_index >= TITAN_VMC_ALLOC_END)
                break;
            value = TitanVmc_Read32(g_titan_vmc_cluster, entry * 4u);
            if (value != TITAN_VMC_FAT_FREE && value != TITAN_VMC_FAT_END) {
                unsigned int linked_cluster;

                /* PS2 FAT chain links store the destination cluster in the
                 * low 31 bits and mark allocated links with bit 31.  Testing
                 * the raw 32-bit value against alloc_end falsely classified
                 * every normal multi-cluster save/root chain as corrupt. */
                if ((value & TITAN_VMC_FAT_ALLOCATED_BIT) == 0u) {
                    TitanStorageIo_Close(descriptor);
                    return TITAN_VMC_STATUS_CORRUPT;
                }
                linked_cluster = value & TITAN_VMC_FAT_CLUSTER_MASK;
                if (linked_cluster >= TITAN_VMC_ALLOC_END ||
                    linked_cluster == allocation_index) {
                    TitanStorageIo_Close(descriptor);
                    return TITAN_VMC_STATUS_CORRUPT;
                }
            }
        }
        TitanShellBusy_Pulse();
    }

    /* FAT[0] is EOF only on an empty card. As soon as saves add root-directory
     * entries, mcman may extend that directory and FAT[0] becomes a normal
     * in-range link. The generic FAT validation above already validates it. */

    if (!TitanVmc_ReadAt(
            descriptor,
            (TitanStorageIoOffset)TITAN_VMC_ALLOC_OFFSET *
                TITAN_VMC_CLUSTER_SIZE,
            g_titan_vmc_cluster,
            sizeof(g_titan_vmc_cluster))) {
        TitanStorageIo_Close(descriptor);
        return TITAN_VMC_STATUS_IO_ERROR;
    }

    if (!TitanVmc_IsDirectoryEntry(g_titan_vmc_cluster, ".") ||
        !TitanVmc_IsDirectoryEntry(
            g_titan_vmc_cluster + TITAN_VMC_DIRECTORY_ENTRY_SIZE,
            "..")) {
        TitanStorageIo_Close(descriptor);
        return TITAN_VMC_STATUS_CORRUPT;
    }

    if (TitanStorageIo_Close(descriptor) < 0)
        return TITAN_VMC_STATUS_IO_ERROR;
    return TITAN_VMC_STATUS_READY;
}

static TitanVmcStatus TitanVmc_RecoverPath(const char *path)
{
    char temporary_path[TITAN_USER_PROFILE_VMC_PATH_CAPACITY + 8u];
    char backup_path[TITAN_USER_PROFILE_VMC_PATH_CAPACITY + 8u];
    TitanVmcStatus current;
    TitanVmcStatus temporary;
    TitanVmcStatus backup;

    if (!TitanVmc_BuildDerivedPath(
            path, ".new", temporary_path, sizeof(temporary_path)) ||
        !TitanVmc_BuildDerivedPath(
            path, ".bak", backup_path, sizeof(backup_path)))
        return TITAN_VMC_STATUS_IO_ERROR;

    current = TitanVmc_GetPathStatusFast(path);
    TitanShellBusy_Pulse();
    temporary = TitanVmc_GetPathStatusFast(temporary_path);
    TitanShellBusy_Pulse();
    backup = TitanVmc_GetPathStatusFast(backup_path);
    TitanShellBusy_Pulse();

    if (current == TITAN_VMC_STATUS_READY) {
        if (temporary != TITAN_VMC_STATUS_MISSING)
            TitanStorageIo_Remove(temporary_path);
        if (backup == TITAN_VMC_STATUS_READY)
            TitanStorageIo_Remove(backup_path);
        return current;
    }

    if (temporary == TITAN_VMC_STATUS_READY) {
        if (TitanStorageIo_AtomicReplace(
                temporary_path,
                path,
                backup_path) &&
            TitanVmc_GetPathStatusFast(path) == TITAN_VMC_STATUS_READY) {
            TitanStorageIo_Remove(backup_path);
            return TITAN_VMC_STATUS_READY;
        }
        return current;
    }

    if (backup == TITAN_VMC_STATUS_READY) {
        /* Quarantine a damaged current file in .new while restoring .bak. */
        if (current != TITAN_VMC_STATUS_MISSING) {
            TitanStorageIo_Remove(temporary_path);
            if (TitanStorageIo_Rename(path, temporary_path) < 0)
                return current;
        }
        if (TitanStorageIo_Rename(backup_path, path) == 0 &&
            TitanVmc_GetPathStatusFast(path) == TITAN_VMC_STATUS_READY) {
            TitanStorageIo_Remove(temporary_path);
            return TITAN_VMC_STATUS_READY;
        }
        if (TitanStorageIo_PathInfo64(temporary_path, 0, 0)) {
            TitanStorageIo_Remove(path);
            TitanStorageIo_Rename(temporary_path, path);
        }
    }

    return current;
}

TitanVmcStatus TitanVmc_RefreshStatus(
    const TitanUserProfile *profile
)
{
    TitanVmcStatus status;

    if (profile == 0 || profile->vmc_path[0] == '\0')
        return TITAN_VMC_STATUS_IO_ERROR;

    status = TitanVmc_RecoverPath(profile->vmc_path);
    TitanVmc_StoreCachedStatus(profile, status);
    return status;
}

TitanVmcStatus TitanVmc_GetStatus(
    const TitanUserProfile *profile
)
{
    return TitanVmc_RefreshStatus(profile);
}

TitanVmcStatus TitanVmc_Verify(
    const TitanUserProfile *profile
)
{
    TitanVmcStatus status;

    if (profile == 0 || profile->vmc_path[0] == '\0')
        return TITAN_VMC_STATUS_IO_ERROR;

    status = TitanVmc_RecoverPath(profile->vmc_path);
    if (status == TITAN_VMC_STATUS_READY)
    {
        status = TitanVmc_VerifyPathRaw(profile->vmc_path);
    }
    TitanVmc_StoreCachedStatus(profile, status);
    return status;
}

static int TitanVmc_WriteAt(
    int descriptor,
    unsigned int offset,
    const void *buffer,
    unsigned int size
)
{
    return TitanStorageIo_Seek64(
            descriptor,
            (TitanStorageIoOffset)offset,
            TITAN_STORAGE_IO_SEEK_SET
        ) == (TitanStorageIoOffset)offset &&
        TitanStorageIo_WriteExact(descriptor, buffer, size);
}

static int TitanVmc_FillErasedImage(int descriptor)
{
    unsigned int remaining;

    memset(g_titan_vmc_write_chunk, 0xFF, sizeof(g_titan_vmc_write_chunk));
    remaining = TITAN_VMC_SIZE_BYTES;

    while (remaining > 0u)
    {
        unsigned int write_size;

        write_size = remaining < sizeof(g_titan_vmc_write_chunk)
            ? remaining
            : sizeof(g_titan_vmc_write_chunk);

        if (!TitanStorageIo_WriteExact(
                descriptor,
                g_titan_vmc_write_chunk,
                write_size))
        {
            return 0;
        }

        remaining -= write_size;
        TitanShellBusy_Pulse();
    }

    return 1;
}

static void TitanVmc_BuildSuperblock(unsigned char *cluster)
{
    unsigned int index;

    memset(cluster, 0xFF, TITAN_VMC_CLUSTER_SIZE);
    memset(cluster, 0, TITAN_VMC_DEVICE_INFO_SIZE);
    memcpy(cluster + 0x00u, g_titan_vmc_magic, TITAN_VMC_MAGIC_SIZE);
    memcpy(cluster + 0x1Cu, g_titan_vmc_version, sizeof(g_titan_vmc_version));

    TitanVmc_Write16(cluster, 0x28u, TITAN_VMC_PAGE_SIZE);
    TitanVmc_Write16(cluster, 0x2Au, TITAN_VMC_PAGES_PER_CLUSTER);
    TitanVmc_Write16(cluster, 0x2Cu, TITAN_VMC_PAGES_PER_BLOCK);
    TitanVmc_Write16(cluster, 0x2Eu, 0xFF00u);
    TitanVmc_Write32(cluster, 0x30u, TITAN_VMC_CLUSTERS_PER_CARD);
    TitanVmc_Write32(cluster, 0x34u, TITAN_VMC_ALLOC_OFFSET);
    TitanVmc_Write32(cluster, 0x38u, TITAN_VMC_ALLOC_END);
    TitanVmc_Write32(cluster, 0x3Cu, 0u);
    TitanVmc_Write32(cluster, 0x40u, TITAN_VMC_BACKUP_BLOCK_1);
    TitanVmc_Write32(cluster, 0x44u, TITAN_VMC_BACKUP_BLOCK_2);

    for (index = 0u; index < 32u; index++)
    {
        TitanVmc_Write32(
            cluster,
            0x50u + (index * 4u),
            index == 0u ? TITAN_VMC_IFC_CLUSTER : 0xFFFFFFFFu
        );
        TitanVmc_Write32(
            cluster,
            0xD0u + (index * 4u),
            0xFFFFFFFFu
        );
    }

    cluster[0x150u] = 0x02u;
    cluster[0x151u] = 0x2Bu;
    TitanVmc_Write16(cluster, 0x152u, 0u);
    TitanVmc_Write32(cluster, 0x154u, TITAN_VMC_CLUSTER_SIZE);
    TitanVmc_Write32(cluster, 0x158u, TITAN_VMC_CLUSTER_SIZE / 4u);
    TitanVmc_Write32(cluster, 0x15Cu, TITAN_VMC_CLUSTERS_PER_BLOCK);
    TitanVmc_Write32(cluster, 0x160u, 1u);
    TitanVmc_Write32(cluster, 0x164u, 0u);
    TitanVmc_Write32(cluster, 0x168u, 0u);
    TitanVmc_Write32(cluster, 0x16Cu, 0u);
    TitanVmc_Write32(
        cluster,
        0x170u,
        TITAN_VMC_MAX_ALLOCATABLE_CLUSTERS
    );
    TitanVmc_Write32(cluster, 0x174u, 0u);
    TitanVmc_Write32(cluster, 0x178u, 0u);
    TitanVmc_Write32(cluster, 0x17Cu, 0xFFFFFFFFu);
}

static void TitanVmc_BuildIndirectFat(unsigned char *cluster)
{
    unsigned int index;

    memset(cluster, 0, TITAN_VMC_CLUSTER_SIZE);
    for (index = 0u; index < TITAN_VMC_FAT_CLUSTER_COUNT; index++)
    {
        TitanVmc_Write32(
            cluster,
            index * 4u,
            TITAN_VMC_FAT_FIRST_CLUSTER + index
        );
    }
}

static void TitanVmc_BuildFatCluster(
    unsigned char *cluster,
    unsigned int fat_cluster_index
)
{
    unsigned int entry;
    unsigned int first_allocation_index;

    memset(cluster, 0, TITAN_VMC_CLUSTER_SIZE);
    first_allocation_index = fat_cluster_index * 256u;

    for (entry = 0u; entry < 256u; entry++)
    {
        unsigned int allocation_index;

        allocation_index = first_allocation_index + entry;
        if (allocation_index >= TITAN_VMC_ALLOC_END)
        {
            break;
        }

        TitanVmc_Write32(
            cluster,
            entry * 4u,
            allocation_index == 0u
                ? TITAN_VMC_FAT_END
                : TITAN_VMC_FAT_FREE
        );
    }
}

static void TitanVmc_WriteDate(
    unsigned char *entry,
    unsigned int offset
)
{
    entry[offset + 0u] = 0u;
    entry[offset + 1u] = 0u;
    entry[offset + 2u] = 0u;
    entry[offset + 3u] = 0u;
    entry[offset + 4u] = 1u;
    entry[offset + 5u] = 1u;
    TitanVmc_Write16(entry, offset + 6u, 2000u);
}

static void TitanVmc_BuildDirectoryEntry(
    unsigned char *entry,
    unsigned int mode,
    const char *name
)
{
    unsigned int name_length;

    memset(entry, 0, TITAN_VMC_DIRECTORY_ENTRY_SIZE);
    TitanVmc_Write16(entry, 0x00u, mode);
    TitanVmc_Write32(entry, 0x04u, 2u);
    TitanVmc_WriteDate(entry, 0x08u);
    TitanVmc_Write32(entry, 0x10u, 0u);
    TitanVmc_Write32(entry, 0x14u, 0u);
    TitanVmc_WriteDate(entry, 0x18u);
    TitanVmc_Write32(entry, 0x20u, 0u);

    name_length = (unsigned int)strlen(name);
    if (name_length > 31u)
    {
        name_length = 31u;
    }
    memcpy(entry + 0x40u, name, name_length);
    entry[0x40u + name_length] = '\0';
}

static void TitanVmc_BuildRootDirectory(unsigned char *cluster)
{
    unsigned int current_mode;
    unsigned int parent_mode;

    current_mode =
        TITAN_VMC_MODE_EXISTS |
        TITAN_VMC_MODE_0400 |
        TITAN_VMC_MODE_DIRECTORY |
        TITAN_VMC_MODE_READ |
        TITAN_VMC_MODE_WRITE |
        TITAN_VMC_MODE_EXECUTE;

    parent_mode =
        TITAN_VMC_MODE_EXISTS |
        TITAN_VMC_MODE_HIDDEN |
        TITAN_VMC_MODE_0400 |
        TITAN_VMC_MODE_DIRECTORY |
        TITAN_VMC_MODE_WRITE |
        TITAN_VMC_MODE_EXECUTE;

    memset(cluster, 0, TITAN_VMC_CLUSTER_SIZE);
    TitanVmc_BuildDirectoryEntry(cluster, current_mode, ".");
    TitanVmc_BuildDirectoryEntry(
        cluster + TITAN_VMC_DIRECTORY_ENTRY_SIZE,
        parent_mode,
        ".."
    );
}

static int TitanVmc_WriteFormattedImage(const char *path)
{
    unsigned int index;
    int descriptor;

    descriptor = TitanStorageIo_Open(
        path,
        TITAN_STORAGE_IO_WRITE |
        TITAN_STORAGE_IO_CREATE |
        TITAN_STORAGE_IO_TRUNCATE
    );

    if (descriptor < 0)
    {
        return 0;
    }

    if (!TitanVmc_FillErasedImage(descriptor))
    {
        TitanStorageIo_Close(descriptor);
        TitanStorageIo_Remove(path);
        return 0;
    }

    TitanVmc_BuildIndirectFat(g_titan_vmc_cluster);
    if (!TitanVmc_WriteAt(
            descriptor,
            TITAN_VMC_IFC_CLUSTER * TITAN_VMC_CLUSTER_SIZE,
            g_titan_vmc_cluster,
            TITAN_VMC_CLUSTER_SIZE))
    {
        TitanStorageIo_Close(descriptor);
        TitanStorageIo_Remove(path);
        return 0;
    }

    for (index = 0u; index < TITAN_VMC_FAT_CLUSTER_COUNT; index++)
    {
        TitanVmc_BuildFatCluster(g_titan_vmc_cluster, index);
        if (!TitanVmc_WriteAt(
                descriptor,
                (TITAN_VMC_FAT_FIRST_CLUSTER + index) *
                    TITAN_VMC_CLUSTER_SIZE,
                g_titan_vmc_cluster,
                TITAN_VMC_CLUSTER_SIZE))
        {
            TitanStorageIo_Close(descriptor);
            TitanStorageIo_Remove(path);
            return 0;
        }

        if ((index & 7u) == 7u)
        {
            TitanShellBusy_Pulse();
        }
    }

    TitanVmc_BuildRootDirectory(g_titan_vmc_cluster);
    if (!TitanVmc_WriteAt(
            descriptor,
            TITAN_VMC_ALLOC_OFFSET * TITAN_VMC_CLUSTER_SIZE,
            g_titan_vmc_cluster,
            TITAN_VMC_CLUSTER_SIZE))
    {
        TitanStorageIo_Close(descriptor);
        TitanStorageIo_Remove(path);
        return 0;
    }

    TitanVmc_BuildSuperblock(g_titan_vmc_cluster);
    if (!TitanVmc_WriteAt(
            descriptor,
            0u,
            g_titan_vmc_cluster,
            TITAN_VMC_CLUSTER_SIZE))
    {
        TitanStorageIo_Close(descriptor);
        TitanStorageIo_Remove(path);
        return 0;
    }

    if (TitanStorageIo_Flush(descriptor) < 0 ||
        TitanStorageIo_Close(descriptor) < 0)
    {
        TitanStorageIo_Remove(path);
        return 0;
    }

    return TitanVmc_VerifyPathRaw(path) == TITAN_VMC_STATUS_READY;
}

static TitanVmcResult TitanVmc_Create(
    const TitanUserProfile *profile
)
{
    TitanVmcResult result;

    if (!TitanVmc_EnsureProfileDirectories(profile))
        return TITAN_VMC_RESULT_NO_STORAGE;

    result = TitanVmc_Format(profile);
    return result == TITAN_VMC_RESULT_FORMATTED ?
        TITAN_VMC_RESULT_CREATED : result;
}

TitanVmcResult TitanVmc_Ensure(
    const TitanUserProfile *profile
)
{
    TitanVmcStatus status;

    status = TitanVmc_GetCachedStatus(profile);
    if (status != TITAN_VMC_STATUS_READY)
    {
        status = TitanVmc_RefreshStatus(profile);
    }

    if (status == TITAN_VMC_STATUS_READY)
    {
        return TITAN_VMC_RESULT_OK;
    }
    if (status == TITAN_VMC_STATUS_NEEDS_FORMAT)
    {
        return TITAN_VMC_RESULT_NEEDS_FORMAT;
    }
    if (status == TITAN_VMC_STATUS_MISSING)
    {
        return TitanVmc_Create(profile);
    }
    if (
        status == TITAN_VMC_STATUS_INVALID_SIZE ||
        status == TITAN_VMC_STATUS_CORRUPT
    )
    {
        return TITAN_VMC_RESULT_INVALID;
    }

    return TITAN_VMC_RESULT_IO_ERROR;
}

TitanVmcResult TitanVmc_Format(
    const TitanUserProfile *profile
)
{
    char temporary_path[TITAN_USER_PROFILE_VMC_PATH_CAPACITY + 8u];
    char backup_path[TITAN_USER_PROFILE_VMC_PATH_CAPACITY + 8u];

    if (
        profile == 0 ||
        profile->vmc_path[0] == '\0' ||
        !TitanVmc_EnsureProfileDirectories(profile) ||
        !TitanVmc_BuildDerivedPath(
            profile->vmc_path,
            ".new",
            temporary_path,
            sizeof(temporary_path)) ||
        !TitanVmc_BuildDerivedPath(
            profile->vmc_path,
            ".bak",
            backup_path,
            sizeof(backup_path))
    )
        return TITAN_VMC_RESULT_NO_STORAGE;

    TitanStorageIo_Remove(temporary_path);
    if (!TitanVmc_WriteFormattedImage(temporary_path))
        return TITAN_VMC_RESULT_IO_ERROR;

    if (!TitanStorageIo_AtomicReplace(
            temporary_path,
            profile->vmc_path,
            backup_path)) {
        TitanStorageIo_Remove(temporary_path);
        return TITAN_VMC_RESULT_IO_ERROR;
    }

    if (TitanVmc_VerifyPathRaw(profile->vmc_path) !=
        TITAN_VMC_STATUS_READY) {
        TitanStorageIo_Remove(profile->vmc_path);
        if (TitanStorageIo_PathInfo64(backup_path, 0, 0))
            TitanStorageIo_Rename(backup_path, profile->vmc_path);
        return TITAN_VMC_RESULT_IO_ERROR;
    }

    /* Keep no stale backup after the new card has been structurally verified. */
    TitanStorageIo_Remove(backup_path);
    TitanVmc_StoreCachedStatus(profile, TITAN_VMC_STATUS_READY);
    return TITAN_VMC_RESULT_FORMATTED;
}

TitanVmcResult TitanVmc_Recreate(
    const TitanUserProfile *profile
)
{
    if (profile == 0 || profile->vmc_path[0] == '\0')
    {
        return TITAN_VMC_RESULT_IO_ERROR;
    }

    return TitanVmc_Format(profile);
}


typedef struct TitanVmcRawDirectoryEntry
{
    char name[TITAN_VMC_DIRECTORY_NAME_CAPACITY];
    unsigned int size;
    unsigned int flags;
    unsigned int cluster;
} TitanVmcRawDirectoryEntry;

static int TitanVmc_ReadFatEntry(
    int descriptor,
    unsigned int allocation_index,
    unsigned int *value
)
{
    unsigned char bytes[4];
    unsigned int fat_cluster;
    unsigned int fat_offset;

    if (value == 0 || allocation_index >= TITAN_VMC_ALLOC_END)
        return 0;
    fat_cluster = TITAN_VMC_FAT_FIRST_CLUSTER + allocation_index / 256u;
    fat_offset = (allocation_index % 256u) * 4u;
    if (!TitanVmc_ReadAt(
            descriptor,
            (TitanStorageIoOffset)fat_cluster * TITAN_VMC_CLUSTER_SIZE + fat_offset,
            bytes,
            sizeof(bytes)))
        return 0;
    *value = TitanVmc_Read32(bytes, 0u);
    return 1;
}

static void TitanVmc_DecodeDirectoryEntry(
    const unsigned char *raw,
    TitanVmcRawDirectoryEntry *entry
)
{
    unsigned int mode;
    unsigned int name_len = 0u;
    const char *name;

    memset(entry, 0, sizeof(*entry));
    mode = TitanVmc_Read16(raw, 0x00u);
    name = (const char *)(raw + 0x40u);
    while (name_len < 31u && name[name_len] != '\0') name_len++;
    if (name_len != 0u)
    {
        memcpy(entry->name, name, name_len);
        entry->name[name_len] = '\0';
    }
    entry->size = TitanVmc_Read32(raw, 0x04u);
    entry->cluster = TitanVmc_Read32(raw, 0x10u);
    if ((mode & TITAN_VMC_MODE_DIRECTORY) != 0u)
        entry->flags |= TITAN_VMC_DIRECTORY_ENTRY_DIRECTORY;
    if ((mode & TITAN_VMC_MODE_READ) != 0u)
        entry->flags |= TITAN_VMC_DIRECTORY_ENTRY_READABLE;
    if ((mode & TITAN_VMC_MODE_WRITE) != 0u)
        entry->flags |= TITAN_VMC_DIRECTORY_ENTRY_WRITABLE;
    if ((mode & TITAN_VMC_MODE_HIDDEN) != 0u)
        entry->flags |= TITAN_VMC_DIRECTORY_ENTRY_HIDDEN;
}

/* Scan one directory chain. If find_name is non-null, visible_index is ignored
 * and the first exact existing entry is returned. Otherwise returns the
 * visible_index-th entry after hiding . and ... */
static int TitanVmc_ScanDirectory(
    int descriptor,
    unsigned int start_allocation,
    unsigned int directory_length,
    unsigned int visible_index,
    const char *find_name,
    TitanVmcRawDirectoryEntry *entry
)
{
    unsigned char cluster[TITAN_VMC_CLUSTER_SIZE];
    unsigned int allocation_index = start_allocation;
    unsigned int directory_ordinal = 0u;
    unsigned int visible = 0u;
    unsigned int steps = 0u;

    if (entry == 0 || start_allocation >= TITAN_VMC_ALLOC_END ||
        directory_length < 2u || directory_length > TITAN_VMC_ALLOC_END * 2u)
        return -1;

    while (steps++ < TITAN_VMC_ALLOC_END)
    {
        unsigned int slot;
        unsigned int fat_value;
        if (allocation_index >= TITAN_VMC_ALLOC_END ||
            !TitanVmc_ReadAt(
                descriptor,
                (TitanStorageIoOffset)(TITAN_VMC_ALLOC_OFFSET + allocation_index) * TITAN_VMC_CLUSTER_SIZE,
                cluster,
                sizeof(cluster)))
            return -1;

        for (slot = 0u; slot < 2u && directory_ordinal < directory_length;
             slot++, directory_ordinal++)
        {
            const unsigned char *raw = cluster + slot * TITAN_VMC_DIRECTORY_ENTRY_SIZE;
            unsigned int mode = TitanVmc_Read16(raw, 0x00u);
            TitanVmcRawDirectoryEntry decoded;
            if (directory_ordinal < 2u || (mode & TITAN_VMC_MODE_EXISTS) == 0u)
                continue;
            TitanVmc_DecodeDirectoryEntry(raw, &decoded);
            if (decoded.name[0] == '\0') continue;
            if (find_name != 0)
            {
                if (strcmp(decoded.name, find_name) == 0)
                {
                    *entry = decoded;
                    return 1;
                }
            }
            else if (visible++ == visible_index)
            {
                *entry = decoded;
                return 1;
            }
        }

        if (directory_ordinal >= directory_length)
            return find_name == 0 ? 0 : 0;
        if (!TitanVmc_ReadFatEntry(descriptor, allocation_index, &fat_value))
            return -1;
        if (fat_value == TITAN_VMC_FAT_END)
            return directory_ordinal >= directory_length ? 0 : -1;
        if ((fat_value & TITAN_VMC_FAT_ALLOCATED_BIT) == 0u)
            return -1;
        fat_value &= TITAN_VMC_FAT_CLUSTER_MASK;
        if (fat_value >= TITAN_VMC_ALLOC_END || fat_value == allocation_index)
            return -1;
        allocation_index = fat_value;
    }
    return -1;
}

static int TitanVmc_ReadDirectoryLength(
    int descriptor,
    unsigned int start_allocation,
    unsigned int *directory_length
)
{
    unsigned char first_entry[TITAN_VMC_DIRECTORY_ENTRY_SIZE];
    unsigned int mode;
    unsigned int length;
    if (directory_length == 0 || start_allocation >= TITAN_VMC_ALLOC_END)
        return 0;
    if (!TitanVmc_ReadAt(
            descriptor,
            (TitanStorageIoOffset)(TITAN_VMC_ALLOC_OFFSET + start_allocation) * TITAN_VMC_CLUSTER_SIZE,
            first_entry,
            sizeof(first_entry)))
        return 0;
    mode = TitanVmc_Read16(first_entry, 0x00u);
    length = TitanVmc_Read32(first_entry, 0x04u);
    if ((mode & TITAN_VMC_MODE_EXISTS) == 0u ||
        (mode & TITAN_VMC_MODE_DIRECTORY) == 0u ||
        length < 2u || length > TITAN_VMC_ALLOC_END * 2u)
        return 0;
    *directory_length = length;
    return 1;
}

static int TitanVmc_ResolveDirectory(
    int descriptor,
    const char *relative_path,
    unsigned int *start_allocation,
    unsigned int *directory_length
)
{
    char component[TITAN_VMC_DIRECTORY_NAME_CAPACITY];
    unsigned int start = 0u;
    unsigned int length;
    unsigned int pos = 0u;
    unsigned int depth = 0u;

    if (start_allocation == 0 || directory_length == 0 || relative_path == 0)
        return 0;
    if (!TitanVmc_ReadDirectoryLength(descriptor, start, &length))
        return 0;
    if (relative_path[0] == '\0')
    {
        *start_allocation = start;
        *directory_length = length;
        return 1;
    }
    if (relative_path[0] == '/') return 0;

    while (relative_path[pos] != '\0')
    {
        unsigned int n = 0u;
        TitanVmcRawDirectoryEntry found;
        if (++depth > 8u) return 0;
        while (relative_path[pos] != '\0' && relative_path[pos] != '/')
        {
            if (n + 1u >= sizeof(component)) return 0;
            component[n++] = relative_path[pos++];
        }
        component[n] = '\0';
        if (n == 0u || strcmp(component, ".") == 0 || strcmp(component, "..") == 0)
            return 0;
        if (TitanVmc_ScanDirectory(descriptor, start, length, 0u, component, &found) != 1)
            return 0;
        if ((found.flags & TITAN_VMC_DIRECTORY_ENTRY_DIRECTORY) == 0u ||
            found.cluster >= TITAN_VMC_ALLOC_END || found.size < 2u ||
            found.size > TITAN_VMC_ALLOC_END * 2u)
            return 0;
        start = found.cluster;
        length = found.size;
        if (relative_path[pos] == '/')
        {
            pos++;
            if (relative_path[pos] == '\0') return 0;
        }
    }
    *start_allocation = start;
    *directory_length = length;
    return 1;
}

static int TitanVmc_OpenVerified(
    const TitanUserProfile *profile,
    int *descriptor
)
{
    unsigned char superblock[TITAN_VMC_SUPERBLOCK_SIZE];
    TitanStorageIoSize file_size;
    int fd;
    if (profile == 0 || descriptor == 0 || profile->vmc_path[0] == '\0')
        return 0;
    if (!TitanStorageIo_PathInfo64(profile->vmc_path, 0, &file_size) ||
        file_size != (TitanStorageIoSize)TITAN_VMC_SIZE_BYTES)
        return 0;
    fd = TitanStorageIo_Open(profile->vmc_path, TITAN_STORAGE_IO_READ);
    if (fd < 0) return 0;
    if (!TitanVmc_ReadAt(fd, 0, superblock, sizeof(superblock)) ||
        TitanVmc_ValidateSuperblock(superblock) != TITAN_VMC_STATUS_READY)
    {
        TitanStorageIo_Close(fd);
        return 0;
    }
    *descriptor = fd;
    return 1;
}

int TitanVmc_ListDirectory(
    const TitanUserProfile *profile,
    const char *relative_path,
    unsigned int visible_index,
    TitanVmcDirectoryEntry *entry
)
{
    TitanVmcRawDirectoryEntry raw;
    unsigned int start_allocation;
    unsigned int directory_length;
    int descriptor;
    int result;

    if (entry == 0) return -1;
    memset(entry, 0, sizeof(*entry));
    if (!TitanVmc_OpenVerified(profile, &descriptor)) return -1;
    if (!TitanVmc_ResolveDirectory(
            descriptor, relative_path, &start_allocation, &directory_length))
    {
        TitanStorageIo_Close(descriptor);
        return -1;
    }
    result = TitanVmc_ScanDirectory(
        descriptor, start_allocation, directory_length, visible_index, 0, &raw);
    TitanStorageIo_Close(descriptor);
    if (result != 1) return result;
    memcpy(entry->name, raw.name, sizeof(entry->name));
    entry->size = raw.size;
    entry->flags = raw.flags;
    return 1;
}

int TitanVmc_ListRoot(
    const TitanUserProfile *profile,
    unsigned int visible_index,
    TitanVmcDirectoryEntry *entry
)
{
    return TitanVmc_ListDirectory(profile, "", visible_index, entry);
}

static int TitanVmc_ResolveFile(
    int descriptor,
    const char *relative_path,
    TitanVmcRawDirectoryEntry *entry
)
{
    char parent[256];
    char leaf[TITAN_VMC_DIRECTORY_NAME_CAPACITY];
    unsigned int slash = 0xffffffffu;
    unsigned int length = 0u;
    unsigned int start_allocation;
    unsigned int directory_length;
    unsigned int i;

    if (relative_path == 0 || entry == 0 || relative_path[0] == '\0' || relative_path[0] == '/')
        return 0;
    while (relative_path[length] != '\0')
    {
        if (length + 1u >= sizeof(parent)) return 0;
        if (relative_path[length] == '/') slash = length;
        length++;
    }
    if (slash == length - 1u) return 0;
    if (slash == 0xffffffffu)
    {
        parent[0] = '\0';
        if (length >= sizeof(leaf)) return 0;
        memcpy(leaf, relative_path, length + 1u);
    }
    else
    {
        unsigned int leaf_len = length - slash - 1u;
        if (slash >= sizeof(parent) || leaf_len == 0u || leaf_len >= sizeof(leaf)) return 0;
        memcpy(parent, relative_path, slash);
        parent[slash] = '\0';
        memcpy(leaf, relative_path + slash + 1u, leaf_len);
        leaf[leaf_len] = '\0';
    }
    if (strcmp(leaf, ".") == 0 || strcmp(leaf, "..") == 0) return 0;
    for (i = 0u; leaf[i] != '\0'; i++) if (leaf[i] == '/') return 0;
    if (!TitanVmc_ResolveDirectory(descriptor, parent, &start_allocation, &directory_length))
        return 0;
    if (TitanVmc_ScanDirectory(descriptor, start_allocation, directory_length, 0u, leaf, entry) != 1)
        return 0;
    if ((entry->flags & TITAN_VMC_DIRECTORY_ENTRY_DIRECTORY) != 0u ||
        entry->cluster >= TITAN_VMC_ALLOC_END)
        return 0;
    return 1;
}

int TitanVmc_ReadFile(
    const TitanUserProfile *profile,
    const char *relative_path,
    unsigned int offset,
    void *buffer,
    unsigned int capacity
)
{
    TitanVmcRawDirectoryEntry file;
    unsigned char *out = (unsigned char *)buffer;
    unsigned int allocation_index;
    unsigned int cluster_skip;
    unsigned int cluster_offset;
    unsigned int remaining;
    unsigned int total = 0u;
    unsigned int steps = 0u;
    int descriptor;

    if (buffer == 0 || capacity == 0u) return 0;
    if (!TitanVmc_OpenVerified(profile, &descriptor)) return -1;
    if (!TitanVmc_ResolveFile(descriptor, relative_path, &file))
    {
        TitanStorageIo_Close(descriptor);
        return -1;
    }
    if (offset >= file.size)
    {
        TitanStorageIo_Close(descriptor);
        return 0;
    }
    remaining = file.size - offset;
    if (remaining > capacity) remaining = capacity;
    allocation_index = file.cluster;
    cluster_skip = offset / TITAN_VMC_CLUSTER_SIZE;
    cluster_offset = offset % TITAN_VMC_CLUSTER_SIZE;

    while (cluster_skip-- != 0u)
    {
        unsigned int fat_value;
        if (++steps > TITAN_VMC_ALLOC_END ||
            !TitanVmc_ReadFatEntry(descriptor, allocation_index, &fat_value) ||
            fat_value == TITAN_VMC_FAT_END ||
            (fat_value & TITAN_VMC_FAT_ALLOCATED_BIT) == 0u)
        {
            TitanStorageIo_Close(descriptor);
            return -1;
        }
        fat_value &= TITAN_VMC_FAT_CLUSTER_MASK;
        if (fat_value >= TITAN_VMC_ALLOC_END || fat_value == allocation_index)
        {
            TitanStorageIo_Close(descriptor);
            return -1;
        }
        allocation_index = fat_value;
    }

    while (remaining != 0u)
    {
        unsigned int chunk = TITAN_VMC_CLUSTER_SIZE - cluster_offset;
        if (chunk > remaining) chunk = remaining;
        if (allocation_index >= TITAN_VMC_ALLOC_END ||
            !TitanVmc_ReadAt(
                descriptor,
                (TitanStorageIoOffset)(TITAN_VMC_ALLOC_OFFSET + allocation_index) * TITAN_VMC_CLUSTER_SIZE + cluster_offset,
                out + total,
                chunk))
        {
            TitanStorageIo_Close(descriptor);
            return -1;
        }
        total += chunk;
        remaining -= chunk;
        cluster_offset = 0u;
        if (remaining != 0u)
        {
            unsigned int fat_value;
            if (++steps > TITAN_VMC_ALLOC_END ||
                !TitanVmc_ReadFatEntry(descriptor, allocation_index, &fat_value) ||
                fat_value == TITAN_VMC_FAT_END ||
                (fat_value & TITAN_VMC_FAT_ALLOCATED_BIT) == 0u)
            {
                TitanStorageIo_Close(descriptor);
                return -1;
            }
            fat_value &= TITAN_VMC_FAT_CLUSTER_MASK;
            if (fat_value >= TITAN_VMC_ALLOC_END || fat_value == allocation_index)
            {
                TitanStorageIo_Close(descriptor);
                return -1;
            }
            allocation_index = fat_value;
        }
    }
    TitanStorageIo_Close(descriptor);
    return (int)total;
}

int TitanVmc_BuildNeutrinoArgument(
    const TitanUserProfile *profile,
    int port,
    char *destination,
    unsigned int capacity
)
{
    const char *prefix;
    unsigned int prefix_length;
    unsigned int path_length;

    if (
        profile == 0 ||
        destination == 0 ||
        capacity == 0u ||
        (port != 0 && port != 1) ||
        (TitanVmc_GetCachedStatus(profile) != TITAN_VMC_STATUS_READY &&
         TitanVmc_RefreshStatus(profile) != TITAN_VMC_STATUS_READY)
    )
    {
        if (destination != 0 && capacity > 0u)
        {
            destination[0] = '\0';
        }
        return 0;
    }

    prefix = port == 0 ? "-mc0=" : "-mc1=";
    prefix_length = (unsigned int)strlen(prefix);
    path_length = (unsigned int)strlen(profile->vmc_path);

    if (prefix_length + path_length + 1u > capacity)
    {
        destination[0] = '\0';
        return 0;
    }

    memcpy(destination, prefix, prefix_length);
    memcpy(
        destination + prefix_length,
        profile->vmc_path,
        path_length + 1u
    );
    return 1;
}

const char *TitanVmc_GetStatusName(
    TitanVmcStatus status
)
{
    switch (status)
    {
        case TITAN_VMC_STATUS_READY:
            return "Ready";
        case TITAN_VMC_STATUS_NEEDS_FORMAT:
            return "Needs format";
        case TITAN_VMC_STATUS_MISSING:
            return "Not created";
        case TITAN_VMC_STATUS_INVALID_SIZE:
            return "Invalid size";
        case TITAN_VMC_STATUS_CORRUPT:
            return "Invalid data";
        case TITAN_VMC_STATUS_CHECKING:
            return "Checking...";
        case TITAN_VMC_STATUS_IO_ERROR:
        default:
            return "Unavailable";
    }
}
