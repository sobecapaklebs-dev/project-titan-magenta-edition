#ifndef PROJECT_TITAN_VMC_H
#define PROJECT_TITAN_VMC_H

#include "titan_user_profile.h"

#define TITAN_VMC_SIZE_BYTES (8u * 1024u * 1024u)

typedef enum TitanVmcStatus
{
    TITAN_VMC_STATUS_MISSING = 0,
    TITAN_VMC_STATUS_READY,
    TITAN_VMC_STATUS_NEEDS_FORMAT,
    TITAN_VMC_STATUS_INVALID_SIZE,
    TITAN_VMC_STATUS_CORRUPT,
    TITAN_VMC_STATUS_IO_ERROR,
    TITAN_VMC_STATUS_CHECKING
} TitanVmcStatus;

#define TITAN_VMC_DIRECTORY_NAME_CAPACITY 32u
#define TITAN_VMC_DIRECTORY_ENTRY_DIRECTORY 0x00000001u
#define TITAN_VMC_DIRECTORY_ENTRY_READABLE  0x00000002u
#define TITAN_VMC_DIRECTORY_ENTRY_WRITABLE  0x00000004u
#define TITAN_VMC_DIRECTORY_ENTRY_HIDDEN    0x00000008u

typedef struct TitanVmcDirectoryEntry
{
    char name[TITAN_VMC_DIRECTORY_NAME_CAPACITY];
    unsigned int size;
    unsigned int flags;
} TitanVmcDirectoryEntry;

typedef enum TitanVmcResult
{
    TITAN_VMC_RESULT_OK = 0,
    TITAN_VMC_RESULT_CREATED,
    TITAN_VMC_RESULT_FORMATTED,
    TITAN_VMC_RESULT_NEEDS_FORMAT,
    TITAN_VMC_RESULT_INVALID,
    TITAN_VMC_RESULT_NO_STORAGE,
    TITAN_VMC_RESULT_IO_ERROR
} TitanVmcResult;

TitanVmcStatus TitanVmc_GetStatus(
    const TitanUserProfile *profile
);

/* Cached status never performs storage I/O and is safe to call from Draw(). */
TitanVmcStatus TitanVmc_GetCachedStatus(
    const TitanUserProfile *profile
);

/* Refreshes the lightweight readiness probe (size + superblock + root). */
TitanVmcStatus TitanVmc_RefreshStatus(
    const TitanUserProfile *profile
);

/* Full filesystem consistency check. Use only for explicit user verification. */
TitanVmcStatus TitanVmc_Verify(
    const TitanUserProfile *profile
);

void TitanVmc_InvalidateCachedStatus(
    const TitanUserProfile *profile
);

TitanVmcResult TitanVmc_Ensure(
    const TitanUserProfile *profile
);

TitanVmcResult TitanVmc_Recreate(
    const TitanUserProfile *profile
);

TitanVmcResult TitanVmc_Format(
    const TitanUserProfile *profile
);

/* Bounded read-only inspection of the active PS2 memory-card filesystem image.
 * Returns 1 for an existing root entry, 0 at end-of-list and -1 on invalid or
 * corrupt storage. This does not mount a vmc: device or expose raw filesystem
 * ownership outside Titan. */
int TitanVmc_ListRoot(
    const TitanUserProfile *profile,
    unsigned int visible_index,
    TitanVmcDirectoryEntry *entry
);

/* Lists one visible entry in a normalized relative directory path. Paths are
 * rooted inside the active VMC image, may contain at most 8 components and
 * cannot use absolute, dot or parent traversal syntax. */
int TitanVmc_ListDirectory(
    const TitanUserProfile *profile,
    const char *relative_path,
    unsigned int visible_index,
    TitanVmcDirectoryEntry *entry
);

/* Reads a bounded byte range from one VMC-internal regular file. Returns the
 * byte count, 0 at/after EOF, or -1 for invalid/corrupt paths/chains. */
int TitanVmc_ReadFile(
    const TitanUserProfile *profile,
    const char *relative_path,
    unsigned int offset,
    void *buffer,
    unsigned int capacity
);

int TitanVmc_BuildNeutrinoArgument(
    const TitanUserProfile *profile,
    int port,
    char *destination,
    unsigned int capacity
);

const char *TitanVmc_GetStatusName(
    TitanVmcStatus status
);

#endif
