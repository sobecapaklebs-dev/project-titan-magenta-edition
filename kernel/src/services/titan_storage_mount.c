#include "titan_storage_mount.h"

#include <stdio.h>
#include <string.h>

typedef struct TitanStorageMountSlot
{
    TitanStorageMountInfo info;
    uint8_t allocated;
} TitanStorageMountSlot;

static TitanStorageMountSlot g_mounts[TITAN_STORAGE_MOUNT_MAX_SLOTS];
static TitanStoragePfsProvider g_pfs_provider;
static void *g_pfs_context;
static uint8_t g_pfs_registered;
static uint32_t g_generation = 1u;

static int TitanStorageMount_Copy(char *out, uint32_t capacity, const char *text)
{
    size_t length;
    if (out == 0 || capacity == 0u || text == 0) return 0;
    length = strlen(text);
    if (length >= capacity) return 0;
    memcpy(out, text, length + 1u);
    return 1;
}

static int TitanStorageMount_SourceSafe(const char *source)
{
    const char *p;
    if (source == 0 || strncmp(source, "hdd0:", 5u) != 0 || source[5] == '\0') return 0;
    for (p = source; *p != '\0'; ++p)
    {
        if (*p == '\\' || *p == '\r' || *p == '\n') return 0;
    }
    if (strstr(source, "..") != 0) return 0;
    return strlen(source) < TITAN_STORAGE_MOUNT_SOURCE_CAPACITY;
}

static int TitanStorageMount_RootForSlot(uint32_t slot, char *out, uint32_t capacity)
{
    int count;
    if (slot >= TITAN_STORAGE_MOUNT_MAX_SLOTS || out == 0 || capacity == 0u) return 0;
    count = snprintf(out, capacity, "pfs%u:/", (unsigned int)slot);
    return count > 0 && (uint32_t)count < capacity;
}

void TitanStorageMount_Initialize(void)
{
    memset(g_mounts, 0, sizeof(g_mounts));
    memset(&g_pfs_provider, 0, sizeof(g_pfs_provider));
    g_pfs_context = 0;
    g_pfs_registered = 0u;
    g_generation = 1u;
}

int TitanStorageMount_RegisterPfsProvider(const TitanStoragePfsProvider *provider, void *context)
{
    uint32_t i;
    if (provider == 0 || provider->available == 0 || provider->mount == 0 || provider->unmount == 0)
        return 0;
    for (i = 0u; i < TITAN_STORAGE_MOUNT_MAX_SLOTS; ++i)
        if (g_mounts[i].allocated && g_mounts[i].info.state == TITAN_STORAGE_MOUNT_ONLINE)
            return 0;
    g_pfs_provider = *provider;
    g_pfs_context = context;
    g_pfs_registered = 1u;
    return 1;
}

int TitanStorageMount_PfsAvailable(void)
{
    return g_pfs_registered && g_pfs_provider.available(g_pfs_context) != 0;
}

int TitanStorageMount_MountPfs(const char *source, uint32_t requested_slot, uint32_t capabilities, TitanStorageMountInfo *out)
{
    TitanStorageMountSlot *slot;
    uint32_t i;
    char root[TITAN_STORAGE_MOUNT_ROOT_CAPACITY];
    if (!TitanStorageMount_PfsAvailable() || !TitanStorageMount_SourceSafe(source) ||
        requested_slot >= TITAN_STORAGE_MOUNT_MAX_SLOTS ||
        (capabilities & ~(TITAN_STORAGE_MOUNT_CAP_READ | TITAN_STORAGE_MOUNT_CAP_WRITE)) != 0u ||
        (capabilities & TITAN_STORAGE_MOUNT_CAP_READ) == 0u ||
        !TitanStorageMount_RootForSlot(requested_slot, root, sizeof(root))) return 0;
    for (i = 0u; i < TITAN_STORAGE_MOUNT_MAX_SLOTS; ++i)
    {
        if (g_mounts[i].allocated && g_mounts[i].info.state == TITAN_STORAGE_MOUNT_ONLINE &&
            (i == requested_slot || strcmp(g_mounts[i].info.source, source) == 0)) return 0;
    }
    slot = &g_mounts[requested_slot];
    memset(slot, 0, sizeof(*slot));
    slot->allocated = 1u;
    slot->info.slot = requested_slot;
    slot->info.generation = g_generation++;
    slot->info.capabilities = capabilities;
    slot->info.state = TITAN_STORAGE_MOUNT_MOUNTING;
    if (!TitanStorageMount_Copy(slot->info.source, sizeof(slot->info.source), source) ||
        !TitanStorageMount_Copy(slot->info.root, sizeof(slot->info.root), root) ||
        !g_pfs_provider.mount(g_pfs_context, source, root, capabilities))
    {
        slot->info.state = TITAN_STORAGE_MOUNT_ERROR;
        if (out != 0) *out = slot->info;
        return 0;
    }
    slot->info.state = TITAN_STORAGE_MOUNT_ONLINE;
    if (out != 0) *out = slot->info;
    return 1;
}

int TitanStorageMount_Unmount(uint32_t slot_index)
{
    TitanStorageMountSlot *slot;
    if (slot_index >= TITAN_STORAGE_MOUNT_MAX_SLOTS) return 0;
    slot = &g_mounts[slot_index];
    if (!slot->allocated || slot->info.state != TITAN_STORAGE_MOUNT_ONLINE || !g_pfs_registered)
        return 0;
    slot->info.state = TITAN_STORAGE_MOUNT_UNMOUNTING;
    if (!g_pfs_provider.unmount(g_pfs_context, slot->info.root))
    {
        slot->info.state = TITAN_STORAGE_MOUNT_ERROR;
        return 0;
    }
    slot->info.state = TITAN_STORAGE_MOUNT_OFFLINE;
    slot->info.capabilities = 0u;
    slot->info.generation = g_generation++;
    return 1;
}

int TitanStorageMount_Get(uint32_t slot_index, TitanStorageMountInfo *out)
{
    if (out == 0 || slot_index >= TITAN_STORAGE_MOUNT_MAX_SLOTS || !g_mounts[slot_index].allocated) return 0;
    *out = g_mounts[slot_index].info;
    return 1;
}

int TitanStorageMount_List(uint32_t index, TitanStorageMountInfo *out)
{
    uint32_t i;
    uint32_t seen = 0u;
    if (out == 0) return 0;
    for (i = 0u; i < TITAN_STORAGE_MOUNT_MAX_SLOTS; ++i)
    {
        if (!g_mounts[i].allocated || g_mounts[i].info.state == TITAN_STORAGE_MOUNT_OFFLINE) continue;
        if (seen++ == index) { *out = g_mounts[i].info; return 1; }
    }
    return 0;
}
