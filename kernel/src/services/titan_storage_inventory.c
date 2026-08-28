#include "titan_storage_inventory.h"
#include "titan_applications.h"
#include "titan_theme.h"
#include "titan_storage_io.h"
#include "titan_storage_paths.h"
#include "titan_storage_scheduler.h"

#include <stdio.h>
#include <string.h>

/* One directory entry is consumed per storage-worker quantum.  This is
 * intentionally conservative: Settings may inventory multi-gigabyte native
 * games without monopolizing the single FileXio lane or starving audio/network
 * QoS.  No filesystem operation is performed from the render/frame path. */
#define TITAN_STORAGE_INVENTORY_MAX_DEPTH 12u
#define TITAN_STORAGE_INVENTORY_MAX_NODES 8192u
#define TITAN_STORAGE_INVENTORY_MAX_DIRECTORY_ENTRIES 512u
#define TITAN_STORAGE_INVENTORY_SCAN_PATH_CAPACITY \
    (TITAN_STORAGE_INVENTORY_PATH_CAPACITY + TITAN_STORAGE_IO_ENTRY_NAME_CAPACITY + 2u)

typedef struct TitanStorageInventoryScanFrame
{
    char path[TITAN_STORAGE_INVENTORY_SCAN_PATH_CAPACITY];
    unsigned int next_index;
} TitanStorageInventoryScanFrame;

typedef struct TitanStorageInventoryState
{
    TitanStorageInventorySnapshot snapshot;
    TitanStorageInventoryEntry entries[TITAN_STORAGE_INVENTORY_MAX_ENTRIES];
    volatile int work_pending;
    unsigned int work_index;
    unsigned int work_nodes;
    unsigned int scan_nodes;

    int scan_active;
    int scan_ok;
    int scan_is_titan_root;
    uint64_t scan_total;
    unsigned int scan_depth;
    TitanStorageInventoryScanFrame scan_stack[TITAN_STORAGE_INVENTORY_MAX_DEPTH + 1u];
} TitanStorageInventoryState;

static TitanStorageInventoryState g_inventory;

static void copy_text(char *dst, unsigned int cap, const char *src)
{
    unsigned int i = 0u;
    if (!dst || cap == 0u) return;
    while (src && src[i] != '\0' && i + 1u < cap) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int path_join(char *dst, unsigned int cap, const char *root, const char *name)
{
    size_t root_len;
    const char *separator;
    int n;
    if (!dst || !root || !name || cap == 0u) return 0;
    root_len = strlen(root);
    separator = (root_len != 0u && root[root_len - 1u] != '/' && root[root_len - 1u] != ':') ? "/" : "";
    n = snprintf(dst, cap, "%s%s%s", root, separator, name);
    return n >= 0 && (unsigned int)n < cap;
}

static void saturating_add(uint64_t *total, uint64_t value)
{
    if (!total) return;
    if (UINT64_MAX - *total < value) *total = UINT64_MAX;
    else *total += value;
}

static void reset_scan_cursor(void)
{
    g_inventory.scan_active = 0;
    g_inventory.scan_ok = 1;
    g_inventory.scan_is_titan_root = 0;
    g_inventory.scan_total = 0u;
    g_inventory.scan_nodes = 0u;
    g_inventory.scan_depth = 0u;
    memset(g_inventory.scan_stack, 0, sizeof(g_inventory.scan_stack));
}

static int begin_scan_path(const char *path, int titan_root)
{
    TitanStorageIoSize size = 0u;
    int is_dir = 0;

    reset_scan_cursor();
    g_inventory.scan_is_titan_root = titan_root ? 1 : 0;
    if (!path || !path[0]) {
        g_inventory.scan_ok = 0;
        g_inventory.scan_active = 1;
        return 1;
    }

    g_inventory.work_nodes++;
    g_inventory.scan_nodes++;
    if (!TitanStorageIo_PathInfo64(path, &is_dir, &size)) {
        g_inventory.scan_ok = 0;
        g_inventory.scan_active = 1;
        return 1;
    }
    if (!is_dir) {
        g_inventory.scan_total = (uint64_t)size;
        g_inventory.scan_active = 1;
        return 1;
    }

    copy_text(g_inventory.scan_stack[0].path,
        sizeof(g_inventory.scan_stack[0].path), path);
    g_inventory.scan_stack[0].next_index = 0u;
    g_inventory.scan_depth = 1u;
    g_inventory.scan_active = 1;
    return 1;
}

static int scan_quantum(void)
{
    TitanStorageIoEntry entry;
    TitanStorageInventoryScanFrame *frame;
    char child[TITAN_STORAGE_INVENTORY_SCAN_PATH_CAPACITY];
    int got;

    if (!g_inventory.scan_active) return 0;
    if (!g_inventory.scan_ok || g_inventory.scan_depth == 0u)
        return 2; /* target complete */
    if (g_inventory.scan_nodes >= TITAN_STORAGE_INVENTORY_MAX_NODES) {
        g_inventory.scan_ok = 0;
        return 2;
    }

    frame = &g_inventory.scan_stack[g_inventory.scan_depth - 1u];
    memset(&entry, 0, sizeof(entry));
    got = TitanStorageIo_ListDirectory(frame->path, frame->next_index, &entry, 1u);
    if (got < 0) {
        g_inventory.scan_ok = 0;
        return 2;
    }
    if (got == 0) {
        g_inventory.scan_depth--;
        return g_inventory.scan_depth == 0u ? 2 : 1;
    }
    /* TitanStorageIo_ListDirectory is paged/reopen-based. Bound the maximum
     * skip length of a single quantum so an unexpectedly huge flat directory
     * can degrade this optional size estimate to unavailable rather than
     * monopolizing the storage worker for an unbounded period. */
    if (frame->next_index >= TITAN_STORAGE_INVENTORY_MAX_DIRECTORY_ENTRIES) {
        g_inventory.scan_ok = 0;
        return 2;
    }

    frame->next_index++;
    if (!path_join(child, sizeof(child), frame->path, entry.name)) {
        g_inventory.scan_ok = 0;
        return 1;
    }

    g_inventory.work_nodes++;
    g_inventory.scan_nodes++;
    if ((entry.flags & TITAN_STORAGE_IO_ENTRY_DIRECTORY) != 0u) {
        if (g_inventory.scan_depth >= TITAN_STORAGE_INVENTORY_MAX_DEPTH + 1u) {
            g_inventory.scan_ok = 0;
            return 1;
        }
        copy_text(g_inventory.scan_stack[g_inventory.scan_depth].path,
            sizeof(g_inventory.scan_stack[g_inventory.scan_depth].path), child);
        g_inventory.scan_stack[g_inventory.scan_depth].next_index = 0u;
        g_inventory.scan_depth++;
    } else {
        saturating_add(&g_inventory.scan_total, (uint64_t)entry.size);
    }
    return 1;
}

static void finish_current_target(void)
{
    if (g_inventory.scan_is_titan_root) {
        g_inventory.snapshot.titan_bytes = g_inventory.scan_total;
        g_inventory.snapshot.titan_bytes_valid = g_inventory.scan_ok;
        if (!g_inventory.scan_ok) g_inventory.snapshot.scan_errors++;
        g_inventory.snapshot.scanning = 0;
        g_inventory.work_pending = 0;
        g_inventory.snapshot.generation++;
        if (g_inventory.snapshot.generation == 0u) g_inventory.snapshot.generation = 1u;
        reset_scan_cursor();
        return;
    }

    if (g_inventory.work_index < g_inventory.snapshot.count) {
        TitanStorageInventoryEntry *entry = &g_inventory.entries[g_inventory.work_index];
        entry->bytes = g_inventory.scan_total;
        entry->bytes_valid = g_inventory.scan_ok;
        if (!g_inventory.scan_ok) g_inventory.snapshot.scan_errors++;
        if (entry->kind == TITAN_STORAGE_INVENTORY_APPLICATION)
            saturating_add(&g_inventory.snapshot.application_bytes, g_inventory.scan_total);
        else if (entry->kind == TITAN_STORAGE_INVENTORY_NATIVE_GAME)
            saturating_add(&g_inventory.snapshot.native_game_bytes, g_inventory.scan_total);
        else if (entry->kind == TITAN_STORAGE_INVENTORY_THEME)
            saturating_add(&g_inventory.snapshot.theme_bytes, g_inventory.scan_total);
        g_inventory.work_index++;
        g_inventory.snapshot.scan_index = g_inventory.work_index;
    }
    reset_scan_cursor();
}

static void rebuild_catalog(void)
{
    unsigned int i;
    unsigned int count = 0u;
    unsigned int next_generation = g_inventory.snapshot.generation + 1u;
    if (!next_generation) next_generation = 1u;

    memset(g_inventory.entries, 0, sizeof(g_inventory.entries));
    memset(&g_inventory.snapshot, 0, sizeof(g_inventory.snapshot));
    g_inventory.snapshot.generation = next_generation;
    reset_scan_cursor();

    for (i = 0u; i < TitanApplications_GetCount() && count < TITAN_STORAGE_INVENTORY_MAX_ENTRIES; i++) {
        const TitanApplication *app = TitanApplications_Get(i);
        TitanStorageInventoryEntry *entry;
        if (!app || !app->installed || app->type == TITAN_APPLICATION_SYSTEM_BROWSER) continue;
        if (app->root_path[0] == '\0') continue;
        entry = &g_inventory.entries[count++];
        entry->kind = app->type == TITAN_APPLICATION_TITAN_GAME
            ? TITAN_STORAGE_INVENTORY_NATIVE_GAME
            : TITAN_STORAGE_INVENTORY_APPLICATION;
        entry->source_index = i;
        copy_text(entry->name, sizeof(entry->name), app->title);
        copy_text(entry->path, sizeof(entry->path), app->root_path);
        if (entry->kind == TITAN_STORAGE_INVENTORY_NATIVE_GAME)
            g_inventory.snapshot.native_game_count++;
        else
            g_inventory.snapshot.application_count++;
    }

    for (i = 0u; i < TitanTheme_GetCount() && count < TITAN_STORAGE_INVENTORY_MAX_ENTRIES; i++) {
        const TitanThemeInfo *theme = TitanTheme_Get(i);
        TitanStorageInventoryEntry *entry;
        if (!theme || theme->built_in || theme->path[0] == '\0') continue;
        entry = &g_inventory.entries[count++];
        entry->kind = TITAN_STORAGE_INVENTORY_THEME;
        entry->source_index = i;
        copy_text(entry->name, sizeof(entry->name), theme->name);
        copy_text(entry->path, sizeof(entry->path), theme->path);
        g_inventory.snapshot.theme_count++;
    }

    g_inventory.snapshot.count = count;
    g_inventory.snapshot.scanning = 1;
    g_inventory.snapshot.scan_index = 0u;
    g_inventory.work_index = 0u;
    g_inventory.work_nodes = 0u;
    g_inventory.work_pending = 1;
}

void TitanStorageInventory_Initialize(void)
{
    memset(&g_inventory, 0, sizeof(g_inventory));
    g_inventory.snapshot.generation = 1u;
    reset_scan_cursor();
}

void TitanStorageInventory_RequestRefresh(void)
{
    rebuild_catalog();
    TitanStorageScheduler_NotifyBackgroundWork();
}

const TitanStorageInventorySnapshot *TitanStorageInventory_GetSnapshot(void)
{
    return &g_inventory.snapshot;
}

const TitanStorageInventoryEntry *TitanStorageInventory_GetEntry(unsigned int index)
{
    if (index >= g_inventory.snapshot.count) return 0;
    return &g_inventory.entries[index];
}

unsigned int TitanStorageInventory_GetKindCount(TitanStorageInventoryKind kind)
{
    unsigned int i, count = 0u;
    for (i = 0u; i < g_inventory.snapshot.count; i++)
        if (g_inventory.entries[i].kind == kind) count++;
    return count;
}

const TitanStorageInventoryEntry *TitanStorageInventory_GetKindEntry(
    TitanStorageInventoryKind kind,
    unsigned int visible_index)
{
    unsigned int i, count = 0u;
    for (i = 0u; i < g_inventory.snapshot.count; i++) {
        if (g_inventory.entries[i].kind != kind) continue;
        if (count++ == visible_index) return &g_inventory.entries[i];
    }
    return 0;
}

int TitanStorageInventory_HasStorageWork(void)
{
    return g_inventory.work_pending != 0;
}

int TitanStorageInventory_StorageWorkerPump(void)
{
    int step;
    if (!g_inventory.work_pending) return 0;

    if (!g_inventory.scan_active) {
        if (g_inventory.work_index < g_inventory.snapshot.count)
            (void)begin_scan_path(g_inventory.entries[g_inventory.work_index].path, 0);
        else
            (void)begin_scan_path(TITAN_STORAGE_DEVICE_ROOT, 1);
        return 1;
    }

    step = scan_quantum();
    if (step == 2) finish_current_target();
    return 1;
}
