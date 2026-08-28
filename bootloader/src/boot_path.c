#include "boot_path.h"

#include <string.h>

static int TitanBootPath_Add(
    char candidates[][TITAN_BOOT_MAX_PATH],
    size_t capacity,
    size_t *count,
    const char *path
)
{
    size_t index;
    size_t length;

    if (
        candidates == 0 ||
        count == 0 ||
        path == 0 ||
        path[0] == '\0' ||
        *count >= capacity
    )
    {
        return 0;
    }

    length = strlen(path);

    if (length >= TITAN_BOOT_MAX_PATH)
    {
        return 0;
    }

    for (index = 0u; index < *count; index++)
    {
        if (strcmp(candidates[index], path) == 0)
        {
            return 1;
        }
    }

    memcpy(candidates[*count], path, length + 1u);
    (*count)++;
    return 1;
}

static int TitanBootPath_JoinSibling(
    const char *executable_path,
    const char *relative_name,
    char output[TITAN_BOOT_MAX_PATH]
)
{
    const char *separator;
    const char *cursor;
    const char *colon;
    size_t prefix_length;
    size_t name_length;

    if (
        executable_path == 0 ||
        executable_path[0] == '\0' ||
        relative_name == 0 ||
        output == 0
    )
    {
        return 0;
    }

    separator = 0;
    cursor = executable_path;

    while (*cursor != '\0')
    {
        if (*cursor == '/' || *cursor == '\\')
        {
            separator = cursor;
        }

        cursor++;
    }

    if (separator != 0)
    {
        prefix_length = (size_t)(separator - executable_path) + 1u;
    }
    else
    {
        colon = strchr(executable_path, ':');
        prefix_length = colon != 0
            ? (size_t)(colon - executable_path) + 1u
            : 0u;
    }

    name_length = strlen(relative_name);

    if (prefix_length + name_length >= TITAN_BOOT_MAX_PATH)
    {
        return 0;
    }

    if (prefix_length > 0u)
    {
        memcpy(output, executable_path, prefix_length);
    }

    memcpy(output + prefix_length, relative_name, name_length + 1u);
    return 1;
}

static int TitanBootPath_MakeMassAlias(
    const char *source,
    char output[TITAN_BOOT_MAX_PATH]
)
{
    const char *suffix;
    const char *prefix;
    size_t total_length;

    if (source == 0 || output == 0)
    {
        return 0;
    }

    if (strncmp(source, "mass0:", 6u) == 0)
    {
        prefix = "mass:";
        suffix = source + 6u;
    }
    else if (strncmp(source, "mass:", 5u) == 0)
    {
        prefix = "mass0:";
        suffix = source + 5u;
    }
    else
    {
        return 0;
    }

    total_length = strlen(prefix) + strlen(suffix);

    if (total_length >= TITAN_BOOT_MAX_PATH)
    {
        return 0;
    }

    strcpy(output, prefix);
    strcat(output, suffix);
    return 1;
}

size_t TitanBootPath_BuildCandidates(
    const char *executable_path,
    const char *override_path,
    char candidates[][TITAN_BOOT_MAX_PATH],
    size_t capacity
)
{
    char sibling[TITAN_BOOT_MAX_PATH];
    char alias[TITAN_BOOT_MAX_PATH];
    size_t count;

    if (candidates == 0 || capacity == 0u)
    {
        return 0u;
    }

    count = 0u;

    TitanBootPath_Add(
        candidates,
        capacity,
        &count,
        override_path
    );

    /* Canonical Project Titan Magenta Edition deployment path. */
    TitanBootPath_Add(candidates, capacity, &count, "mass:/kernel.elf");
    TitanBootPath_Add(candidates, capacity, &count, "mass0:/kernel.elf");
    TitanBootPath_Add(candidates, capacity, &count, "mass1:/kernel.elf");
    TitanBootPath_Add(candidates, capacity, &count, "mass2:/kernel.elf");
    TitanBootPath_Add(candidates, capacity, &count, "mass3:/kernel.elf");
    TitanBootPath_Add(candidates, capacity, &count, "mass4:/kernel.elf");
    TitanBootPath_Add(candidates, capacity, &count, "mass5:/kernel.elf");
    TitanBootPath_Add(candidates, capacity, &count, "mass6:/kernel.elf");
    TitanBootPath_Add(candidates, capacity, &count, "mass7:/kernel.elf");
    TitanBootPath_Add(candidates, capacity, &count, "mass8:/kernel.elf");
    TitanBootPath_Add(candidates, capacity, &count, "mass9:/kernel.elf");
    TitanBootPath_Add(candidates, capacity, &count, "mass0:/TITAN/kernel.elf");
    TitanBootPath_Add(candidates, capacity, &count, "mass1:/TITAN/kernel.elf");
    TitanBootPath_Add(candidates, capacity, &count, "mass2:/TITAN/kernel.elf");
    TitanBootPath_Add(candidates, capacity, &count, "mass3:/TITAN/kernel.elf");
    TitanBootPath_Add(candidates, capacity, &count, "mass4:/TITAN/kernel.elf");
    TitanBootPath_Add(candidates, capacity, &count, "mass5:/TITAN/kernel.elf");
    TitanBootPath_Add(candidates, capacity, &count, "mass6:/TITAN/kernel.elf");
    TitanBootPath_Add(candidates, capacity, &count, "mass7:/TITAN/kernel.elf");
    TitanBootPath_Add(candidates, capacity, &count, "mass8:/TITAN/kernel.elf");
    TitanBootPath_Add(candidates, capacity, &count, "mass9:/TITAN/kernel.elf");
    TitanBootPath_Add(candidates, capacity, &count, "mass0:/TITAN/kernel/kernel.elf");
    TitanBootPath_Add(candidates, capacity, &count, "mass1:/TITAN/kernel/kernel.elf");
    TitanBootPath_Add(candidates, capacity, &count, "mass2:/TITAN/kernel/kernel.elf");
    TitanBootPath_Add(candidates, capacity, &count, "mass3:/TITAN/kernel/kernel.elf");
    TitanBootPath_Add(candidates, capacity, &count, "mass4:/TITAN/kernel/kernel.elf");
    TitanBootPath_Add(candidates, capacity, &count, "mass5:/TITAN/kernel/kernel.elf");
    TitanBootPath_Add(candidates, capacity, &count, "mass6:/TITAN/kernel/kernel.elf");
    TitanBootPath_Add(candidates, capacity, &count, "mass7:/TITAN/kernel/kernel.elf");
    TitanBootPath_Add(candidates, capacity, &count, "mass8:/TITAN/kernel/kernel.elf");
    TitanBootPath_Add(candidates, capacity, &count, "mass9:/TITAN/kernel/kernel.elf");

    if (
        TitanBootPath_JoinSibling(
            executable_path,
            "kernel.elf",
            sibling
        )
    )
    {
        TitanBootPath_Add(
            candidates,
            capacity,
            &count,
            sibling
        );

        if (TitanBootPath_MakeMassAlias(sibling, alias))
        {
            TitanBootPath_Add(
                candidates,
                capacity,
                &count,
                alias
            );
        }
    }

    if (
        TitanBootPath_JoinSibling(
            executable_path,
            "kernel/kernel.elf",
            sibling
        )
    )
    {
        TitanBootPath_Add(
            candidates,
            capacity,
            &count,
            sibling
        );

        if (TitanBootPath_MakeMassAlias(sibling, alias))
        {
            TitanBootPath_Add(
                candidates,
                capacity,
                &count,
                alias
            );
        }
    }

    TitanBootPath_Add(
        candidates,
        capacity,
        &count,
        "mc0:/BOOT/kernel.elf"
    );

    TitanBootPath_Add(
        candidates,
        capacity,
        &count,
        "host:kernel.elf"
    );

    return count;
}
