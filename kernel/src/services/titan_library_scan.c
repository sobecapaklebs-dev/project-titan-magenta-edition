#include "titan_library_scan.h"

#include "titan_elf_validate.h"
#include "titan_library_cache.h"
#include "titan_localization.h"
#include "titan_applications.h"
#include "titan_neutrino.h"
#include "titan_notifications.h"
#include "titan_diagnostics.h"
#include "titan_storage_io.h"
#include "titan_storage_paths.h"
#include "titan_shell_busy.h"
#include "titan_vlombiscore.h"

#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

#if defined(__GNUC__)
extern void TitanVlombisCore_RecordWork(uint32_t capability_mask, TitanVlombisEvent event, uint32_t work_units, uint32_t bytes_avoided) __attribute__((weak));
extern void TitanVlombisCore_RecordLibraryMetadataReuse(uint32_t count, uint32_t bytes_avoided) __attribute__((weak));
extern void TitanDiagnostics_Event(TitanDiagLevel level, TitanDiagSubsystem subsystem, uint16_t event, int32_t result, uint32_t a, uint32_t b, uint32_t c, uint32_t d) __attribute__((weak));
#endif

static void TitanLibraryScan_Diag(
    TitanDiagLevel level,
    uint16_t event,
    int32_t result,
    uint32_t a,
    uint32_t b,
    uint32_t c,
    uint32_t d
)
{
#if defined(__GNUC__)
    if (TitanDiagnostics_Event == 0)
        return;
#endif
    TitanDiagnostics_Event(level, TITAN_DIAG_LIBRARY, event, result, a, b, c, d);
}

#ifndef TITAN_LIBRARY_PRIMARY_DEVICE
#define TITAN_LIBRARY_PRIMARY_DEVICE "mass:"
#endif

#ifndef TITAN_LIBRARY_FALLBACK_DEVICE
#define TITAN_LIBRARY_FALLBACK_DEVICE "mass0:"
#endif

#define TITAN_LIBRARY_PRIMARY_ROOT TITAN_LIBRARY_PRIMARY_DEVICE "/"
#define TITAN_LIBRARY_FALLBACK_ROOT TITAN_LIBRARY_FALLBACK_DEVICE "/"

#define TITAN_LIBRARY_APP_STACK_CAPACITY 4u
#define TITAN_LIBRARY_APP_MAX_DEPTH 3u
#define TITAN_LIBRARY_UL_ENTRY_SIZE 64u

typedef struct TitanLibraryAppFrame
{
    DIR *directory;
    char path[TITAN_APPLICATION_PATH_CAPACITY];
    unsigned int depth;
    unsigned int manifest_checked;
} TitanLibraryAppFrame;

typedef struct TitanLibraryScanner
{
    TitanLibraryScanStatus status;
    TitanNeutrinoGame games[TITAN_NEUTRINO_GAME_CAPACITY];
    TitanApplication applications[TITAN_APPLICATION_CAPACITY];
    char neutrino_elf[TITAN_NEUTRINO_PATH_CAPACITY];
    unsigned int game_count;
    unsigned int application_count;
    unsigned int device_pass;
    unsigned int game_root_index;
    DIR *game_directory;
    FILE *ul_file;
    unsigned int metadata_index;
    unsigned int app_root_index;
    unsigned int app_root_active;
    unsigned int app_primary_root_opened;
    TitanLibraryAppFrame app_stack[TITAN_LIBRARY_APP_STACK_CAPACITY];
    unsigned int app_stack_count;
    unsigned int scan_failed;
    unsigned int capacity_exceeded;
    unsigned int cache_save_failed;
    unsigned int deferred_ui;
    volatile unsigned int catalog_commit_pending;
} TitanLibraryScanner;

static TitanLibraryScanner g_library_scan;


typedef struct TitanLibraryPreviousCatalog
{
    TitanNeutrinoGame games[TITAN_NEUTRINO_GAME_CAPACITY];
    unsigned int game_count;
    unsigned int valid;
} TitanLibraryPreviousCatalog;

static TitanLibraryPreviousCatalog g_library_previous;

static const char *g_primary_game_roots[] =
{
    TITAN_LIBRARY_PRIMARY_DEVICE "/DVD",
    TITAN_LIBRARY_PRIMARY_DEVICE "/CD"
};

static const char *g_fallback_game_roots[] =
{
    TITAN_LIBRARY_FALLBACK_DEVICE "/DVD",
    TITAN_LIBRARY_FALLBACK_DEVICE "/CD"
};

/* TITAN_ELFSCAN68_HW_FIX
 * Automatic background verification scans only the canonical Titan app tree.
 * External ELF applications remain supported recursively inside TITAN/apps.
 * Legacy /APPS and /BOOT probing is deliberately not part of the automatic
 * hardware scan because PS2 BDM/FileXio can block opening a missing directory. */
static const char *g_primary_app_roots[] =
{
    TITAN_LIBRARY_PRIMARY_DEVICE "/TITAN/apps"
};

static const char *g_fallback_app_roots[] =
{
    TITAN_LIBRARY_FALLBACK_DEVICE "/TITAN/apps"
};

static void TitanLibraryScan_CopyString(
    char *destination,
    unsigned int capacity,
    const char *source
)
{
    if (destination == 0 || capacity == 0u)
    {
        return;
    }

    destination[0] = '\0';
    if (source == 0)
    {
        return;
    }

    {
        unsigned int index;

        index = 0u;
        while (source[index] != '\0' && index + 1u < capacity)
        {
            destination[index] = source[index];
            index++;
        }
        destination[index] = '\0';
    }
}

static int TitanLibraryScan_EqualsInsensitive(
    const char *left,
    const char *right
)
{
    if (left == 0 || right == 0)
    {
        return 0;
    }

    while (*left != '\0' && *right != '\0')
    {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right))
        {
            return 0;
        }
        left++;
        right++;
    }

    return *left == '\0' && *right == '\0';
}


static const TitanNeutrinoGame *TitanLibraryScan_FindPreviousIso(const char *path)
{
    unsigned int index;

    if (!g_library_previous.valid || path == 0)
        return 0;
    for (index = 0u; index < g_library_previous.game_count; index++)
    {
        const TitanNeutrinoGame *game = &g_library_previous.games[index];
        if (game->format == TITAN_NEUTRINO_GAME_ISO &&
            game->startup[0] != '\0' &&
            TitanLibraryScan_EqualsInsensitive(game->path, path))
            return game;
    }
    return 0;
}

static void TitanLibraryScan_SnapshotCurrentGames(void)
{
    unsigned int count = g_library_scan.game_count;
    if (count > TITAN_NEUTRINO_GAME_CAPACITY)
        count = TITAN_NEUTRINO_GAME_CAPACITY;
    memset(&g_library_previous, 0, sizeof(g_library_previous));
    if (count != 0u)
    {
        memcpy(g_library_previous.games, g_library_scan.games, count * sizeof(g_library_previous.games[0]));
        g_library_previous.game_count = count;
        g_library_previous.valid = 1u;
    }
}

static int TitanLibraryScan_HasExtension(
    const char *name,
    const char *extension
)
{
    unsigned int name_length;
    unsigned int extension_length;
    unsigned int index;

    if (name == 0 || extension == 0)
    {
        return 0;
    }

    name_length = (unsigned int)strlen(name);
    extension_length = (unsigned int)strlen(extension);
    if (name_length < extension_length)
    {
        return 0;
    }

    for (index = 0u; index < extension_length; index++)
    {
        if (tolower((unsigned char)name[name_length - extension_length + index]) !=
            tolower((unsigned char)extension[index]))
        {
            return 0;
        }
    }

    return 1;
}

static const char *TitanLibraryScan_FirstDeviceSuffix(const char *path)
{
    if (path == 0)
    {
        return 0;
    }
    if (strncmp(path, "mass:/", 6u) == 0)
    {
        return path + 5u;
    }
    if (strncmp(path, "mass0:/", 7u) == 0)
    {
        return path + 6u;
    }
    return 0;
}

static int TitanLibraryScan_GameExists(const TitanNeutrinoGame *candidate)
{
    unsigned int index;

    if (candidate == 0)
    {
        return 0;
    }

    for (index = 0u; index < g_library_scan.game_count; index++)
    {
        const TitanNeutrinoGame *current;

        current = &g_library_scan.games[index];
        if (current->format != candidate->format)
        {
            continue;
        }
        if (candidate->format == TITAN_NEUTRINO_GAME_USBEXTREME)
        {
            if (TitanLibraryScan_EqualsInsensitive(
                current->startup,
                candidate->startup
            ))
            {
                return 1;
            }
        }
        else if (TitanLibraryScan_EqualsInsensitive(
            current->path,
            candidate->path
        ))
        {
            return 1;
        }
        else
        {
            const char *current_suffix;
            const char *candidate_suffix;

            current_suffix = TitanLibraryScan_FirstDeviceSuffix(current->path);
            candidate_suffix = TitanLibraryScan_FirstDeviceSuffix(candidate->path);
            if (
                current_suffix != 0 && candidate_suffix != 0 &&
                TitanLibraryScan_EqualsInsensitive(
                    current_suffix,
                    candidate_suffix
                )
            )
            {
                return 1;
            }
        }
    }
    return 0;
}

static int TitanLibraryScan_PathExistsInApplications(const char *path)
{
    unsigned int index;

    for (index = 0u; index < g_library_scan.application_count; index++)
    {
        if (TitanLibraryScan_EqualsInsensitive(g_library_scan.applications[index].path, path))
        {
            return 1;
        }
    }
    return 0;
}

static int TitanLibraryScan_JoinPath(
    char *destination,
    unsigned int capacity,
    const char *directory,
    const char *name
)
{
    unsigned int directory_length;
    unsigned int name_length;
    unsigned int separator;

    if (destination == 0 || capacity == 0u || directory == 0 || name == 0)
    {
        return 0;
    }

    directory_length = (unsigned int)strlen(directory);
    name_length = (unsigned int)strlen(name);
    separator = directory_length > 0u && directory[directory_length - 1u] != '/' ? 1u : 0u;
    if (directory_length + separator + name_length + 1u > capacity)
    {
        return 0;
    }

    memcpy(destination, directory, directory_length);
    if (separator != 0u)
    {
        destination[directory_length++] = '/';
    }
    memcpy(&destination[directory_length], name, name_length + 1u);
    return 1;
}

static void TitanLibraryScan_BuildTitle(
    char *destination,
    unsigned int capacity,
    const char *filename
)
{
    unsigned int source_length;
    unsigned int index;
    unsigned int output;
    int capitalize_next;

    if (destination == 0 || capacity == 0u)
    {
        return;
    }

    destination[0] = '\0';
    if (filename == 0)
    {
        return;
    }

    source_length = (unsigned int)strlen(filename);
    if (source_length >= 4u && TitanLibraryScan_HasExtension(filename, ".elf"))
    {
        source_length -= 4u;
    }

    output = 0u;
    capitalize_next = 1;
    for (index = 0u; index < source_length && output + 1u < capacity; index++)
    {
        unsigned char character;

        character = (unsigned char)filename[index];
        if (character == '_' || character == '-')
        {
            if (output > 0u && destination[output - 1u] != ' ')
            {
                destination[output++] = ' ';
            }
            capitalize_next = 1;
            continue;
        }

        if (capitalize_next && isalpha(character))
        {
            character = (unsigned char)toupper(character);
        }
        destination[output++] = (char)character;
        capitalize_next = character == ' ';
    }

    while (output > 0u && destination[output - 1u] == ' ')
    {
        output--;
    }
    destination[output] = '\0';
}

static int TitanLibraryScan_IsInternalElf(const char *name)
{
    if (name == 0)
    {
        return 1;
    }

    return TitanLibraryScan_EqualsInsensitive(name, "kernel.elf") ||
        TitanLibraryScan_EqualsInsensitive(name, "titan.elf") ||
        TitanLibraryScan_EqualsInsensitive(name, "neutrino.elf");
}

static int TitanLibraryScan_ShouldSkipDirectory(const char *name)
{
    static const char *skipped[] =
    {
        ".", "..", "DVD", "CD", "ART", "VMC", "THM", "CFG",
        "modules", "config"
    };
    unsigned int index;

    if (name == 0 || name[0] == '\0' || name[0] == '.')
    {
        return 1;
    }

    for (index = 0u; index < sizeof(skipped) / sizeof(skipped[0]); index++)
    {
        if (TitanLibraryScan_EqualsInsensitive(name, skipped[index]))
        {
            return 1;
        }
    }
    return 0;
}

static void TitanLibraryScan_Report(
    const char *message,
    unsigned int progress,
    unsigned int processed,
    unsigned int total
)
{
    const char *localized_message;

    localized_message = TitanLocalization_Compat(message);
    if (progress > 100u)
    {
        progress = 100u;
    }
    if (progress < g_library_scan.status.progress_percent)
    {
        progress = g_library_scan.status.progress_percent;
    }

    g_library_scan.status.progress_percent = progress;
    g_library_scan.status.processed = processed;
    g_library_scan.status.total = total;
    g_library_scan.status.games_found = g_library_scan.game_count;
    g_library_scan.status.applications_found = g_library_scan.application_count;
    TitanLibraryScan_CopyString(
        g_library_scan.status.message,
        sizeof(g_library_scan.status.message),
        localized_message
    );
    if (!g_library_scan.deferred_ui)
    {
        TitanNotifications_UpdateBackgroundActivity(
            localized_message,
            progress,
            processed,
            total,
            g_library_scan.game_count,
            g_library_scan.application_count
        );
    }
}

static int TitanLibraryScan_DeviceAvailable(const char *device)
{
    DIR *directory;

    directory = opendir(device);
    if (!g_library_scan.deferred_ui)
    {
        TitanShellBusy_Pulse();
    }
    if (directory == 0)
    {
        return 0;
    }

    closedir(directory);
    if (!g_library_scan.deferred_ui)
    {
        TitanShellBusy_Pulse();
    }
    return 1;
}

static const char *TitanLibraryScan_GetGameRoot(unsigned int index)
{
    return g_library_scan.device_pass == 0u
        ? g_primary_game_roots[index]
        : g_fallback_game_roots[index];
}

static const char *TitanLibraryScan_GetAppRoot(unsigned int index)
{
    return g_library_scan.device_pass == 0u
        ? g_primary_app_roots[index]
        : g_fallback_app_roots[index];
}

static void TitanLibraryScan_AddIso(const char *directory, const char *name)
{
    TitanNeutrinoGame *game;
    unsigned int title_length;

    if (g_library_scan.game_count >= TITAN_NEUTRINO_GAME_CAPACITY)
    {
        g_library_scan.capacity_exceeded = 1u;
        return;
    }

    game = &g_library_scan.games[g_library_scan.game_count];
    memset(game, 0, sizeof(*game));
    if (!TitanLibraryScan_JoinPath(game->path, sizeof(game->path), directory, name))
    {
        memset(game, 0, sizeof(*game));
        return;
    }

    title_length = (unsigned int)strlen(name);
    if (title_length >= 4u)
    {
        title_length -= 4u;
    }
    if (title_length >= sizeof(game->title))
    {
        title_length = sizeof(game->title) - 1u;
    }
    memcpy(game->title, name, title_length);
    game->title[title_length] = '\0';
    game->format = TITAN_NEUTRINO_GAME_ISO;
    game->parts = 1u;
    game->media = strstr(directory, "/CD") != 0 ? 0x12u : 0x14u;
    {
        const TitanNeutrinoGame *previous = TitanLibraryScan_FindPreviousIso(game->path);
        if (previous != 0)
        {
            TitanLibraryScan_CopyString(game->startup, sizeof(game->startup), previous->startup);
            TitanLibraryScan_CopyString(game->version, sizeof(game->version), previous->version);
            g_library_scan.status.metadata_reused++;
        }
    }
    if (TitanLibraryScan_GameExists(game))
    {
        memset(game, 0, sizeof(*game));
    }
    else
    {
        g_library_scan.game_count++;
        TitanLibraryScan_Diag(
            TITAN_DIAG_INFO, 0x0712u, 0,
            g_library_scan.device_pass,
            g_library_scan.game_root_index,
            g_library_scan.game_count,
            game->media
        );
    }
}

static void TitanLibraryScan_CopyFixedText(
    char *destination,
    unsigned int capacity,
    const unsigned char *source,
    unsigned int length
)
{
    if (length >= capacity)
    {
        length = capacity - 1u;
    }
    memcpy(destination, source, length);
    while (length > 0u &&
           (destination[length - 1u] == '\0' || destination[length - 1u] == ' '))
    {
        length--;
    }
    destination[length] = '\0';
}

static void TitanLibraryScan_StepUlCfg(void)
{
    unsigned char entry[TITAN_LIBRARY_UL_ENTRY_SIZE];
    char cfg_path[TITAN_NEUTRINO_PATH_CAPACITY];
    const char *device;
    size_t read_size;

    device = g_library_scan.device_pass == 0u
        ? TITAN_LIBRARY_PRIMARY_DEVICE
        : TITAN_LIBRARY_FALLBACK_DEVICE;

    if (g_library_scan.ul_file == 0)
    {
        if (!TitanLibraryScan_JoinPath(cfg_path, sizeof(cfg_path), device, "ul.cfg"))
        {
            g_library_scan.scan_failed = 1u;
            return;
        }
        errno = 0;
        g_library_scan.ul_file = fopen(cfg_path, "rb");
        TitanLibraryScan_Diag(
            g_library_scan.ul_file != 0 ? TITAN_DIAG_INFO : TITAN_DIAG_DEBUG,
            0x0711u,
            g_library_scan.ul_file != 0 ? 0 : -errno,
            g_library_scan.device_pass,
            g_library_scan.game_count,
            0u,
            0u
        );
        if (g_library_scan.ul_file == 0)
        {
            g_library_scan.game_root_index = 3u;
            return;
        }
    }

    errno = 0;
    read_size = fread(entry, 1u, sizeof(entry), g_library_scan.ul_file);
    if (read_size == sizeof(entry))
    {
        TitanNeutrinoGame *game;

        g_library_scan.status.processed++;
        if (g_library_scan.game_count >= TITAN_NEUTRINO_GAME_CAPACITY)
        {
            g_library_scan.capacity_exceeded = 1u;
            return;
        }
        if (
            memcmp(&entry[32], "ul.", 3u) != 0 ||
            entry[47] == 0u ||
            (entry[48] != 0x12u && entry[48] != 0x14u)
        )
        {
            return;
        }

        game = &g_library_scan.games[g_library_scan.game_count];
        memset(game, 0, sizeof(*game));
        TitanLibraryScan_CopyFixedText(game->title, sizeof(game->title), entry, 32u);
        TitanLibraryScan_CopyFixedText(game->startup, sizeof(game->startup), &entry[35], 12u);
        if (!TitanLibraryScan_JoinPath(game->path, sizeof(game->path), device, "ul.cfg"))
        {
            memset(game, 0, sizeof(*game));
            g_library_scan.scan_failed = 1u;
            return;
        }
        game->format = TITAN_NEUTRINO_GAME_USBEXTREME;
        game->parts = entry[47];
        game->media = entry[48];
        if (
            game->title[0] != '\0' &&
            game->startup[0] != '\0' &&
            !TitanLibraryScan_GameExists(game)
        )
        {
            g_library_scan.game_count++;
        }
        else
        {
            memset(game, 0, sizeof(*game));
        }
        return;
    }

    /* ul.cfg consists of fixed-size 64-byte records. A short trailing read
     * at EOF is a truncated final record, not a device read failure; retain
     * the complete records already parsed. A real stdio error remains fatal
     * so a USB I/O failure cannot publish a silently partial catalog. */
    if (ferror(g_library_scan.ul_file))
    {
        g_library_scan.scan_failed = 1u;
        TitanLibraryScan_Diag(
            TITAN_DIAG_WARN, 0x0715u, -errno,
            g_library_scan.device_pass,
            (uint32_t)read_size,
            g_library_scan.game_count,
            0u
        );
    }

    fclose(g_library_scan.ul_file);
    g_library_scan.ul_file = 0;
    g_library_scan.game_root_index = 3u;
}

static void TitanLibraryScan_EnterMetadataOrFallback(void)
{
    if (g_library_scan.device_pass == 0u)
    {
        g_library_scan.device_pass = 1u;
        g_library_scan.game_root_index = 0u;
        TitanLibraryScan_Report("Searching for games on USB storage", 18u, 0u, 0u);
        return;
    }

    g_library_scan.status.phase = TITAN_LIBRARY_SCAN_GAME_METADATA;
    g_library_scan.metadata_index = 0u;
    TitanLibraryScan_Report(
        "Identifying discovered games",
        35u,
        0u,
        g_library_scan.game_count
    );
}

static void TitanLibraryScan_StepGames(void)
{
    const unsigned int root_count = 2u;

    if (g_library_scan.game_root_index < root_count)
    {
        const char *root;
        struct dirent *entry;

        root = TitanLibraryScan_GetGameRoot(g_library_scan.game_root_index);
        if (g_library_scan.game_directory == 0)
        {
            int open_errno;
            errno = 0;
            g_library_scan.game_directory = opendir(root);
            open_errno = errno;
            TitanLibraryScan_Diag(
                g_library_scan.game_directory != 0 ? TITAN_DIAG_INFO : TITAN_DIAG_WARN,
                0x0710u,
                g_library_scan.game_directory != 0 ? 0 : -open_errno,
                g_library_scan.device_pass,
                g_library_scan.game_root_index,
                g_library_scan.game_count,
                0u
            );
            if (g_library_scan.game_directory == 0)
            {
                g_library_scan.game_root_index++;
                return;
            }
        }

        errno = 0;
        entry = readdir(g_library_scan.game_directory);
        if (entry == 0)
        {
            /* End-of-directory is authoritative. Re-probing a bare device
             * alias here made a successful scan fail on hardware stacks that
             * accept mass:/DVD but do not accept opendir("mass:"). */
            if (errno != 0)
            {
                g_library_scan.scan_failed = 1u;
            }
            closedir(g_library_scan.game_directory);
            g_library_scan.game_directory = 0;
            g_library_scan.game_root_index++;
            return;
        }

        g_library_scan.status.processed++;
        if (TitanLibraryScan_HasExtension(entry->d_name, ".iso"))
        {
            TitanLibraryScan_AddIso(root, entry->d_name);
        }
        TitanLibraryScan_Report(
            "Searching for games on USB storage",
            10u + g_library_scan.game_root_index * 8u +
                (g_library_scan.status.processed > 16u ? 7u : g_library_scan.status.processed / 2u),
            g_library_scan.status.processed,
            0u
        );
        return;
    }

    if (g_library_scan.game_root_index == 2u)
    {
        TitanLibraryScan_StepUlCfg();
        TitanLibraryScan_Report(
            "Checking USBExtreme catalog",
            30u,
            g_library_scan.status.processed,
            0u
        );
        return;
    }

    TitanLibraryScan_EnterMetadataOrFallback();
}

static void TitanLibraryScan_StepMetadata(void)
{
    while (g_library_scan.metadata_index < g_library_scan.game_count)
    {
        TitanNeutrinoGame *game;

        game = &g_library_scan.games[g_library_scan.metadata_index];
        g_library_scan.metadata_index++;
        if (game->format == TITAN_NEUTRINO_GAME_ISO && game->startup[0] == '\0')
        {
            TitanNeutrino_ReadIsoStartupMetadata(
                game->path,
                game->startup,
                sizeof(game->startup)
            );
            break;
        }
    }

    if (g_library_scan.game_count > 0u)
    {
        TitanLibraryScan_Report(
            "Identifying discovered games",
            35u + (g_library_scan.metadata_index * 30u / g_library_scan.game_count),
            g_library_scan.metadata_index,
            g_library_scan.game_count
        );
    }

    if (g_library_scan.metadata_index >= g_library_scan.game_count)
    {
        g_library_scan.status.phase = TITAN_LIBRARY_SCAN_APPLICATIONS;
        g_library_scan.device_pass = 0u;
        g_library_scan.app_root_index = 0u;
        g_library_scan.app_root_active = 0u;
        g_library_scan.status.processed = 0u;
        TitanLibraryScan_Report("Searching for ELF applications", 68u, 0u, 0u);
    }
}

static int TitanLibraryScan_PushAppDirectory(
    const char *path,
    unsigned int depth
)
{
    TitanLibraryAppFrame *frame;
    DIR *directory;

    if (g_library_scan.app_stack_count >= TITAN_LIBRARY_APP_STACK_CAPACITY)
    {
        return 0;
    }

    errno = 0;
    directory = opendir(path);
    TitanLibraryScan_Diag(
        directory != 0 ? TITAN_DIAG_INFO : TITAN_DIAG_WARN,
        0x0713u,
        directory != 0 ? 0 : -errno,
        g_library_scan.device_pass,
        g_library_scan.app_root_index,
        depth,
        g_library_scan.status.processed
    );
    if (directory == 0)
    {
        return 0;
    }

    if (depth == 0u && g_library_scan.device_pass == 0u)
    {
        g_library_scan.app_primary_root_opened = 1u;
    }

    frame = &g_library_scan.app_stack[g_library_scan.app_stack_count++];
    memset(frame, 0, sizeof(*frame));
    frame->directory = directory;
    frame->depth = depth;
    TitanLibraryScan_CopyString(frame->path, sizeof(frame->path), path);
    return 1;
}

static void TitanLibraryScan_CloseAppStack(void)
{
    while (g_library_scan.app_stack_count > 0u)
    {
        TitanLibraryAppFrame *frame;

        frame = &g_library_scan.app_stack[g_library_scan.app_stack_count - 1u];
        if (frame->directory != 0)
        {
            closedir(frame->directory);
        }
        memset(frame, 0, sizeof(*frame));
        g_library_scan.app_stack_count--;
    }
}

static void TitanLibraryScan_AddApplication(
    const char *path,
    const char *name,
    const char *title_override
)
{
    TitanApplication *application;

    if (g_library_scan.application_count >= TITAN_APPLICATION_CAPACITY)
    {
        g_library_scan.capacity_exceeded = 1u;
        return;
    }

    if (
        TitanLibraryScan_IsInternalElf(name) ||
        TitanLibraryScan_PathExistsInApplications(path) ||
        TitanElf_ValidateFile(path) != TITAN_ELF_VALID
    )
    {
        return;
    }

    application = &g_library_scan.applications[g_library_scan.application_count];
    memset(application, 0, sizeof(*application));
    TitanLibraryScan_CopyString(application->path, sizeof(application->path), path);
    if (title_override != 0 && title_override[0] != '\0')
    {
        TitanLibraryScan_CopyString(
            application->title,
            sizeof(application->title),
            title_override
        );
    }
    else
    {
        TitanLibraryScan_BuildTitle(
            application->title,
            sizeof(application->title),
            name
        );
    }
    if (application->title[0] == '\0')
    {
        TitanLibraryScan_CopyString(application->title, sizeof(application->title), "Application");
    }
    g_library_scan.application_count++;
}

static int TitanLibraryScan_AddInstalledManifest(const char *directory_path)
{
    TitanApplication application;
    int result;

    result = TitanApplications_ReadInstalledManifest(directory_path, &application);
    if (result == 0)
        return 0;
    if (result < 0)
        return 1;

    if (g_library_scan.application_count >= TITAN_APPLICATION_CAPACITY)
    {
        g_library_scan.capacity_exceeded = 1u;
        return 1;
    }
    if (!TitanLibraryScan_PathExistsInApplications(application.path))
        g_library_scan.applications[g_library_scan.application_count++] = application;
    return 1;
}

static void TitanLibraryScan_EnterFinalizeOrAppFallback(void)
{
    if (g_library_scan.application_count == 0u &&
        g_library_scan.device_pass == 0u &&
        !g_library_scan.app_primary_root_opened)
    {
        TitanLibraryScan_CloseAppStack();
        g_library_scan.device_pass = 1u;
        g_library_scan.app_root_index = 0u;
        g_library_scan.app_root_active = 0u;
        return;
    }

    TitanLibraryScan_CloseAppStack();
    g_library_scan.status.phase = TITAN_LIBRARY_SCAN_FINALIZING;
    TitanLibraryScan_Report("Finalizing library", 97u, 0u, 0u);
}

static void TitanLibraryScan_StepApplications(void)
{
    const unsigned int root_count = 1u;

    if (g_library_scan.app_root_index >= root_count)
    {
        TitanLibraryScan_EnterFinalizeOrAppFallback();
        return;
    }

    if (!g_library_scan.app_root_active)
    {
        const char *root;

        root = TitanLibraryScan_GetAppRoot(g_library_scan.app_root_index);
        g_library_scan.app_root_active = 1u;
        if (!TitanLibraryScan_PushAppDirectory(root, 0u))
        {
            g_library_scan.app_root_active = 0u;
            g_library_scan.app_root_index++;
            TitanLibraryScan_Report(
                "Searching for ELF applications",
                93u,
                g_library_scan.status.processed,
                0u
            );
        }
        return;
    }

    if (g_library_scan.app_stack_count == 0u)
    {
        g_library_scan.app_root_active = 0u;
        g_library_scan.app_root_index++;
        TitanLibraryScan_Report(
            "Searching for ELF applications",
            93u,
            g_library_scan.status.processed,
            0u
        );
        return;
    }

    {
        TitanLibraryAppFrame *frame;
        struct dirent *entry;
        char path[TITAN_APPLICATION_PATH_CAPACITY];
        struct stat information;

        frame = &g_library_scan.app_stack[g_library_scan.app_stack_count - 1u];
        if (!frame->manifest_checked)
        {
            frame->manifest_checked = 1u;
            if (TitanLibraryScan_AddInstalledManifest(frame->path))
            {
                closedir(frame->directory);
                memset(frame, 0, sizeof(*frame));
                g_library_scan.app_stack_count--;
                return;
            }
        }
        errno = 0;
        entry = readdir(frame->directory);
        if (entry == 0)
        {
            if (errno != 0)
            {
                g_library_scan.scan_failed = 1u;
            }
            closedir(frame->directory);
            memset(frame, 0, sizeof(*frame));
            g_library_scan.app_stack_count--;
            return;
        }

        if (entry->d_name[0] == '\0' ||
            strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
        {
            return;
        }

        if (entry->d_name[0] == '.')
        {
            g_library_scan.status.processed++;
            TitanLibraryScan_Diag(
                TITAN_DIAG_DEBUG, 0x0714u, 0,
                g_library_scan.device_pass,
                g_library_scan.app_root_index,
                frame->depth,
                g_library_scan.status.processed
            );
            TitanLibraryScan_Report(
                "Searching for ELF applications",
                68u + (g_library_scan.status.processed > 20u
                    ? 20u : g_library_scan.status.processed),
                g_library_scan.status.processed,
                0u
            );
            return;
        }

        if (!TitanLibraryScan_JoinPath(path, sizeof(path), frame->path, entry->d_name))
        {
            return;
        }

        g_library_scan.status.processed++;
        if (stat(path, &information) < 0)
        {
            return;
        }

        if (S_ISDIR(information.st_mode))
        {
            if (frame->depth < TITAN_LIBRARY_APP_MAX_DEPTH &&
                !TitanLibraryScan_ShouldSkipDirectory(entry->d_name))
            {
                TitanLibraryScan_PushAppDirectory(path, frame->depth + 1u);
            }
        }
        else if (TitanLibraryScan_HasExtension(entry->d_name, ".elf"))
        {
            TitanLibraryScan_AddApplication(path, entry->d_name, 0);
        }
    }

    TitanLibraryScan_Report(
        "Searching for ELF applications",
        68u + g_library_scan.app_root_index * 5u +
            (g_library_scan.status.processed > 20u ? 4u : g_library_scan.status.processed / 5u),
        g_library_scan.status.processed,
        0u
    );
}

static void TitanLibraryScan_Finalize(void)
{
    char complete_message[112];
    int cache_saved;

    if (g_library_scan.scan_failed || g_library_scan.capacity_exceeded)
    {
        const char *message;

        message = TitanLocalization_Compat(
            g_library_scan.capacity_exceeded
                ? "Library capacity exceeded; previous catalog preserved"
                : "USB read failure; previous catalog preserved"
        );
        g_library_scan.status.phase = TITAN_LIBRARY_SCAN_ERROR;
        g_library_scan.status.active = 0u;
        if (!g_library_scan.deferred_ui)
        {
            TitanShellBusy_End();
        }
        TitanLibraryScan_CopyString(
            g_library_scan.status.message,
            sizeof(g_library_scan.status.message),
            message
        );
        if (!g_library_scan.deferred_ui)
        {
            TitanNotifications_FailBackgroundActivity(message);
        }
        return;
    }

    cache_saved = TitanLibraryCache_Save(
        g_library_scan.games,
        g_library_scan.game_count,
        g_library_scan.applications,
        g_library_scan.application_count,
        g_library_scan.neutrino_elf
    );
    g_library_scan.cache_save_failed = cache_saved ? 0u : 1u;

    if (g_library_scan.status.metadata_reused != 0u)
    {
        uint32_t avoided = g_library_scan.status.metadata_reused <= (UINT32_MAX / 2048u)
            ? g_library_scan.status.metadata_reused * 2048u : UINT32_MAX;
#if defined(__GNUC__)
        if (TitanVlombisCore_RecordLibraryMetadataReuse != 0)
#endif
            TitanVlombisCore_RecordLibraryMetadataReuse(
                g_library_scan.status.metadata_reused, avoided);
    }
    TitanLibraryScan_SnapshotCurrentGames();

    if (g_library_scan.deferred_ui)
    {
#if defined(__GNUC__)
        __sync_synchronize();
#endif
        g_library_scan.catalog_commit_pending = 1u;
    }
    else
    {
        TitanNeutrino_ReplaceCatalog(
            g_library_scan.games,
            g_library_scan.game_count,
            g_library_scan.neutrino_elf
        );
        TitanApplications_ReplaceCatalog(
            g_library_scan.applications,
            g_library_scan.application_count
        );
    }

    if (TitanLocalization_GetLanguage() == TITAN_LANGUAGE_PORTUGUESE)
    {
        snprintf(complete_message, sizeof(complete_message),
            cache_saved ? "%u jogos e %u aplicativos disponíveis" : "%u jogos e %u aplicativos; cache não salvo",
            g_library_scan.game_count, g_library_scan.application_count);
    }
    else if (TitanLocalization_GetLanguage() == TITAN_LANGUAGE_SPANISH)
    {
        snprintf(complete_message, sizeof(complete_message),
            cache_saved ? "%u juegos y %u aplicaciones disponibles" : "%u juegos y %u aplicaciones; caché no guardada",
            g_library_scan.game_count, g_library_scan.application_count);
    }
    else
    {
        snprintf(complete_message, sizeof(complete_message),
            cache_saved ? "%u games and %u applications available" : "%u games and %u applications; cache not saved",
            g_library_scan.game_count, g_library_scan.application_count);
    }

    g_library_scan.status.phase = TITAN_LIBRARY_SCAN_COMPLETE;
    g_library_scan.status.active = 0u;
    if (!g_library_scan.deferred_ui)
    {
        TitanShellBusy_End();
    }
    g_library_scan.status.progress_percent = 100u;
    TitanLibraryScan_CopyString(
        g_library_scan.status.message,
        sizeof(g_library_scan.status.message),
        complete_message
    );
    if (!g_library_scan.deferred_ui)
    {
        TitanNotifications_CompleteBackgroundActivity(
            complete_message,
            g_library_scan.game_count,
            g_library_scan.application_count
        );
    }
}

void TitanLibraryScan_Initialize(void)
{
    unsigned int game_count;
    unsigned int application_count;

    memset(&g_library_scan, 0, sizeof(g_library_scan));
    memset(&g_library_previous, 0, sizeof(g_library_previous));
    g_library_scan.status.phase = TITAN_LIBRARY_SCAN_IDLE;

    game_count = 0u;
    application_count = 0u;
    if (TitanLibraryCache_Load(
        g_library_scan.games,
        &game_count,
        g_library_scan.applications,
        &application_count,
        g_library_scan.neutrino_elf,
        sizeof(g_library_scan.neutrino_elf)
    ))
    {
        g_library_scan.game_count = game_count;
        g_library_scan.application_count = application_count;
        TitanNeutrino_ReplaceCatalog(
            g_library_scan.games,
            g_library_scan.game_count,
            g_library_scan.neutrino_elf
        );
        TitanApplications_ReplaceCatalog(
            g_library_scan.applications,
            g_library_scan.application_count
        );
        g_library_scan.status.cache_loaded = 1u;
        TitanLibraryScan_SnapshotCurrentGames();
#if defined(__GNUC__)
        if (TitanVlombisCore_RecordWork != 0)
#endif
            TitanVlombisCore_RecordWork(
                TITAN_VLOMBIS_CAP_LIBRARY_INCREMENTAL_SCAN |
                    TITAN_VLOMBIS_CAP_STORAGE_QOS | TITAN_VLOMBIS_CAP_IO_COORDINATION,
                TITAN_VLOMBIS_EVENT_LIBRARY_CACHE_HIT,
                game_count + application_count, 0u);
    }
}

void TitanLibraryScan_SetDeferredUi(int enabled)
{
    g_library_scan.deferred_ui = enabled != 0;
}

int TitanLibraryScan_HasPendingCatalog(void)
{
    return g_library_scan.catalog_commit_pending != 0u;
}

int TitanLibraryScan_CommitPendingCatalog(void)
{
    if (!g_library_scan.catalog_commit_pending)
    {
        return 0;
    }

#if defined(__GNUC__)
    __sync_synchronize();
#endif
    TitanNeutrino_ReplaceCatalog(
        g_library_scan.games,
        g_library_scan.game_count,
        g_library_scan.neutrino_elf
    );
    TitanApplications_ReplaceCatalog(
        g_library_scan.applications,
        g_library_scan.application_count
    );
#if defined(__GNUC__)
    __sync_synchronize();
#endif
    g_library_scan.catalog_commit_pending = 0u;
    return 1;
}

void TitanLibraryScan_Begin(int force_rebuild)
{
    unsigned int cache_loaded;
    unsigned int deferred_ui;

    if (g_library_scan.status.active)
    {
        return;
    }

    cache_loaded = g_library_scan.status.cache_loaded;
    deferred_ui = g_library_scan.deferred_ui;
    if (!force_rebuild && cache_loaded)
        TitanLibraryScan_SnapshotCurrentGames();
    else if (force_rebuild)
        memset(&g_library_previous, 0, sizeof(g_library_previous));
    TitanLibraryScan_CloseAppStack();
    if (g_library_scan.game_directory != 0)
    {
        closedir(g_library_scan.game_directory);
    }
    if (g_library_scan.ul_file != 0)
    {
        fclose(g_library_scan.ul_file);
    }

    memset(&g_library_scan, 0, sizeof(g_library_scan));
    g_library_scan.deferred_ui = deferred_ui;
    g_library_scan.status.cache_loaded = force_rebuild ? 0u : cache_loaded;
    g_library_scan.status.phase = TITAN_LIBRARY_SCAN_GAMES;
    g_library_scan.status.active = 1u;

    if (!g_library_scan.deferred_ui)
    {
        TitanShellBusy_Begin();
        TitanShellBusy_Pulse();
    }

    if (
        !TitanLibraryScan_DeviceAvailable(TITAN_LIBRARY_PRIMARY_ROOT) &&
        !TitanLibraryScan_DeviceAvailable(TITAN_LIBRARY_FALLBACK_ROOT)
    )
    {
        g_library_scan.status.phase = TITAN_LIBRARY_SCAN_ERROR;
        g_library_scan.status.active = 0u;
        TitanLibraryScan_CopyString(
            g_library_scan.status.message,
            sizeof(g_library_scan.status.message),
            TitanLocalization_Compat("USB storage not found; previous catalog preserved")
        );
        if (!g_library_scan.deferred_ui)
        {
            TitanNotifications_BeginBackgroundActivity(
                TitanLocalization_Compat("Updating library"),
                TitanLocalization_Compat("Checking USB storage")
            );
            TitanNotifications_FailBackgroundActivity(
                TitanLocalization_Compat("USB storage not found; previous catalog preserved")
            );
            TitanShellBusy_End();
        }
        return;
    }

    if (!g_library_scan.deferred_ui)
    {
        TitanShellBusy_Pulse();
    }
    TitanNeutrino_FindInstalledElf(
        g_library_scan.neutrino_elf,
        sizeof(g_library_scan.neutrino_elf)
    );
    if (!g_library_scan.deferred_ui)
    {
        TitanNotifications_BeginBackgroundActivity(
            TitanLocalization_Compat("Updating library"),
            TitanLocalization_Compat("Searching for games on USB storage")
        );
    }
    TitanLibraryScan_Report(
        "Searching for games on USB storage",
        8u,
        0u,
        0u
    );
}

void TitanLibraryScan_Update(unsigned int entry_budget)
{
    unsigned int step;

    if (!g_library_scan.status.active)
    {
        return;
    }

    if (entry_budget == 0u)
    {
        entry_budget = 1u;
    }
    if (entry_budget > 8u)
    {
        entry_budget = 8u;
    }

    for (step = 0u; step < entry_budget && g_library_scan.status.active; step++)
    {
        if (g_library_scan.status.phase == TITAN_LIBRARY_SCAN_GAMES)
        {
            TitanLibraryScan_StepGames();
        }
        else if (g_library_scan.status.phase == TITAN_LIBRARY_SCAN_GAME_METADATA)
        {
            TitanLibraryScan_StepMetadata();
        }
        else if (g_library_scan.status.phase == TITAN_LIBRARY_SCAN_APPLICATIONS)
        {
            TitanLibraryScan_StepApplications();
        }
        else if (g_library_scan.status.phase == TITAN_LIBRARY_SCAN_FINALIZING)
        {
            TitanLibraryScan_Finalize();
        }
        else
        {
            g_library_scan.status.active = 0u;
            if (!g_library_scan.deferred_ui)
            {
                TitanShellBusy_End();
            }
        }
    }
    g_library_scan.status.scan_steps += step;
    if (step != 0u)
    {
#if defined(__GNUC__)
        if (TitanVlombisCore_RecordWork != 0)
#endif
            TitanVlombisCore_RecordWork(
                TITAN_VLOMBIS_CAP_LIBRARY_INCREMENTAL_SCAN |
                    TITAN_VLOMBIS_CAP_BACKGROUND_QOS | TITAN_VLOMBIS_CAP_STORAGE_QOS,
                TITAN_VLOMBIS_EVENT_LIBRARY_SCAN_STEP, step, 0u);
    }
}

int TitanLibraryScan_IsActive(void)
{
    return g_library_scan.status.active != 0u;
}

const TitanLibraryScanStatus *TitanLibraryScan_GetStatus(void)
{
    return &g_library_scan.status;
}
