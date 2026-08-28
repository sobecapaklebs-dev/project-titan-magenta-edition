#include "titan_system_info.h"

#define TITAN_BOOTLOADER_ARGUMENT_PREFIX \
    "--bootloader-version="

static TitanSystemInfo g_system_info;
static int g_system_info_initialized;

static void TitanSystemInfo_CopyString(
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

    while (
        index + 1u < capacity &&
        source[index] != '\0'
    )
    {
        destination[index] = source[index];
        index++;
    }

    destination[index] = '\0';
}

static int TitanSystemInfo_HasPrefix(
    const char *value,
    const char *prefix
)
{
    unsigned int index;

    if (value == 0 || prefix == 0)
    {
        return 0;
    }

    index = 0u;

    while (prefix[index] != '\0')
    {
        if (value[index] != prefix[index])
        {
            return 0;
        }

        index++;
    }

    return 1;
}

void TitanSystemInfo_Initialize(
    const TitanContext *context
)
{
    int argument_index;
    const char *argument;
    const char *bootloader_value;
    unsigned int prefix_length;

    if (g_system_info_initialized)
    {
        return;
    }

    TitanSystemInfo_CopyString(
        g_system_info.bootloader_version,
        sizeof(g_system_info.bootloader_version),
        "Not reported"
    );

    TitanSystemInfo_CopyString(
        g_system_info.bios_version,
        sizeof(g_system_info.bios_version),
        "Unavailable in this build"
    );

    TitanSystemInfo_CopyString(
        g_system_info.console_model,
        sizeof(g_system_info.console_model),
        "Unavailable in this build"
    );

    TitanSystemInfo_CopyString(
        g_system_info.boot_path,
        sizeof(g_system_info.boot_path),
        context != 0
            ? context->boot_path
            : ""
    );

    prefix_length =
        sizeof(TITAN_BOOTLOADER_ARGUMENT_PREFIX) - 1u;

    if (
        context != 0 &&
        context->argument_values != 0
    )
    {
        for (
            argument_index = 1;
            argument_index < context->argument_count;
            argument_index++
        )
        {
            argument =
                context->argument_values[argument_index];

            if (
                !TitanSystemInfo_HasPrefix(
                    argument,
                    TITAN_BOOTLOADER_ARGUMENT_PREFIX
                )
            )
            {
                continue;
            }

            bootloader_value =
                argument + prefix_length;

            if (bootloader_value[0] != '\0')
            {
                TitanSystemInfo_CopyString(
                    g_system_info.bootloader_version,
                    sizeof(g_system_info.bootloader_version),
                    bootloader_value
                );
            }
        }
    }

    g_system_info_initialized = 1;
}

const TitanSystemInfo *TitanSystemInfo_Get(void)
{
    if (!g_system_info_initialized)
    {
        TitanSystemInfo_Initialize(0);
    }

    return &g_system_info;
}
