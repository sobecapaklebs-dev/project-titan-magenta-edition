#ifndef PROJECT_TITAN_BOOT_PATH_H
#define PROJECT_TITAN_BOOT_PATH_H

#include <stddef.h>

#define TITAN_BOOT_MAX_PATH 256u
#define TITAN_BOOT_MAX_CANDIDATES 40u

size_t TitanBootPath_BuildCandidates(
    const char *executable_path,
    const char *override_path,
    char candidates[][TITAN_BOOT_MAX_PATH],
    size_t capacity
);

#endif
