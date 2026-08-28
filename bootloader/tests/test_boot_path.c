#include "boot_path.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int Contains(
    char candidates[][TITAN_BOOT_MAX_PATH],
    size_t count,
    const char *expected
)
{
    size_t index;

    for (index = 0u; index < count; index++)
    {
        if (strcmp(candidates[index], expected) == 0)
        {
            return 1;
        }
    }

    return 0;
}

int main(void)
{
    char candidates[
        TITAN_BOOT_MAX_CANDIDATES
    ][TITAN_BOOT_MAX_PATH];
    size_t count;

    count = TitanBootPath_BuildCandidates(
        "mass:/TITAN/titan.elf",
        0,
        candidates,
        TITAN_BOOT_MAX_CANDIDATES
    );

    assert(count >= 7u);
    assert(strcmp(candidates[0], "mass:/kernel.elf") == 0);
    assert(strcmp(candidates[1], "mass0:/kernel.elf") == 0);
    assert(Contains(candidates, count, "mass:/TITAN/kernel.elf"));
    assert(Contains(candidates, count, "mass0:/TITAN/kernel.elf"));
    assert(Contains(candidates, count, "mass:/TITAN/kernel/kernel.elf"));
    assert(Contains(candidates, count, "host:kernel.elf"));

    count = TitanBootPath_BuildCandidates(
        "host:titan.elf",
        "mass1:/CUSTOM/KERNEL.ELF",
        candidates,
        TITAN_BOOT_MAX_CANDIDATES
    );

    assert(count > 4u);
    assert(strcmp(candidates[0], "mass1:/CUSTOM/KERNEL.ELF") == 0);
    assert(strcmp(candidates[1], "mass:/kernel.elf") == 0);
    assert(Contains(candidates, count, "host:kernel.elf"));

    puts("test_boot_path: PASS");
    return 0;
}
