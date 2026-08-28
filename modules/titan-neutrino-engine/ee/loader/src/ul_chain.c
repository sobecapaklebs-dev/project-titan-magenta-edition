#if !defined(__EE__) && !defined(_EE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "ul_chain.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <limits.h>
#if !defined(__EE__) && !defined(_EE)
#include <errno.h>
#include <time.h>
#endif

#if defined(__EE__) || defined(_EE)
#include <delaythread.h>
#endif

#if defined(__EE__) || defined(_EE)
typedef _off64_t UlChainOffset;
_off64_t lseek64(int file_descriptor, _off64_t offset, int whence);
#else
typedef off_t UlChainOffset;
#define lseek64 lseek
#endif

static int UlChain_ParseUnsigned(
    const char *text,
    unsigned int minimum,
    unsigned int maximum,
    unsigned int *value
)
{
    char *end;
    unsigned long parsed;

    if (text == NULL || text[0] == '\0' || value == NULL)
    {
        return 0;
    }

    end = NULL;
    parsed = strtoul(text, &end, 0);
    if (end == text || end == NULL || *end != '\0' ||
        parsed < minimum || parsed > maximum)
    {
        return 0;
    }

    *value = (unsigned int)parsed;
    return 1;
}


#define UL_CHAIN_ENTRY_SIZE 64u
#define UL_CHAIN_ENTRY_TITLE_OFFSET 0u
#define UL_CHAIN_ENTRY_MAGIC_OFFSET 32u
#define UL_CHAIN_ENTRY_STARTUP_OFFSET 35u
#define UL_CHAIN_ENTRY_PARTS_OFFSET 47u
#define UL_CHAIN_ENTRY_MEDIA_OFFSET 48u

static unsigned int g_ul_crc_table[256];
static unsigned int g_ul_crc_seed;
static int g_ul_crc_ready;

static void UlChain_InitCrc(void)
{
    unsigned int table_index;
    unsigned int crc;

    if (g_ul_crc_ready)
    {
        return;
    }

    crc = 0u;
    for (table_index = 0u; table_index < 256u; table_index++)
    {
        unsigned int bit_count;

        crc = table_index << 24;
        for (bit_count = 8u; bit_count > 0u; bit_count--)
        {
            if ((int32_t)crc < 0)
            {
                crc <<= 1;
            }
            else
            {
                crc = (crc << 1) ^ 0x04C11DB7u;
            }
        }
        g_ul_crc_table[255u - table_index] = crc;
    }

    g_ul_crc_seed = crc;
    g_ul_crc_ready = 1;
}

static unsigned int UlChain_Crc32(const char *text)
{
    unsigned int crc;
    unsigned int index;

    if (text == NULL)
    {
        return 0u;
    }

    UlChain_InitCrc();
    crc = g_ul_crc_seed;
    index = 0u;

    for (;;)
    {
        unsigned int byte;

        byte = (unsigned int)(unsigned char)text[index++];
        crc = g_ul_crc_table[
            byte ^ ((crc >> 24) & 0xFFu)
        ] ^ ((crc << 8) & 0xFFFFFF00u);
        if (byte == 0u)
        {
            break;
        }
    }

    return crc;
}

static void UlChain_CopyFixedText(
    char *destination,
    unsigned int destination_capacity,
    const unsigned char *source,
    unsigned int source_length,
    int trim_spaces
)
{
    unsigned int length;

    if (destination == NULL || destination_capacity == 0u)
    {
        return;
    }

    length = source_length;
    if (length >= destination_capacity)
    {
        length = destination_capacity - 1u;
    }

    memcpy(destination, source, length);
    destination[length] = '\0';

    while (length > 0u &&
           (destination[length - 1u] == '\0' ||
            (trim_spaces && destination[length - 1u] == ' ')))
    {
        length--;
    }
    destination[length] = '\0';
}

static void UlChain_DelayRetry(void)
{
#if defined(__EE__) || defined(_EE)
    DelayThread(10000);
#else
    struct timespec delay;

    delay.tv_sec = 0;
    delay.tv_nsec = 10000000L;
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR)
    {
    }
#endif
}

static int UlChain_ReadExact(
    int file_descriptor,
    void *destination,
    unsigned int size,
    int *clean_eof
)
{
    unsigned char *output;
    unsigned int completed;

    if (clean_eof != NULL)
    {
        *clean_eof = 0;
    }

    output = (unsigned char *)destination;
    completed = 0u;
    while (completed < size)
    {
        int amount = read(file_descriptor, output + completed, size - completed);
        if (amount == 0)
        {
            if (clean_eof != NULL && completed == 0u)
            {
                *clean_eof = 1;
            }
            return 0;
        }
        if (amount < 0)
        {
            return 0;
        }
        completed += (unsigned int)amount;
    }
    return 1;
}

int UlChain_LoadConfig(const char *specification, UlChain *chain)
{
    const char *startup_separator;
    const char *device_separator;
    unsigned int catalog_length;
    unsigned int startup_length;
    unsigned int device_length;
    char catalog_path[UL_CHAIN_PATH_CAPACITY];
    char requested_startup[UL_CHAIN_STARTUP_CAPACITY];
    char device_prefix[32];
    unsigned char entry[UL_CHAIN_ENTRY_SIZE];
    int file_descriptor;
    int found;
    int written;

    if (specification == NULL || chain == NULL)
    {
        return 0;
    }

    startup_separator = strchr(specification, ',');
    if (startup_separator == NULL || startup_separator == specification)
    {
        return 0;
    }

    device_separator = strchr(startup_separator + 1, ',');
    if (device_separator == NULL || device_separator == startup_separator + 1 ||
        device_separator[1] == '\0' || strchr(device_separator + 1, ',') != NULL)
    {
        return 0;
    }

    catalog_length = (unsigned int)(startup_separator - specification);
    startup_length = (unsigned int)(device_separator - startup_separator - 1);
    device_length = (unsigned int)strlen(device_separator + 1);

    if (catalog_length >= sizeof(catalog_path) ||
        startup_length >= sizeof(requested_startup) ||
        device_length >= sizeof(device_prefix))
    {
        return 0;
    }

    memcpy(catalog_path, specification, catalog_length);
    catalog_path[catalog_length] = '\0';
    memcpy(requested_startup, startup_separator + 1, startup_length);
    requested_startup[startup_length] = '\0';
    memcpy(device_prefix, device_separator + 1, device_length + 1u);

    memset(chain, 0, sizeof(*chain));
    file_descriptor = open(catalog_path, O_RDONLY);
    if (file_descriptor < 0)
    {
        printf("Unable to open USBExtreme catalog %s\n", catalog_path);
        return 0;
    }

    found = 0;
    for (;;)
    {
        char startup[UL_CHAIN_STARTUP_CAPACITY];
        int clean_eof;

        if (!UlChain_ReadExact(file_descriptor, entry, sizeof(entry), &clean_eof))
        {
            if (!clean_eof)
            {
                printf("Corrupt or truncated USBExtreme catalog %s\n", catalog_path);
                close(file_descriptor);
                memset(chain, 0, sizeof(*chain));
                return 0;
            }
            break;
        }

        if (memcmp(
                &entry[UL_CHAIN_ENTRY_MAGIC_OFFSET],
                "ul.",
                3u) != 0 ||
            entry[UL_CHAIN_ENTRY_PARTS_OFFSET] == 0u ||
            (entry[UL_CHAIN_ENTRY_MEDIA_OFFSET] != 0x12u &&
             entry[UL_CHAIN_ENTRY_MEDIA_OFFSET] != 0x14u))
        {
            continue;
        }

        UlChain_CopyFixedText(
            startup,
            sizeof(startup),
            &entry[UL_CHAIN_ENTRY_STARTUP_OFFSET],
            12u,
            1
        );
        if (strcmp(startup, requested_startup) != 0)
        {
            continue;
        }

        /*
         * Match OPL: copy all 32 title bytes, then append one terminator.
         * Do not trim spaces because the CRC is calculated from this exact
         * C string representation.
         */
        memcpy(
            chain->title,
            &entry[UL_CHAIN_ENTRY_TITLE_OFFSET],
            32u
        );
        chain->title[32] = '\0';
        strcpy(chain->startup, startup);
        chain->part_count = entry[UL_CHAIN_ENTRY_PARTS_OFFSET];
        chain->media = entry[UL_CHAIN_ENTRY_MEDIA_OFFSET];
        found = 1;
        break;
    }
    close(file_descriptor);

    if (!found || chain->title[0] == '\0')
    {
        printf(
            "Unable to find USBExtreme startup %s in %s\n",
            requested_startup,
            catalog_path
        );
        memset(chain, 0, sizeof(*chain));
        return 0;
    }

    written = snprintf(
        chain->base_path,
        sizeof(chain->base_path),
        "%sul.%08X.%s",
        device_prefix,
        UlChain_Crc32(chain->title),
        chain->startup
    );
    if (written < 0 || (unsigned int)written >= sizeof(chain->base_path))
    {
        memset(chain, 0, sizeof(*chain));
        return 0;
    }

    written = snprintf(
        chain->elf_path,
        sizeof(chain->elf_path),
        "cdrom0:\\%s;1",
        chain->startup
    );
    if (written < 0 || (unsigned int)written >= sizeof(chain->elf_path))
    {
        memset(chain, 0, sizeof(*chain));
        return 0;
    }

    printf("USBExtreme catalog entry:\n");
    printf("- title = %s\n", chain->title);
    printf("- startup = %s\n", chain->startup);
    printf("- parts = %u\n", chain->part_count);
    printf("- media = 0x%02x\n", chain->media);
    printf("- base = %s\n", chain->base_path);

    return 1;
}

int UlChain_Parse(const char *specification, UlChain *chain)
{
    const char *media_separator;
    const char *parts_separator;
    unsigned int base_length;

    if (specification == NULL || chain == NULL)
    {
        return 0;
    }

    memset(chain, 0, sizeof(*chain));

    media_separator = strrchr(specification, ',');
    if (media_separator == NULL || media_separator == specification)
    {
        return 0;
    }

    parts_separator = media_separator - 1;
    while (parts_separator > specification && *parts_separator != ',')
    {
        parts_separator--;
    }
    if (*parts_separator != ',' || parts_separator == specification)
    {
        return 0;
    }

    base_length = (unsigned int)(parts_separator - specification);
    if (base_length == 0u || base_length >= sizeof(chain->base_path))
    {
        return 0;
    }

    memcpy(chain->base_path, specification, base_length);
    chain->base_path[base_length] = '\0';

    {
        char parts_text[16];
        unsigned int parts_length;

        parts_length = (unsigned int)(media_separator - parts_separator - 1);
        if (parts_length == 0u || parts_length >= sizeof(parts_text))
        {
            return 0;
        }
        memcpy(parts_text, parts_separator + 1, parts_length);
        parts_text[parts_length] = '\0';

        if (!UlChain_ParseUnsigned(
                parts_text,
                1u,
                UL_CHAIN_MAX_PARTS,
                &chain->part_count))
        {
            return 0;
        }
    }

    if (!UlChain_ParseUnsigned(
            media_separator + 1,
            0u,
            255u,
            &chain->media) ||
        (chain->media != 0x12u && chain->media != 0x14u))
    {
        return 0;
    }

    return 1;
}

int UlChain_BuildPartPath(
    const UlChain *chain,
    unsigned int part_index,
    char *destination,
    unsigned int capacity
)
{
    int written;

    if (chain == NULL || destination == NULL || capacity == 0u ||
        part_index >= chain->part_count)
    {
        return 0;
    }

    written = snprintf(
        destination,
        capacity,
        "%s.%02x",
        chain->base_path,
        part_index
    );
    return written >= 0 && (unsigned int)written < capacity;
}

static int UlChain_OpenReadOnly(const char *path)
{
    int attempt;
    int file_descriptor;

    /* 100 bounded attempts over roughly one second, never a CPU spin loop. */
    for (attempt = 0; attempt < 100; attempt++)
    {
        file_descriptor = open(path, O_RDONLY);
        if (file_descriptor >= 0)
        {
            return file_descriptor;
        }
        UlChain_DelayRetry();
    }

    return -1;
}

int UlChain_Probe(UlChain *chain)
{
    char part_path[UL_CHAIN_PATH_CAPACITY + 8u];
    unsigned int part_index;

    if (chain == NULL || chain->part_count == 0u)
    {
        return 0;
    }

    chain->total_size = 0u;
    memset(chain->part_sizes, 0, sizeof(chain->part_sizes));

    for (part_index = 0u; part_index < chain->part_count; part_index++)
    {
        int file_descriptor;
        UlChainOffset size;

        if (!UlChain_BuildPartPath(
                chain,
                part_index,
                part_path,
                sizeof(part_path)))
        {
            return 0;
        }

        file_descriptor = UlChain_OpenReadOnly(part_path);
        if (file_descriptor < 0)
        {
            printf("Unable to open USBExtreme part %s\n", part_path);
            return 0;
        }

        size = lseek64(file_descriptor, 0, SEEK_END);
        close(file_descriptor);
        if (size <= 0)
        {
            printf("USBExtreme part has an invalid size: %s\n", part_path);
            return 0;
        }

        if ((uint64_t)size > UINT64_MAX - chain->total_size)
        {
            printf("USBExtreme total size overflow: %s\n", part_path);
            memset(chain->part_sizes, 0, sizeof(chain->part_sizes));
            chain->total_size = 0u;
            return 0;
        }

        chain->part_sizes[part_index] = (uint64_t)size;
        chain->total_size += (uint64_t)size;
    }

    return chain->total_size > 0u;
}

int UlChain_ReadAt(
    const UlChain *chain,
    uint64_t logical_offset,
    void *destination,
    unsigned int size
)
{
    unsigned char *output;
    uint64_t part_start;
    unsigned int part_index;
    unsigned int remaining;

    if (chain == NULL || destination == NULL || size == 0u ||
        logical_offset > chain->total_size ||
        (uint64_t)size > chain->total_size - logical_offset)
    {
        return 0;
    }

    output = (unsigned char *)destination;
    remaining = size;
    part_start = 0u;

    for (part_index = 0u;
         part_index < chain->part_count && remaining > 0u;
         part_index++)
    {
        uint64_t part_end;

        if (chain->part_sizes[part_index] > UINT64_MAX - part_start)
        {
            return 0;
        }
        part_end = part_start + chain->part_sizes[part_index];
        if (logical_offset >= part_start && logical_offset < part_end)
        {
            char part_path[UL_CHAIN_PATH_CAPACITY + 8u];
            uint64_t offset_in_part;
            uint64_t available;
            unsigned int request;
            int file_descriptor;
            int read_size;

            offset_in_part = logical_offset - part_start;
            available = chain->part_sizes[part_index] - offset_in_part;
            request = remaining;
            if ((uint64_t)request > available)
            {
                request = (unsigned int)available;
            }

            if (!UlChain_BuildPartPath(
                    chain,
                    part_index,
                    part_path,
                    sizeof(part_path)))
            {
                return 0;
            }

            file_descriptor = UlChain_OpenReadOnly(part_path);
            if (file_descriptor < 0 ||
                lseek64(file_descriptor, (UlChainOffset)offset_in_part, SEEK_SET) < 0)
            {
                if (file_descriptor >= 0)
                {
                    close(file_descriptor);
                }
                return 0;
            }

            {
                int clean_eof;
                read_size = UlChain_ReadExact(
                    file_descriptor,
                    output,
                    request,
                    &clean_eof
                ) ? (int)request : -1;
            }
            close(file_descriptor);
            if (read_size != (int)request)
            {
                return 0;
            }

            output += request;
            remaining -= request;
            logical_offset += request;
        }

        part_start = part_end;
    }

    return remaining == 0u;
}
