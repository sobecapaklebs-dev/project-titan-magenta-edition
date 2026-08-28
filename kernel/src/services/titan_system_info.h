#ifndef PROJECT_TITAN_SYSTEM_INFO_H
#define PROJECT_TITAN_SYSTEM_INFO_H

#include "titan_context.h"

#define TITAN_SYSTEM_INFO_VALUE_CAPACITY 64

typedef struct TitanSystemInfo
{
    char bootloader_version[
        TITAN_SYSTEM_INFO_VALUE_CAPACITY
    ];

    char bios_version[
        TITAN_SYSTEM_INFO_VALUE_CAPACITY
    ];

    char console_model[
        TITAN_SYSTEM_INFO_VALUE_CAPACITY
    ];

    char boot_path[
        TITAN_BOOT_PATH_CAPACITY
    ];
} TitanSystemInfo;

void TitanSystemInfo_Initialize(
    const TitanContext *context
);

const TitanSystemInfo *TitanSystemInfo_Get(void);

#endif
