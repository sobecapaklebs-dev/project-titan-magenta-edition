#include "titan_context.h"

static void TitanContext_CopyString(
    char *destination,
    unsigned int capacity,
    const char *source
)
{
    unsigned int index;

    if (
        destination == 0 ||
        capacity == 0
    )
    {
        return;
    }

    destination[0] = '\0';

    if (source == 0)
    {
        return;
    }

    index = 0;

    while (
        index + 1 < capacity &&
        source[index] != '\0'
    )
    {
        destination[index] =
            source[index];

        index++;
    }

    destination[index] = '\0';
}

void TitanContext_Initialize(
    TitanContext *context,
    int argc,
    char *argv[]
)
{
    if (context == 0)
    {
        return;
    }

    context->magic =
        TITAN_CONTEXT_MAGIC;

    context->version =
        TITAN_CONTEXT_VERSION;

    context->state =
        TITAN_KERNEL_STATE_COLD;

    context->last_result =
        TITAN_RESULT_OK;

    context->frame_count = 0;

    context->argument_count = argc;
    context->argument_values = argv;

    context->boot_path[0] = '\0';

    if (
        argc > 0 &&
        argv != 0 &&
        argv[0] != 0
    )
    {
        TitanContext_CopyString(
            context->boot_path,
            TITAN_BOOT_PATH_CAPACITY,
            argv[0]
        );
    }
}

int TitanContext_IsValid(
    const TitanContext *context
)
{
    if (context == 0)
    {
        return 0;
    }

    if (
        context->magic !=
        TITAN_CONTEXT_MAGIC
    )
    {
        return 0;
    }

    if (
        context->version !=
        TITAN_CONTEXT_VERSION
    )
    {
        return 0;
    }

    return 1;
}
