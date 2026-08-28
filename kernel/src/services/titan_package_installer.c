#include "titan_package_installer.h"

#include "titan_applications.h"
#include "titan_app_assets.h"
#include "titan_app_runtime.h"
#include "titan_dex.h"
#include "titan_elf_validate.h"
#include "titan_localization.h"
#include "titan_storage_io.h"
#include "titan_storage_paths.h"
#include "titan_storage_scheduler.h"
#include "titan_theme.h"
#include "titan_vlombiscore.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#if defined(__GNUC__)
extern void TitanVlombisCore_RecordWork(uint32_t capability_mask, TitanVlombisEvent event, uint32_t work_units, uint32_t bytes_avoided) __attribute__((weak));
#endif

#define TITAN_PACKAGE_FORMAT_VERSION_V1 1u
#define TITAN_PACKAGE_FORMAT_VERSION_V2 2u
#define TITAN_PACKAGE_HEADER_SIZE 256u
#define TITAN_PACKAGE_ENTRY_SIZE 144u
#define TITAN_PACKAGE_MAX_FILES 64u
#define TITAN_PACKAGE_ENTRY_PATH_CAPACITY 128u
#define TITAN_PACKAGE_MAX_SIZE (256u * 1024u * 1024u)
#define TITAN_PACKAGE_TRANSFER_BUFFER_SIZE 4096u
#define TITAN_PACKAGE_RAM_PRELOAD_LIMIT (1024u * 1024u)

#define TITAN_PACKAGE_TABLE_CHECKSUM_OFFSET 32u
#define TITAN_PACKAGE_HEADER_CHECKSUM_OFFSET 36u
#define TITAN_PACKAGE_TITLE_ID_OFFSET 40u
#define TITAN_PACKAGE_TITLE_OFFSET 56u
#define TITAN_PACKAGE_VERSION_OFFSET 136u
#define TITAN_PACKAGE_ENTRYPOINT_OFFSET 152u
#define TITAN_PACKAGE_ENTRYPOINT_V1_CAPACITY 104u
#define TITAN_PACKAGE_ENTRYPOINT_V2_CAPACITY 96u
#define TITAN_PACKAGE_APP_TYPE_OFFSET 248u
#define TITAN_PACKAGE_API_VERSION_OFFSET 252u
#define TITAN_PACKAGE_ICON_NAME "icon.rgb565"
#define TITAN_PACKAGE_TILE_NAME "tile.rgb565"
#define TITAN_PACKAGE_BACKGROUND_NAME "background.rgb565"
#define TITAN_PACKAGE_META_NAME "titan_meta.ini"
#define TITAN_PACKAGE_ICON_BYTES (64u * 64u * 2u)
#define TITAN_PACKAGE_TILE_BYTES (256u * 256u * 2u)
#define TITAN_PACKAGE_BACKGROUND_PRIMARY_BYTES (640u * 480u * 2u)
#define TITAN_PACKAGE_BACKGROUND_LEGACY_BYTES (512u * 256u * 2u)
#define TITAN_PACKAGE_THEME_CONFIG_NAME "theme.cfg"
#define TITAN_PACKAGE_THEME_PREVIEW_NAME "preview.rgb565"
#define TITAN_PACKAGE_THEME_PREVIEW_BYTES (256u * 128u * 2u)

#define TITAN_PACKAGE_PRIMARY_ROOT "mass:/TITAN/packages"
#define TITAN_PACKAGE_FALLBACK_ROOT "mass0:/TITAN/packages"
#define TITAN_PACKAGE_APPS_ROOT TITAN_STORAGE_DEVICE_ROOT "/apps"
#define TITAN_PACKAGE_THEMES_ROOT TITAN_STORAGE_DEVICE_ROOT "/themes"

typedef struct TitanPackageHeader
{
    unsigned int format_version;
    unsigned int file_count;
    unsigned int payload_offset;
    unsigned int package_size;
    unsigned int table_checksum;
    unsigned int app_type;
    unsigned int api_version;
    char title_id[TITAN_PACKAGE_TITLE_ID_CAPACITY];
    char title[TITAN_PACKAGE_TITLE_CAPACITY];
    char version[TITAN_PACKAGE_VERSION_CAPACITY];
    char entrypoint[TITAN_PACKAGE_ENTRYPOINT_V1_CAPACITY];
} TitanPackageHeader;

typedef struct TitanPackageEntry
{
    char path[TITAN_PACKAGE_ENTRY_PATH_CAPACITY];
    unsigned int offset;
    unsigned int size;
    unsigned int checksum;
    unsigned int flags;
} TitanPackageEntry;


typedef enum TitanPackagePrepareStep
{
    TITAN_PACKAGE_PREPARE_NONE = 0,
    TITAN_PACKAGE_PREPARE_STAT_PACKAGE,
    TITAN_PACKAGE_PREPARE_OPEN_PACKAGE,
    TITAN_PACKAGE_PREPARE_READ_HEADER,
    TITAN_PACKAGE_PREPARE_PARSE_HEADER,
    TITAN_PACKAGE_PREPARE_READ_ENTRY,
    TITAN_PACKAGE_PREPARE_VALIDATE_TABLE,
    TITAN_PACKAGE_PREPARE_BUILD_PATHS,
    TITAN_PACKAGE_PREPARE_ENSURE_DEVICE_ROOT,
    TITAN_PACKAGE_PREPARE_ENSURE_APPS_ROOT,
    TITAN_PACKAGE_PREPARE_CHECK_FINAL,
    TITAN_PACKAGE_PREPARE_CREATE_STAGE,
    TITAN_PACKAGE_PREPARE_PRELOAD_SEEK,
    TITAN_PACKAGE_PREPARE_PRELOAD_PAYLOAD,
    TITAN_PACKAGE_PREPARE_READY
} TitanPackagePrepareStep;

typedef struct TitanPackageInstallerRuntime
{
    TitanPackageInfo packages[TITAN_PACKAGE_CAPACITY];
    /* Package catalog inspection already validates the header and full file
     * table before the user can select a package. Keep that validated plan in
     * RAM so BeginInstall never has to repeat stat/header/table I/O on a
     * different EE thread. BuildFix20 raises discovery to 96 entries; the
     * validated-plan cache costs roughly 0.9 MiB at that ceiling and removes
     * the entire 0%% preparation read path on real hardware. */
    TitanPackageHeader package_headers[TITAN_PACKAGE_CAPACITY];
    TitanPackageEntry package_entries[TITAN_PACKAGE_CAPACITY][TITAN_PACKAGE_MAX_FILES];
    unsigned char package_plan_valid[TITAN_PACKAGE_CAPACITY];
    unsigned int package_count;
    TitanPackageInstallerStatus status;
    TitanPackageHeader header;
    TitanPackageEntry entries[TITAN_PACKAGE_MAX_FILES];
    int package_descriptor;
    int output_descriptor;
    unsigned int selected_index;
    unsigned int current_file_bytes;
    unsigned int current_file_checksum;
    unsigned int installed_bytes;
    unsigned int total_bytes;
    unsigned int transfer_size;
    unsigned int transfer_offset;
    volatile unsigned int begin_requested;
    volatile unsigned int cancel_requested;
    TitanPackagePrepareStep prepare_step;
    TitanStorageIoSize prepare_package_size;
    unsigned int prepare_header_bytes;
    unsigned int prepare_entry_bytes;
    unsigned int prepare_entry_index;
    unsigned int prepare_expected_offset;
    unsigned int prepare_entrypoint_found;
    uint32_t prepare_table_checksum;
    unsigned int ram_preload_enabled;
    unsigned int ram_preload_bytes;
    unsigned int ram_preload_size;
    unsigned int stage_generation;
    unsigned int stage_attempts;
    unsigned char prepare_header[TITAN_PACKAGE_HEADER_SIZE];
    unsigned char prepare_entry[TITAN_PACKAGE_ENTRY_SIZE];
    char stage_path[TITAN_PACKAGE_PATH_CAPACITY];
    char final_path[TITAN_PACKAGE_PATH_CAPACITY];
    char output_path[TITAN_PACKAGE_PATH_CAPACITY];
    char effective_package_path[TITAN_PACKAGE_PATH_CAPACITY];
    /* Small packages use a one-shot RAM read cache. The Package Installer
     * pauses music/background storage, so spending 1 MiB of EE RAM here buys a
     * much simpler hardware path: source USB reads finish before destination
     * USB writes begin. Larger packages keep the bounded streaming fallback. */
    unsigned char ram_preload[TITAN_PACKAGE_RAM_PRELOAD_LIMIT]
        __attribute__((aligned(64)));
    unsigned char transfer_buffer[TITAN_PACKAGE_TRANSFER_BUFFER_SIZE];
} TitanPackageInstallerRuntime;

static TitanPackageInstallerRuntime g_package_installer;
static const unsigned char g_package_magic[4] = {'T', 'P', 'K', 'G'};

static uint32_t TitanPackageInstaller_ReadU32Le(const unsigned char *source)
{
    return (uint32_t)source[0] |
        ((uint32_t)source[1] << 8u) |
        ((uint32_t)source[2] << 16u) |
        ((uint32_t)source[3] << 24u);
}

static uint32_t TitanPackageInstaller_Crc32Update(
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
        unsigned int bit;

        checksum ^= bytes[index];
        for (bit = 0u; bit < 8u; bit++)
        {
            checksum = (checksum >> 1u) ^
                (0xEDB88320u & (0u - (checksum & 1u)));
        }
    }
    return checksum;
}

static uint32_t TitanPackageInstaller_HeaderChecksum(
    const unsigned char header[TITAN_PACKAGE_HEADER_SIZE]
)
{
    uint32_t checksum;
    static const unsigned char zero[4] = {0u, 0u, 0u, 0u};

    checksum = 0xFFFFFFFFu;
    checksum = TitanPackageInstaller_Crc32Update(
        checksum,
        header,
        TITAN_PACKAGE_HEADER_CHECKSUM_OFFSET
    );
    checksum = TitanPackageInstaller_Crc32Update(
        checksum,
        zero,
        sizeof(zero)
    );
    checksum = TitanPackageInstaller_Crc32Update(
        checksum,
        header + TITAN_PACKAGE_HEADER_CHECKSUM_OFFSET + 4u,
        TITAN_PACKAGE_HEADER_SIZE - TITAN_PACKAGE_HEADER_CHECKSUM_OFFSET - 4u
    );
    return checksum ^ 0xFFFFFFFFu;
}

static void TitanPackageInstaller_CopyString(
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

static int TitanPackageInstaller_StringTerminated(
    const char *text,
    unsigned int capacity
)
{
    return text != 0 && memchr(text, '\0', capacity) != 0;
}

static int TitanPackageInstaller_HasPackageExtension(const char *name)
{
    unsigned int length;

    if (name == 0)
        return 0;
    length = (unsigned int)strlen(name);
    if (length >= 5u && name[length - 4u] == '.' &&
        tolower((unsigned char)name[length - 3u]) == 'p' &&
        tolower((unsigned char)name[length - 2u]) == 'k' &&
        tolower((unsigned char)name[length - 1u]) == 'g')
        return 1;
    return length >= 6u && name[length - 5u] == '.' &&
        tolower((unsigned char)name[length - 4u]) == 't' &&
        tolower((unsigned char)name[length - 3u]) == 't' &&
        tolower((unsigned char)name[length - 2u]) == 'h' &&
        tolower((unsigned char)name[length - 1u]) == 'm';
}

static int TitanPackageInstaller_IsThemeFilename(const char *name)
{
    unsigned int length;

    if (name == 0)
        return 0;
    length = (unsigned int)strlen(name);
    return length >= 6u && name[length - 5u] == '.' &&
        tolower((unsigned char)name[length - 4u]) == 't' &&
        tolower((unsigned char)name[length - 3u]) == 't' &&
        tolower((unsigned char)name[length - 2u]) == 'h' &&
        tolower((unsigned char)name[length - 1u]) == 'm';
}

static int TitanPackageInstaller_TitleIdSafe(const char *title_id)
{
    unsigned int length;
    unsigned int index;

    if (title_id == 0)
    {
        return 0;
    }
    length = (unsigned int)strlen(title_id);
    if (length < 3u || length >= TITAN_PACKAGE_TITLE_ID_CAPACITY)
    {
        return 0;
    }
    for (index = 0u; index < length; index++)
    {
        unsigned char character;

        character = (unsigned char)title_id[index];
        if (
            !(character >= 'A' && character <= 'Z') &&
            !(character >= '0' && character <= '9') &&
            character != '_' && character != '-'
        )
        {
            return 0;
        }
    }
    return 1;
}

static int TitanPackageInstaller_MetadataSafe(const char *text)
{
    const unsigned char *cursor;

    if (text == 0 || text[0] == '\0')
    {
        return 0;
    }
    cursor = (const unsigned char *)text;
    while (*cursor != 0u)
    {
        if (*cursor == '\r' || *cursor == '\n' || *cursor == '=')
        {
            return 0;
        }
        cursor++;
    }
    return 1;
}

static int TitanPackageInstaller_RelativePathSafe(const char *path)
{
    unsigned int length;
    unsigned int index;

    if (path == 0 || path[0] == '\0')
    {
        return 0;
    }
    length = (unsigned int)strlen(path);
    if (
        length >= TITAN_PACKAGE_ENTRY_PATH_CAPACITY ||
        path[0] == '/' || path[0] == '\\' || path[length - 1u] == '/'
    )
    {
        return 0;
    }

    for (index = 0u; index < length; index++)
    {
        unsigned char character;

        character = (unsigned char)path[index];
        if (
            character < 0x20u || character == ':' || character == '\\' ||
            (character == '.' &&
             (index == 0u || path[index - 1u] == '/') &&
             (path[index + 1u] == '/' || path[index + 1u] == '\0' ||
              (path[index + 1u] == '.' &&
               (path[index + 2u] == '/' || path[index + 2u] == '\0'))))
        )
        {
            return 0;
        }
    }
    return 1;
}

static int TitanPackageInstaller_EntrypointSafe(
    const char *path,
    unsigned int app_type
)
{
    unsigned int length;

    if (app_type == TITAN_PACKAGE_APP_SYSTEM_BROWSER)
        return path != 0 && strcmp(path, "@system-browser") == 0;
    if (app_type == TITAN_PACKAGE_APP_THEME)
        return path != 0 && strcmp(path, "@theme") == 0;
    if (!TitanPackageInstaller_RelativePathSafe(path) || strchr(path, '/') != 0)
        return 0;
    length = (unsigned int)strlen(path);
    if (app_type == TITAN_PACKAGE_APP_TITAN_NATIVE ||
        app_type == TITAN_PACKAGE_APP_TITAN_GAME)
    {
        return length >= 6u && path[length - 5u] == '.' &&
            tolower((unsigned char)path[length - 4u]) == 't' &&
            tolower((unsigned char)path[length - 3u]) == 'm' &&
            tolower((unsigned char)path[length - 2u]) == 'o' &&
            tolower((unsigned char)path[length - 1u]) == 'd';
    }
    return length >= 5u && path[length - 4u] == '.' &&
        tolower((unsigned char)path[length - 3u]) == 'e' &&
        tolower((unsigned char)path[length - 2u]) == 'l' &&
        tolower((unsigned char)path[length - 1u]) == 'f';
}

static int TitanPackageInstaller_PathJoin(
    char *destination,
    unsigned int capacity,
    const char *root,
    const char *relative
)
{
    int written;

    if (
        destination == 0 || capacity == 0u || root == 0 || relative == 0
    )
    {
        return 0;
    }
    written = snprintf(destination, capacity, "%s/%s", root, relative);
    return written >= 0 && (unsigned int)written < capacity;
}

static int TitanPackageInstaller_ParseHeaderBuffer(
    const unsigned char header[TITAN_PACKAGE_HEADER_SIZE],
    unsigned int actual_size,
    TitanPackageHeader *parsed
)
{
    unsigned int stored_header_checksum;
    unsigned int payload_offset;
    unsigned int file_count;
    unsigned int package_size;
    unsigned int format_version;
    unsigned int app_type;
    unsigned int api_version;
    unsigned int entrypoint_capacity;

    if (
        parsed == 0 || header == 0 ||
        actual_size < TITAN_PACKAGE_HEADER_SIZE ||
        actual_size > TITAN_PACKAGE_MAX_SIZE ||
        memcmp(header, g_package_magic, sizeof(g_package_magic)) != 0 ||
        TitanPackageInstaller_ReadU32Le(header + 8u) != TITAN_PACKAGE_HEADER_SIZE ||
        TitanPackageInstaller_ReadU32Le(header + 12u) != TITAN_PACKAGE_ENTRY_SIZE ||
        TitanPackageInstaller_ReadU32Le(header + 20u) != TITAN_PACKAGE_HEADER_SIZE
    )
        return 0;

    format_version = TitanPackageInstaller_ReadU32Le(header + 4u);
    if (format_version != TITAN_PACKAGE_FORMAT_VERSION_V1 &&
        format_version != TITAN_PACKAGE_FORMAT_VERSION_V2)
        return 0;

    file_count = TitanPackageInstaller_ReadU32Le(header + 16u);
    payload_offset = TitanPackageInstaller_ReadU32Le(header + 24u);
    package_size = TitanPackageInstaller_ReadU32Le(header + 28u);
    stored_header_checksum = TitanPackageInstaller_ReadU32Le(
        header + TITAN_PACKAGE_HEADER_CHECKSUM_OFFSET
    );
    entrypoint_capacity = format_version == TITAN_PACKAGE_FORMAT_VERSION_V1
        ? TITAN_PACKAGE_ENTRYPOINT_V1_CAPACITY
        : TITAN_PACKAGE_ENTRYPOINT_V2_CAPACITY;
    app_type = format_version == TITAN_PACKAGE_FORMAT_VERSION_V1
        ? TITAN_PACKAGE_APP_EXTERNAL_ELF
        : TitanPackageInstaller_ReadU32Le(header + TITAN_PACKAGE_APP_TYPE_OFFSET);
    api_version = format_version == TITAN_PACKAGE_FORMAT_VERSION_V1
        ? 0u
        : TitanPackageInstaller_ReadU32Le(header + TITAN_PACKAGE_API_VERSION_OFFSET);

    if (file_count == 0u || file_count > TITAN_PACKAGE_MAX_FILES ||
        payload_offset != TITAN_PACKAGE_HEADER_SIZE + file_count * TITAN_PACKAGE_ENTRY_SIZE ||
        payload_offset > package_size || package_size != actual_size ||
        stored_header_checksum != TitanPackageInstaller_HeaderChecksum(header) ||
        !TitanPackageInstaller_StringTerminated((const char *)(header + TITAN_PACKAGE_TITLE_ID_OFFSET), TITAN_PACKAGE_TITLE_ID_CAPACITY) ||
        !TitanPackageInstaller_StringTerminated((const char *)(header + TITAN_PACKAGE_TITLE_OFFSET), TITAN_PACKAGE_TITLE_CAPACITY) ||
        !TitanPackageInstaller_StringTerminated((const char *)(header + TITAN_PACKAGE_VERSION_OFFSET), TITAN_PACKAGE_VERSION_CAPACITY) ||
        !TitanPackageInstaller_StringTerminated((const char *)(header + TITAN_PACKAGE_ENTRYPOINT_OFFSET), entrypoint_capacity) ||
        app_type > TITAN_PACKAGE_APP_TITAN_GAME ||
        ((app_type == TITAN_PACKAGE_APP_TITAN_NATIVE ||
          app_type == TITAN_PACKAGE_APP_TITAN_GAME) &&
         api_version != TITAN_APPLICATION_API_VERSION) ||
        ((app_type == TITAN_PACKAGE_APP_EXTERNAL_ELF ||
          app_type == TITAN_PACKAGE_APP_SYSTEM_BROWSER ||
          app_type == TITAN_PACKAGE_APP_THEME) && api_version != 0u))
        return 0;

    memset(parsed, 0, sizeof(*parsed));
    parsed->format_version = format_version;
    parsed->file_count = file_count;
    parsed->payload_offset = payload_offset;
    parsed->package_size = package_size;
    parsed->table_checksum = TitanPackageInstaller_ReadU32Le(header + TITAN_PACKAGE_TABLE_CHECKSUM_OFFSET);
    parsed->app_type = app_type;
    parsed->api_version = api_version;
    TitanPackageInstaller_CopyString(parsed->title_id, sizeof(parsed->title_id), (const char *)(header + TITAN_PACKAGE_TITLE_ID_OFFSET));
    TitanPackageInstaller_CopyString(parsed->title, sizeof(parsed->title), (const char *)(header + TITAN_PACKAGE_TITLE_OFFSET));
    TitanPackageInstaller_CopyString(parsed->version, sizeof(parsed->version), (const char *)(header + TITAN_PACKAGE_VERSION_OFFSET));
    TitanPackageInstaller_CopyString(parsed->entrypoint, sizeof(parsed->entrypoint), (const char *)(header + TITAN_PACKAGE_ENTRYPOINT_OFFSET));

    return TitanPackageInstaller_TitleIdSafe(parsed->title_id) &&
        TitanPackageInstaller_MetadataSafe(parsed->title) &&
        TitanPackageInstaller_MetadataSafe(parsed->version) &&
        TitanPackageInstaller_EntrypointSafe(parsed->entrypoint, parsed->app_type);
}

static int TitanPackageInstaller_ReadHeader(
    int descriptor,
    unsigned int actual_size,
    TitanPackageHeader *parsed
)
{
    unsigned char header[TITAN_PACKAGE_HEADER_SIZE];

    if (
        parsed == 0 ||
        TitanStorageIo_Seek64(descriptor, 0, TITAN_STORAGE_IO_SEEK_SET) != 0 ||
        !TitanStorageIo_ReadExact(descriptor, header, sizeof(header))
    )
        return 0;

    return TitanPackageInstaller_ParseHeaderBuffer(header, actual_size, parsed);
}

static int TitanPackageInstaller_ReadEntries(
    int descriptor,
    const TitanPackageHeader *header,
    TitanPackageEntry entries[TITAN_PACKAGE_MAX_FILES]
)
{
    unsigned char raw[TITAN_PACKAGE_ENTRY_SIZE];
    uint32_t table_checksum;
    unsigned int expected_offset;
    unsigned int index;
    int entrypoint_found;

    if (
        header == 0 || entries == 0 ||
        TitanStorageIo_Seek64(
            descriptor,
            TITAN_PACKAGE_HEADER_SIZE,
            TITAN_STORAGE_IO_SEEK_SET
        ) != TITAN_PACKAGE_HEADER_SIZE
    )
    {
        return 0;
    }

    table_checksum = 0xFFFFFFFFu;
    expected_offset = 0u;
    entrypoint_found = 0;
    for (index = 0u; index < header->file_count; index++)
    {
        unsigned int end;
        unsigned int previous;

        if (!TitanStorageIo_ReadExact(descriptor, raw, sizeof(raw)))
        {
            return 0;
        }
        table_checksum = TitanPackageInstaller_Crc32Update(
            table_checksum,
            raw,
            sizeof(raw)
        );
        if (!TitanPackageInstaller_StringTerminated(
            (const char *)raw,
            TITAN_PACKAGE_ENTRY_PATH_CAPACITY
        ))
        {
            return 0;
        }

        memset(&entries[index], 0, sizeof(entries[index]));
        TitanPackageInstaller_CopyString(
            entries[index].path,
            sizeof(entries[index].path),
            (const char *)raw
        );
        entries[index].offset = TitanPackageInstaller_ReadU32Le(raw + 128u);
        entries[index].size = TitanPackageInstaller_ReadU32Le(raw + 132u);
        entries[index].checksum = TitanPackageInstaller_ReadU32Le(raw + 136u);
        entries[index].flags = TitanPackageInstaller_ReadU32Le(raw + 140u);
        end = entries[index].offset + entries[index].size;

        if (
            !TitanPackageInstaller_RelativePathSafe(entries[index].path) ||
            entries[index].flags != 0u ||
            entries[index].offset != expected_offset ||
            end < entries[index].offset ||
            end > header->package_size - header->payload_offset
        )
        {
            return 0;
        }
        for (previous = 0u; previous < index; previous++)
        {
            if (strcmp(entries[previous].path, entries[index].path) == 0)
            {
                return 0;
            }
        }
        if (strcmp(entries[index].path, header->entrypoint) == 0)
        {
            entrypoint_found++;
        }
        expected_offset = end;
    }

    table_checksum ^= 0xFFFFFFFFu;
    return
        table_checksum == header->table_checksum &&
        entrypoint_found == ((header->app_type == TITAN_PACKAGE_APP_SYSTEM_BROWSER ||
            header->app_type == TITAN_PACKAGE_APP_THEME) ? 0 : 1) &&
        expected_offset == header->package_size - header->payload_offset;
}

static int TitanPackageInstaller_CommitPreparedEntry(void)
{
    unsigned char *raw;
    TitanPackageEntry *entry;
    unsigned int end;
    unsigned int previous;
    unsigned int index;

    index = g_package_installer.prepare_entry_index;
    if (index >= g_package_installer.header.file_count)
        return 0;

    raw = g_package_installer.prepare_entry;
    entry = &g_package_installer.entries[index];

    g_package_installer.prepare_table_checksum = TitanPackageInstaller_Crc32Update(
        g_package_installer.prepare_table_checksum,
        raw,
        TITAN_PACKAGE_ENTRY_SIZE
    );
    if (!TitanPackageInstaller_StringTerminated(
        (const char *)raw,
        TITAN_PACKAGE_ENTRY_PATH_CAPACITY
    ))
    {
        return 0;
    }

    memset(entry, 0, sizeof(*entry));
    TitanPackageInstaller_CopyString(
        entry->path,
        sizeof(entry->path),
        (const char *)raw
    );
    entry->offset = TitanPackageInstaller_ReadU32Le(raw + 128u);
    entry->size = TitanPackageInstaller_ReadU32Le(raw + 132u);
    entry->checksum = TitanPackageInstaller_ReadU32Le(raw + 136u);
    entry->flags = TitanPackageInstaller_ReadU32Le(raw + 140u);
    end = entry->offset + entry->size;

    if (
        !TitanPackageInstaller_RelativePathSafe(entry->path) ||
        entry->flags != 0u ||
        entry->offset != g_package_installer.prepare_expected_offset ||
        end < entry->offset ||
        end > g_package_installer.header.package_size -
            g_package_installer.header.payload_offset
    )
    {
        return 0;
    }

    for (previous = 0u; previous < index; previous++)
    {
        if (strcmp(g_package_installer.entries[previous].path, entry->path) == 0)
            return 0;
    }
    if (strcmp(entry->path, g_package_installer.header.entrypoint) == 0)
        g_package_installer.prepare_entrypoint_found++;

    g_package_installer.prepare_expected_offset = end;
    return 1;
}

static int TitanPackageInstaller_LoadPackage(
    const char *path,
    TitanPackageHeader *header,
    TitanPackageEntry entries[TITAN_PACKAGE_MAX_FILES],
    int *descriptor_out
)
{
    TitanStorageIoSize size;
    int descriptor;

    if (
        path == 0 || header == 0 || entries == 0 || descriptor_out == 0 ||
        !TitanStorageIo_PathInfo64(path, 0, &size) ||
        size > UINT32_MAX
    )
    {
        return 0;
    }

    descriptor = TitanStorageIo_Open(path, TITAN_STORAGE_IO_READ);
    if (descriptor < 0)
    {
        return 0;
    }
    if (
        !TitanPackageInstaller_ReadHeader(descriptor, (unsigned int)size, header) ||
        !TitanPackageInstaller_ReadEntries(descriptor, header, entries)
    )
    {
        TitanStorageIo_Close(descriptor);
        return 0;
    }

    *descriptor_out = descriptor;
    return 1;
}

static int TitanPackageInstaller_PackageKnown(const char *path)
{
    unsigned int index;

    for (index = 0u; index < g_package_installer.package_count; index++)
    {
        if (strcmp(g_package_installer.packages[index].path, path) == 0)
        {
            return 1;
        }
    }
    return 0;
}

static int TitanPackageInstaller_PackageIdentityKnown(
    const TitanPackageInfo *info
)
{
    unsigned int index;

    if (info == 0)
        return 0;
    for (index = 0u; index < g_package_installer.package_count; index++)
    {
        const TitanPackageInfo *known = &g_package_installer.packages[index];
        if (
            strcmp(known->title_id, info->title_id) == 0 &&
            strcmp(known->version, info->version) == 0 &&
            strcmp(known->filename, info->filename) == 0
        )
        {
            return 1;
        }
    }
    return 0;
}

static const char *TitanPackageInstaller_GetInstallRoot(unsigned int app_type);

static int TitanPackageInstaller_Inspect(
    const char *path,
    TitanPackageInfo *info,
    TitanPackageHeader *cached_header,
    TitanPackageEntry cached_entries[TITAN_PACKAGE_MAX_FILES]
)
{
    TitanPackageHeader header;
    TitanPackageEntry entries[TITAN_PACKAGE_MAX_FILES];
    char installed_path[TITAN_PACKAGE_PATH_CAPACITY];
    int descriptor;

    if (
        path == 0 || info == 0 ||
        !TitanPackageInstaller_LoadPackage(path, &header, entries, &descriptor)
    )
    {
        return 0;
    }
    TitanStorageIo_Close(descriptor);

    if ((header.app_type == TITAN_PACKAGE_APP_THEME) !=
        TitanPackageInstaller_IsThemeFilename(path))
        return 0;

    if (cached_header != 0)
        *cached_header = header;
    if (cached_entries != 0)
        memcpy(
            cached_entries,
            entries,
            sizeof(TitanPackageEntry) * TITAN_PACKAGE_MAX_FILES
        );

    memset(info, 0, sizeof(*info));
    TitanPackageInstaller_CopyString(info->title, sizeof(info->title), header.title);
    TitanPackageInstaller_CopyString(
        info->title_id,
        sizeof(info->title_id),
        header.title_id
    );
    TitanPackageInstaller_CopyString(
        info->version,
        sizeof(info->version),
        header.version
    );
    TitanPackageInstaller_CopyString(info->path, sizeof(info->path), path);
    {
        const char *filename;
        filename = strrchr(path, '/');
        filename = filename != 0 ? filename + 1 : path;
        TitanPackageInstaller_CopyString(info->filename, sizeof(info->filename), filename);
    }
    info->format_version = header.format_version;
    info->app_type = header.app_type;
    info->api_version = header.api_version;
    info->file_count = header.file_count;
    info->payload_size = header.package_size - header.payload_offset;

    if (TitanPackageInstaller_PathJoin(
        installed_path,
        sizeof(installed_path),
        TitanPackageInstaller_GetInstallRoot(header.app_type),
        header.title_id
    ))
    {
        char manifest[TITAN_PACKAGE_PATH_CAPACITY];
        int is_directory;
        const char *manifest_name = header.app_type == TITAN_PACKAGE_APP_THEME
            ? TITAN_PACKAGE_THEME_CONFIG_NAME : "titan_app.ini";

        info->installed =
            TitanStorageIo_PathInfo64(installed_path, &is_directory, 0) &&
            is_directory &&
            TitanPackageInstaller_PathJoin(
                manifest,
                sizeof(manifest),
                installed_path,
                manifest_name
            ) &&
            TitanStorageIo_PathInfo64(manifest, 0, 0);
    }
    return 1;
}

static void TitanPackageInstaller_Sort(void)
{
    unsigned int left;

    for (left = 1u; left < g_package_installer.package_count; left++)
    {
        unsigned int right;

        right = left;
        while (
            right > 0u &&
            strcmp(
                g_package_installer.packages[right - 1u].title,
                g_package_installer.packages[right].title
            ) > 0
        )
        {
            TitanPackageInfo info;
            TitanPackageHeader header;
            TitanPackageEntry entries[TITAN_PACKAGE_MAX_FILES];
            unsigned char valid;

            info = g_package_installer.packages[right - 1u];
            g_package_installer.packages[right - 1u] =
                g_package_installer.packages[right];
            g_package_installer.packages[right] = info;

            header = g_package_installer.package_headers[right - 1u];
            g_package_installer.package_headers[right - 1u] =
                g_package_installer.package_headers[right];
            g_package_installer.package_headers[right] = header;

            memcpy(
                entries,
                g_package_installer.package_entries[right - 1u],
                sizeof(entries)
            );
            memcpy(
                g_package_installer.package_entries[right - 1u],
                g_package_installer.package_entries[right],
                sizeof(entries)
            );
            memcpy(
                g_package_installer.package_entries[right],
                entries,
                sizeof(entries)
            );

            valid = g_package_installer.package_plan_valid[right - 1u];
            g_package_installer.package_plan_valid[right - 1u] =
                g_package_installer.package_plan_valid[right];
            g_package_installer.package_plan_valid[right] = valid;
            right--;
        }
    }
}

static int TitanPackageInstaller_CreateDirectoryIfMissing(const char *path)
{
    int is_directory;

    if (TitanStorageIo_PathInfo64(path, &is_directory, 0))
        return is_directory;
    if (TitanStorageIo_CreateDirectory(path) == 0)
        return 1;

    /* FileXio does not reliably mirror IOP errno into EE errno. Re-probe the
     * path instead of treating EEXIST as an errno contract. */
    return TitanStorageIo_PathInfo64(path, &is_directory, 0) && is_directory;
}

static void TitanPackageInstaller_SetMessage(const char *message);

static void TitanPackageInstaller_SetPreflightError(const char *step)
{
    char message[sizeof(g_package_installer.status.message)];

    snprintf(
        message,
        sizeof(message),
        "PKG preflight %.24s: %.64s",
        step != 0 ? step : "?",
        g_package_installer.status.package_filename
    );
    message[sizeof(message) - 1u] = '\0';
    TitanPackageInstaller_SetMessage(message);
}

static int TitanPackageInstaller_BuildUsbAliasPath(
    const char *source,
    char *destination,
    unsigned int capacity
)
{
    int length;

    if (source == 0 || destination == 0 || capacity == 0u)
        return 0;

    if (strncmp(source, "mass0:/", 7u) == 0)
        length = snprintf(destination, capacity, "mass:/%s", source + 7u);
    else if (strncmp(source, "mass:/", 6u) == 0)
        length = snprintf(destination, capacity, "mass0:/%s", source + 6u);
    else
        return 0;

    if (length < 0 || (unsigned int)length >= capacity)
    {
        destination[0] = '\0';
        return 0;
    }
    destination[capacity - 1u] = '\0';
    return 1;
}

static int TitanPackageInstaller_OpenInstallSource(const char *catalog_path)
{
    char alternate[TITAN_PACKAGE_PATH_CAPACITY];
    const char *primary;
    const char *fallback;
    int descriptor;

    if (catalog_path == 0 || catalog_path[0] == '\0')
        return -1;

    alternate[0] = '\0';
    primary = catalog_path;
    fallback = 0;

    if (TitanPackageInstaller_BuildUsbAliasPath(
            catalog_path, alternate, sizeof(alternate)))
    {
        /* The hardware candidate boots from mass:. Prefer that canonical alias
         * when the catalog happened to discover the same USB package as mass0:.
         * This avoids mixing mass0: source I/O with mass: transaction staging. */
        if (strncmp(catalog_path, "mass0:/", 7u) == 0)
        {
            primary = alternate;
            fallback = catalog_path;
        }
        else
        {
            primary = catalog_path;
            fallback = alternate;
        }
    }

    descriptor = TitanStorageIo_Open(primary, TITAN_STORAGE_IO_READ);
    if (descriptor >= 0)
    {
        TitanPackageInstaller_CopyString(
            g_package_installer.effective_package_path,
            sizeof(g_package_installer.effective_package_path),
            primary
        );
        return descriptor;
    }

    if (fallback != 0)
    {
        descriptor = TitanStorageIo_Open(fallback, TITAN_STORAGE_IO_READ);
        if (descriptor >= 0)
        {
            TitanPackageInstaller_CopyString(
                g_package_installer.effective_package_path,
                sizeof(g_package_installer.effective_package_path),
                fallback
            );
            return descriptor;
        }
    }

    g_package_installer.effective_package_path[0] = '\0';
    return -1;
}

static int TitanPackageInstaller_CreateParents(const char *relative_path)
{
    char path[TITAN_PACKAGE_PATH_CAPACITY];
    unsigned int root_length;
    unsigned int index;

    if (!TitanPackageInstaller_PathJoin(
        path,
        sizeof(path),
        g_package_installer.stage_path,
        relative_path
    ))
    {
        return 0;
    }
    root_length = (unsigned int)strlen(g_package_installer.stage_path) + 1u;
    for (index = root_length; path[index] != '\0'; index++)
    {
        if (path[index] == '/')
        {
            path[index] = '\0';
            if (!TitanPackageInstaller_CreateDirectoryIfMissing(path))
            {
                return 0;
            }
            path[index] = '/';
        }
    }
    return 1;
}

static void TitanPackageInstaller_CloseFiles(void)
{
    if (g_package_installer.output_descriptor >= 0)
    {
        TitanStorageIo_Close(g_package_installer.output_descriptor);
        g_package_installer.output_descriptor = -1;
    }
    if (g_package_installer.package_descriptor >= 0)
    {
        TitanStorageIo_Close(g_package_installer.package_descriptor);
        g_package_installer.package_descriptor = -1;
    }
}

static void TitanPackageInstaller_SetMessage(const char *message)
{
    TitanPackageInstaller_CopyString(
        g_package_installer.status.message,
        sizeof(g_package_installer.status.message),
        TitanLocalization_Compat(message)
    );
}

static void TitanPackageInstaller_Fail(
    TitanPackageInstallerResult result,
    const char *message
)
{
    TitanPackageInstaller_CloseFiles();
    /* Never recursively delete a staging tree on the interactive install lane.
     * Failed/cancelled generations are intentionally orphaned as private
     * .install-* directories. A later idle scavenger may reclaim them; the next
     * installation always uses a fresh generation and is never forced to wait
     * for USB/FAT cleanup before it can start. */
    g_package_installer.prepare_step = TITAN_PACKAGE_PREPARE_NONE;
    g_package_installer.begin_requested = 0u;
    g_package_installer.cancel_requested = 0u;
    g_package_installer.status.phase = TITAN_PACKAGE_PHASE_ERROR;
    g_package_installer.status.result = result;
    g_package_installer.status.active = 0u;
    TitanPackageInstaller_SetMessage(message);
}

static int TitanPackageInstaller_OpenCurrentFile(void)
{
    const TitanPackageEntry *entry;
    TitanStorageIoOffset source_offset;

    if (g_package_installer.status.file_index >= g_package_installer.header.file_count)
    {
        return 0;
    }
    entry = &g_package_installer.entries[g_package_installer.status.file_index];
    if (
        !TitanPackageInstaller_CreateParents(entry->path) ||
        !TitanPackageInstaller_PathJoin(
            g_package_installer.output_path,
            sizeof(g_package_installer.output_path),
            g_package_installer.stage_path,
            entry->path
        )
    )
    {
        return 0;
    }

    g_package_installer.output_descriptor = TitanStorageIo_Open(
        g_package_installer.output_path,
        TITAN_STORAGE_IO_WRITE | TITAN_STORAGE_IO_CREATE |
            TITAN_STORAGE_IO_TRUNCATE
    );
    if (g_package_installer.output_descriptor < 0)
    {
        return 0;
    }
    source_offset = (TitanStorageIoOffset)g_package_installer.header.payload_offset +
        (TitanStorageIoOffset)entry->offset;
    if (!g_package_installer.ram_preload_enabled &&
        (g_package_installer.package_descriptor < 0 ||
         TitanStorageIo_Seek64(
             g_package_installer.package_descriptor,
             source_offset,
             TITAN_STORAGE_IO_SEEK_SET
         ) != source_offset))
    {
        TitanStorageIo_Close(g_package_installer.output_descriptor);
        g_package_installer.output_descriptor = -1;
        return 0;
    }

    g_package_installer.current_file_bytes = 0u;
    g_package_installer.current_file_checksum = 0xFFFFFFFFu;
    return 1;
}

static const TitanPackageEntry *TitanPackageInstaller_FindEntry(const char *path)
{
    unsigned int index;
    for (index = 0u; index < g_package_installer.header.file_count; index++)
    {
        if (strcmp(g_package_installer.entries[index].path, path) == 0)
            return &g_package_installer.entries[index];
    }
    return 0;
}

static int TitanPackageInstaller_OptionalAssetValid(
    const char *name,
    unsigned int expected_size
)
{
    const TitanPackageEntry *entry;
    entry = TitanPackageInstaller_FindEntry(name);
    return entry == 0 || entry->size == expected_size;
}

static int TitanPackageInstaller_OptionalBackgroundValid(void)
{
    const TitanPackageEntry *entry;

    entry = TitanPackageInstaller_FindEntry(TITAN_PACKAGE_BACKGROUND_NAME);
    return entry == 0 ||
        entry->size == TITAN_PACKAGE_BACKGROUND_PRIMARY_BYTES ||
        entry->size == TITAN_PACKAGE_BACKGROUND_LEGACY_BYTES;
}

static const char *TitanPackageInstaller_GetInstallRoot(unsigned int app_type)
{
    return app_type == TITAN_PACKAGE_APP_THEME
        ? TITAN_PACKAGE_THEMES_ROOT
        : TITAN_PACKAGE_APPS_ROOT;
}

static int TitanPackageInstaller_ThemeEntriesSafe(void)
{
    unsigned int index;

    if (TitanPackageInstaller_FindEntry(TITAN_PACKAGE_THEME_CONFIG_NAME) == 0)
        return 0;
    for (index = 0u; index < g_package_installer.header.file_count; index++)
    {
        const char *path = g_package_installer.entries[index].path;
        unsigned int length = (unsigned int)strlen(path);
        const char *suffix = strrchr(path, '.');
        (void)length;
        if (suffix != 0 &&
            (strcasecmp(suffix, ".elf") == 0 ||
             strcasecmp(suffix, ".tmod") == 0 ||
             strcasecmp(suffix, ".irx") == 0 ||
             strcasecmp(suffix, ".so") == 0 ||
             strcasecmp(suffix, ".vsm") == 0))
            return 0;
    }
    return 1;
}

static void TitanPackageInstaller_ReadMetadata(
    char *author,
    unsigned int author_capacity,
    char *description,
    unsigned int description_capacity
)
{
    char path[TITAN_PACKAGE_PATH_CAPACITY];
    char line[192];
    FILE *file;

    if (author != 0 && author_capacity > 0u) author[0] = '\0';
    if (description != 0 && description_capacity > 0u) description[0] = '\0';
    if (!TitanPackageInstaller_PathJoin(path, sizeof(path), g_package_installer.stage_path, TITAN_PACKAGE_META_NAME))
        return;
    file = fopen(path, "rb");
    if (file == 0) return;
    while (fgets(line, sizeof(line), file) != 0)
    {
        char *newline = strpbrk(line, "\r\n");
        if (newline != 0) *newline = '\0';
        if (strncmp(line, "author=", 7u) == 0 && TitanPackageInstaller_MetadataSafe(line + 7u))
            TitanPackageInstaller_CopyString(author, author_capacity, line + 7u);
        else if (strncmp(line, "description=", 12u) == 0 && TitanPackageInstaller_MetadataSafe(line + 12u))
            TitanPackageInstaller_CopyString(description, description_capacity, line + 12u);
    }
    fclose(file);
}

static int TitanPackageInstaller_WriteManifest(void)
{
    char manifest[TITAN_PACKAGE_PATH_CAPACITY];
    char temporary[TITAN_PACKAGE_PATH_CAPACITY];
    char contents[1024];
    char author[TITAN_APPLICATION_AUTHOR_CAPACITY];
    char description[TITAN_APPLICATION_DESCRIPTION_CAPACITY];
    const TitanPackageEntry *icon_entry;
    const TitanPackageEntry *tile_entry;
    const TitanPackageEntry *background_entry;
    int descriptor;
    int length;

    if (!TitanPackageInstaller_PathJoin(manifest, sizeof(manifest), g_package_installer.stage_path, "titan_app.ini") ||
        !TitanPackageInstaller_PathJoin(temporary, sizeof(temporary), g_package_installer.stage_path, "titan_app.ini.tmp"))
        return 0;

    TitanPackageInstaller_ReadMetadata(author, sizeof(author), description, sizeof(description));
    icon_entry = TitanPackageInstaller_FindEntry(TITAN_PACKAGE_ICON_NAME);
    tile_entry = TitanPackageInstaller_FindEntry(TITAN_PACKAGE_TILE_NAME);
    background_entry = TitanPackageInstaller_FindEntry(TITAN_PACKAGE_BACKGROUND_NAME);

    if (g_package_installer.header.format_version == TITAN_PACKAGE_FORMAT_VERSION_V1)
    {
        length = snprintf(contents, sizeof(contents),
            "format=1\ntitle_id=%s\ntitle=%s\nversion=%s\nexecutable=%s\n",
            g_package_installer.header.title_id,
            g_package_installer.header.title,
            g_package_installer.header.version,
            g_package_installer.header.entrypoint);
    }
    else
    {
        length = snprintf(contents, sizeof(contents),
            "format=2\ntitle_id=%s\ntitle=%s\nversion=%s\ntype=%s\nentrypoint=%s\napi=%u\nauthor=%s\ndescription=%s\n%s%s%s%s%s%s",
            g_package_installer.header.title_id,
            g_package_installer.header.title,
            g_package_installer.header.version,
            g_package_installer.header.app_type == TITAN_PACKAGE_APP_TITAN_NATIVE
                ? "titan-native"
                : (g_package_installer.header.app_type == TITAN_PACKAGE_APP_TITAN_GAME
                    ? "titan-game"
                : (g_package_installer.header.app_type == TITAN_PACKAGE_APP_SYSTEM_BROWSER
                    ? "system-browser" : "elf")),
            g_package_installer.header.entrypoint,
            g_package_installer.header.api_version,
            author,
            description,
            icon_entry != 0 ? "icon=" : "",
            icon_entry != 0 ? TITAN_PACKAGE_ICON_NAME "\n" : "",
            tile_entry != 0 ? "tile=" : "",
            tile_entry != 0 ? TITAN_PACKAGE_TILE_NAME "\n" : "",
            background_entry != 0 ? "background=" : "",
            background_entry != 0 ? TITAN_PACKAGE_BACKGROUND_NAME "\n" : "");
    }
    if (length < 0 || (unsigned int)length >= sizeof(contents))
        return 0;

    TitanStorageIo_Remove(temporary);
    descriptor = TitanStorageIo_Open(temporary, TITAN_STORAGE_IO_WRITE | TITAN_STORAGE_IO_CREATE | TITAN_STORAGE_IO_TRUNCATE);
    if (descriptor < 0) return 0;
    /* The manifest is still private inside the staging directory. A close is
     * sufficient here: forcing a FILEXIO_SYNC/fsync before publication can
     * monopolize the single PS2 storage RPC lane for seconds on USB/FAT. The
     * final directory rename remains the transaction boundary. */
    if (!TitanStorageIo_WriteExact(descriptor, contents, (unsigned int)length) ||
        TitanStorageIo_Close(descriptor) < 0)
    {
        TitanStorageIo_Close(descriptor);
        TitanStorageIo_Remove(temporary);
        return 0;
    }
    TitanStorageIo_Remove(manifest);
    return TitanStorageIo_Rename(temporary, manifest) == 0;
}

static void TitanPackageInstaller_Finish(void)
{
    char entrypoint[TITAN_PACKAGE_PATH_CAPACITY];
    char theme_error[112];
    int is_theme;

    is_theme = g_package_installer.header.app_type == TITAN_PACKAGE_APP_THEME;
    g_package_installer.status.phase = TITAN_PACKAGE_PHASE_VALIDATING;
    TitanPackageInstaller_SetMessage(
        is_theme ? "Validando conteudo do tema" : "Validando conteudo do aplicativo"
    );
    entrypoint[0] = '\0';
    theme_error[0] = '\0';

    if (
        g_package_installer.header.app_type != TITAN_PACKAGE_APP_SYSTEM_BROWSER &&
        g_package_installer.header.app_type != TITAN_PACKAGE_APP_THEME &&
        !TitanPackageInstaller_PathJoin(
            entrypoint,
            sizeof(entrypoint),
            g_package_installer.stage_path,
            g_package_installer.header.entrypoint
        )
    )
    {
        TitanPackageInstaller_Fail(
            TITAN_PACKAGE_INSTALLER_UNSAFE_PATH,
            "Caminho do entrypoint invalido"
        );
        return;
    }

    if (!is_theme &&
        (!TitanPackageInstaller_OptionalAssetValid(
            TITAN_PACKAGE_ICON_NAME,
            TITAN_PACKAGE_ICON_BYTES
        ) ||
         !TitanPackageInstaller_OptionalAssetValid(
            TITAN_PACKAGE_TILE_NAME,
            TITAN_PACKAGE_TILE_BYTES
        ) ||
         !TitanPackageInstaller_OptionalBackgroundValid()))
    {
        TitanPackageInstaller_Fail(
            TITAN_PACKAGE_INSTALLER_INVALID_PACKAGE,
            "Icone, tile ou background Titan possui tamanho invalido"
        );
        return;
    }

    if (is_theme &&
        !TitanPackageInstaller_OptionalAssetValid(
            TITAN_PACKAGE_THEME_PREVIEW_NAME,
            TITAN_PACKAGE_THEME_PREVIEW_BYTES
        ))
    {
        TitanPackageInstaller_Fail(
            TITAN_PACKAGE_INSTALLER_INVALID_PACKAGE,
            "Preview do tema precisa ser RGB565 256x128"
        );
        return;
    }

    if (
        g_package_installer.header.format_version == TITAN_PACKAGE_FORMAT_VERSION_V2 &&
        !is_theme &&
        TitanPackageInstaller_FindEntry(TITAN_PACKAGE_ICON_NAME) == 0
    )
    {
        TitanPackageInstaller_Fail(
            TITAN_PACKAGE_INSTALLER_INVALID_PACKAGE,
            "TPKG v2 precisa incluir icon.rgb565 (64x64 RGB565)"
        );
        return;
    }

    if (g_package_installer.header.app_type == TITAN_PACKAGE_APP_TITAN_NATIVE ||
        g_package_installer.header.app_type == TITAN_PACKAGE_APP_TITAN_GAME)
    {
        if (g_package_installer.header.api_version != TITAN_APPLICATION_API_VERSION)
        {
            TitanPackageInstaller_Fail(
                TITAN_PACKAGE_INSTALLER_API_MISMATCH,
                "Versao da Titan App API nao suportada"
            );
            return;
        }
        if (!(g_package_installer.header.app_type == TITAN_PACKAGE_APP_TITAN_GAME
                ? TitanAppRuntime_ValidateGameModuleFile(entrypoint)
                : TitanAppRuntime_ValidateRuntimeModuleFile(entrypoint)))
        {
            TitanPackageInstaller_Fail(
                TITAN_PACKAGE_INSTALLER_INVALID_TMOD,
                TitanAppRuntime_GetLastError()
            );
            return;
        }
    }
    else if (g_package_installer.header.app_type == TITAN_PACKAGE_APP_SYSTEM_BROWSER)
    {
        if (strcmp(g_package_installer.header.entrypoint, "@system-browser") != 0)
        {
            TitanPackageInstaller_Fail(
                TITAN_PACKAGE_INSTALLER_INVALID_PACKAGE,
                "System browser entrypoint invalido"
            );
            return;
        }
    }
    else if (is_theme)
    {
        if (strcmp(g_package_installer.header.entrypoint, "@theme") != 0 ||
            !TitanPackageInstaller_ThemeEntriesSafe() ||
            !TitanTheme_ValidateDirectoryForId(
                g_package_installer.stage_path,
                g_package_installer.header.title_id,
                theme_error,
                sizeof(theme_error)
            ))
        {
            TitanPackageInstaller_Fail(
                TITAN_PACKAGE_INSTALLER_INVALID_PACKAGE,
                theme_error[0] != '\0'
                    ? theme_error
                    : "Tema contem codigo ou arquivos invalidos"
            );
            return;
        }
    }
    else if (TitanElf_ValidateFile(entrypoint) != TITAN_ELF_VALID)
    {
        TitanPackageInstaller_Fail(
            TITAN_PACKAGE_INSTALLER_INVALID_ELF,
            "O executavel do pacote nao e um ELF MIPS seguro"
        );
        return;
    }

    if (!is_theme && !TitanPackageInstaller_WriteManifest())
    {
        TitanPackageInstaller_Fail(
            TITAN_PACKAGE_INSTALLER_STORAGE_ERROR,
            "Falha ao publicar o manifesto do aplicativo"
        );
        return;
    }

    g_package_installer.status.phase = TITAN_PACKAGE_PHASE_PUBLISHING;
    TitanPackageInstaller_SetMessage(
        is_theme ? "Publicando tema" : "Publicando aplicativo na grade"
    );
    if (TitanStorageIo_PathInfo64(g_package_installer.final_path, 0, 0) ||
        TitanStorageIo_Rename(
            g_package_installer.stage_path,
            g_package_installer.final_path
        ) < 0)
    {
        TitanPackageInstaller_Fail(
            TITAN_PACKAGE_INSTALLER_PUBLISH_FAILED,
            is_theme
                ? "Nao foi possivel publicar o tema"
                : "Nao foi possivel publicar o aplicativo"
        );
        return;
    }

    if (is_theme)
    {
        (void)TitanTheme_Refresh();
    }
    else
    {
        /* Publish directly into the live application catalog. Installing an
         * app must not force a complete USB game/library rescan just to make
         * one new tile visible. */
        (void)TitanApplications_AddInstalledDirectory(g_package_installer.final_path);
        TitanAppAssets_RequestRebuild();
    }
    if (g_package_installer.selected_index < g_package_installer.package_count)
        g_package_installer.packages[g_package_installer.selected_index].installed = 1;

    g_package_installer.status.phase = TITAN_PACKAGE_PHASE_COMPLETE;
    g_package_installer.status.result = TITAN_PACKAGE_INSTALLER_OK;
    g_package_installer.status.active = 0u;
    g_package_installer.status.progress_percent = 100u;
    TitanPackageInstaller_SetMessage(
        is_theme ? "Tema instalado com sucesso" : "Pacote instalado com sucesso"
    );
}

void TitanPackageInstaller_Initialize(void)
{
    memset(&g_package_installer, 0, sizeof(g_package_installer));
    g_package_installer.package_descriptor = -1;
    g_package_installer.output_descriptor = -1;
    g_package_installer.stage_generation = 1u;
    g_package_installer.status.phase = TITAN_PACKAGE_PHASE_IDLE;
    g_package_installer.status.result = TITAN_PACKAGE_INSTALLER_OK;
}

unsigned int TitanPackageInstaller_RefreshFromRoot(const char *root_path)
{
    DIR *directory;
    struct dirent *entry;

    if (
        root_path == 0 || root_path[0] == '\0' ||
        g_package_installer.package_count >= TITAN_PACKAGE_CAPACITY
    )
    {
        return g_package_installer.package_count;
    }

    directory = opendir(root_path);
    if (directory == 0)
    {
        return g_package_installer.package_count;
    }

    while (
        g_package_installer.package_count < TITAN_PACKAGE_CAPACITY &&
        (entry = readdir(directory)) != 0
    )
    {
        char path[TITAN_PACKAGE_PATH_CAPACITY];
        TitanPackageInfo info;

        if (
            !TitanPackageInstaller_HasPackageExtension(entry->d_name) ||
            !TitanPackageInstaller_PathJoin(
                path,
                sizeof(path),
                root_path,
                entry->d_name
            ) ||
            TitanPackageInstaller_PackageKnown(path) ||
            !TitanPackageInstaller_Inspect(
                path,
                &info,
                &g_package_installer.package_headers[
                    g_package_installer.package_count
                ],
                g_package_installer.package_entries[
                    g_package_installer.package_count
                ]
            )
        )
        {
            continue;
        }
        /* mass: and mass0: may be aliases for the same USB device. Scan both
         * roots so no files are hidden, but collapse the exact same package
         * identity if both aliases expose it. */
        if (TitanPackageInstaller_PackageIdentityKnown(&info))
        {
            continue;
        }
        g_package_installer.packages[g_package_installer.package_count] = info;
        g_package_installer.package_plan_valid[g_package_installer.package_count] = 1u;
        g_package_installer.package_count++;
    }
    closedir(directory);
    TitanPackageInstaller_Sort();
    return g_package_installer.package_count;
}

unsigned int TitanPackageInstaller_Refresh(void)
{
    if (g_package_installer.status.active)
    {
        return g_package_installer.package_count;
    }
    memset(g_package_installer.packages, 0, sizeof(g_package_installer.packages));
    memset(g_package_installer.package_plan_valid, 0, sizeof(g_package_installer.package_plan_valid));
    g_package_installer.package_count = 0u;
    /* Scan both aliases every time.  The old fallback-only-if-empty rule hid
     * packages when mass: and mass0: exposed different mounted devices or
     * directory views.  PackageKnown/inspection still filters duplicates and
     * invalid files. */
    TitanPackageInstaller_RefreshFromRoot(TITAN_PACKAGE_PRIMARY_ROOT);
    TitanPackageInstaller_RefreshFromRoot(TITAN_PACKAGE_FALLBACK_ROOT);
    return g_package_installer.package_count;
}

unsigned int TitanPackageInstaller_RefreshThemes(void)
{
    unsigned int read_index, write_index;
    TitanPackageInstaller_Refresh();
    write_index=0u;
    for(read_index=0u;read_index<g_package_installer.package_count;read_index++){
        if(g_package_installer.packages[read_index].app_type!=TITAN_PACKAGE_APP_THEME)continue;
        if(write_index!=read_index){
            g_package_installer.packages[write_index]=g_package_installer.packages[read_index];
            g_package_installer.package_headers[write_index]=g_package_installer.package_headers[read_index];
            memcpy(g_package_installer.package_entries[write_index],g_package_installer.package_entries[read_index],sizeof(g_package_installer.package_entries[write_index]));
            g_package_installer.package_plan_valid[write_index]=g_package_installer.package_plan_valid[read_index];
        }
        write_index++;
    }
    g_package_installer.package_count=write_index;
    return write_index;
}

unsigned int TitanPackageInstaller_GetCount(void)
{
    return g_package_installer.package_count;
}

const TitanPackageInfo *TitanPackageInstaller_Get(unsigned int index)
{
    return index < g_package_installer.package_count
        ? &g_package_installer.packages[index]
        : 0;
}

static TitanPackageInstallerResult TitanPackageInstaller_PrepareStep(void)
{
    const TitanPackageInfo *info;
    TitanStorageIoResult io_result;
    unsigned int remaining;
    unsigned int index;
    int is_directory;
    int stage_length;
    int final_length;

    info = TitanPackageInstaller_Get(g_package_installer.selected_index);
    if (info == 0)
        return TITAN_PACKAGE_INSTALLER_INVALID_INDEX;

    switch (g_package_installer.prepare_step)
    {
        case TITAN_PACKAGE_PREPARE_STAT_PACKAGE:
            TitanPackageInstaller_SetMessage("Preparando: consultando pacote");
            if (!TitanStorageIo_PathInfo64(
                info->path,
                0,
                &g_package_installer.prepare_package_size
            ) || g_package_installer.prepare_package_size > UINT32_MAX ||
                g_package_installer.prepare_package_size < TITAN_PACKAGE_HEADER_SIZE ||
                g_package_installer.prepare_package_size > TITAN_PACKAGE_MAX_SIZE)
            {
                return TITAN_PACKAGE_INSTALLER_INVALID_PACKAGE;
            }
            g_package_installer.prepare_step = TITAN_PACKAGE_PREPARE_OPEN_PACKAGE;
            return TITAN_PACKAGE_INSTALLER_OK;

        case TITAN_PACKAGE_PREPARE_OPEN_PACKAGE:
            TitanPackageInstaller_SetMessage("Preparando: abrindo payload");
            g_package_installer.package_descriptor =
                TitanPackageInstaller_OpenInstallSource(info->path);
            if (g_package_installer.package_descriptor < 0)
            {
                TitanPackageInstaller_SetPreflightError("OPEN");
                return TITAN_PACKAGE_INSTALLER_STORAGE_ERROR;
            }
            g_package_installer.ram_preload_size =
                g_package_installer.header.package_size -
                g_package_installer.header.payload_offset;
            g_package_installer.ram_preload_bytes = 0u;
            g_package_installer.ram_preload_enabled =
                g_package_installer.ram_preload_size <=
                    TITAN_PACKAGE_RAM_PRELOAD_LIMIT;
            g_package_installer.prepare_step =
                g_package_installer.ram_preload_enabled
                    ? TITAN_PACKAGE_PREPARE_PRELOAD_SEEK
                    : TITAN_PACKAGE_PREPARE_READY;
            return TITAN_PACKAGE_INSTALLER_OK;

        case TITAN_PACKAGE_PREPARE_PRELOAD_SEEK:
        {
            TitanStorageIoOffset payload_offset;

            TitanPackageInstaller_SetMessage("Preparando: posicionando payload");
            payload_offset =
                (TitanStorageIoOffset)g_package_installer.header.payload_offset;
            if (TitanStorageIo_Seek64(
                    g_package_installer.package_descriptor,
                    payload_offset,
                    TITAN_STORAGE_IO_SEEK_SET
                ) != payload_offset)
            {
                TitanPackageInstaller_SetPreflightError("SEEK");
                return TITAN_PACKAGE_INSTALLER_STORAGE_ERROR;
            }
            g_package_installer.prepare_step =
                TITAN_PACKAGE_PREPARE_PRELOAD_PAYLOAD;
            return TITAN_PACKAGE_INSTALLER_OK;
        }

        case TITAN_PACKAGE_PREPARE_PRELOAD_PAYLOAD:
            TitanPackageInstaller_SetMessage("Preparando: carregando pacote em RAM");
            if (g_package_installer.ram_preload_bytes >=
                g_package_installer.ram_preload_size)
            {
                if (TitanStorageIo_Close(g_package_installer.package_descriptor) < 0)
                {
                    TitanPackageInstaller_SetPreflightError("CLOSE");
                    return TITAN_PACKAGE_INSTALLER_STORAGE_ERROR;
                }
                g_package_installer.package_descriptor = -1;
                g_package_installer.prepare_step = TITAN_PACKAGE_PREPARE_READY;
                return TITAN_PACKAGE_INSTALLER_OK;
            }
            remaining = g_package_installer.ram_preload_size -
                g_package_installer.ram_preload_bytes;
            if (remaining > TITAN_PACKAGE_TRANSFER_BUFFER_SIZE)
                remaining = TITAN_PACKAGE_TRANSFER_BUFFER_SIZE;
            io_result = TitanStorageIo_ReadSome(
                g_package_installer.package_descriptor,
                g_package_installer.ram_preload +
                    g_package_installer.ram_preload_bytes,
                remaining
            );
            if (io_result <= 0 || (unsigned int)io_result > remaining)
            {
                TitanPackageInstaller_SetPreflightError("READ");
                return TITAN_PACKAGE_INSTALLER_STORAGE_ERROR;
            }
            g_package_installer.ram_preload_bytes += (unsigned int)io_result;
            return TITAN_PACKAGE_INSTALLER_OK;

        case TITAN_PACKAGE_PREPARE_READ_HEADER:
            TitanPackageInstaller_SetMessage("Preparando: lendo cabecalho");
            remaining = TITAN_PACKAGE_HEADER_SIZE -
                g_package_installer.prepare_header_bytes;
            io_result = TitanStorageIo_ReadSome(
                g_package_installer.package_descriptor,
                g_package_installer.prepare_header +
                    g_package_installer.prepare_header_bytes,
                remaining
            );
            if (io_result <= 0 || (unsigned int)io_result > remaining)
                return TITAN_PACKAGE_INSTALLER_INVALID_PACKAGE;
            g_package_installer.prepare_header_bytes += (unsigned int)io_result;
            if (g_package_installer.prepare_header_bytes == TITAN_PACKAGE_HEADER_SIZE)
                g_package_installer.prepare_step = TITAN_PACKAGE_PREPARE_PARSE_HEADER;
            return TITAN_PACKAGE_INSTALLER_OK;

        case TITAN_PACKAGE_PREPARE_PARSE_HEADER:
            TitanPackageInstaller_SetMessage("Preparando: validando cabecalho");
            if (!TitanPackageInstaller_ParseHeaderBuffer(
                g_package_installer.prepare_header,
                (unsigned int)g_package_installer.prepare_package_size,
                &g_package_installer.header
            ))
            {
                return TITAN_PACKAGE_INSTALLER_INVALID_PACKAGE;
            }
            memset(g_package_installer.entries, 0, sizeof(g_package_installer.entries));
            g_package_installer.prepare_entry_bytes = 0u;
            g_package_installer.prepare_entry_index = 0u;
            g_package_installer.prepare_expected_offset = 0u;
            g_package_installer.prepare_entrypoint_found = 0u;
            g_package_installer.prepare_table_checksum = 0xFFFFFFFFu;
            g_package_installer.prepare_step = TITAN_PACKAGE_PREPARE_READ_ENTRY;
            return TITAN_PACKAGE_INSTALLER_OK;

        case TITAN_PACKAGE_PREPARE_READ_ENTRY:
            if (g_package_installer.prepare_entry_index >=
                g_package_installer.header.file_count)
            {
                g_package_installer.prepare_step =
                    TITAN_PACKAGE_PREPARE_VALIDATE_TABLE;
                return TITAN_PACKAGE_INSTALLER_OK;
            }
            TitanPackageInstaller_SetMessage("Preparando: lendo tabela de arquivos");
            remaining = TITAN_PACKAGE_ENTRY_SIZE -
                g_package_installer.prepare_entry_bytes;
            io_result = TitanStorageIo_ReadSome(
                g_package_installer.package_descriptor,
                g_package_installer.prepare_entry +
                    g_package_installer.prepare_entry_bytes,
                remaining
            );
            if (io_result <= 0 || (unsigned int)io_result > remaining)
                return TITAN_PACKAGE_INSTALLER_INVALID_PACKAGE;
            g_package_installer.prepare_entry_bytes += (unsigned int)io_result;
            if (g_package_installer.prepare_entry_bytes == TITAN_PACKAGE_ENTRY_SIZE)
            {
                if (!TitanPackageInstaller_CommitPreparedEntry())
                    return TITAN_PACKAGE_INSTALLER_INVALID_PACKAGE;
                g_package_installer.prepare_entry_index++;
                g_package_installer.prepare_entry_bytes = 0u;
            }
            return TITAN_PACKAGE_INSTALLER_OK;

        case TITAN_PACKAGE_PREPARE_VALIDATE_TABLE:
            TitanPackageInstaller_SetMessage("Preparando: validando tabela");
            if ((g_package_installer.prepare_table_checksum ^ 0xFFFFFFFFu) !=
                    g_package_installer.header.table_checksum ||
                g_package_installer.prepare_entrypoint_found !=
                    ((g_package_installer.header.app_type == TITAN_PACKAGE_APP_SYSTEM_BROWSER ||
                      g_package_installer.header.app_type == TITAN_PACKAGE_APP_THEME) ? 0u : 1u) ||
                g_package_installer.prepare_expected_offset !=
                    g_package_installer.header.package_size -
                        g_package_installer.header.payload_offset)
            {
                return TITAN_PACKAGE_INSTALLER_INVALID_PACKAGE;
            }
            g_package_installer.prepare_step = TITAN_PACKAGE_PREPARE_BUILD_PATHS;
            return TITAN_PACKAGE_INSTALLER_OK;

        case TITAN_PACKAGE_PREPARE_BUILD_PATHS:
            TitanPackageInstaller_SetMessage("Preparando: reservando destino");
            final_length = snprintf(
                g_package_installer.final_path,
                sizeof(g_package_installer.final_path),
                "%s/%s",
                TitanPackageInstaller_GetInstallRoot(g_package_installer.header.app_type),
                g_package_installer.header.title_id
            );
            if (final_length < 0 ||
                (unsigned int)final_length >= sizeof(g_package_installer.final_path))
            {
                TitanPackageInstaller_SetPreflightError("DEST_PATH");
                return TITAN_PACKAGE_INSTALLER_STORAGE_ERROR;
            }
            g_package_installer.final_path[
                sizeof(g_package_installer.final_path) - 1u
            ] = '\0';
            g_package_installer.prepare_step =
                TITAN_PACKAGE_PREPARE_ENSURE_DEVICE_ROOT;
            return TITAN_PACKAGE_INSTALLER_OK;

        case TITAN_PACKAGE_PREPARE_ENSURE_DEVICE_ROOT:
            TitanPackageInstaller_SetMessage("Preparando: verificando TITAN");
            if (!TitanPackageInstaller_CreateDirectoryIfMissing(
                TITAN_STORAGE_DEVICE_ROOT
            ))
            {
                TitanPackageInstaller_SetPreflightError("TITAN_ROOT");
                return TITAN_PACKAGE_INSTALLER_STORAGE_ERROR;
            }
            g_package_installer.prepare_step = TITAN_PACKAGE_PREPARE_ENSURE_APPS_ROOT;
            return TITAN_PACKAGE_INSTALLER_OK;

        case TITAN_PACKAGE_PREPARE_ENSURE_APPS_ROOT:
            TitanPackageInstaller_SetMessage(
                g_package_installer.header.app_type == TITAN_PACKAGE_APP_THEME
                    ? "Preparando: verificando themes"
                    : "Preparando: verificando apps"
            );
            if (!TitanPackageInstaller_CreateDirectoryIfMissing(
                TitanPackageInstaller_GetInstallRoot(g_package_installer.header.app_type)
            ))
            {
                TitanPackageInstaller_SetPreflightError("INSTALL_ROOT");
                return TITAN_PACKAGE_INSTALLER_STORAGE_ERROR;
            }
            g_package_installer.prepare_step = TITAN_PACKAGE_PREPARE_CHECK_FINAL;
            return TITAN_PACKAGE_INSTALLER_OK;

        case TITAN_PACKAGE_PREPARE_CHECK_FINAL:
            TitanPackageInstaller_SetMessage("Preparando: verificando instalacao existente");
            if (TitanStorageIo_PathInfo64(
                g_package_installer.final_path,
                &is_directory,
                0
            ))
            {
                return TITAN_PACKAGE_INSTALLER_ALREADY_INSTALLED;
            }
            g_package_installer.stage_attempts = 0u;
            if (g_package_installer.stage_generation == 0u)
                g_package_installer.stage_generation = 1u;
            g_package_installer.prepare_step = TITAN_PACKAGE_PREPARE_CREATE_STAGE;
            return TITAN_PACKAGE_INSTALLER_OK;

        case TITAN_PACKAGE_PREPARE_CREATE_STAGE:
            TitanPackageInstaller_SetMessage("Preparando: criando transacao");
            if (g_package_installer.stage_attempts >= 256u)
            {
                TitanPackageInstaller_SetPreflightError("STAGE_EXHAUSTED");
                return TITAN_PACKAGE_INSTALLER_STORAGE_ERROR;
            }

            stage_length = snprintf(
                g_package_installer.stage_path,
                sizeof(g_package_installer.stage_path),
                "%s/.install-%s-%08x",
                TitanPackageInstaller_GetInstallRoot(g_package_installer.header.app_type),
                g_package_installer.header.title_id,
                g_package_installer.stage_generation
            );
            if (stage_length < 0 ||
                (unsigned int)stage_length >= sizeof(g_package_installer.stage_path))
            {
                TitanPackageInstaller_SetPreflightError("STAGE_PATH");
                return TITAN_PACKAGE_INSTALLER_STORAGE_ERROR;
            }
            g_package_installer.stage_path[
                sizeof(g_package_installer.stage_path) - 1u
            ] = '\0';

            /* Failed/cancelled installs intentionally leave .install-* staging
             * generations behind. Detect those generations explicitly instead
             * of trusting FileXio to propagate EEXIST through EE errno. */
            {
                int stage_is_directory;

                if (TitanStorageIo_PathInfo64(
                        g_package_installer.stage_path,
                        &stage_is_directory,
                        0))
                {
                    if (!stage_is_directory)
                    {
                        TitanPackageInstaller_SetPreflightError("STAGE_OCCUPIED");
                        return TITAN_PACKAGE_INSTALLER_STORAGE_ERROR;
                    }
                    g_package_installer.stage_generation++;
                    if (g_package_installer.stage_generation == 0u)
                        g_package_installer.stage_generation = 1u;
                    g_package_installer.stage_attempts++;
                    return TITAN_PACKAGE_INSTALLER_OK;
                }
            }

            if (TitanStorageIo_CreateDirectory(g_package_installer.stage_path) < 0)
            {
                int stage_is_directory;

                /* Handle a late FAT visibility/create race without errno too. */
                if (TitanStorageIo_PathInfo64(
                        g_package_installer.stage_path,
                        &stage_is_directory,
                        0) &&
                    stage_is_directory)
                {
                    g_package_installer.stage_generation++;
                    if (g_package_installer.stage_generation == 0u)
                        g_package_installer.stage_generation = 1u;
                    g_package_installer.stage_attempts++;
                    return TITAN_PACKAGE_INSTALLER_OK;
                }
                TitanPackageInstaller_SetPreflightError("STAGE_MKDIR");
                return TITAN_PACKAGE_INSTALLER_STORAGE_ERROR;
            }
            g_package_installer.prepare_step = TITAN_PACKAGE_PREPARE_OPEN_PACKAGE;
            return TITAN_PACKAGE_INSTALLER_OK;

        case TITAN_PACKAGE_PREPARE_READY:
            g_package_installer.output_descriptor = -1;
            g_package_installer.current_file_bytes = 0u;
            g_package_installer.transfer_size = 0u;
            g_package_installer.transfer_offset = 0u;
            g_package_installer.installed_bytes = 0u;
            g_package_installer.total_bytes = 0u;
            for (index = 0u; index < g_package_installer.header.file_count; index++)
                g_package_installer.total_bytes += g_package_installer.entries[index].size;

            g_package_installer.status.phase = TITAN_PACKAGE_PHASE_EXTRACTING;
            g_package_installer.status.result = TITAN_PACKAGE_INSTALLER_OK;
            g_package_installer.status.file_count =
                g_package_installer.header.file_count;
            g_package_installer.status.file_index = 0u;
            g_package_installer.status.progress_percent = 0u;
            g_package_installer.prepare_step = TITAN_PACKAGE_PREPARE_NONE;
            TitanPackageInstaller_SetMessage("Extraindo pacote para area temporaria");
            return TITAN_PACKAGE_INSTALLER_OK;

        case TITAN_PACKAGE_PREPARE_NONE:
        default:
            return TITAN_PACKAGE_INSTALLER_OK;
    }
}

TitanPackageInstallerResult TitanPackageInstaller_BeginInstall(
    unsigned int index
)
{
    const TitanPackageInfo *info;

    if (g_package_installer.status.active)
    {
        return TITAN_PACKAGE_INSTALLER_STORAGE_ERROR;
    }
    info = TitanPackageInstaller_Get(index);
    if (!TitanDex_IsEnabled() &&
        (info == 0 || info->app_type != TITAN_PACKAGE_APP_THEME))
    {
        return TITAN_PACKAGE_INSTALLER_DEX_REQUIRED;
    }
    if (info == 0)
    {
        return TITAN_PACKAGE_INSTALLER_INVALID_INDEX;
    }
    if (info->installed)
    {
        return TITAN_PACKAGE_INSTALLER_ALREADY_INSTALLED;
    }
    if (!g_package_installer.package_plan_valid[index])
    {
        return TITAN_PACKAGE_INSTALLER_INVALID_PACKAGE;
    }

    /* The package was fully inspected when the list was built. Reuse that
     * validated in-RAM plan instead of re-reading stat/header/table after the
     * click. On PS2 hardware this also keeps all package file I/O on the same
     * EE thread that successfully enumerated the package. */
    g_package_installer.header = g_package_installer.package_headers[index];
    memcpy(
        g_package_installer.entries,
        g_package_installer.package_entries[index],
        sizeof(g_package_installer.entries)
    );
    g_package_installer.selected_index = index;
    g_package_installer.begin_requested = 1u;
    g_package_installer.cancel_requested = 0u;
    g_package_installer.prepare_step = TITAN_PACKAGE_PREPARE_BUILD_PATHS;
    g_package_installer.prepare_package_size = 0u;
    g_package_installer.prepare_header_bytes = 0u;
    g_package_installer.prepare_entry_bytes = 0u;
    g_package_installer.prepare_entry_index = 0u;
    g_package_installer.ram_preload_enabled = 0u;
    g_package_installer.ram_preload_bytes = 0u;
    g_package_installer.ram_preload_size = 0u;
    g_package_installer.stage_path[0] = '\0';
    TitanPackageInstaller_CopyString(
        g_package_installer.effective_package_path,
        sizeof(g_package_installer.effective_package_path),
        info->path
    );
    memset(&g_package_installer.status, 0, sizeof(g_package_installer.status));
    g_package_installer.status.phase = TITAN_PACKAGE_PHASE_PREPARING;
    g_package_installer.status.result = TITAN_PACKAGE_INSTALLER_OK;
    g_package_installer.status.active = 1u;
    TitanPackageInstaller_CopyString(
        g_package_installer.status.package_filename,
        sizeof(g_package_installer.status.package_filename),
        info->filename
    );
    TitanPackageInstaller_SetMessage("Preparando instalacao");
    return TITAN_PACKAGE_INSTALLER_OK;
}

void TitanPackageInstaller_Update(unsigned int byte_budget)
{
    if (!g_package_installer.status.active)
    {
        return;
    }
    if (g_package_installer.cancel_requested)
    {
        TitanPackageInstaller_Fail(
            TITAN_PACKAGE_INSTALLER_CANCELLED,
            "Instalacao cancelada; nenhum tile foi publicado"
        );
        return;
    }
    if (g_package_installer.status.phase == TITAN_PACKAGE_PHASE_PREPARING)
    {
        TitanPackageInstallerResult prepare_result;

        g_package_installer.begin_requested = 0u;
        prepare_result = TitanPackageInstaller_PrepareStep();
        if (prepare_result != TITAN_PACKAGE_INSTALLER_OK)
        {
            char prepare_message[sizeof(g_package_installer.status.message)];
            const char *failure_message;

            TitanPackageInstaller_CopyString(
                prepare_message,
                sizeof(prepare_message),
                g_package_installer.status.message
            );
            failure_message =
                prepare_result == TITAN_PACKAGE_INSTALLER_CANCELLED
                    ? "Instalacao cancelada; nenhum tile foi publicado"
                    : TitanPackageInstaller_GetResultName(prepare_result);
            if (prepare_result == TITAN_PACKAGE_INSTALLER_STORAGE_ERROR &&
                strncmp(prepare_message, "PKG preflight ", 14u) == 0)
            {
                failure_message = prepare_message;
            }
            TitanPackageInstaller_Fail(prepare_result, failure_message);
        }
        return;
    }

    while (g_package_installer.status.active && byte_budget > 0u)
    {
        int opened_this_turn;

        opened_this_turn = 0;
        if (g_package_installer.cancel_requested)
        {
            TitanPackageInstaller_Fail(
                TITAN_PACKAGE_INSTALLER_CANCELLED,
                "Instalacao cancelada; nenhum tile foi publicado"
            );
            return;
        }
        TitanPackageEntry *entry;
        unsigned int remaining;
        unsigned int chunk;

        if (g_package_installer.status.file_index >= g_package_installer.header.file_count)
        {
            TitanPackageInstaller_CloseFiles();
            TitanPackageInstaller_Finish();
            return;
        }
        if (g_package_installer.output_descriptor < 0)
        {
            TitanPackageInstaller_SetMessage("Abrindo arquivo do aplicativo");
            if (!TitanPackageInstaller_OpenCurrentFile())
            {
                TitanPackageInstaller_Fail(
                    TITAN_PACKAGE_INSTALLER_STORAGE_ERROR,
                    "Falha ao criar arquivo do aplicativo"
                );
                return;
            }
            opened_this_turn = 1;
        }

        /* Opening a destination may itself require mkdir/open/lseek RPCs.
         * Yield immediately afterwards instead of stacking a payload read on
         * the same scheduler turn. This makes every long-latency boundary
         * visible to cancellation and to the storage scheduler. */
        if (opened_this_turn)
        {
            return;
        }

        entry = &g_package_installer.entries[g_package_installer.status.file_index];
        remaining = entry->size - g_package_installer.current_file_bytes;
        if (remaining == 0u && g_package_installer.transfer_size == 0u)
        {
            unsigned int checksum;

            checksum = g_package_installer.current_file_checksum ^ 0xFFFFFFFFu;
            TitanPackageInstaller_SetMessage("Concluindo arquivo do aplicativo");
            /* Do not fsync every staged payload file. The previous build could
             * visibly stop after the first small file (about 5% for the test
             * package) while the USB/FAT stack serviced FILEXIO_SYNC. Closing
             * the private staging file is enough until the package is
             * validated and atomically published. */
            if (TitanStorageIo_Close(g_package_installer.output_descriptor) < 0)
            {
                g_package_installer.output_descriptor = -1;
                TitanPackageInstaller_Fail(
                    TITAN_PACKAGE_INSTALLER_STORAGE_ERROR,
                    "Falha ao concluir arquivo do aplicativo"
                );
                return;
            }
            g_package_installer.output_descriptor = -1;
            if (checksum != entry->checksum)
            {
                TitanPackageInstaller_Fail(
                    TITAN_PACKAGE_INSTALLER_CHECKSUM_FAILED,
                    "Checksum de arquivo invalido"
                );
                return;
            }
            g_package_installer.status.file_index++;
            return;
        }

        /* A package quantum performs at most one payload read OR one payload
         * write. Never hide a ReadExact+WriteExact loop inside one turn: a
         * single synchronous fileXio call is the smallest safe preemption
         * boundary available to the EE worker. */
        if (g_package_installer.transfer_size > 0u)
        {
            TitanStorageIoResult written;
            unsigned int pending;

            pending = g_package_installer.transfer_size -
                g_package_installer.transfer_offset;
            chunk = pending;
            if (chunk > byte_budget)
            {
                chunk = byte_budget;
            }
            TitanPackageInstaller_SetMessage("Gravando dados do aplicativo");
            written = TitanStorageIo_WriteSome(
                g_package_installer.output_descriptor,
                g_package_installer.transfer_buffer +
                    g_package_installer.transfer_offset,
                chunk
            );
            if (written <= 0 || (uint64_t)written > (uint64_t)chunk)
            {
                TitanPackageInstaller_Fail(
                    TITAN_PACKAGE_INSTALLER_STORAGE_ERROR,
                    "Falha de escrita USB"
                );
                return;
            }

            chunk = (unsigned int)written;
#if defined(__GNUC__)
            if (TitanVlombisCore_RecordWork != 0)
#endif
                TitanVlombisCore_RecordWork(
                    TITAN_VLOMBIS_CAP_ASYNC_PACKAGE_IO | TITAN_VLOMBIS_CAP_STORAGE_QOS |
                        TITAN_VLOMBIS_CAP_IO_COORDINATION | TITAN_VLOMBIS_CAP_FRAME_BUDGETING,
                    TITAN_VLOMBIS_EVENT_ASYNC_PACKAGE_QUANTUM, chunk, 0u);
            g_package_installer.current_file_checksum =
                TitanPackageInstaller_Crc32Update(
                    g_package_installer.current_file_checksum,
                    g_package_installer.transfer_buffer +
                        g_package_installer.transfer_offset,
                    chunk
                );
            g_package_installer.transfer_offset += chunk;
            g_package_installer.current_file_bytes += chunk;
            g_package_installer.installed_bytes += chunk;
            if (
                g_package_installer.transfer_offset >=
                g_package_installer.transfer_size
            )
            {
                g_package_installer.transfer_size = 0u;
                g_package_installer.transfer_offset = 0u;
            }
            g_package_installer.status.progress_percent =
                g_package_installer.total_bytes > 0u
                    ? (unsigned int)(
                        ((uint64_t)g_package_installer.installed_bytes * 100u) /
                        g_package_installer.total_bytes
                    )
                    : 100u;
            return;
        }

        chunk = remaining;
        if (chunk > sizeof(g_package_installer.transfer_buffer))
        {
            chunk = sizeof(g_package_installer.transfer_buffer);
        }
        if (chunk > byte_budget)
        {
            chunk = byte_budget;
        }
        if (g_package_installer.ram_preload_enabled)
        {
            unsigned int cache_offset;

            cache_offset = entry->offset +
                g_package_installer.current_file_bytes;
            if (cache_offset > g_package_installer.ram_preload_size ||
                chunk > g_package_installer.ram_preload_size - cache_offset)
            {
                TitanPackageInstaller_Fail(
                    TITAN_PACKAGE_INSTALLER_INVALID_PACKAGE,
                    "Payload em RAM fora dos limites"
                );
                return;
            }
            TitanPackageInstaller_SetMessage("Preparando dados em RAM");
            memcpy(
                g_package_installer.transfer_buffer,
                g_package_installer.ram_preload + cache_offset,
                chunk
            );
            g_package_installer.transfer_size = chunk;
            g_package_installer.transfer_offset = 0u;
            return;
        }
        else
        {
            TitanStorageIoResult received;

            TitanPackageInstaller_SetMessage("Lendo dados do pacote");
            received = TitanStorageIo_ReadSome(
                g_package_installer.package_descriptor,
                g_package_installer.transfer_buffer,
                chunk
            );
            if (received <= 0 || (uint64_t)received > (uint64_t)chunk)
            {
                TitanPackageInstaller_Fail(
                    TITAN_PACKAGE_INSTALLER_STORAGE_ERROR,
                    "Falha de leitura USB"
                );
                return;
            }
            g_package_installer.transfer_size = (unsigned int)received;
            g_package_installer.transfer_offset = 0u;
#if defined(__GNUC__)
            if (TitanVlombisCore_RecordWork != 0)
#endif
                TitanVlombisCore_RecordWork(
                    TITAN_VLOMBIS_CAP_ASYNC_PACKAGE_IO | TITAN_VLOMBIS_CAP_STORAGE_QOS |
                        TITAN_VLOMBIS_CAP_IO_COORDINATION | TITAN_VLOMBIS_CAP_FRAME_BUDGETING,
                    TITAN_VLOMBIS_EVENT_ASYNC_PACKAGE_QUANTUM,
                    (uint32_t)received, 0u);
            return;
        }

    }
}

void TitanPackageInstaller_Cancel(void)
{
    if (!g_package_installer.status.active)
    {
        return;
    }

    /* Cancellation is request-only on the UI thread. Closing descriptors or
     * recursively deleting the staging tree here races the storage worker and
     * can block the render thread on PS2SDK's global fileXio RPC lock. The
     * worker observes this flag at bounded chunk boundaries and performs all
     * cleanup from the same serialized I/O lane. */
    g_package_installer.cancel_requested = 1u;
    TitanPackageInstaller_SetMessage("Cancelamento solicitado...");
}

int TitanPackageInstaller_IsActive(void)
{
    return g_package_installer.status.active != 0u;
}

const TitanPackageInstallerStatus *TitanPackageInstaller_GetStatus(void)
{
    return &g_package_installer.status;
}

const char *TitanPackageInstaller_GetResultName(
    TitanPackageInstallerResult result
)
{
    switch (result)
    {
        case TITAN_PACKAGE_INSTALLER_NO_PACKAGES:
            return TitanLocalization_Compat("Nenhum pacote encontrado");
        case TITAN_PACKAGE_INSTALLER_INVALID_INDEX:
            return TitanLocalization_Compat("Selecao de pacote invalida");
        case TITAN_PACKAGE_INSTALLER_INVALID_PACKAGE:
            return TitanLocalization_Compat("Pacote invalido ou corrompido");
        case TITAN_PACKAGE_INSTALLER_UNSAFE_PATH:
            return TitanLocalization_Compat("Pacote contem caminho inseguro");
        case TITAN_PACKAGE_INSTALLER_CHECKSUM_FAILED:
            return TitanLocalization_Compat("Checksum do pacote falhou");
        case TITAN_PACKAGE_INSTALLER_ALREADY_INSTALLED:
            return TitanLocalization_Compat("Aplicativo ja instalado");
        case TITAN_PACKAGE_INSTALLER_STORAGE_ERROR:
            return TitanLocalization_Compat("Falha de leitura ou escrita USB");
        case TITAN_PACKAGE_INSTALLER_INVALID_ELF:
            return TitanLocalization_Compat("Executavel ELF MIPS invalido");
        case TITAN_PACKAGE_INSTALLER_INVALID_TMOD:
            return TitanLocalization_Compat("Modulo Titan App invalido");
        case TITAN_PACKAGE_INSTALLER_API_MISMATCH:
            return TitanLocalization_Compat("Titan App API incompativel");
        case TITAN_PACKAGE_INSTALLER_DEX_REQUIRED:
            return TitanLocalization_Compat("Package Installer requer DEX Mode");
        case TITAN_PACKAGE_INSTALLER_PUBLISH_FAILED:
            return TitanLocalization_Compat("Falha ao publicar aplicativo");
        case TITAN_PACKAGE_INSTALLER_CANCELLED:
            return TitanLocalization_Compat("Instalacao cancelada");
        case TITAN_PACKAGE_INSTALLER_OK:
        default:
            return TitanLocalization_Compat("Pronto");
    }
}

const char *TitanPackageInstaller_GetTypeName(unsigned int app_type)
{
    if (app_type == TITAN_PACKAGE_APP_TITAN_NATIVE)
        return "Titan App";
    if (app_type == TITAN_PACKAGE_APP_SYSTEM_BROWSER)
        return "System App";
    if (app_type == TITAN_PACKAGE_APP_THEME)
        return "Theme";
    if (app_type == TITAN_PACKAGE_APP_TITAN_GAME)
        return "Titan Game";
    return "ELF";
}

int TitanPackageInstaller_ValidatePackageFile(const char *path, TitanPackageInfo *info)
{
    TitanPackageInfo local_info;
    if (info == 0) info = &local_info;
    return TitanPackageInstaller_Inspect(path, info, 0, 0);
}
