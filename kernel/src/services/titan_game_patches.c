#include "titan_game_patches.h"

#include "titan_storage_io.h"
#include "titan_storage_paths.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define TITAN_GAME_PATCH_CHEAT_ROOT "mass:/CHT"
#define TITAN_GAME_PATCH_ENHANCEMENT_ROOT "mass:/ENH"
#define TITAN_GAME_PATCH_STATE_ROOT TITAN_STORAGE_DEVICE_ROOT "/gamepatches"
#define TITAN_GAME_PATCH_TEXT_LIMIT (128u * 1024u)
#define TITAN_GAME_PATCH_CATALOG_CODE_CAPACITY 2048u
#define TITAN_GAME_PATCH_PAYLOAD_PAIR_CAPACITY 254u
#define TITAN_GAME_PATCH_PS2RD_MAX_HOOKS 5u
#define TITAN_GAME_PATCH_PS2RD_MAX_CODES 250u
#define TITAN_GAME_PATCH_LINE_CAPACITY 320u
#define TITAN_GAME_PATCH_UL_CHT_ROOT "mass:/CHT"
#define TITAN_GAME_PATCH_UL_CFG_ROOT "mass:/CFG"
#define TITAN_GAME_PATCH_UL_CFG_MAX_BYTES (32u * 1024u)
#define TITAN_GAME_PATCH_UL_CHT_MAX_BYTES TITAN_GAME_PATCH_TEXT_LIMIT
#define TITAN_GAME_PATCH_UL_JOURNAL_MAGIC 0x4a4c5554u /* TULJ */
#define TITAN_GAME_PATCH_UL_JOURNAL_VERSION 1u

#define TITAN_PATCH_PAYLOAD_MAGIC 0x48435054u /* TPCH, little endian */
#define TITAN_PATCH_PAYLOAD_VERSION 1u
#define TITAN_PATCH_PAYLOAD_GAMEPLAY 0x0001u
#define TITAN_PATCH_PAYLOAD_ENHANCEMENT 0x0002u

typedef struct TitanGamePatchCode
{
    uint32_t address;
    uint32_t value;
} TitanGamePatchCode;

typedef struct TitanGamePatchEntry
{
    char name[TITAN_GAME_PATCH_NAME_CAPACITY];
    unsigned int code_first;
    unsigned int code_count;
    unsigned int state_hash;
    unsigned char enabled;
    unsigned char automatic;
    unsigned char experimental;
    unsigned char master;
} TitanGamePatchEntry;

typedef struct TitanGamePatchCatalog
{
    char startup[16];
    TitanGamePatchEntry entries[TITAN_GAME_PATCH_ENTRY_CAPACITY];
    TitanGamePatchCode codes[TITAN_GAME_PATCH_CATALOG_CODE_CAPACITY];
    unsigned int entry_count;
    unsigned int code_count;
    int loaded;
    int file_present;
} TitanGamePatchCatalog;

typedef struct TitanPatchPayloadHeader
{
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t pair_count;
    uint32_t checksum;
} __attribute__((packed)) TitanPatchPayloadHeader;

typedef struct TitanGamePatchUlJournal
{
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t cht_present;
    uint32_t cfg_present;
} TitanGamePatchUlJournal;

static TitanGamePatchCatalog g_catalogs[TITAN_GAME_PATCH_KIND_COUNT];

static int TitanGamePatches_IsValidStartup(const char *startup)
{
    unsigned int index;

    if (startup == 0 || strlen(startup) != 11u)
        return 0;
    for (index = 0u; index < 4u; index++)
        if (!isalnum((unsigned char)startup[index])) return 0;
    if (startup[4] != '_') return 0;
    for (index = 5u; index < 8u; index++)
        if (!isdigit((unsigned char)startup[index])) return 0;
    if (startup[8] != '.') return 0;
    return isdigit((unsigned char)startup[9]) &&
        isdigit((unsigned char)startup[10]);
}

static unsigned int TitanGamePatches_FnvStep(unsigned int hash, unsigned char value)
{
    hash ^= (unsigned int)value;
    hash *= 16777619u;
    return hash;
}

static unsigned int TitanGamePatches_HashName(
    const char *name,
    unsigned int ordinal
)
{
    unsigned int hash = 2166136261u;
    const unsigned char *cursor = (const unsigned char *)name;

    while (cursor != 0 && *cursor != '\0')
    {
        hash = TitanGamePatches_FnvStep(
            hash,
            (unsigned char)tolower((int)*cursor)
        );
        cursor++;
    }
    hash = TitanGamePatches_FnvStep(hash, (unsigned char)(ordinal & 0xffu));
    hash = TitanGamePatches_FnvStep(hash, (unsigned char)((ordinal >> 8) & 0xffu));
    return hash;
}

static unsigned int TitanGamePatches_PayloadChecksum(
    const TitanGamePatchCode *codes,
    unsigned int count
)
{
    unsigned int hash = 2166136261u;
    unsigned int index;
    int shift;

    for (index = 0u; index < count; index++)
    {
        for (shift = 0; shift < 32; shift += 8)
            hash = TitanGamePatches_FnvStep(
                hash,
                (unsigned char)((codes[index].address >> shift) & 0xffu)
            );
        for (shift = 0; shift < 32; shift += 8)
            hash = TitanGamePatches_FnvStep(
                hash,
                (unsigned char)((codes[index].value >> shift) & 0xffu)
            );
    }
    return hash;
}

static void TitanGamePatches_Trim(char *text)
{
    char *first;
    char *last;

    if (text == 0 || text[0] == '\0') return;
    first = text;
    while (*first != '\0' && isspace((unsigned char)*first)) first++;
    if (first != text) memmove(text, first, strlen(first) + 1u);
    if (text[0] == '\0') return;
    last = text + strlen(text) - 1;
    while (last >= text && isspace((unsigned char)*last))
        *last-- = '\0';
}

static int TitanGamePatches_ContainsInsensitive(
    const char *text,
    const char *needle
)
{
    size_t needle_length;

    if (text == 0 || needle == 0) return 0;
    needle_length = strlen(needle);
    if (needle_length == 0u) return 1;
    while (*text != '\0')
    {
        if (!strncasecmp(text, needle, needle_length)) return 1;
        text++;
    }
    return 0;
}

static int TitanGamePatches_IsMasterName(const char *name)
{
    return
        TitanGamePatches_ContainsInsensitive(name, "master code") ||
        TitanGamePatches_ContainsInsensitive(name, "mastercode") ||
        TitanGamePatches_ContainsInsensitive(name, "enable code") ||
        TitanGamePatches_ContainsInsensitive(name, "must be on") ||
        TitanGamePatches_ContainsInsensitive(name, "codigo mestre") ||
        TitanGamePatches_ContainsInsensitive(name, "código mestre") ||
        TitanGamePatches_ContainsInsensitive(name, "codigo maestro") ||
        TitanGamePatches_ContainsInsensitive(name, "código maestro");
}

static int TitanGamePatches_ParseCode(
    const char *text,
    TitanGamePatchCode *code
)
{
    char digits[17];
    unsigned int count = 0u;
    const char *cursor;
    unsigned long address;
    unsigned long value;
    char left[9];
    char right[9];

    if (text == 0 || code == 0) return 0;
    for (cursor = text; *cursor != '\0'; cursor++)
    {
        if (isxdigit((unsigned char)*cursor))
        {
            if (count >= 16u) return 0;
            digits[count++] = *cursor;
        }
        else if (!isspace((unsigned char)*cursor))
        {
            return 0;
        }
    }
    if (count != 16u) return 0;
    digits[16] = '\0';
    memcpy(left, digits, 8u);
    left[8] = '\0';
    memcpy(right, digits + 8u, 8u);
    right[8] = '\0';
    address = strtoul(left, 0, 16);
    value = strtoul(right, 0, 16);
    code->address = (uint32_t)address;
    code->value = (uint32_t)value;
    return code->address != 0u || code->value != 0u;
}

static void TitanGamePatches_StripInlineComment(char *line)
{
    char *hash;
    char *slash;
    char *cut = 0;

    if (line == 0) return;
    hash = strchr(line, '#');
    slash = strstr(line, "//");
    if (hash != 0) cut = hash;
    if (slash != 0 && (cut == 0 || slash < cut)) cut = slash;
    if (cut != 0) *cut = '\0';
    TitanGamePatches_Trim(line);
}

static int TitanGamePatches_BuildSourcePath(
    const char *startup,
    TitanGamePatchKind kind,
    char *path,
    unsigned int capacity
)
{
    const char *root;
    const char *extension;
    int written;

    if (path == 0 || capacity == 0u) return 0;
    root = kind == TITAN_GAME_PATCH_CHEAT ?
        TITAN_GAME_PATCH_CHEAT_ROOT : TITAN_GAME_PATCH_ENHANCEMENT_ROOT;
    extension = kind == TITAN_GAME_PATCH_CHEAT ? ".cht" : ".enh";
    written = snprintf(path, capacity, "%s/%s%s", root, startup, extension);
    return written >= 0 && (unsigned int)written < capacity;
}

static int TitanGamePatches_BuildStatePath(
    const char *startup,
    TitanGamePatchKind kind,
    char *path,
    unsigned int capacity,
    int temporary
)
{
    const char *suffix = kind == TITAN_GAME_PATCH_CHEAT ? "cht" : "enh";
    int written = snprintf(
        path,
        capacity,
        "%s/%s.%s.state%s",
        TITAN_GAME_PATCH_STATE_ROOT,
        startup,
        suffix,
        temporary ? ".tmp" : ""
    );
    return written >= 0 && (unsigned int)written < capacity;
}

static int TitanGamePatches_BuildPayloadPath(
    const char *startup,
    char *path,
    unsigned int capacity,
    int temporary
)
{
    int written = snprintf(
        path,
        capacity,
        "%s/%s.tpch%s",
        TITAN_GAME_PATCH_STATE_ROOT,
        startup,
        temporary ? ".tmp" : ""
    );
    return written >= 0 && (unsigned int)written < capacity;
}

static int TitanGamePatches_EnsureStateDirectory(void)
{
    int is_directory = 0;

    if (TitanStorageIo_PathInfo64(TITAN_STORAGE_DEVICE_ROOT, &is_directory, 0))
    {
        if (!is_directory) return 0;
    }
    else if (TitanStorageIo_CreateDirectory(TITAN_STORAGE_DEVICE_ROOT) < 0)
    {
        return 0;
    }

    is_directory = 0;
    if (TitanStorageIo_PathInfo64(TITAN_GAME_PATCH_STATE_ROOT, &is_directory, 0))
        return is_directory;
    return TitanStorageIo_CreateDirectory(TITAN_GAME_PATCH_STATE_ROOT) >= 0;
}

static char *TitanGamePatches_ReadTextFile(
    const char *path,
    TitanGamePatchResult *result
)
{
    TitanStorageIoSize size = 0u;
    int is_directory = 0;
    int descriptor;
    char *buffer;

    if (result != 0) *result = TITAN_GAME_PATCH_IO_ERROR;
    if (!TitanStorageIo_PathInfo64(path, &is_directory, &size) || is_directory)
        return 0;
    if (size > TITAN_GAME_PATCH_TEXT_LIMIT)
    {
        if (result != 0) *result = TITAN_GAME_PATCH_TOO_LARGE;
        return 0;
    }
    buffer = (char *)malloc((size_t)size + 1u);
    if (buffer == 0) return 0;
    descriptor = TitanStorageIo_Open(path, TITAN_STORAGE_IO_READ);
    if (descriptor < 0)
    {
        free(buffer);
        return 0;
    }
    if (size > 0u && !TitanStorageIo_ReadExact(descriptor, buffer, (size_t)size))
    {
        TitanStorageIo_Close(descriptor);
        free(buffer);
        return 0;
    }
    TitanStorageIo_Close(descriptor);
    buffer[(size_t)size] = '\0';
    if (result != 0) *result = TITAN_GAME_PATCH_OK;
    return buffer;
}


static int TitanGamePatches_EnsureDirectoryPath(const char *path)
{
    int is_directory = 0;
    if (TitanStorageIo_PathInfo64(path, &is_directory, 0)) return is_directory;
    if (TitanStorageIo_CreateDirectory(path) == 0) return 1;
    return TitanStorageIo_PathInfo64(path, &is_directory, 0) && is_directory;
}

static int TitanGamePatches_BuildUlPath(
    const char *startup,
    const char *kind,
    char *path,
    unsigned int capacity
)
{
    int written;
    if (!TitanGamePatches_IsValidStartup(startup) || kind == 0 || path == 0 || capacity == 0u)
        return 0;
    if (!strcmp(kind, "cht"))
        written = snprintf(path, capacity, TITAN_GAME_PATCH_UL_CHT_ROOT "/%s.cht", startup);
    else if (!strcmp(kind, "cfg"))
        written = snprintf(path, capacity, TITAN_GAME_PATCH_UL_CFG_ROOT "/%s.cfg", startup);
    else
        written = snprintf(path, capacity, TITAN_GAME_PATCH_STATE_ROOT "/%s.ul.%s", startup, kind);
    return written >= 0 && (unsigned int)written < capacity;
}

static int TitanGamePatches_WritePrivateFile(
    const char *path,
    const void *data,
    size_t size
)
{
    int fd = TitanStorageIo_Open(
        path,
        TITAN_STORAGE_IO_WRITE | TITAN_STORAGE_IO_CREATE | TITAN_STORAGE_IO_TRUNCATE
    );
    int ok;
    if (fd < 0) return 0;
    ok = (size == 0u || TitanStorageIo_WriteExact(fd, data, size));
    if (ok && TitanStorageIo_Flush(fd) < 0) ok = 0;
    if (TitanStorageIo_Close(fd) < 0) ok = 0;
    if (!ok) (void)TitanStorageIo_Remove(path);
    return ok;
}

static int TitanGamePatches_Snapshot(
    const char *source,
    const char *backup,
    TitanStorageIoSize maximum,
    uint32_t *present
)
{
    TitanStorageIoSize size = 0u;
    int is_directory = 0;
    int fd = -1;
    unsigned char *buffer = 0;
    int ok = 0;

    if (present == 0) return 0;
    *present = 0u;
    if (!TitanStorageIo_PathInfo64(source, &is_directory, &size))
    {
        (void)TitanStorageIo_Remove(backup);
        return 1;
    }
    if (is_directory || size > maximum || size > (TitanStorageIoSize)SIZE_MAX) return 0;
    buffer = (unsigned char *)malloc(size > 0u ? (size_t)size : 1u);
    if (buffer == 0) return 0;
    if (size > 0u)
    {
        fd = TitanStorageIo_Open(source, TITAN_STORAGE_IO_READ);
        if (fd < 0 || !TitanStorageIo_ReadExact(fd, buffer, (size_t)size)) goto cleanup;
        if (TitanStorageIo_Close(fd) < 0) { fd = -1; goto cleanup; }
        fd = -1;
    }
    if (!TitanGamePatches_WritePrivateFile(backup, buffer, (size_t)size)) goto cleanup;
    *present = 1u;
    ok = 1;
cleanup:
    if (fd >= 0) (void)TitanStorageIo_Close(fd);
    free(buffer);
    return ok;
}

static int TitanGamePatches_CopyFileAtomic(
    const char *source,
    const char *target,
    TitanStorageIoSize maximum
)
{
    TitanStorageIoSize size = 0u;
    int is_directory = 0;
    int fd = -1;
    unsigned char *buffer = 0;
    char temporary[320];
    char swap_backup[320];
    int ok = 0;

    if (!TitanStorageIo_PathInfo64(source, &is_directory, &size) || is_directory ||
        size > maximum || size > (TitanStorageIoSize)SIZE_MAX) return 0;
    if (snprintf(temporary, sizeof(temporary), "%s.titan-restore.tmp", target) >= (int)sizeof(temporary) ||
        snprintf(swap_backup, sizeof(swap_backup), "%s.titan-restore.bak", target) >= (int)sizeof(swap_backup))
        return 0;
    buffer = (unsigned char *)malloc(size > 0u ? (size_t)size : 1u);
    if (buffer == 0) return 0;
    if (size > 0u)
    {
        fd = TitanStorageIo_Open(source, TITAN_STORAGE_IO_READ);
        if (fd < 0 || !TitanStorageIo_ReadExact(fd, buffer, (size_t)size)) goto cleanup;
        if (TitanStorageIo_Close(fd) < 0) { fd = -1; goto cleanup; }
        fd = -1;
    }
    (void)TitanStorageIo_Remove(temporary);
    if (!TitanGamePatches_WritePrivateFile(temporary, buffer, (size_t)size)) goto cleanup;
    if (!TitanStorageIo_AtomicReplace(temporary, target, swap_backup)) goto cleanup;
    (void)TitanStorageIo_Remove(swap_backup);
    ok = 1;
cleanup:
    if (fd >= 0) (void)TitanStorageIo_Close(fd);
    if (!ok) (void)TitanStorageIo_Remove(temporary);
    free(buffer);
    return ok;
}

static int TitanGamePatches_RecoverUlStartup(const char *startup)
{
    char journal_path[256], cht_path[256], cfg_path[256], cht_backup[256], cfg_backup[256];
    TitanGamePatchUlJournal journal;
    int fd = -1;
    int cht_ok, cfg_ok;

    if (!TitanGamePatches_BuildUlPath(startup, "journal", journal_path, sizeof(journal_path)) ||
        !TitanGamePatches_BuildUlPath(startup, "cht", cht_path, sizeof(cht_path)) ||
        !TitanGamePatches_BuildUlPath(startup, "cfg", cfg_path, sizeof(cfg_path)) ||
        !TitanGamePatches_BuildUlPath(startup, "cht.original", cht_backup, sizeof(cht_backup)) ||
        !TitanGamePatches_BuildUlPath(startup, "cfg.original", cfg_backup, sizeof(cfg_backup)))
        return 0;

    if (!TitanStorageIo_PathInfo64(journal_path, 0, 0)) return 1;
    fd = TitanStorageIo_Open(journal_path, TITAN_STORAGE_IO_READ);
    if (fd < 0 || !TitanStorageIo_ReadExact(fd, &journal, sizeof(journal))) goto invalid;
    (void)TitanStorageIo_Close(fd); fd = -1;
    if (journal.magic != TITAN_GAME_PATCH_UL_JOURNAL_MAGIC ||
        journal.version != TITAN_GAME_PATCH_UL_JOURNAL_VERSION ||
        journal.cht_present > 1u || journal.cfg_present > 1u) goto invalid;

    cht_ok = journal.cht_present ?
        TitanGamePatches_CopyFileAtomic(cht_backup, cht_path, TITAN_GAME_PATCH_UL_CHT_MAX_BYTES) :
        (TitanStorageIo_Remove(cht_path) == 0 || !TitanStorageIo_PathInfo64(cht_path, 0, 0));
    cfg_ok = journal.cfg_present ?
        TitanGamePatches_CopyFileAtomic(cfg_backup, cfg_path, TITAN_GAME_PATCH_UL_CFG_MAX_BYTES) :
        (TitanStorageIo_Remove(cfg_path) == 0 || !TitanStorageIo_PathInfo64(cfg_path, 0, 0));
    if (!cht_ok || !cfg_ok) return 0;

    (void)TitanStorageIo_Remove(cht_backup);
    (void)TitanStorageIo_Remove(cfg_backup);
    (void)TitanStorageIo_Remove(journal_path);
    return 1;

invalid:
    if (fd >= 0) (void)TitanStorageIo_Close(fd);
    return 0;
}

void TitanGamePatches_RecoverUlGoldenRuntime(void)
{
    TitanStorageIoEntry entries[16];
    unsigned int start = 0u;
    const char *suffix = ".ul.journal";
    size_t suffix_length = strlen(suffix);

    for (;;)
    {
        int count = TitanStorageIo_ListDirectory(
            TITAN_GAME_PATCH_STATE_ROOT, start, entries,
            (unsigned int)(sizeof(entries) / sizeof(entries[0]))
        );
        int i;
        if (count <= 0) break;
        for (i = 0; i < count; ++i)
        {
            size_t length = strlen(entries[i].name);
            if ((entries[i].flags & TITAN_STORAGE_IO_ENTRY_DIRECTORY) != 0u ||
                length != 11u + suffix_length || strcmp(entries[i].name + 11u, suffix) != 0)
                continue;
            {
                char startup[16];
                memcpy(startup, entries[i].name, 11u);
                startup[11] = '\0';
                if (TitanGamePatches_IsValidStartup(startup))
                    (void)TitanGamePatches_RecoverUlStartup(startup);
            }
        }
        start += (unsigned int)count;
    }
}

static int TitanGamePatches_VisibleToRawIndex(
    const TitanGamePatchCatalog *catalog,
    unsigned int visible_index,
    unsigned int *raw_index
)
{
    unsigned int index;
    unsigned int visible = 0u;

    if (catalog == 0 || raw_index == 0) return 0;
    for (index = 0u; index < catalog->entry_count; index++)
    {
        if (catalog->entries[index].master) continue;
        if (visible == visible_index)
        {
            *raw_index = index;
            return 1;
        }
        visible++;
    }
    return 0;
}

static void TitanGamePatches_ApplyState(TitanGamePatchCatalog *catalog, TitanGamePatchKind kind)
{
    char path[256];
    TitanGamePatchResult result;
    char *buffer;
    char *cursor;

    if (catalog == 0 || !TitanGamePatches_BuildStatePath(
            catalog->startup, kind, path, sizeof(path), 0))
        return;

    buffer = TitanGamePatches_ReadTextFile(path, &result);
    if (buffer == 0) return;
    cursor = buffer;
    while (*cursor != '\0')
    {
        char *end = strchr(cursor, '\n');
        char line[64];
        unsigned int length = end != 0 ? (unsigned int)(end - cursor) : (unsigned int)strlen(cursor);
        unsigned long hash;
        int enabled;
        unsigned int index;

        if (length >= sizeof(line)) length = sizeof(line) - 1u;
        memcpy(line, cursor, length);
        line[length] = '\0';
        TitanGamePatches_Trim(line);
        if (sscanf(line, "%lx=%d", &hash, &enabled) == 2)
        {
            for (index = 0u; index < catalog->entry_count; index++)
            {
                TitanGamePatchEntry *entry = &catalog->entries[index];
                if (!entry->master && entry->state_hash == (unsigned int)hash)
                {
                    entry->enabled = enabled ? 1u : 0u;
                    break;
                }
            }
        }
        if (end == 0) break;
        cursor = end + 1;
    }
    free(buffer);
}

static TitanGamePatchResult TitanGamePatches_SaveState(
    const TitanGamePatchCatalog *catalog,
    TitanGamePatchKind kind
)
{
    char target[256];
    char temporary[256];
    char backup[256];
    int descriptor;
    unsigned int index;

    if (catalog == 0 || !TitanGamePatches_EnsureStateDirectory())
        return TITAN_GAME_PATCH_IO_ERROR;
    if (!TitanGamePatches_BuildStatePath(catalog->startup, kind, target, sizeof(target), 0) ||
        !TitanGamePatches_BuildStatePath(catalog->startup, kind, temporary, sizeof(temporary), 1) ||
        snprintf(backup, sizeof(backup), "%s.bak", target) >= (int)sizeof(backup))
        return TITAN_GAME_PATCH_IO_ERROR;

    (void)TitanStorageIo_Remove(temporary);
    descriptor = TitanStorageIo_Open(
        temporary,
        TITAN_STORAGE_IO_WRITE | TITAN_STORAGE_IO_CREATE | TITAN_STORAGE_IO_TRUNCATE
    );
    if (descriptor < 0) return TITAN_GAME_PATCH_IO_ERROR;

    for (index = 0u; index < catalog->entry_count; index++)
    {
        const TitanGamePatchEntry *entry = &catalog->entries[index];
        char line[32];
        int length;
        if (entry->master) continue;
        length = snprintf(line, sizeof(line), "%08X=%u\n", entry->state_hash, entry->enabled ? 1u : 0u);
        if (length <= 0 || (unsigned int)length >= sizeof(line) ||
            !TitanStorageIo_WriteExact(descriptor, line, (size_t)length))
        {
            TitanStorageIo_Close(descriptor);
            (void)TitanStorageIo_Remove(temporary);
            return TITAN_GAME_PATCH_IO_ERROR;
        }
    }
    if (TitanStorageIo_Flush(descriptor) < 0 || TitanStorageIo_Close(descriptor) < 0)
    {
        (void)TitanStorageIo_Remove(temporary);
        return TITAN_GAME_PATCH_IO_ERROR;
    }
    if (!TitanStorageIo_AtomicReplace(temporary, target, backup))
    {
        (void)TitanStorageIo_Remove(temporary);
        return TITAN_GAME_PATCH_IO_ERROR;
    }
    (void)TitanStorageIo_Remove(backup);
    return TITAN_GAME_PATCH_OK;
}

void TitanGamePatches_Initialize(void)
{
    TitanGamePatches_RecoverUlGoldenRuntime();
    memset(g_catalogs, 0, sizeof(g_catalogs));
}

TitanGamePatchResult TitanGamePatches_Load(
    const char *startup,
    TitanGamePatchKind kind
)
{
    TitanGamePatchCatalog *catalog;
    TitanGamePatchResult read_result = TITAN_GAME_PATCH_OK;
    char path[256];
    char *buffer;
    char *cursor;
    unsigned int pending_automatic = 0u;
    unsigned int pending_experimental = 0u;
    unsigned int current_entry = (unsigned int)-1;

    if (!TitanGamePatches_IsValidStartup(startup)) return TITAN_GAME_PATCH_INVALID_STARTUP;
    if ((unsigned int)kind >= TITAN_GAME_PATCH_KIND_COUNT) return TITAN_GAME_PATCH_INVALID_FILE;

    catalog = &g_catalogs[kind];
    memset(catalog, 0, sizeof(*catalog));
    strncpy(catalog->startup, startup, sizeof(catalog->startup) - 1u);
    catalog->loaded = 1;

    if (!TitanGamePatches_BuildSourcePath(startup, kind, path, sizeof(path)))
        return TITAN_GAME_PATCH_IO_ERROR;
    {
        int source_is_directory = 0;

        /* Missing CHT/ENH files are the normal no-database state.  Once a
         * path exists, however, every read/shape error must fail closed: a
         * directory or unreadable file must never silently disable patches. */
        if (!TitanStorageIo_PathInfo64(path, &source_is_directory, 0))
            return TITAN_GAME_PATCH_OK;
        if (source_is_directory) return TITAN_GAME_PATCH_INVALID_FILE;
    }
    buffer = TitanGamePatches_ReadTextFile(path, &read_result);
    if (buffer == 0) return read_result;
    catalog->file_present = 1;

    cursor = buffer;
    while (*cursor != '\0')
    {
        char *end = strchr(cursor, '\n');
        char line[TITAN_GAME_PATCH_LINE_CAPACITY];
        char raw_line[TITAN_GAME_PATCH_LINE_CAPACITY];
        unsigned int length = end != 0 ? (unsigned int)(end - cursor) : (unsigned int)strlen(cursor);
        TitanGamePatchCode code;

        /* Never truncate a database line into a different valid-looking code
         * or heading.  Reject malformed/oversized source text deterministically. */
        if (length >= sizeof(line))
        {
            free(buffer);
            return TITAN_GAME_PATCH_INVALID_FILE;
        }
        memcpy(line, cursor, length);
        line[length] = '\0';
        strncpy(raw_line, line, sizeof(raw_line) - 1u);
        raw_line[sizeof(raw_line) - 1u] = '\0';
        TitanGamePatches_Trim(raw_line);

        if (kind == TITAN_GAME_PATCH_ENHANCEMENT &&
            (raw_line[0] == '#' || !strncmp(raw_line, "//", 2)))
        {
            if (TitanGamePatches_ContainsInsensitive(raw_line, "@auto"))
                pending_automatic = 1u;
            if (TitanGamePatches_ContainsInsensitive(raw_line, "@experimental"))
                pending_experimental = 1u;
        }

        TitanGamePatches_StripInlineComment(line);
        if (line[0] != '\0')
        {
            if (TitanGamePatches_ParseCode(line, &code))
            {
                if (current_entry != (unsigned int)-1 &&
                    current_entry < catalog->entry_count)
                {
                    TitanGamePatchEntry *entry = &catalog->entries[current_entry];
                    if (catalog->code_count >= TITAN_GAME_PATCH_CATALOG_CODE_CAPACITY)
                    {
                        free(buffer);
                        return TITAN_GAME_PATCH_CODE_OVERFLOW;
                    }
                    catalog->codes[catalog->code_count++] = code;
                    entry->code_count++;
                }
            }
            else
            {
                TitanGamePatchEntry *entry;
                if (catalog->entry_count >= TITAN_GAME_PATCH_ENTRY_CAPACITY)
                {
                    free(buffer);
                    return TITAN_GAME_PATCH_CODE_OVERFLOW;
                }
                current_entry = catalog->entry_count++;
                entry = &catalog->entries[current_entry];
                memset(entry, 0, sizeof(*entry));
                strncpy(entry->name, line, sizeof(entry->name) - 1u);
                entry->code_first = catalog->code_count;
                entry->automatic = pending_automatic ? 1u : 0u;
                entry->experimental = pending_experimental ? 1u : 0u;
                entry->master = TitanGamePatches_IsMasterName(entry->name) ? 1u : 0u;
                entry->enabled =
                    kind == TITAN_GAME_PATCH_ENHANCEMENT &&
                    entry->automatic && !entry->experimental && !entry->master;
                entry->state_hash = TitanGamePatches_HashName(entry->name, current_entry);
                pending_automatic = 0u;
                pending_experimental = 0u;
            }
        }

        if (end == 0) break;
        cursor = end + 1;
    }
    free(buffer);

    /* Empty headings are not actionable; keep them hidden as master-like rows. */
    {
        unsigned int index;
        for (index = 0u; index < catalog->entry_count; index++)
            if (catalog->entries[index].code_count == 0u)
                catalog->entries[index].master = 1u;
    }

    TitanGamePatches_ApplyState(catalog, kind);
    return TITAN_GAME_PATCH_OK;
}

unsigned int TitanGamePatches_GetEntryCount(TitanGamePatchKind kind)
{
    const TitanGamePatchCatalog *catalog;
    unsigned int index;
    unsigned int count = 0u;

    if ((unsigned int)kind >= TITAN_GAME_PATCH_KIND_COUNT) return 0u;
    catalog = &g_catalogs[kind];
    for (index = 0u; index < catalog->entry_count; index++)
        if (!catalog->entries[index].master) count++;
    return count;
}

int TitanGamePatches_GetEntry(
    TitanGamePatchKind kind,
    unsigned int visible_index,
    TitanGamePatchEntryInfo *info
)
{
    const TitanGamePatchCatalog *catalog;
    unsigned int raw_index;
    const TitanGamePatchEntry *entry;

    if (info == 0 || (unsigned int)kind >= TITAN_GAME_PATCH_KIND_COUNT) return 0;
    catalog = &g_catalogs[kind];
    if (!TitanGamePatches_VisibleToRawIndex(catalog, visible_index, &raw_index)) return 0;
    entry = &catalog->entries[raw_index];
    memset(info, 0, sizeof(*info));
    strncpy(info->name, entry->name, sizeof(info->name) - 1u);
    info->enabled = entry->enabled ? 1 : 0;
    info->automatic = entry->automatic ? 1 : 0;
    info->experimental = entry->experimental ? 1 : 0;
    info->code_count = entry->code_count;
    return 1;
}

TitanGamePatchResult TitanGamePatches_ToggleEntry(
    TitanGamePatchKind kind,
    unsigned int visible_index
)
{
    TitanGamePatchCatalog *catalog;
    unsigned int raw_index;

    if ((unsigned int)kind >= TITAN_GAME_PATCH_KIND_COUNT) return TITAN_GAME_PATCH_INVALID_FILE;
    catalog = &g_catalogs[kind];
    if (!catalog->loaded || !TitanGamePatches_VisibleToRawIndex(catalog, visible_index, &raw_index))
        return TITAN_GAME_PATCH_INVALID_FILE;
    {
        unsigned char previous = catalog->entries[raw_index].enabled;
        TitanGamePatchResult result;

        catalog->entries[raw_index].enabled = previous ? 0u : 1u;
        result = TitanGamePatches_SaveState(catalog, kind);
        if (result != TITAN_GAME_PATCH_OK)
            catalog->entries[raw_index].enabled = previous;
        return result;
    }
}

static int TitanGamePatches_CatalogMatches(
    const TitanGamePatchCatalog *catalog,
    const char *startup
)
{
    return catalog != 0 && catalog->loaded && !strcmp(catalog->startup, startup);
}

static unsigned int TitanGamePatches_ActiveCount(const TitanGamePatchCatalog *catalog)
{
    unsigned int index;
    unsigned int count = 0u;
    if (catalog == 0) return 0u;
    for (index = 0u; index < catalog->entry_count; index++)
        if (!catalog->entries[index].master && catalog->entries[index].enabled) count++;
    return count;
}

static int TitanGamePatches_AppendCode(
    TitanGamePatchCode output[TITAN_GAME_PATCH_PAYLOAD_PAIR_CAPACITY],
    unsigned int *count,
    TitanGamePatchCode code
)
{
    if (*count >= TITAN_GAME_PATCH_PAYLOAD_PAIR_CAPACITY) return 0;
    output[(*count)++] = code;
    return 1;
}

static int TitanGamePatches_AppendEntryCodes(
    const TitanGamePatchCatalog *catalog,
    const TitanGamePatchEntry *entry,
    TitanGamePatchCode output[TITAN_GAME_PATCH_PAYLOAD_PAIR_CAPACITY],
    unsigned int *count
)
{
    unsigned int code_index;
    for (code_index = 0u; code_index < entry->code_count; code_index++)
    {
        unsigned int source_index = entry->code_first + code_index;
        if (source_index >= catalog->code_count ||
            !TitanGamePatches_AppendCode(output, count, catalog->codes[source_index]))
            return 0;
    }
    return 1;
}

static int TitanGamePatches_ValidatePs2rdCapacity(
    const TitanGamePatchCode *codes,
    unsigned int count
)
{
    unsigned int index;
    unsigned int hooks = 0u;
    unsigned int regular = 0u;
    int next_code_can_be_hook = 1;

    for (index = 0u; index < count; index++)
    {
        uint32_t address = codes[index].address;
        if ((address & 0xfe000000u) == 0x90000000u && next_code_can_be_hook)
            hooks++;
        else
            regular++;

        if (hooks > TITAN_GAME_PATCH_PS2RD_MAX_HOOKS ||
            regular > TITAN_GAME_PATCH_PS2RD_MAX_CODES)
            return 0;

        next_code_can_be_hook =
            ((address & 0xf0000000u) == 0x40000000u ||
             (address & 0xf0000000u) == 0x30000000u) ? 0 : 1;
    }
    return 1;
}

static int TitanGamePatches_AppendCatalog(
    const TitanGamePatchCatalog *catalog,
    TitanGamePatchCode output[TITAN_GAME_PATCH_PAYLOAD_PAIR_CAPACITY],
    unsigned int *count
)
{
    unsigned int index;
    int have_active = TitanGamePatches_ActiveCount(catalog) > 0u;

    if (!have_active) return 1;
    /* Master/enable codes go first and are never exposed as user toggles. */
    for (index = 0u; index < catalog->entry_count; index++)
        if (catalog->entries[index].master && catalog->entries[index].code_count > 0u &&
            !TitanGamePatches_AppendEntryCodes(catalog, &catalog->entries[index], output, count))
            return 0;
    for (index = 0u; index < catalog->entry_count; index++)
        if (!catalog->entries[index].master && catalog->entries[index].enabled &&
            !TitanGamePatches_AppendEntryCodes(catalog, &catalog->entries[index], output, count))
            return 0;
    return 1;
}

static int TitanGamePatches_EnabledNameContains(
    const TitanGamePatchCatalog *catalog,
    const char *needle
)
{
    unsigned int index;
    for (index = 0u; catalog != 0 && index < catalog->entry_count; index++)
    {
        const TitanGamePatchEntry *entry = &catalog->entries[index];
        if (!entry->master && entry->enabled &&
            TitanGamePatches_ContainsInsensitive(entry->name, needle))
            return 1;
    }
    return 0;
}

static const char *TitanGamePatches_SelectTaunt(
    const char *startup,
    const TitanGamePatchCatalog *cheats,
    unsigned int active_count
)
{
    unsigned int hash;

    if (active_count >= 4u)
        return "Do you want to play, or should Titan play for you?";
    if (active_count >= 3u)
        return "Alright, champ. You won before it even started.";

    if (TitanGamePatches_EnabledNameContains(cheats, "invinc") ||
        TitanGamePatches_EnabledNameContains(cheats, "god mode") ||
        TitanGamePatches_EnabledNameContains(cheats, "health") ||
        TitanGamePatches_EnabledNameContains(cheats, "vida") ||
        TitanGamePatches_EnabledNameContains(cheats, "salud"))
        return "Damage was too scary, huh?";
    if (TitanGamePatches_EnabledNameContains(cheats, "ammo") ||
        TitanGamePatches_EnabledNameContains(cheats, "ammunition") ||
        TitanGamePatches_EnabledNameContains(cheats, "muni"))
        return "Reloading was asking too much, apparently.";
    if (TitanGamePatches_EnabledNameContains(cheats, "money") ||
        TitanGamePatches_EnabledNameContains(cheats, "cash") ||
        TitanGamePatches_EnabledNameContains(cheats, "coin") ||
        TitanGamePatches_EnabledNameContains(cheats, "dinheiro") ||
        TitanGamePatches_EnabledNameContains(cheats, "dinero"))
        return "Economy? Never heard of it.";
    if (TitanGamePatches_EnabledNameContains(cheats, "unlock") ||
        TitanGamePatches_EnabledNameContains(cheats, "all weapons") ||
        TitanGamePatches_EnabledNameContains(cheats, "desblo"))
        return "Patience for unlocking things: 0%.";
    if (TitanGamePatches_EnabledNameContains(cheats, "jump") ||
        TitanGamePatches_EnabledNameContains(cheats, "gravity") ||
        TitanGamePatches_EnabledNameContains(cheats, "gravidade") ||
        TitanGamePatches_EnabledNameContains(cheats, "gravedad") ||
        TitanGamePatches_EnabledNameContains(cheats, "salto"))
        return "Gravity has been uninstalled.";
    if (TitanGamePatches_EnabledNameContains(cheats, "wanted") ||
        TitanGamePatches_EnabledNameContains(cheats, "police") ||
        TitanGamePatches_EnabledNameContains(cheats, "polic"))
        return "Consequences are optional now.";

    hash = TitanGamePatches_HashName(startup, active_count);
    if ((hash & 1u) != 0u)
        return "Titan saw that. So did the game.";
    return "Congrats. Now the game just needs to press the buttons for you.";
}

static TitanGamePatchResult TitanGamePatches_WritePayload(
    const char *startup,
    const TitanGamePatchCode *codes,
    unsigned int code_count,
    unsigned int flags,
    char *output_path,
    unsigned int output_capacity
)
{
    TitanPatchPayloadHeader header;
    char target[256];
    char temporary[256];
    char backup[256];
    int descriptor;

    if (!TitanGamePatches_EnsureStateDirectory() ||
        !TitanGamePatches_BuildPayloadPath(startup, target, sizeof(target), 0) ||
        !TitanGamePatches_BuildPayloadPath(startup, temporary, sizeof(temporary), 1) ||
        snprintf(backup, sizeof(backup), "%s.bak", target) >= (int)sizeof(backup))
        return TITAN_GAME_PATCH_IO_ERROR;

    (void)TitanStorageIo_Remove(temporary);
    if (code_count == 0u)
    {
        (void)TitanStorageIo_Remove(target);
        if (output_path != 0 && output_capacity > 0u) output_path[0] = '\0';
        return TITAN_GAME_PATCH_OK;
    }

    memset(&header, 0, sizeof(header));
    header.magic = TITAN_PATCH_PAYLOAD_MAGIC;
    header.version = TITAN_PATCH_PAYLOAD_VERSION;
    header.flags = (uint16_t)flags;
    header.pair_count = code_count;
    header.checksum = TitanGamePatches_PayloadChecksum(codes, code_count);

    descriptor = TitanStorageIo_Open(
        temporary,
        TITAN_STORAGE_IO_WRITE | TITAN_STORAGE_IO_CREATE | TITAN_STORAGE_IO_TRUNCATE
    );
    if (descriptor < 0) return TITAN_GAME_PATCH_IO_ERROR;
    if (!TitanStorageIo_WriteExact(descriptor, &header, sizeof(header)) ||
        !TitanStorageIo_WriteExact(descriptor, codes, sizeof(codes[0]) * code_count) ||
        TitanStorageIo_Flush(descriptor) < 0 || TitanStorageIo_Close(descriptor) < 0)
    {
        (void)TitanStorageIo_Close(descriptor);
        (void)TitanStorageIo_Remove(temporary);
        return TITAN_GAME_PATCH_IO_ERROR;
    }
    if (!TitanStorageIo_AtomicReplace(temporary, target, backup))
    {
        (void)TitanStorageIo_Remove(temporary);
        return TITAN_GAME_PATCH_IO_ERROR;
    }
    (void)TitanStorageIo_Remove(backup);
    if (output_path != 0 && output_capacity > 0u)
    {
        strncpy(output_path, target, output_capacity - 1u);
        output_path[output_capacity - 1u] = '\0';
    }
    return TITAN_GAME_PATCH_OK;
}

TitanGamePatchResult TitanGamePatches_PrepareLaunch(
    const char *startup,
    TitanGamePatchLaunchState *state
)
{
    TitanGamePatchResult result;
    TitanGamePatchCode codes[TITAN_GAME_PATCH_PAYLOAD_PAIR_CAPACITY];
    unsigned int code_count = 0u;
    unsigned int gameplay_count;
    unsigned int enhancement_count;
    unsigned int flags = 0u;

    if (state != 0) memset(state, 0, sizeof(*state));
    if (!TitanGamePatches_IsValidStartup(startup)) return TITAN_GAME_PATCH_INVALID_STARTUP;

    if (!TitanGamePatches_CatalogMatches(&g_catalogs[TITAN_GAME_PATCH_CHEAT], startup))
    {
        result = TitanGamePatches_Load(startup, TITAN_GAME_PATCH_CHEAT);
        if (result != TITAN_GAME_PATCH_OK) return result;
    }
    if (!TitanGamePatches_CatalogMatches(&g_catalogs[TITAN_GAME_PATCH_ENHANCEMENT], startup))
    {
        result = TitanGamePatches_Load(startup, TITAN_GAME_PATCH_ENHANCEMENT);
        if (result != TITAN_GAME_PATCH_OK) return result;
    }

    gameplay_count = TitanGamePatches_ActiveCount(&g_catalogs[TITAN_GAME_PATCH_CHEAT]);
    enhancement_count = TitanGamePatches_ActiveCount(&g_catalogs[TITAN_GAME_PATCH_ENHANCEMENT]);
    if (gameplay_count > 0u) flags |= TITAN_PATCH_PAYLOAD_GAMEPLAY;
    if (enhancement_count > 0u) flags |= TITAN_PATCH_PAYLOAD_ENHANCEMENT;

    if (!TitanGamePatches_AppendCatalog(&g_catalogs[TITAN_GAME_PATCH_CHEAT], codes, &code_count) ||
        !TitanGamePatches_AppendCatalog(&g_catalogs[TITAN_GAME_PATCH_ENHANCEMENT], codes, &code_count) ||
        !TitanGamePatches_ValidatePs2rdCapacity(codes, code_count))
    {
        char stale[256];
        if (TitanGamePatches_BuildPayloadPath(startup, stale, sizeof(stale), 0))
            (void)TitanStorageIo_Remove(stale);
        return TITAN_GAME_PATCH_CODE_OVERFLOW;
    }

    result = TitanGamePatches_WritePayload(
        startup,
        codes,
        code_count,
        flags,
        state != 0 ? state->payload_path : 0,
        state != 0 ? sizeof(state->payload_path) : 0u
    );
    if (result != TITAN_GAME_PATCH_OK) return result;

    if (state != 0)
    {
        state->gameplay_active = gameplay_count > 0u;
        state->enhancement_active = enhancement_count > 0u;
        state->gameplay_count = gameplay_count;
        state->enhancement_count = enhancement_count;
        state->taunt = gameplay_count > 0u ?
            TitanGamePatches_SelectTaunt(
                startup,
                &g_catalogs[TITAN_GAME_PATCH_CHEAT],
                gameplay_count
            ) : 0;
    }
    return TITAN_GAME_PATCH_OK;
}


static int TitanGamePatches_IsOplCheatConfigLine(const char *line, size_t length)
{
    static const char *keys[] = { "$CheatsSource=", "$EnableCheat=", "$CheatMode=" };
    unsigned int i;
    while (length > 0u && (*line == ' ' || *line == '\t')) { ++line; --length; }
    for (i = 0u; i < sizeof(keys) / sizeof(keys[0]); ++i)
    {
        size_t n = strlen(keys[i]);
        if (length >= n && !strncmp(line, keys[i], n)) return 1;
    }
    return 0;
}

static int TitanGamePatches_WriteUlEffectiveCht(
    const char *startup,
    const TitanGamePatchCode *codes,
    unsigned int code_count
)
{
    char target[256], temporary[320], swap_backup[320];
    int fd = -1;
    unsigned int i;
    const char *heading = "Titan Active Runtime\r\n";

    if (!TitanGamePatches_BuildUlPath(startup, "cht", target, sizeof(target)) ||
        snprintf(temporary, sizeof(temporary), "%s.titan-active.tmp", target) >= (int)sizeof(temporary) ||
        snprintf(swap_backup, sizeof(swap_backup), "%s.titan-active.bak", target) >= (int)sizeof(swap_backup))
        return 0;
    (void)TitanStorageIo_Remove(temporary);
    fd = TitanStorageIo_Open(temporary, TITAN_STORAGE_IO_WRITE | TITAN_STORAGE_IO_CREATE | TITAN_STORAGE_IO_TRUNCATE);
    if (fd < 0) return 0;
    if (!TitanStorageIo_WriteExact(fd, heading, strlen(heading))) goto fail;
    for (i = 0u; i < code_count; ++i)
    {
        char line[32];
        int n = snprintf(line, sizeof(line), "%08X %08X\r\n", codes[i].address, codes[i].value);
        if (n <= 0 || (unsigned int)n >= sizeof(line) || !TitanStorageIo_WriteExact(fd, line, (size_t)n)) goto fail;
    }
    if (TitanStorageIo_Flush(fd) < 0 || TitanStorageIo_Close(fd) < 0) { fd = -1; goto fail; }
    fd = -1;
    if (!TitanStorageIo_AtomicReplace(temporary, target, swap_backup)) goto fail;
    (void)TitanStorageIo_Remove(swap_backup);
    return 1;
fail:
    if (fd >= 0) (void)TitanStorageIo_Close(fd);
    (void)TitanStorageIo_Remove(temporary);
    return 0;
}

static int TitanGamePatches_WriteUlCheatConfig(const char *startup)
{
    char target[256], temporary[320], swap_backup[320];
    TitanStorageIoSize size = 0u;
    int is_directory = 0, fd = -1;
    char *input = 0, *output = 0;
    size_t used = 0u, cursor = 0u, capacity;
    int ok = 0;

    if (!TitanGamePatches_BuildUlPath(startup, "cfg", target, sizeof(target)) ||
        snprintf(temporary, sizeof(temporary), "%s.titan-cheat.tmp", target) >= (int)sizeof(temporary) ||
        snprintf(swap_backup, sizeof(swap_backup), "%s.titan-cheat.bak", target) >= (int)sizeof(swap_backup)) return 0;
    if (!TitanStorageIo_PathInfo64(target, &is_directory, &size)) size = 0u;
    if (is_directory || size > TITAN_GAME_PATCH_UL_CFG_MAX_BYTES || size > (TitanStorageIoSize)SIZE_MAX) return 0;
    capacity = (size_t)size + 192u;
    input = (char *)malloc((size_t)size + 1u);
    output = (char *)malloc(capacity);
    if (input == 0 || output == 0) goto cleanup;
    input[0] = '\0'; output[0] = '\0';
    if (size > 0u)
    {
        fd = TitanStorageIo_Open(target, TITAN_STORAGE_IO_READ);
        if (fd < 0 || !TitanStorageIo_ReadExact(fd, input, (size_t)size)) goto cleanup;
        if (TitanStorageIo_Close(fd) < 0) { fd = -1; goto cleanup; }
        fd = -1; input[(size_t)size] = '\0';
    }
    while (cursor < (size_t)size)
    {
        size_t start = cursor, length;
        while (cursor < (size_t)size && input[cursor] != '\n') ++cursor;
        if (cursor < (size_t)size) ++cursor;
        length = cursor - start;
        if (!TitanGamePatches_IsOplCheatConfigLine(input + start, length))
        {
            if (used + length + 1u >= capacity) goto cleanup;
            memcpy(output + used, input + start, length); used += length; output[used] = '\0';
        }
    }
    if (used > 0u && output[used - 1u] != '\n')
    {
        if (used + 2u >= capacity) goto cleanup;
        output[used++]='\r'; output[used++]='\n'; output[used]='\0';
    }
    {
        const char *tail = "$CheatsSource=1\r\n$EnableCheat=1\r\n$CheatMode=0\r\n";
        size_t n = strlen(tail);
        if (used + n >= capacity) goto cleanup;
        memcpy(output + used, tail, n + 1u); used += n;
    }
    (void)TitanStorageIo_Remove(temporary);
    fd = TitanStorageIo_Open(temporary, TITAN_STORAGE_IO_WRITE | TITAN_STORAGE_IO_CREATE | TITAN_STORAGE_IO_TRUNCATE);
    if (fd < 0 || !TitanStorageIo_WriteExact(fd, output, used) || TitanStorageIo_Flush(fd) < 0) goto cleanup;
    if (TitanStorageIo_Close(fd) < 0) { fd = -1; goto cleanup; }
    fd = -1;
    if (!TitanStorageIo_AtomicReplace(temporary, target, swap_backup)) goto cleanup;
    (void)TitanStorageIo_Remove(swap_backup);
    ok = 1;
cleanup:
    if (fd >= 0) (void)TitanStorageIo_Close(fd);
    if (!ok) (void)TitanStorageIo_Remove(temporary);
    free(input); free(output);
    return ok;
}

TitanGamePatchResult TitanGamePatches_PrepareUlGoldenRuntime(const char *startup)
{
    TitanGamePatchLaunchState launch_state;
    TitanGamePatchResult result;
    TitanGamePatchCode codes[TITAN_GAME_PATCH_PAYLOAD_PAIR_CAPACITY];
    unsigned int code_count = 0u;
    char journal_path[256], cht_path[256], cfg_path[256], cht_backup[256], cfg_backup[256];
    TitanGamePatchUlJournal journal;

    if (!TitanGamePatches_IsValidStartup(startup)) return TITAN_GAME_PATCH_INVALID_STARTUP;
    if (!TitanGamePatches_RecoverUlStartup(startup)) return TITAN_GAME_PATCH_UL_RUNTIME_FAILED;

    memset(&launch_state, 0, sizeof(launch_state));
    result = TitanGamePatches_PrepareLaunch(startup, &launch_state);
    if (result != TITAN_GAME_PATCH_OK) return result;
    if (!launch_state.gameplay_active && !launch_state.enhancement_active) return TITAN_GAME_PATCH_OK;

    if (!TitanGamePatches_AppendCatalog(&g_catalogs[TITAN_GAME_PATCH_CHEAT], codes, &code_count) ||
        !TitanGamePatches_AppendCatalog(&g_catalogs[TITAN_GAME_PATCH_ENHANCEMENT], codes, &code_count) ||
        !TitanGamePatches_ValidatePs2rdCapacity(codes, code_count) || code_count == 0u)
        return TITAN_GAME_PATCH_CODE_OVERFLOW;

    if (!TitanGamePatches_EnsureStateDirectory() ||
        !TitanGamePatches_EnsureDirectoryPath(TITAN_GAME_PATCH_UL_CHT_ROOT) ||
        !TitanGamePatches_EnsureDirectoryPath(TITAN_GAME_PATCH_UL_CFG_ROOT) ||
        !TitanGamePatches_BuildUlPath(startup, "journal", journal_path, sizeof(journal_path)) ||
        !TitanGamePatches_BuildUlPath(startup, "cht", cht_path, sizeof(cht_path)) ||
        !TitanGamePatches_BuildUlPath(startup, "cfg", cfg_path, sizeof(cfg_path)) ||
        !TitanGamePatches_BuildUlPath(startup, "cht.original", cht_backup, sizeof(cht_backup)) ||
        !TitanGamePatches_BuildUlPath(startup, "cfg.original", cfg_backup, sizeof(cfg_backup)))
        return TITAN_GAME_PATCH_UL_RUNTIME_FAILED;

    memset(&journal, 0, sizeof(journal));
    journal.magic = TITAN_GAME_PATCH_UL_JOURNAL_MAGIC;
    journal.version = TITAN_GAME_PATCH_UL_JOURNAL_VERSION;
    if (!TitanGamePatches_Snapshot(cht_path, cht_backup, TITAN_GAME_PATCH_UL_CHT_MAX_BYTES, &journal.cht_present) ||
        !TitanGamePatches_Snapshot(cfg_path, cfg_backup, TITAN_GAME_PATCH_UL_CFG_MAX_BYTES, &journal.cfg_present) ||
        !TitanGamePatches_WritePrivateFile(journal_path, &journal, sizeof(journal)))
        return TITAN_GAME_PATCH_UL_RUNTIME_FAILED;

    if (!TitanGamePatches_WriteUlEffectiveCht(startup, codes, code_count) ||
        !TitanGamePatches_WriteUlCheatConfig(startup))
    {
        (void)TitanGamePatches_RecoverUlStartup(startup);
        return TITAN_GAME_PATCH_UL_RUNTIME_FAILED;
    }
    return TITAN_GAME_PATCH_OK;
}

const char *TitanGamePatches_GetKindTitle(TitanGamePatchKind kind)
{
    return kind == TITAN_GAME_PATCH_ENHANCEMENT ? "Graphics Enhancements" : "Cheats";
}

const char *TitanGamePatches_GetResultName(TitanGamePatchResult result)
{
    switch (result)
    {
        case TITAN_GAME_PATCH_OK: return "Ready";
        case TITAN_GAME_PATCH_INVALID_STARTUP: return "Invalid game ID";
        case TITAN_GAME_PATCH_IO_ERROR: return "Patch storage error";
        case TITAN_GAME_PATCH_INVALID_FILE: return "Invalid patch file";
        case TITAN_GAME_PATCH_TOO_LARGE: return "Patch file is too large";
        case TITAN_GAME_PATCH_CODE_OVERFLOW: return "Too many patch codes are active";
        case TITAN_GAME_PATCH_UL_RUNTIME_FAILED: return "Golden UL patch staging failed";
        default: return "Unknown patch error";
    }
}
