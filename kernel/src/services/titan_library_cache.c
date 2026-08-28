#include "titan_library_cache.h"

#include "titan_storage_io.h"
#include "titan_storage_paths.h"

#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

#define TITAN_LIBRARY_CACHE_DIRECTORY TITAN_STORAGE_DEVICE_ROOT "/cache"
#define TITAN_LIBRARY_CACHE_PATH TITAN_LIBRARY_CACHE_DIRECTORY "/library_catalog.bin"
#define TITAN_LIBRARY_CACHE_TEMP_PATH TITAN_LIBRARY_CACHE_DIRECTORY "/library_catalog.tmp"
#define TITAN_LIBRARY_CACHE_BACKUP_PATH TITAN_LIBRARY_CACHE_DIRECTORY "/library_catalog.bak"

#define TITAN_LIBRARY_CACHE_VERSION 4u
#define TITAN_LIBRARY_CACHE_HEADER_SIZE 288u
#define TITAN_LIBRARY_CACHE_GAME_RECORD_SIZE 372u
#define TITAN_LIBRARY_CACHE_APPLICATION_RECORD_SIZE 1312u
#define TITAN_LIBRARY_CACHE_CHECKSUM_OFFSET 20u

static const unsigned char g_cache_magic[4] = {'T', 'C', 'M', '4'};

static void TitanLibraryCache_WriteU32Le(
    unsigned char *destination,
    uint32_t value
)
{
    destination[0] = (unsigned char)(value & 0xffu);
    destination[1] = (unsigned char)((value >> 8) & 0xffu);
    destination[2] = (unsigned char)((value >> 16) & 0xffu);
    destination[3] = (unsigned char)((value >> 24) & 0xffu);
}

static uint32_t TitanLibraryCache_ReadU32Le(
    const unsigned char *source
)
{
    return (uint32_t)source[0] |
        ((uint32_t)source[1] << 8) |
        ((uint32_t)source[2] << 16) |
        ((uint32_t)source[3] << 24);
}

static uint32_t TitanLibraryCache_ChecksumUpdate(
    uint32_t checksum,
    const void *data,
    size_t size
)
{
    const unsigned char *bytes;
    size_t index;

    bytes = (const unsigned char *)data;
    for (index = 0u; index < size; index++)
    {
        checksum ^= bytes[index];
        checksum *= 16777619u;
    }
    return checksum;
}

static uint32_t TitanLibraryCache_HeaderChecksum(
    const unsigned char header[TITAN_LIBRARY_CACHE_HEADER_SIZE]
)
{
    uint32_t checksum;

    checksum = 2166136261u;
    checksum = TitanLibraryCache_ChecksumUpdate(
        checksum,
        header,
        TITAN_LIBRARY_CACHE_CHECKSUM_OFFSET
    );
    checksum = TitanLibraryCache_ChecksumUpdate(
        checksum,
        header + TITAN_LIBRARY_CACHE_CHECKSUM_OFFSET + 4u,
        TITAN_LIBRARY_CACHE_HEADER_SIZE -
            TITAN_LIBRARY_CACHE_CHECKSUM_OFFSET - 4u
    );
    return checksum;
}

static int TitanLibraryCache_StringTerminated(
    const char *text,
    size_t capacity
)
{
    return text != 0 && memchr(text, '\0', capacity) != 0;
}

static int TitanLibraryCache_EqualsInsensitive(
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
        if (
            tolower((unsigned char)*left) !=
            tolower((unsigned char)*right)
        )
        {
            return 0;
        }
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static int TitanLibraryCache_HasExtension(
    const char *path,
    const char *extension
)
{
    size_t path_length;
    size_t extension_length;
    size_t index;

    if (path == 0 || extension == 0)
    {
        return 0;
    }

    path_length = strlen(path);
    extension_length = strlen(extension);
    if (path_length < extension_length)
    {
        return 0;
    }

    for (index = 0u; index < extension_length; index++)
    {
        if (
            tolower((unsigned char)path[path_length - extension_length + index]) !=
            tolower((unsigned char)extension[index])
        )
        {
            return 0;
        }
    }
    return 1;
}

static int TitanLibraryCache_GameValid(
    const TitanNeutrinoGame *game
)
{
    if (
        game == 0 ||
        !TitanLibraryCache_StringTerminated(game->title, sizeof(game->title)) ||
        !TitanLibraryCache_StringTerminated(game->path, sizeof(game->path)) ||
        !TitanLibraryCache_StringTerminated(game->startup, sizeof(game->startup)) ||
        game->title[0] == '\0' ||
        game->path[0] == '\0' ||
        game->format < TITAN_NEUTRINO_GAME_ISO ||
        game->format > TITAN_NEUTRINO_GAME_USBEXTREME ||
        game->parts == 0u ||
        (game->media != 0x12u && game->media != 0x14u)
    )
    {
        return 0;
    }

    if (game->format == TITAN_NEUTRINO_GAME_ISO)
    {
        return TitanLibraryCache_HasExtension(game->path, ".iso");
    }

    return game->startup[0] != '\0' &&
        TitanLibraryCache_HasExtension(game->path, "ul.cfg");
}

static int TitanLibraryCache_ApplicationValid(
    const TitanApplication *application
)
{
    int path_valid;

    if (application == 0 ||
        !TitanLibraryCache_StringTerminated(application->title, sizeof(application->title)) ||
        !TitanLibraryCache_StringTerminated(application->path, sizeof(application->path)) ||
        application->title[0] == '\0' || application->path[0] == '\0')
        return 0;

    path_valid =
        (application->type == TITAN_APPLICATION_TITAN_NATIVE ||
         application->type == TITAN_APPLICATION_TITAN_GAME)
        ? TitanLibraryCache_HasExtension(application->path, ".tmod") &&
          application->installed &&
          application->api_version == TITAN_APPLICATION_API_VERSION
        : TitanLibraryCache_HasExtension(application->path, ".elf");
    return path_valid;
}

static int TitanLibraryCache_GameCompare(
    const TitanNeutrinoGame *left,
    const TitanNeutrinoGame *right
)
{
    int result;

    result = strcasecmp(left->title, right->title);
    if (result == 0)
    {
        result = (int)left->format - (int)right->format;
    }
    if (result == 0)
    {
        result = left->format == TITAN_NEUTRINO_GAME_USBEXTREME
            ? strcasecmp(left->startup, right->startup)
            : strcasecmp(left->path, right->path);
    }
    return result;
}

static int TitanLibraryCache_ApplicationCompare(
    const TitanApplication *left,
    const TitanApplication *right
)
{
    int result;

    result = strcasecmp(left->title, right->title);
    if (result == 0)
    {
        result = strcasecmp(left->path, right->path);
    }
    return result;
}

static void TitanLibraryCache_SortGames(
    TitanNeutrinoGame *games,
    unsigned int count
)
{
    unsigned int left;

    for (left = 1u; left < count; left++)
    {
        TitanNeutrinoGame current;
        unsigned int right;

        current = games[left];
        right = left;
        while (
            right > 0u &&
            TitanLibraryCache_GameCompare(&games[right - 1u], &current) > 0
        )
        {
            games[right] = games[right - 1u];
            right--;
        }
        games[right] = current;
    }
}

static void TitanLibraryCache_SortApplications(
    TitanApplication *applications,
    unsigned int count
)
{
    unsigned int left;

    for (left = 1u; left < count; left++)
    {
        TitanApplication current;
        unsigned int right;

        current = applications[left];
        right = left;
        while (
            right > 0u &&
            TitanLibraryCache_ApplicationCompare(
                &applications[right - 1u],
                &current
            ) > 0
        )
        {
            applications[right] = applications[right - 1u];
            right--;
        }
        applications[right] = current;
    }
}

static int TitanLibraryCache_DeduplicateGames(
    const TitanNeutrinoGame *games,
    unsigned int count
)
{
    unsigned int left;
    unsigned int right;

    for (left = 0u; left < count; left++)
    {
        for (right = left + 1u; right < count; right++)
        {
            if (games[left].format != games[right].format)
            {
                continue;
            }
            if (
                games[left].format == TITAN_NEUTRINO_GAME_USBEXTREME
                    ? TitanLibraryCache_EqualsInsensitive(
                        games[left].startup,
                        games[right].startup
                    )
                    : TitanLibraryCache_EqualsInsensitive(
                        games[left].path,
                        games[right].path
                    )
            )
            {
                return 0;
            }
        }
    }
    return 1;
}

static int TitanLibraryCache_DeduplicateApplications(
    const TitanApplication *applications,
    unsigned int count
)
{
    unsigned int left;
    unsigned int right;

    for (left = 0u; left < count; left++)
    {
        for (right = left + 1u; right < count; right++)
        {
            if (TitanLibraryCache_EqualsInsensitive(
                applications[left].path,
                applications[right].path
            ))
            {
                return 0;
            }
        }
    }
    return 1;
}

static void TitanLibraryCache_SerializeGame(
    unsigned char record[TITAN_LIBRARY_CACHE_GAME_RECORD_SIZE],
    const TitanNeutrinoGame *game
)
{
    memset(record, 0, TITAN_LIBRARY_CACHE_GAME_RECORD_SIZE);
    memcpy(record, game->title, sizeof(game->title));
    memcpy(record + 96u, game->path, sizeof(game->path));
    memcpy(record + 352u, game->startup, sizeof(game->startup));
    record[368u] = (unsigned char)game->format;
    record[369u] = game->parts;
    record[370u] = game->media;
}

static void TitanLibraryCache_DeserializeGame(
    TitanNeutrinoGame *game,
    const unsigned char record[TITAN_LIBRARY_CACHE_GAME_RECORD_SIZE]
)
{
    memset(game, 0, sizeof(*game));
    memcpy(game->title, record, sizeof(game->title));
    memcpy(game->path, record + 96u, sizeof(game->path));
    memcpy(game->startup, record + 352u, sizeof(game->startup));
    game->format = (TitanNeutrinoGameFormat)record[368u];
    game->parts = record[369u];
    game->media = record[370u];
}

static void TitanLibraryCache_SerializeApplication(
    unsigned char record[TITAN_LIBRARY_CACHE_APPLICATION_RECORD_SIZE],
    const TitanApplication *application
)
{
    memset(record, 0, TITAN_LIBRARY_CACHE_APPLICATION_RECORD_SIZE);
    memcpy(record + 0u, application->title, sizeof(application->title));
    memcpy(record + 80u, application->title_id, sizeof(application->title_id));
    memcpy(record + 96u, application->version, sizeof(application->version));
    memcpy(record + 112u, application->author, sizeof(application->author));
    memcpy(record + 160u, application->description, sizeof(application->description));
    memcpy(record + 272u, application->root_path, sizeof(application->root_path));
    memcpy(record + 528u, application->path, sizeof(application->path));
    memcpy(record + 784u, application->icon_path, sizeof(application->icon_path));
    memcpy(record + 1040u, application->background_path, sizeof(application->background_path));
    TitanLibraryCache_WriteU32Le(record + 1296u, application->api_version);
    TitanLibraryCache_WriteU32Le(record + 1300u, (uint32_t)application->type);
    TitanLibraryCache_WriteU32Le(record + 1304u, application->installed ? 1u : 0u);
}

static void TitanLibraryCache_DeserializeApplication(
    TitanApplication *application,
    const unsigned char record[TITAN_LIBRARY_CACHE_APPLICATION_RECORD_SIZE]
)
{
    memset(application, 0, sizeof(*application));
    memcpy(application->title, record + 0u, sizeof(application->title));
    memcpy(application->title_id, record + 80u, sizeof(application->title_id));
    memcpy(application->version, record + 96u, sizeof(application->version));
    memcpy(application->author, record + 112u, sizeof(application->author));
    memcpy(application->description, record + 160u, sizeof(application->description));
    memcpy(application->root_path, record + 272u, sizeof(application->root_path));
    memcpy(application->path, record + 528u, sizeof(application->path));
    memcpy(application->icon_path, record + 784u, sizeof(application->icon_path));
    memcpy(application->background_path, record + 1040u, sizeof(application->background_path));
    application->api_version = TitanLibraryCache_ReadU32Le(record + 1296u);
    application->type = (TitanApplicationType)TitanLibraryCache_ReadU32Le(record + 1300u);
    application->installed = TitanLibraryCache_ReadU32Le(record + 1304u) != 0u;
}

static int TitanLibraryCache_LoadPath(
    const char *path,
    TitanNeutrinoGame *games,
    unsigned int *game_count,
    TitanApplication *applications,
    unsigned int *application_count,
    char *neutrino_elf,
    unsigned int neutrino_capacity
)
{
    unsigned char header[TITAN_LIBRARY_CACHE_HEADER_SIZE];
    unsigned char game_record[TITAN_LIBRARY_CACHE_GAME_RECORD_SIZE];
    unsigned char application_record[TITAN_LIBRARY_CACHE_APPLICATION_RECORD_SIZE];
    TitanStorageIoSize file_size;
    uint64_t expected_size;
    uint32_t stored_checksum;
    uint32_t checksum;
    unsigned int games_in_file;
    unsigned int applications_in_file;
    unsigned int index;
    int descriptor;

    *game_count = 0u;
    *application_count = 0u;
    if (neutrino_capacity > 0u)
    {
        neutrino_elf[0] = '\0';
    }

    if (!TitanStorageIo_PathInfo64(path, 0, &file_size))
    {
        return 0;
    }

    descriptor = TitanStorageIo_Open(path, TITAN_STORAGE_IO_READ);
    if (descriptor < 0)
    {
        return 0;
    }

    if (!TitanStorageIo_ReadExact(descriptor, header, sizeof(header)))
    {
        TitanStorageIo_Close(descriptor);
        return 0;
    }

    if (
        memcmp(header, g_cache_magic, sizeof(g_cache_magic)) != 0 ||
        TitanLibraryCache_ReadU32Le(header + 4u) != TITAN_LIBRARY_CACHE_VERSION ||
        TitanLibraryCache_ReadU32Le(header + 8u) != (uint32_t)file_size
    )
    {
        TitanStorageIo_Close(descriptor);
        return 0;
    }

    games_in_file = TitanLibraryCache_ReadU32Le(header + 12u);
    applications_in_file = TitanLibraryCache_ReadU32Le(header + 16u);
    stored_checksum = TitanLibraryCache_ReadU32Le(
        header + TITAN_LIBRARY_CACHE_CHECKSUM_OFFSET
    );

    if (
        games_in_file > TITAN_NEUTRINO_GAME_CAPACITY ||
        applications_in_file > TITAN_APPLICATION_CAPACITY
    )
    {
        TitanStorageIo_Close(descriptor);
        return 0;
    }

    expected_size = TITAN_LIBRARY_CACHE_HEADER_SIZE +
        ((uint64_t)games_in_file * TITAN_LIBRARY_CACHE_GAME_RECORD_SIZE) +
        ((uint64_t)applications_in_file * TITAN_LIBRARY_CACHE_APPLICATION_RECORD_SIZE);
    if (expected_size != file_size)
    {
        TitanStorageIo_Close(descriptor);
        return 0;
    }

    header[TITAN_LIBRARY_CACHE_CHECKSUM_OFFSET + 0u] = 0u;
    header[TITAN_LIBRARY_CACHE_CHECKSUM_OFFSET + 1u] = 0u;
    header[TITAN_LIBRARY_CACHE_CHECKSUM_OFFSET + 2u] = 0u;
    header[TITAN_LIBRARY_CACHE_CHECKSUM_OFFSET + 3u] = 0u;
    checksum = TitanLibraryCache_HeaderChecksum(header);

    if (
        neutrino_capacity == 0u ||
        memchr(header + 28u, '\0', TITAN_NEUTRINO_PATH_CAPACITY) == 0
    )
    {
        TitanStorageIo_Close(descriptor);
        return 0;
    }
    strncpy(neutrino_elf, (const char *)(header + 28u), neutrino_capacity - 1u);
    neutrino_elf[neutrino_capacity - 1u] = '\0';

    for (index = 0u; index < games_in_file; index++)
    {
        if (!TitanStorageIo_ReadExact(descriptor, game_record, sizeof(game_record)))
        {
            TitanStorageIo_Close(descriptor);
            return 0;
        }
        checksum = TitanLibraryCache_ChecksumUpdate(
            checksum,
            game_record,
            sizeof(game_record)
        );
        TitanLibraryCache_DeserializeGame(&games[index], game_record);
        if (!TitanLibraryCache_GameValid(&games[index]))
        {
            TitanStorageIo_Close(descriptor);
            return 0;
        }
    }

    for (index = 0u; index < applications_in_file; index++)
    {
        if (!TitanStorageIo_ReadExact(
            descriptor,
            application_record,
            sizeof(application_record)
        ))
        {
            TitanStorageIo_Close(descriptor);
            return 0;
        }
        checksum = TitanLibraryCache_ChecksumUpdate(
            checksum,
            application_record,
            sizeof(application_record)
        );
        TitanLibraryCache_DeserializeApplication(
            &applications[index],
            application_record
        );
        if (!TitanLibraryCache_ApplicationValid(&applications[index]))
        {
            TitanStorageIo_Close(descriptor);
            return 0;
        }
    }

    if (
        TitanStorageIo_Close(descriptor) < 0 ||
        checksum != stored_checksum ||
        !TitanLibraryCache_DeduplicateGames(games, games_in_file) ||
        !TitanLibraryCache_DeduplicateApplications(
            applications,
            applications_in_file
        )
    )
    {
        return 0;
    }

    TitanLibraryCache_SortGames(games, games_in_file);
    TitanLibraryCache_SortApplications(applications, applications_in_file);
    *game_count = games_in_file;
    *application_count = applications_in_file;
    return 1;
}

int TitanLibraryCache_Load(
    TitanNeutrinoGame *games,
    unsigned int *game_count,
    TitanApplication *applications,
    unsigned int *application_count,
    char *neutrino_elf,
    unsigned int neutrino_capacity
)
{
    if (
        games == 0 || game_count == 0 ||
        applications == 0 || application_count == 0 ||
        neutrino_elf == 0 || neutrino_capacity == 0u
    )
    {
        return 0;
    }

    if (TitanLibraryCache_LoadPath(
        TITAN_LIBRARY_CACHE_PATH,
        games,
        game_count,
        applications,
        application_count,
        neutrino_elf,
        neutrino_capacity
    ))
    {
        return 1;
    }

    if (TitanLibraryCache_LoadPath(
        TITAN_LIBRARY_CACHE_BACKUP_PATH,
        games,
        game_count,
        applications,
        application_count,
        neutrino_elf,
        neutrino_capacity
    ))
    {
        TitanStorageIo_Remove(TITAN_LIBRARY_CACHE_PATH);
        TitanStorageIo_Rename(
            TITAN_LIBRARY_CACHE_BACKUP_PATH,
            TITAN_LIBRARY_CACHE_PATH
        );
        return 1;
    }

    if (TitanLibraryCache_LoadPath(
        TITAN_LIBRARY_CACHE_TEMP_PATH,
        games,
        game_count,
        applications,
        application_count,
        neutrino_elf,
        neutrino_capacity
    ))
    {
        TitanStorageIo_AtomicReplace(
            TITAN_LIBRARY_CACHE_TEMP_PATH,
            TITAN_LIBRARY_CACHE_PATH,
            TITAN_LIBRARY_CACHE_BACKUP_PATH
        );
        return 1;
    }

    return 0;
}

int TitanLibraryCache_Save(
    TitanNeutrinoGame *games,
    unsigned int game_count,
    TitanApplication *applications,
    unsigned int application_count,
    const char *neutrino_elf
)
{
    unsigned char header[TITAN_LIBRARY_CACHE_HEADER_SIZE];
    unsigned char game_record[TITAN_LIBRARY_CACHE_GAME_RECORD_SIZE];
    unsigned char application_record[TITAN_LIBRARY_CACHE_APPLICATION_RECORD_SIZE];
    uint64_t total_size;
    uint32_t checksum;
    unsigned int index;
    int descriptor;

    if (
        games == 0 || applications == 0 || neutrino_elf == 0 ||
        game_count > TITAN_NEUTRINO_GAME_CAPACITY ||
        application_count > TITAN_APPLICATION_CAPACITY ||
        strlen(neutrino_elf) >= TITAN_NEUTRINO_PATH_CAPACITY
    )
    {
        return 0;
    }

    for (index = 0u; index < game_count; index++)
    {
        if (!TitanLibraryCache_GameValid(&games[index]))
        {
            return 0;
        }
    }
    for (index = 0u; index < application_count; index++)
    {
        if (!TitanLibraryCache_ApplicationValid(&applications[index]))
        {
            return 0;
        }
    }
    if (
        !TitanLibraryCache_DeduplicateGames(games, game_count) ||
        !TitanLibraryCache_DeduplicateApplications(
            applications,
            application_count
        )
    )
    {
        return 0;
    }

    TitanLibraryCache_SortGames(games, game_count);
    TitanLibraryCache_SortApplications(applications, application_count);

    total_size = TITAN_LIBRARY_CACHE_HEADER_SIZE +
        ((uint64_t)game_count * TITAN_LIBRARY_CACHE_GAME_RECORD_SIZE) +
        ((uint64_t)application_count * TITAN_LIBRARY_CACHE_APPLICATION_RECORD_SIZE);
    if (total_size > UINT32_MAX)
    {
        return 0;
    }

    memset(header, 0, sizeof(header));
    memcpy(header, g_cache_magic, sizeof(g_cache_magic));
    TitanLibraryCache_WriteU32Le(header + 4u, TITAN_LIBRARY_CACHE_VERSION);
    TitanLibraryCache_WriteU32Le(header + 8u, (uint32_t)total_size);
    TitanLibraryCache_WriteU32Le(header + 12u, game_count);
    TitanLibraryCache_WriteU32Le(header + 16u, application_count);
    strncpy(
        (char *)(header + 28u),
        neutrino_elf,
        TITAN_NEUTRINO_PATH_CAPACITY - 1u
    );

    checksum = TitanLibraryCache_HeaderChecksum(header);
    for (index = 0u; index < game_count; index++)
    {
        TitanLibraryCache_SerializeGame(game_record, &games[index]);
        checksum = TitanLibraryCache_ChecksumUpdate(
            checksum,
            game_record,
            sizeof(game_record)
        );
    }
    for (index = 0u; index < application_count; index++)
    {
        TitanLibraryCache_SerializeApplication(
            application_record,
            &applications[index]
        );
        checksum = TitanLibraryCache_ChecksumUpdate(
            checksum,
            application_record,
            sizeof(application_record)
        );
    }
    TitanLibraryCache_WriteU32Le(
        header + TITAN_LIBRARY_CACHE_CHECKSUM_OFFSET,
        checksum
    );

    TitanStorageIo_CreateDirectory(TITAN_STORAGE_DEVICE_ROOT);
    TitanStorageIo_CreateDirectory(TITAN_LIBRARY_CACHE_DIRECTORY);
    TitanStorageIo_Remove(TITAN_LIBRARY_CACHE_TEMP_PATH);

    descriptor = TitanStorageIo_Open(
        TITAN_LIBRARY_CACHE_TEMP_PATH,
        TITAN_STORAGE_IO_WRITE |
            TITAN_STORAGE_IO_CREATE |
            TITAN_STORAGE_IO_TRUNCATE
    );
    if (descriptor < 0)
    {
        return 0;
    }

    if (!TitanStorageIo_WriteExact(descriptor, header, sizeof(header)))
    {
        TitanStorageIo_Close(descriptor);
        TitanStorageIo_Remove(TITAN_LIBRARY_CACHE_TEMP_PATH);
        return 0;
    }

    for (index = 0u; index < game_count; index++)
    {
        TitanLibraryCache_SerializeGame(game_record, &games[index]);
        if (!TitanStorageIo_WriteExact(
            descriptor,
            game_record,
            sizeof(game_record)
        ))
        {
            TitanStorageIo_Close(descriptor);
            TitanStorageIo_Remove(TITAN_LIBRARY_CACHE_TEMP_PATH);
            return 0;
        }
    }

    for (index = 0u; index < application_count; index++)
    {
        TitanLibraryCache_SerializeApplication(
            application_record,
            &applications[index]
        );
        if (!TitanStorageIo_WriteExact(
            descriptor,
            application_record,
            sizeof(application_record)
        ))
        {
            TitanStorageIo_Close(descriptor);
            TitanStorageIo_Remove(TITAN_LIBRARY_CACHE_TEMP_PATH);
            return 0;
        }
    }

    if (
        TitanStorageIo_Flush(descriptor) < 0 ||
        TitanStorageIo_Close(descriptor) < 0
    )
    {
        TitanStorageIo_Remove(TITAN_LIBRARY_CACHE_TEMP_PATH);
        return 0;
    }

    /* Validate the exact bytes before publication. Reuse the caller buffers
     * only after the temporary has already been serialized and flushed. */
    {
        static TitanNeutrinoGame validation_games[TITAN_NEUTRINO_GAME_CAPACITY];
        static TitanApplication validation_applications[TITAN_APPLICATION_CAPACITY];
        char validation_neutrino[TITAN_NEUTRINO_PATH_CAPACITY];
        unsigned int validation_game_count;
        unsigned int validation_application_count;

        if (!TitanLibraryCache_LoadPath(
            TITAN_LIBRARY_CACHE_TEMP_PATH,
            validation_games,
            &validation_game_count,
            validation_applications,
            &validation_application_count,
            validation_neutrino,
            sizeof(validation_neutrino)
        ) ||
            validation_game_count != game_count ||
            validation_application_count != application_count)
        {
            TitanStorageIo_Remove(TITAN_LIBRARY_CACHE_TEMP_PATH);
            return 0;
        }
    }

    if (!TitanStorageIo_AtomicReplace(
        TITAN_LIBRARY_CACHE_TEMP_PATH,
        TITAN_LIBRARY_CACHE_PATH,
        TITAN_LIBRARY_CACHE_BACKUP_PATH
    ))
    {
        TitanStorageIo_Remove(TITAN_LIBRARY_CACHE_TEMP_PATH);
        return 0;
    }

    TitanStorageIo_Remove(TITAN_LIBRARY_CACHE_BACKUP_PATH);
    return 1;
}

int TitanLibraryCache_Clear(void)
{
    const char *paths[] =
    {
        TITAN_LIBRARY_CACHE_PATH,
        TITAN_LIBRARY_CACHE_TEMP_PATH,
        TITAN_LIBRARY_CACHE_BACKUP_PATH
    };
    unsigned int index;

    for (index = 0u; index < sizeof(paths) / sizeof(paths[0]); index++)
    {
        if (
            TitanStorageIo_PathInfo64(paths[index], 0, 0) &&
            TitanStorageIo_Remove(paths[index]) < 0
        )
        {
            return 0;
        }
    }
    return 1;
}
