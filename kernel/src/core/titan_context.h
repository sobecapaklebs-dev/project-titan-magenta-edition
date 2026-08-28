#ifndef PROJECT_TITAN_CONTEXT_H
#define PROJECT_TITAN_CONTEXT_H

#include "titan_result.h"

#define TITAN_CONTEXT_MAGIC 0x544B524E
#define TITAN_CONTEXT_VERSION 1
#define TITAN_BOOT_PATH_CAPACITY 256

typedef enum TitanKernelState
{
    TITAN_KERNEL_STATE_COLD = 0,
    TITAN_KERNEL_STATE_INITIALIZING,
    TITAN_KERNEL_STATE_RUNNING,
    TITAN_KERNEL_STATE_FAILED
} TitanKernelState;

typedef struct TitanContext
{
    unsigned int magic;
    unsigned int version;

    TitanKernelState state;
    TitanResult last_result;

    unsigned int frame_count;

    int argument_count;
    char **argument_values;

    char boot_path[
        TITAN_BOOT_PATH_CAPACITY
    ];
} TitanContext;

void TitanContext_Initialize(
    TitanContext *context,
    int argc,
    char *argv[]
);

int TitanContext_IsValid(
    const TitanContext *context
);

#endif
