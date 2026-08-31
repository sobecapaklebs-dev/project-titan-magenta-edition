#include "titan_neutrino.h"
#include "titan_controller_assignment.h"

#include "titan_exec_handoff.h"
#include "titan_user_profile.h"
#include "titan_game_gsm.h"
#include "titan_game_patches.h"
#include "titan_vmc.h"
#include "titan_shell_busy.h"
#include "titan_storage_io.h"
#include "titan_session_service.h"
#include "titan_igr_compatibility.h"
#include "titan_applications.h"
#include "titan_time.h"

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>


static TitanNeutrinoGame g_games[TITAN_NEUTRINO_GAME_CAPACITY];
static unsigned int g_game_count;
static unsigned int g_catalog_generation;
static char g_neutrino_elf[TITAN_NEUTRINO_PATH_CAPACITY];
static char g_opl_ul_loader[TITAN_NEUTRINO_PATH_CAPACITY];

#define TITAN_NEUTRINO_LAUNCH_ARGUMENT_CAPACITY 16
#define TITAN_UL_TITLE_ARGUMENT_CAPACITY (TITAN_NEUTRINO_TITLE_CAPACITY + 9u)
#define TITAN_UL_SOURCE_ARGUMENT_CAPACITY (TITAN_NEUTRINO_PATH_CAPACITY + 8u)
#define TITAN_UL_NUMERIC_ARGUMENT_CAPACITY 32u
#define TITAN_NEUTRINO_CWD_ARGUMENT_CAPACITY (TITAN_NEUTRINO_PATH_CAPACITY + 6u)
#define TITAN_NEUTRINO_DVD_ARGUMENT_CAPACITY (TITAN_NEUTRINO_PATH_CAPACITY + TITAN_NEUTRINO_STARTUP_CAPACITY + 32u)
#define TITAN_NEUTRINO_ELF_ARGUMENT_CAPACITY (TITAN_NEUTRINO_STARTUP_CAPACITY + 20u)
#define TITAN_NEUTRINO_VMC_ARGUMENT_CAPACITY (TITAN_USER_PROFILE_VMC_PATH_CAPACITY + 8u)
#define TITAN_NEUTRINO_LAUNCH_LOG_PATH "mass:/TITAN/neutrino_launch.log"
#define TITAN_NEUTRINO_LAUNCH_LOG_FALLBACK "mass:/neutrino_launch.log"
#define TITAN_OPL_UL_LAUNCH_LOG_PATH "mass:/TITAN/opl_ul_launch.log"
#define TITAN_OPL_UL_LAUNCH_LOG_FALLBACK "mass:/opl_ul_launch.log"

typedef struct TitanNeutrinoLaunchPlan
{
    char cwd_argument[TITAN_NEUTRINO_CWD_ARGUMENT_CAPACITY];
    char dvd_argument[TITAN_NEUTRINO_DVD_ARGUMENT_CAPACITY];
    char vmc_argument[TITAN_NEUTRINO_VMC_ARGUMENT_CAPACITY];
    char elf_argument[TITAN_NEUTRINO_ELF_ARGUMENT_CAPACITY];
    char ul_title_argument[TITAN_UL_TITLE_ARGUMENT_CAPACITY];
    char ul_parts_argument[TITAN_UL_NUMERIC_ARGUMENT_CAPACITY];
    char ul_media_argument[TITAN_UL_NUMERIC_ARGUMENT_CAPACITY];
    char ul_source_argument[TITAN_UL_SOURCE_ARGUMENT_CAPACITY];
    char ul_igr_argument[32];
    char ul_igr_profile_argument[40];
    char neutrino_igr_argument[32];
    char neutrino_igr_profile_argument[40];
    char neutrino_exit_argument[96];
    char ul_exit_argument[64];
    char ul_pademu_argument[32];
    char ul_mtap_argument[24];
    char ul_mtap_port_argument[32];
    char *arguments[TITAN_NEUTRINO_LAUNCH_ARGUMENT_CAPACITY];
    int argument_count;
} TitanNeutrinoLaunchPlan;

static TitanNeutrinoLaunchPlan g_launch_plan;
static int g_last_load_error;

#define TITAN_ISO_SECTOR_SIZE 2048u
#define TITAN_ISO_DESCRIPTOR_LIMIT 64u
#define TITAN_ISO_ROOT_DIRECTORY_LIMIT (16u * 1024u * 1024u)
#define TITAN_SYSTEM_CNF_READ_LIMIT (16u * 1024u)
#define TITAN_UL_ENTRY_SIZE 64u

static const char *g_neutrino_candidates[] =
{
    "mass:/neutrino/neutrino.elf",
    "mass0:/neutrino/neutrino.elf",
    "mass:/NEUTRINO/neutrino.elf",
    "mass0:/NEUTRINO/neutrino.elf"
};


static const char *g_opl_ul_loader_candidates[] =
{
    "mass:/TITAN/titan_ul_engine.elf",
    "mass0:/TITAN/titan_ul_engine.elf",
    "mass:/TITAN/titan_ul_loader.elf",
    "mass0:/TITAN/titan_ul_loader.elf",
    "mass:/titan_ul_engine.elf",
    "mass0:/titan_ul_engine.elf",
    "mass:/titan_ul_loader.elf",
    "mass0:/titan_ul_loader.elf"
};

static int TitanNeutrino_HasExtension(
    const char *name,
    const char *extension
)
{
    size_t name_length;
    size_t extension_length;

    if (name == 0 || extension == 0)
        return 0;

    name_length = strlen(name);
    extension_length = strlen(extension);
    if (name_length < extension_length)
        return 0;

    return strcasecmp(name + name_length - extension_length, extension) == 0;
}

static int TitanNeutrino_HasIsoExtension(const char *name)
{
    return TitanNeutrino_HasExtension(name, ".iso");
}

static int TitanNeutrino_IsValidStartup(const char *startup)
{
    unsigned int index;

    if (startup == 0 || strlen(startup) != 11u)
        return 0;

    for (index = 0u; index < 4u; index++) {
        if (!isalnum((unsigned char)startup[index]))
            return 0;
    }
    if (startup[4] != '_')
        return 0;
    for (index = 5u; index < 8u; index++) {
        if (!isdigit((unsigned char)startup[index]))
            return 0;
    }
    if (startup[8] != '.')
        return 0;
    return isdigit((unsigned char)startup[9]) &&
        isdigit((unsigned char)startup[10]);
}

static int TitanNeutrino_StringEqualsInsensitive(
    const char *left,
    const char *right
)
{
    return left != 0 && right != 0 && strcasecmp(left, right) == 0;
}

static unsigned int TitanNeutrino_ReadLe32(const unsigned char *value)
{
    return (unsigned int)value[0] |
        ((unsigned int)value[1] << 8) |
        ((unsigned int)value[2] << 16) |
        ((unsigned int)value[3] << 24);
}

static void TitanNeutrino_CopyFixedText(
    char *destination,
    unsigned int destination_capacity,
    const unsigned char *source,
    unsigned int source_length
)
{
    unsigned int length;

    if (destination_capacity == 0u)
    {
        return;
    }
    length = source_length;
    if (length >= destination_capacity)
    {
        length = destination_capacity - 1u;
    }
    memcpy(destination, source, length);
    while (length > 0u && (destination[length - 1u] == '\0' || destination[length - 1u] == ' '))
    {
        length--;
    }
    destination[length] = '\0';
}

static int TitanNeutrino_IsoNameEquals(
    const unsigned char *name,
    unsigned int name_length,
    const char *expected
)
{
    unsigned int index;
    unsigned int expected_length;

    expected_length = (unsigned int)strlen(expected);
    if (name_length < expected_length)
    {
        return 0;
    }
    for (index = 0u; index < expected_length; index++)
    {
        if (toupper(name[index]) != toupper((unsigned char)expected[index]))
        {
            return 0;
        }
    }
    return name_length == expected_length || name[expected_length] == ';';
}

static int TitanNeutrino_ParseBootExecutable(
    const unsigned char *data,
    unsigned int length,
    char *startup,
    unsigned int startup_capacity
)
{
    unsigned int line_start;

    if (startup == 0 || startup_capacity == 0u)
        return 0;
    startup[0] = '\0';
    if (data == 0)
        return 0;

    line_start = 0u;
    while (line_start < length) {
        unsigned int line_end = line_start;
        unsigned int cursor;
        unsigned int basename;
        unsigned int value_end;
        char candidate[TITAN_NEUTRINO_STARTUP_CAPACITY];

        while (line_end < length && data[line_end] != '\r' && data[line_end] != '\n')
            line_end++;

        cursor = line_start;
        while (cursor < line_end && (data[cursor] == ' ' || data[cursor] == '\t'))
            cursor++;

        if (cursor + 5u <= line_end &&
            toupper((unsigned char)data[cursor + 0u]) == 'B' &&
            toupper((unsigned char)data[cursor + 1u]) == 'O' &&
            toupper((unsigned char)data[cursor + 2u]) == 'O' &&
            toupper((unsigned char)data[cursor + 3u]) == 'T' &&
            data[cursor + 4u] == '2') {
            cursor += 5u;
            while (cursor < line_end && (data[cursor] == ' ' || data[cursor] == '\t'))
                cursor++;
            if (cursor < line_end && data[cursor] == '=')
                cursor++;
            else
                goto next_line;
            while (cursor < line_end && (data[cursor] == ' ' || data[cursor] == '\t'))
                cursor++;

            basename = cursor;
            value_end = cursor;
            while (value_end < line_end && data[value_end] != ';') {
                if (data[value_end] == '\\' || data[value_end] == '/')
                    basename = value_end + 1u;
                value_end++;
            }
            while (value_end > basename &&
                (data[value_end - 1u] == ' ' || data[value_end - 1u] == '\t'))
                value_end--;

            if (value_end > basename &&
                value_end - basename < sizeof(candidate)) {
                TitanNeutrino_CopyFixedText(
                    candidate,
                    sizeof(candidate),
                    data + basename,
                    value_end - basename
                );
                if (TitanNeutrino_IsValidStartup(candidate)) {
                    strncpy(startup, candidate, startup_capacity - 1u);
                    startup[startup_capacity - 1u] = '\0';
                    return 1;
                }
            }
        }

next_line:
        while (line_end < length && (data[line_end] == '\r' || data[line_end] == '\n'))
            line_end++;
        line_start = line_end;
    }

    return 0;
}


static int TitanNeutrino_ParseVersionMetadata(
    const unsigned char *data,
    unsigned int length,
    char *version,
    unsigned int version_capacity
)
{
    unsigned int line_start;

    if (version == 0 || version_capacity == 0u)
        return 0;
    version[0] = '\0';
    if (data == 0)
        return 0;

    line_start = 0u;
    while (line_start < length)
    {
        unsigned int line_end = line_start;
        unsigned int cursor;
        unsigned int value_start;
        unsigned int value_end;

        while (line_end < length && data[line_end] != '\r' && data[line_end] != '\n')
            line_end++;

        cursor = line_start;
        while (cursor < line_end && (data[cursor] == ' ' || data[cursor] == '\t'))
            cursor++;

        if (cursor + 3u <= line_end &&
            toupper((unsigned char)data[cursor + 0u]) == 'V' &&
            toupper((unsigned char)data[cursor + 1u]) == 'E' &&
            toupper((unsigned char)data[cursor + 2u]) == 'R')
        {
            cursor += 3u;
            while (cursor < line_end && (data[cursor] == ' ' || data[cursor] == '\t'))
                cursor++;
            if (cursor < line_end && data[cursor] == '=')
            {
                cursor++;
                while (cursor < line_end && (data[cursor] == ' ' || data[cursor] == '\t'))
                    cursor++;
                value_start = cursor;
                value_end = line_end;
                while (value_end > value_start &&
                    (data[value_end - 1u] == ' ' || data[value_end - 1u] == '\t'))
                    value_end--;
                if (value_end > value_start)
                {
                    TitanNeutrino_CopyFixedText(
                        version,
                        version_capacity,
                        data + value_start,
                        value_end - value_start
                    );
                    return version[0] != '\0';
                }
            }
        }

        while (line_end < length && (data[line_end] == '\r' || data[line_end] == '\n'))
            line_end++;
        line_start = line_end;
    }

    return 0;
}

static int TitanNeutrino_ReadAt(
    int descriptor,
    TitanStorageIoOffset offset,
    void *buffer,
    TitanStorageIoSize size
)
{
    return TitanStorageIo_Seek64(descriptor, offset, SEEK_SET) == offset &&
        TitanStorageIo_ReadExact(descriptor, buffer, (size_t)size);
}

static void TitanNeutrino_ReadIsoMetadataInternal(
    const char *path,
    char *startup,
    unsigned int startup_capacity,
    char *version,
    unsigned int version_capacity
)
{
    static unsigned char system_cnf[TITAN_SYSTEM_CNF_READ_LIMIT];
    unsigned char sector[TITAN_ISO_SECTOR_SIZE];
    TitanStorageIoSize image_size;
    uint32_t root_extent = 0u;
    uint32_t root_size = 0u;
    int descriptor;
    unsigned int descriptor_index;
    uint64_t root_base;
    uint64_t root_offset;

    if (startup == 0 || startup_capacity == 0u)
        return;
    startup[0] = '\0';
    if (version != 0 && version_capacity > 0u)
        version[0] = '\0';
    if (path == 0 || path[0] == '\0' ||
        !TitanStorageIo_PathInfo64(path, 0, &image_size))
        return;

    descriptor = TitanStorageIo_Open(path, TITAN_STORAGE_IO_READ);
    if (descriptor < 0)
        return;

    for (descriptor_index = 0u;
         descriptor_index < TITAN_ISO_DESCRIPTOR_LIMIT;
         descriptor_index++) {
        uint64_t offset =
            (uint64_t)(16u + descriptor_index) * TITAN_ISO_SECTOR_SIZE;

        if (offset + sizeof(sector) > image_size ||
            !TitanNeutrino_ReadAt(descriptor, offset, sector, sizeof(sector)))
            goto done;
        TitanShellBusy_Pulse();

        if (memcmp(&sector[1], "CD001", 5u) != 0 || sector[6] != 1u)
            goto done;
        if (sector[0] == 255u)
            goto done;
        if (sector[0] != 1u)
            continue;
        if (sector[156] < 34u)
            goto done;

        root_extent = (uint32_t)TitanNeutrino_ReadLe32(&sector[158]);
        root_size = (uint32_t)TitanNeutrino_ReadLe32(&sector[166]);
        break;
    }

    if (root_extent == 0u || root_size == 0u ||
        root_size > TITAN_ISO_ROOT_DIRECTORY_LIMIT)
        goto done;

    root_base = (uint64_t)root_extent * TITAN_ISO_SECTOR_SIZE;
    if (root_base + root_size < root_base || root_base + root_size > image_size)
        goto done;

    root_offset = 0u;
    while (root_offset < root_size) {
        uint64_t sector_base = root_base +
            (root_offset / TITAN_ISO_SECTOR_SIZE) * TITAN_ISO_SECTOR_SIZE;
        unsigned int record_offset =
            (unsigned int)(root_offset % TITAN_ISO_SECTOR_SIZE);
        unsigned int record_length;
        unsigned int name_length;

        if (!TitanNeutrino_ReadAt(
                descriptor,
                sector_base,
                sector,
                sizeof(sector)))
            goto done;
        TitanShellBusy_Pulse();

        record_length = sector[record_offset];
        if (record_length == 0u) {
            root_offset += TITAN_ISO_SECTOR_SIZE - record_offset;
            continue;
        }
        if (record_length < 34u ||
            record_length > TITAN_ISO_SECTOR_SIZE - record_offset)
            goto done;

        name_length = sector[record_offset + 32u];
        if (name_length == 0u || 33u + name_length > record_length)
            goto done;

        if (TitanNeutrino_IsoNameEquals(
                &sector[record_offset + 33u],
                name_length,
                "SYSTEM.CNF")) {
            uint32_t file_extent =
                (uint32_t)TitanNeutrino_ReadLe32(&sector[record_offset + 2u]);
            uint32_t file_size =
                (uint32_t)TitanNeutrino_ReadLe32(&sector[record_offset + 10u]);
            uint64_t file_offset =
                (uint64_t)file_extent * TITAN_ISO_SECTOR_SIZE;
            unsigned int read_size;

            if (file_size == 0u || file_offset + file_size < file_offset ||
                file_offset + file_size > image_size)
                goto done;

            read_size = file_size > sizeof(system_cnf) ?
                sizeof(system_cnf) : file_size;
            if (!TitanNeutrino_ReadAt(
                    descriptor,
                    file_offset,
                    system_cnf,
                    read_size))
                goto done;
            TitanShellBusy_Pulse();
            TitanNeutrino_ParseBootExecutable(
                system_cnf,
                read_size,
                startup,
                startup_capacity
            );
            if (version != 0 && version_capacity > 0u)
            {
                TitanNeutrino_ParseVersionMetadata(
                    system_cnf,
                    read_size,
                    version,
                    version_capacity
                );
            }
            goto done;
        }

        root_offset += record_length;
    }

done:
    TitanStorageIo_Close(descriptor);
}

void TitanNeutrino_ReadIsoStartupMetadata(
    const char *path,
    char *startup,
    unsigned int startup_capacity
)
{
    TitanNeutrino_ReadIsoMetadataInternal(
        path,
        startup,
        startup_capacity,
        0,
        0u
    );
}

static int TitanNeutrino_PathExists(const char *path)
{
    DIR *directory;
    const char *separator;
    char parent[TITAN_NEUTRINO_PATH_CAPACITY];
    unsigned int length;

    separator = strrchr(path, '/');
    if (separator == 0)
    {
        return 0;
    }

    length = (unsigned int)(separator - path);
    if (length + 1u > sizeof(parent))
    {
        return 0;
    }

    memcpy(parent, path, length);
    parent[length] = '\0';
    directory = opendir(parent);
    if (directory == 0)
    {
        return 0;
    }

    for (;;)
    {
        struct dirent *entry;
        entry = readdir(directory);
        TitanShellBusy_Pulse();
        if (entry == 0)
        {
            break;
        }
        if (strcmp(entry->d_name, separator + 1) == 0)
        {
            closedir(directory);
            return 1;
        }
    }

    closedir(directory);
    return 0;
}

int TitanNeutrino_FindInstalledElf(
    char *destination,
    unsigned int capacity
)
{
    unsigned int index;

    if (destination == 0 || capacity == 0u)
    {
        return 0;
    }

    destination[0] = '\0';
    for (index = 0u; index < sizeof(g_neutrino_candidates) / sizeof(g_neutrino_candidates[0]); index++)
    {
        int exists;

        exists = TitanNeutrino_PathExists(g_neutrino_candidates[index]);
        TitanShellBusy_Pulse();
        if (exists)
        {
            strncpy(destination, g_neutrino_candidates[index], capacity - 1u);
            destination[capacity - 1u] = '\0';
            return 1;
        }
    }

    return 0;
}

int TitanNeutrino_FindInstalledOplUlLoader(
    char *destination,
    unsigned int capacity
)
{
    unsigned int index;

    if (destination == 0 || capacity == 0u)
    {
        return 0;
    }

    destination[0] = '\0';
    for (index = 0u;
         index < sizeof(g_opl_ul_loader_candidates) / sizeof(g_opl_ul_loader_candidates[0]);
         index++)
    {
        int exists;

        exists = TitanNeutrino_PathExists(g_opl_ul_loader_candidates[index]);
        TitanShellBusy_Pulse();
        if (exists)
        {
            strncpy(destination, g_opl_ul_loader_candidates[index], capacity - 1u);
            destination[capacity - 1u] = '\0';
            return 1;
        }
    }

    return 0;
}

static void TitanNeutrino_FindElf(void)
{
    TitanNeutrino_FindInstalledElf(
        g_neutrino_elf,
        sizeof(g_neutrino_elf)
    );
}

static void TitanNeutrino_FindOplUlLoader(void)
{
    TitanNeutrino_FindInstalledOplUlLoader(
        g_opl_ul_loader,
        sizeof(g_opl_ul_loader)
    );
}

static int TitanNeutrino_GameIsValid(const TitanNeutrinoGame *game)
{
    if (game == 0 ||
        memchr(game->title, '\0', sizeof(game->title)) == 0 ||
        memchr(game->path, '\0', sizeof(game->path)) == 0 ||
        memchr(game->startup, '\0', sizeof(game->startup)) == 0 ||
        game->title[0] == '\0' || game->path[0] == '\0')
        return 0;

    if (game->format == TITAN_NEUTRINO_GAME_ISO) {
        return TitanNeutrino_HasIsoExtension(game->path) &&
            game->parts == 1u &&
            (game->media == 0x12u || game->media == 0x14u) &&
            (game->startup[0] == '\0' ||
                TitanNeutrino_IsValidStartup(game->startup));
    }

    if (game->format == TITAN_NEUTRINO_GAME_USBEXTREME) {
        return game->parts > 0u &&
            (game->media == 0x12u || game->media == 0x14u) &&
            TitanNeutrino_IsValidStartup(game->startup) &&
            TitanNeutrino_HasExtension(game->path, "ul.cfg");
    }

    return 0;
}

static int TitanNeutrino_GameEquals(
    const TitanNeutrinoGame *left,
    const TitanNeutrinoGame *right
)
{
    if (left->format != right->format)
        return 0;
    if (left->format == TITAN_NEUTRINO_GAME_USBEXTREME)
        return TitanNeutrino_StringEqualsInsensitive(
            left->startup,
            right->startup
        );
    return TitanNeutrino_StringEqualsInsensitive(left->path, right->path);
}

static int TitanNeutrino_ContainsGame(const TitanNeutrinoGame *game)
{
    unsigned int index;
    for (index = 0u; index < g_game_count; index++) {
        if (TitanNeutrino_GameEquals(&g_games[index], game))
            return 1;
    }
    return 0;
}

static int TitanNeutrino_CompareGames(
    const TitanNeutrinoGame *left,
    const TitanNeutrinoGame *right
)
{
    int result = strcasecmp(left->title, right->title);
    if (result != 0)
        return result;
    result = strcasecmp(left->startup, right->startup);
    if (result != 0)
        return result;
    return strcasecmp(left->path, right->path);
}

static void TitanNeutrino_SortCatalog(void)
{
    unsigned int left;
    for (left = 1u; left < g_game_count; left++) {
        TitanNeutrinoGame current = g_games[left];
        unsigned int right = left;
        while (right > 0u &&
            TitanNeutrino_CompareGames(&g_games[right - 1u], &current) > 0) {
            g_games[right] = g_games[right - 1u];
            right--;
        }
        g_games[right] = current;
    }
}

static void TitanNeutrino_ScanDirectory(const char *directory_path)
{
    DIR *directory;

    directory = opendir(directory_path);
    if (directory == 0)
    {
        return;
    }

    while (g_game_count < TITAN_NEUTRINO_GAME_CAPACITY)
    {
        TitanNeutrinoGame *game;
        struct dirent *entry;
        unsigned int directory_length;
        unsigned int name_length;

        entry = readdir(directory);
        if (entry == 0)
        {
            break;
        }
        if (!TitanNeutrino_HasIsoExtension(entry->d_name))
        {
            continue;
        }

        directory_length = (unsigned int)strlen(directory_path);
        name_length = (unsigned int)strlen(entry->d_name);
        if (directory_length + name_length + 2u > TITAN_NEUTRINO_PATH_CAPACITY)
        {
            continue;
        }

        game = &g_games[g_game_count];
        memset(game, 0, sizeof(*game));
        memcpy(game->path, directory_path, directory_length);
        game->path[directory_length] = '/';
        memcpy(&game->path[directory_length + 1u], entry->d_name, name_length + 1u);

        if (name_length - 4u >= TITAN_NEUTRINO_TITLE_CAPACITY)
        {
            name_length = TITAN_NEUTRINO_TITLE_CAPACITY - 1u + 4u;
        }
        memcpy(game->title, entry->d_name, name_length - 4u);
        game->title[name_length - 4u] = '\0';
        game->format = TITAN_NEUTRINO_GAME_ISO;
        game->parts = 1u;
        game->media = strstr(directory_path, "/CD") != 0 ? 0x12u : 0x14u;
        TitanNeutrino_ReadIsoMetadataInternal(
            game->path,
            game->startup,
            sizeof(game->startup),
            game->version,
            sizeof(game->version)
        );
        if (TitanNeutrino_GameIsValid(game) && !TitanNeutrino_ContainsGame(game))
            g_game_count++;
        else
            memset(game, 0, sizeof(*game));
    }

    closedir(directory);
}

static void TitanNeutrino_ScanUlCfg(const char *root_path)
{
    FILE *file;
    char cfg_path[TITAN_NEUTRINO_PATH_CAPACITY];
    unsigned char entry[TITAN_UL_ENTRY_SIZE];
    unsigned int root_length;

    root_length = (unsigned int)strlen(root_path);
    if (root_length + 8u > sizeof(cfg_path))
    {
        return;
    }
    strcpy(cfg_path, root_path);
    strcat(cfg_path, "/ul.cfg");
    file = fopen(cfg_path, "rb");
    if (file == 0)
    {
        return;
    }
    while (g_game_count < TITAN_NEUTRINO_GAME_CAPACITY &&
        fread(entry, 1u, sizeof(entry), file) == sizeof(entry))
    {
        TitanNeutrinoGame *game;

        if (memcmp(&entry[32], "ul.", 3u) != 0 || entry[47] == 0u ||
            (entry[48] != 0x12u && entry[48] != 0x14u))
        {
            continue;
        }
        game = &g_games[g_game_count];
        memset(game, 0, sizeof(*game));
        TitanNeutrino_CopyFixedText(game->title, sizeof(game->title), entry, 32u);
        TitanNeutrino_CopyFixedText(game->startup, sizeof(game->startup), &entry[35], 12u);
        if (game->title[0] == '\0' || game->startup[0] == '\0')
        {
            continue;
        }
        strcpy(game->path, cfg_path);
        game->format = TITAN_NEUTRINO_GAME_USBEXTREME;
        game->parts = entry[47];
        game->media = entry[48];
        if (TitanNeutrino_GameIsValid(game) && !TitanNeutrino_ContainsGame(game))
            g_game_count++;
        else
            memset(game, 0, sizeof(*game));
    }
    fclose(file);
}

void TitanNeutrino_Initialize(void)
{
    g_game_count = 0u;
    g_catalog_generation = 1u;
    g_neutrino_elf[0] = '\0';
    g_opl_ul_loader[0] = '\0';
    memset(&g_launch_plan, 0, sizeof(g_launch_plan));
    g_last_load_error = 0;
}

void TitanNeutrino_ReplaceCatalog(
    const TitanNeutrinoGame *games,
    unsigned int count,
    const char *neutrino_elf
)
{
    unsigned int index;

    if (count > TITAN_NEUTRINO_GAME_CAPACITY)
        count = TITAN_NEUTRINO_GAME_CAPACITY;

    memset(g_games, 0, sizeof(g_games));
    g_game_count = 0u;

    for (index = 0u; games != 0 && index < count; index++) {
        if (!TitanNeutrino_GameIsValid(&games[index]) ||
            TitanNeutrino_ContainsGame(&games[index]))
            continue;
        g_games[g_game_count++] = games[index];
    }
    TitanNeutrino_SortCatalog();

    g_neutrino_elf[0] = '\0';
    if (neutrino_elf != 0 &&
        strlen(neutrino_elf) < sizeof(g_neutrino_elf)) {
        strncpy(g_neutrino_elf, neutrino_elf, sizeof(g_neutrino_elf) - 1u);
        g_neutrino_elf[sizeof(g_neutrino_elf) - 1u] = '\0';
    }
    g_catalog_generation = g_catalog_generation == 0xffffffffu ? 1u : g_catalog_generation + 1u;
}

unsigned int TitanNeutrino_Refresh(void)
{
    g_game_count = 0u;
    TitanNeutrino_FindElf();
    TitanNeutrino_FindOplUlLoader();
    TitanNeutrino_ScanDirectory("mass:/DVD");
    TitanNeutrino_ScanDirectory("mass:/CD");
    TitanNeutrino_ScanUlCfg("mass:");
    if (g_game_count == 0u)
    {
        TitanNeutrino_ScanDirectory("mass0:/DVD");
        TitanNeutrino_ScanDirectory("mass0:/CD");
        TitanNeutrino_ScanUlCfg("mass0:");
    }
    TitanNeutrino_SortCatalog();
    g_catalog_generation = g_catalog_generation == 0xffffffffu ? 1u : g_catalog_generation + 1u;
    return g_game_count;
}

unsigned int TitanNeutrino_GetGameCount(void)
{
    return g_game_count;
}

unsigned int TitanNeutrino_GetCatalogGeneration(void)
{
    return g_catalog_generation;
}

const TitanNeutrinoGame *TitanNeutrino_GetGame(unsigned int index)
{
    return index < g_game_count ? &g_games[index] : 0;
}

const char *TitanNeutrino_GetElfPath(void)
{
    return g_neutrino_elf;
}

static int TitanNeutrino_BuildWorkingDirectoryArgument(
    const char *elf_path,
    char *destination,
    unsigned int capacity
)
{
    const char *separator;
    unsigned int directory_length;

    if (elf_path == 0 || destination == 0 || capacity == 0u)
    {
        return 0;
    }

    separator = strrchr(elf_path, '/');
    if (separator == 0)
    {
        return 0;
    }

    directory_length = (unsigned int)(separator - elf_path);
    if (5u + directory_length + 1u > capacity)
    {
        return 0;
    }

    memcpy(destination, "-cwd=", 5u);
    memcpy(&destination[5], elf_path, directory_length);
    destination[5u + directory_length] = '\0';
    return 1;
}

static int TitanNeutrino_BuildUsbDvdArgument(
    const char *game_path,
    char *destination,
    unsigned int capacity
)
{
    const char *source_path;
    unsigned int source_length;

    if (game_path == 0 || destination == 0 || capacity == 0u)
    {
        return 0;
    }

    source_path = strchr(game_path, ':');
    if (source_path == 0)
    {
        return 0;
    }

    source_path++;
    while (*source_path == '/' || *source_path == '\\')
    {
        source_path++;
    }

    source_length = (unsigned int)strlen(source_path);
    if (9u + source_length + 1u > capacity)
    {
        return 0;
    }

    memcpy(destination, "-dvd=usb:", 9u);
    memcpy(&destination[9], source_path, source_length + 1u);
    return 1;
}

static int TitanNeutrino_BuildElfArgument(
    const TitanNeutrinoGame *game,
    char *destination,
    unsigned int capacity
)
{
    (void)game;

    if (destination == 0 || capacity < sizeof("-elf=auto"))
    {
        return 0;
    }

    /* ISO launches keep Neutrino's normal SYSTEM.CNF auto detection. */
    strcpy(destination, "-elf=auto");
    return 1;
}

static int TitanNeutrino_BuildUlVmcArgument(
    const TitanUserProfile *profile,
    int port,
    char *destination,
    unsigned int capacity
)
{
    const char *prefix;
    size_t prefix_length;
    size_t path_length;

    if (profile == 0 || destination == 0 ||
        port < 0 || port > 1 ||
        (TitanVmc_GetCachedStatus(profile) != TITAN_VMC_STATUS_READY &&
         TitanVmc_RefreshStatus(profile) != TITAN_VMC_STATUS_READY))
    {
        return 0;
    }

    prefix = port == 0 ? "-vmc0=" : "-vmc1=";
    prefix_length = strlen(prefix);
    path_length = strnlen(profile->vmc_path, sizeof(profile->vmc_path));
    if (path_length == 0u || path_length >= sizeof(profile->vmc_path) ||
        prefix_length + path_length + 1u > capacity)
    {
        return 0;
    }

    memcpy(destination, prefix, prefix_length);
    memcpy(destination + prefix_length, profile->vmc_path, path_length + 1u);
    return 1;
}

static TitanNeutrinoResult TitanNeutrino_PrepareLaunch(
    unsigned int index,
    TitanNeutrinoLaunchPlan *plan
)
{
    const TitanNeutrinoGame *game;
    const TitanUserProfile *profile;
    const char *gsm_argument;
    TitanVmcResult vmc_result;

    if (plan == 0)
    {
        return TITAN_NEUTRINO_ARGUMENT_OVERFLOW;
    }

    game = TitanNeutrino_GetGame(index);
    if (game == 0)
    {
        return TITAN_NEUTRINO_INVALID_GAME;
    }

    memset(plan, 0, sizeof(*plan));

    if (game->format == TITAN_NEUTRINO_GAME_USBEXTREME)
    {
        if (game->title[0] == '\0' || game->startup[0] == '\0' ||
            game->parts == 0u ||
            (game->media != 0x12u && game->media != 0x14u))
        {
            return TITAN_NEUTRINO_USBEXTREME_INVALID;
        }

        profile = TitanUserProfiles_GetActive();
        if (profile == 0)
        {
            return TITAN_NEUTRINO_NO_PROFILE;
        }

        /* VMC creation is deliberately lazy. Boot/profile navigation only
         * probes status; the launch transition owns the potentially long
         * first-time 8 MiB creation while the busy pump keeps UI/audio alive. */
        vmc_result = TitanVmc_Ensure(profile);
        if (vmc_result != TITAN_VMC_RESULT_OK &&
            vmc_result != TITAN_VMC_RESULT_CREATED &&
            vmc_result != TITAN_VMC_RESULT_FORMATTED)
        {
            return TITAN_NEUTRINO_VMC_NOT_READY;
        }

        if (!TitanNeutrino_BuildUlVmcArgument(
                profile,
                0,
                plan->vmc_argument,
                sizeof(plan->vmc_argument)))
        {
            return TITAN_NEUTRINO_VMC_NOT_READY;
        }

        /* Titan UL Engine receives the already-scanned UL record metadata and
         * the active profile VMC explicitly. It must never silently fall back
         * to an OPL per-game CFG that points at a different memory card. */
        if (g_opl_ul_loader[0] == '\0')
        {
            TitanNeutrino_FindOplUlLoader();
        }
        if (g_opl_ul_loader[0] == '\0')
        {
            return TITAN_NEUTRINO_OPL_UL_NOT_FOUND;
        }

        {
            int title_written;
            int parts_written;
            int media_written;
            int source_written;

            title_written = snprintf(
                plan->ul_title_argument,
                sizeof(plan->ul_title_argument),
                "-ulname=%s",
                game->title
            );
            parts_written = snprintf(
                plan->ul_parts_argument,
                sizeof(plan->ul_parts_argument),
                "-ulparts=%u",
                (unsigned int)game->parts
            );
            media_written = snprintf(
                plan->ul_media_argument,
                sizeof(plan->ul_media_argument),
                "-ulmedia=0x%02X",
                (unsigned int)game->media
            );
            source_written = snprintf(
                plan->ul_source_argument,
                sizeof(plan->ul_source_argument),
                "-ulcfg=%s",
                game->path
            );

            if (title_written < 0 ||
                (unsigned int)title_written >= sizeof(plan->ul_title_argument) ||
                parts_written < 0 ||
                (unsigned int)parts_written >= sizeof(plan->ul_parts_argument) ||
                media_written < 0 ||
                (unsigned int)media_written >= sizeof(plan->ul_media_argument) ||
                source_written < 0 ||
                (unsigned int)source_written >= sizeof(plan->ul_source_argument))
            {
                return TITAN_NEUTRINO_ARGUMENT_OVERFLOW;
            }
        }

        /* Golden UL stays byte-exact. Titan materializes only the normal OPL
         * per-game GSM keys before the unchanged three-argument handoff. */
        if (!TitanGameGsm_PrepareUlGoldenConfig(
                game->startup,
                TitanGameGsm_GetMode(game->startup)))
        {
            return TITAN_NEUTRINO_VIDEO_PROFILE_FAILED;
        }

        plan->arguments[plan->argument_count++] = g_opl_ul_loader;
        plan->arguments[plan->argument_count++] = (char *)game->startup;
        plan->arguments[plan->argument_count++] = "titan-ul";
        /* Recovery policy: preserve the proven 2026-08-05 OPL-derived UL
         * contract exactly. The historical engine reads ul.cfg itself and
         * expects only argv[0]=loader, argv[1]=startup, argv[2]=titan-ul. */
        return TITAN_NEUTRINO_OK;
    }

    if (g_neutrino_elf[0] == '\0')
    {
        return TITAN_NEUTRINO_NOT_FOUND;
    }

    /* Golden Neutrino is immutable. Patch preparation happens only at the
     * actual launch boundary and is installed by Titan's existing low-memory
     * stage2. The Golden command line remains byte-for-byte ABI compatible. */

    profile = TitanUserProfiles_GetActive();
    if (profile == 0)
    {
        return TITAN_NEUTRINO_NO_PROFILE;
    }

    vmc_result = TitanVmc_Ensure(profile);
    if (vmc_result != TITAN_VMC_RESULT_OK &&
        vmc_result != TITAN_VMC_RESULT_CREATED &&
        vmc_result != TITAN_VMC_RESULT_FORMATTED)
    {
        return TITAN_NEUTRINO_VMC_NOT_READY;
    }

    if (!TitanNeutrino_BuildWorkingDirectoryArgument(
            g_neutrino_elf,
            plan->cwd_argument,
            sizeof(plan->cwd_argument)))
    {
        return TITAN_NEUTRINO_ARGUMENT_OVERFLOW;
    }

    if (!TitanNeutrino_BuildUsbDvdArgument(
            game->path,
            plan->dvd_argument,
            sizeof(plan->dvd_argument)))
    {
        return TITAN_NEUTRINO_ARGUMENT_OVERFLOW;
    }

    if (!TitanNeutrino_BuildElfArgument(
            game,
            plan->elf_argument,
            sizeof(plan->elf_argument)))
    {
        return TITAN_NEUTRINO_ARGUMENT_OVERFLOW;
    }

    if (!TitanVmc_BuildNeutrinoArgument(
            profile,
            0,
            plan->vmc_argument,
            sizeof(plan->vmc_argument)))
    {
        return TITAN_NEUTRINO_VMC_NOT_READY;
    }

    /* Neutrino reads options from argv[1], so argv[0] remains the Golden
     * executable path exactly as in the validated production contract. */
    plan->arguments[plan->argument_count++] = g_neutrino_elf;
    plan->arguments[plan->argument_count++] = plan->cwd_argument;
    plan->arguments[plan->argument_count++] = "-bsd=usb";
    plan->arguments[plan->argument_count++] = plan->dvd_argument;
    plan->arguments[plan->argument_count++] = plan->vmc_argument;
    plan->arguments[plan->argument_count++] = plan->elf_argument;
    plan->arguments[plan->argument_count++] = "-logo";

    gsm_argument = TitanGameGsm_GetNeutrinoArgument(
        TitanGameGsm_GetMode(game->startup)
    );
    if (gsm_argument != 0 && gsm_argument[0] != '\0')
    {
        if (plan->argument_count >= TITAN_NEUTRINO_LAUNCH_ARGUMENT_CAPACITY)
        {
            return TITAN_NEUTRINO_ARGUMENT_OVERFLOW;
        }
        plan->arguments[plan->argument_count++] = (char *)gsm_argument;
    }

    return TITAN_NEUTRINO_OK;
}

static TitanNeutrinoResult TitanNeutrino_PreparePhysicalDiscLaunch(
    const char *boot_path,
    const char *vmc_argument,
    TitanNeutrinoLaunchPlan *plan
)
{
    int written;

    if (plan == 0 || boot_path == 0 || boot_path[0] == '\0')
        return TITAN_NEUTRINO_INVALID_GAME;
    if (g_neutrino_elf[0] == '\0')
        return TITAN_NEUTRINO_NOT_FOUND;
    if (strncmp(boot_path, "cdrom0:\\", 8u) != 0 || strstr(boot_path, "..") != 0)
        return TITAN_NEUTRINO_INVALID_GAME;

    memset(plan, 0, sizeof(*plan));
    if (!TitanNeutrino_BuildWorkingDirectoryArgument(
            g_neutrino_elf, plan->cwd_argument, sizeof(plan->cwd_argument)))
        return TITAN_NEUTRINO_ARGUMENT_OVERFLOW;

    written = snprintf(plan->elf_argument, sizeof(plan->elf_argument), "-elf=%s", boot_path);
    if (written < 0 || (unsigned int)written >= sizeof(plan->elf_argument))
        return TITAN_NEUTRINO_ARGUMENT_OVERFLOW;

    plan->arguments[plan->argument_count++] = g_neutrino_elf;
    plan->arguments[plan->argument_count++] = plan->cwd_argument;
    plan->arguments[plan->argument_count++] = "-bsd=no";
    plan->arguments[plan->argument_count++] = "-dvd=no";
    if (vmc_argument != 0 && vmc_argument[0] != '\0')
        plan->arguments[plan->argument_count++] = (char *)vmc_argument;
    plan->arguments[plan->argument_count++] = plan->elf_argument;
    plan->arguments[plan->argument_count++] = "-logo";

    return TITAN_NEUTRINO_OK;
}

static TitanNeutrinoResult TitanNeutrino_RenderLaunchCommand(
    TitanNeutrinoLaunchPlan *plan,
    char *destination,
    unsigned int capacity
)
{
    unsigned int used = 0u;
    int i;
    if (plan == 0 || destination == 0 || capacity == 0u)
        return TITAN_NEUTRINO_ARGUMENT_OVERFLOW;
    destination[0] = '\0';
    for (i = 0; i < plan->argument_count; ++i)
    {
        const char *arg = plan->arguments[i];
        unsigned int len = (unsigned int)strlen(arg);
        if (used + len + (i > 0 ? 1u : 0u) + 1u > capacity)
            return TITAN_NEUTRINO_ARGUMENT_OVERFLOW;
        if (i > 0) destination[used++] = ' ';
        memcpy(destination + used, arg, len);
        used += len;
        destination[used] = '\0';
    }
    return TITAN_NEUTRINO_OK;
}

TitanNeutrinoResult TitanNeutrino_GetPhysicalDiscLaunchCommand(
    const char *boot_path,
    const char *vmc_argument,
    char *destination,
    unsigned int capacity
)
{
    TitanNeutrinoResult result;
    result = TitanNeutrino_PreparePhysicalDiscLaunch(boot_path, vmc_argument, &g_launch_plan);
    if (result != TITAN_NEUTRINO_OK) return result;
    return TitanNeutrino_RenderLaunchCommand(&g_launch_plan, destination, capacity);
}

TitanNeutrinoResult TitanNeutrino_LaunchPhysicalDisc(
    const char *boot_path,
    const char *vmc_argument
)
{
    TitanNeutrinoResult result;
    g_last_load_error = 0;
    result = TitanNeutrino_PreparePhysicalDiscLaunch(boot_path, vmc_argument, &g_launch_plan);
    if (result != TITAN_NEUTRINO_OK) return result;
#if defined(__EE__) || defined(_EE)
    {
        TitanExecHandoffResult handoff_result;
        handoff_result = TitanExecHandoff_Run(g_launch_plan.argument_count, g_launch_plan.arguments);
        g_last_load_error = (int)handoff_result;
    }
#else
    g_last_load_error = -1;
#endif
    return TITAN_NEUTRINO_EXEC_FAILED;
}

static void TitanNeutrino_WriteLaunchLog(
    const TitanNeutrinoGame *game,
    const TitanNeutrinoLaunchPlan *plan,
    int load_result
)
{
    FILE *file;
    const char *log_path;
    const char *fallback_path;
    const char *backend_name;
    int index;

    if (game != 0 && game->format == TITAN_NEUTRINO_GAME_USBEXTREME)
    {
        log_path = TITAN_OPL_UL_LAUNCH_LOG_PATH;
        fallback_path = TITAN_OPL_UL_LAUNCH_LOG_FALLBACK;
        backend_name = "OPL BDM UL";
    }
    else
    {
        log_path = TITAN_NEUTRINO_LAUNCH_LOG_PATH;
        fallback_path = TITAN_NEUTRINO_LAUNCH_LOG_FALLBACK;
        backend_name = "Neutrino";
    }

    file = fopen(log_path, "wb");
    if (file == 0)
    {
        file = fopen(fallback_path, "wb");
    }
    if (file == 0)
    {
        return;
    }

    fprintf(file, "Project Titan %s launch\n", backend_name);
    if (plan != 0 && plan->argument_count > 0)
    {
        fprintf(file, "elf=%s\n", plan->arguments[0]);
    }
    if (game != 0)
    {
        fprintf(file, "game=%s\n", game->title);
        fprintf(file, "source=%s\n", game->path);
        fprintf(file, "startup=%s\n", game->startup);
        fprintf(file, "format=%u\n", (unsigned int)game->format);
        fprintf(file, "parts=%u\n", (unsigned int)game->parts);
        fprintf(file, "media=0x%02X\n", (unsigned int)game->media);
    }
    fprintf(file, "load_result=%d\n", load_result);
    if (plan != 0)
    {
        fprintf(file, "argc=%d\n", plan->argument_count);
        for (index = 0; index < plan->argument_count; index++)
        {
            fprintf(file, "argv[%d]=%s\n", index, plan->arguments[index]);
        }
    }
    fclose(file);
}

TitanNeutrinoResult TitanNeutrino_GetLaunchCommand(
    unsigned int index,
    char *destination,
    unsigned int capacity
)
{
    TitanNeutrinoResult result;
    unsigned int used;
    int argument_index;

    if (destination == 0 || capacity == 0u)
    {
        return TITAN_NEUTRINO_ARGUMENT_OVERFLOW;
    }
    destination[0] = '\0';

    result = TitanNeutrino_PrepareLaunch(index, &g_launch_plan);
    if (result != TITAN_NEUTRINO_OK)
    {
        return result;
    }

    used = 0u;
    for (argument_index = 0;
         argument_index < g_launch_plan.argument_count;
         argument_index++)
    {
        const char *argument;
        unsigned int length;

        argument = g_launch_plan.arguments[argument_index];
        length = (unsigned int)strlen(argument);
        if (used + length + (argument_index > 0 ? 1u : 0u) + 1u > capacity)
        {
            destination[0] = '\0';
            return TITAN_NEUTRINO_ARGUMENT_OVERFLOW;
        }
        if (argument_index > 0)
        {
            destination[used++] = ' ';
        }
        memcpy(&destination[used], argument, length);
        used += length;
        destination[used] = '\0';
    }

    return TITAN_NEUTRINO_OK;
}

int TitanNeutrino_GetLastLoadError(void)
{
    return g_last_load_error;
}

static void TitanNeutrino_BeginExternalSession(const TitanNeutrinoGame *game)
{
    TitanSessionDescriptor session;
    const TitanUserProfile *profile;

    if (game == 0)
        return;

    memset(&session, 0, sizeof(session));
    profile = TitanUserProfiles_GetActive();

    snprintf(session.session_id, sizeof(session.session_id), "%s-%u",
        game->format == TITAN_NEUTRINO_GAME_USBEXTREME ? "ul" : "neutrino",
        (unsigned int)TitanTime_NowMilliseconds());
    strncpy(session.title_id,
        game->startup[0] != '\0' ? game->startup : "UNKNOWN",
        sizeof(session.title_id) - 1u);
    strncpy(session.launch_path, game->path, sizeof(session.launch_path) - 1u);
    if (profile != 0) {
        strncpy(session.profile, profile->name, sizeof(session.profile) - 1u);
        strncpy(session.vmc, profile->vmc_path, sizeof(session.vmc) - 1u);
    }

    session.classification = game->format == TITAN_NEUTRINO_GAME_USBEXTREME ?
        TITAN_APPLICATION_CLASS_UL_ENGINE : TITAN_APPLICATION_CLASS_NEUTRINO_ENGINE;
    session.engine = game->format == TITAN_NEUTRINO_GAME_USBEXTREME ?
        TITAN_SESSION_ENGINE_UL : TITAN_SESSION_ENGINE_NEUTRINO;
    session.start_time_ms = TitanTime_NowMilliseconds();
    session.runtime_capabilities = TITAN_SESSION_CAP_SAFE_RETURN |
        TITAN_SESSION_CAP_INPUT_NORMALIZED;
    session.igr_compatibility = TITAN_IGR_UNKNOWN;
    session.overlay_level = TITAN_OVERLAY_RETURN_ONLY;

    TitanSession_Initialize();
    if (TitanSession_Begin(&session))
        (void)TitanSession_Transition(TITAN_SESSION_LAUNCHING);
}

TitanNeutrinoResult TitanNeutrino_Launch(unsigned int index)
{
    const TitanNeutrinoGame *game;
    TitanNeutrinoResult result;
    TitanExecHandoffResult handoff_result;

    g_last_load_error = 0;
    result = TitanNeutrino_PrepareLaunch(index, &g_launch_plan);
    if (result != TITAN_NEUTRINO_OK)
    {
        return result;
    }

    game = TitanNeutrino_GetGame(index);
    TitanNeutrino_BeginExternalSession(game);
    TitanNeutrino_WriteLaunchLog(game, &g_launch_plan, 0);

#if defined(__EE__) || defined(_EE)
    {
        TitanGamePatchLaunchState patch_state;
        TitanGamePatchResult patch_result;

        memset(&patch_state, 0, sizeof(patch_state));
        if (game != 0 && game->format == TITAN_NEUTRINO_GAME_USBEXTREME)
        {
            patch_result = TitanGamePatches_PrepareUlGoldenRuntime(game->startup);
            if (patch_result != TITAN_GAME_PATCH_OK)
                return TITAN_NEUTRINO_UL_PATCH_RUNTIME_FAILED;
        }
        else if (game != 0)
        {
            /* Materialize the current persisted selections only at the actual
             * target launch boundary. GetLaunchCommand() therefore remains a
             * pure view of the validated Golden argv contract. */
            patch_result = TitanGamePatches_PrepareLaunch(game->startup, &patch_state);
            if (patch_result != TITAN_GAME_PATCH_OK)
                return TITAN_NEUTRINO_PATCH_PREPARE_FAILED;
        }

        /* The existing embedded low-memory stage2 owns the handoff. For
         * Neutrino only, active CHT/ENH asks it to install the optional PS2RD
         * segment before loading the unchanged Golden. Golden UL continues
         * through its native OPL cheat path and historical three argv values. */
        if (game != 0 &&
            game->format != TITAN_NEUTRINO_GAME_USBEXTREME &&
            (patch_state.gameplay_active || patch_state.enhancement_active))
        {
            handoff_result = TitanExecHandoff_RunWithPatch(
                g_launch_plan.argument_count,
                g_launch_plan.arguments,
                patch_state.payload_path
            );
        }
        else
        {
            handoff_result = TitanExecHandoff_Run(
                g_launch_plan.argument_count,
                g_launch_plan.arguments
            );
        }
    }
    g_last_load_error = (int)handoff_result;
    TitanNeutrino_WriteLaunchLog(
        game,
        &g_launch_plan,
        (int)handoff_result
    );

    if (handoff_result != TITAN_EXEC_HANDOFF_OK)
    {
        TitanSession_Fail();
        if (game != 0 && game->format == TITAN_NEUTRINO_GAME_USBEXTREME)
        {
            /* The Golden UL staging is transactional. If stage2/ExecPS2 ever
             * returns instead of transferring control, restore the user's
             * original CHT/CFG immediately instead of waiting for next boot. */
            TitanGamePatches_RecoverUlGoldenRuntime();
            return TITAN_NEUTRINO_OPL_UL_LOAD_FAILED;
        }
        return TITAN_NEUTRINO_ELF_LOAD_FAILED;
    }
#else
    handoff_result = TITAN_EXEC_HANDOFF_EXEC_RETURNED;
    (void)handoff_result;
#endif

    return TITAN_NEUTRINO_EXEC_FAILED;
}

const char *TitanNeutrino_GetResultName(TitanNeutrinoResult result)
{
    switch (result)
    {
        case TITAN_NEUTRINO_NOT_FOUND: return "neutrino.elf not found in mass:/neutrino";
        case TITAN_NEUTRINO_PATCH_PREPARE_FAILED: return "Unable to prepare Titan cheats/enhancements";
        case TITAN_NEUTRINO_UL_PATCH_RUNTIME_FAILED: return "Unable to stage Golden UL cheat/enhancement runtime";
        case TITAN_NEUTRINO_NO_GAMES: return "No ISO or ul.cfg game found on USB";
        case TITAN_NEUTRINO_INVALID_GAME: return "Invalid game selection";
        case TITAN_NEUTRINO_NO_PROFILE: return "No active profile";
        case TITAN_NEUTRINO_VMC_NOT_READY: return "Active profile VMC is not ready";
        case TITAN_NEUTRINO_USBEXTREME_INVALID: return "Invalid USBExtreme entry in ul.cfg";
        case TITAN_NEUTRINO_USBEXTREME_PART_MISSING: return "USBExtreme game has a missing split file";
        case TITAN_NEUTRINO_ARGUMENT_OVERFLOW: return "Launch argument is too long";
        case TITAN_NEUTRINO_OPL_UL_NOT_FOUND: return "Titan UL Engine not found in mass:/TITAN";
        case TITAN_NEUTRINO_VIDEO_PROFILE_FAILED: return "Unable to prepare Golden engine resolution profile";
        case TITAN_NEUTRINO_OPL_UL_LOAD_FAILED: return "Unable to load Titan UL Engine from USB";
        case TITAN_NEUTRINO_ELF_LOAD_FAILED: return "Unable to load neutrino.elf from USB";
        case TITAN_NEUTRINO_EXEC_FAILED: return "Selected game backend returned unexpectedly";
        case TITAN_NEUTRINO_OK:
        default: return "Ready";
    }
}
