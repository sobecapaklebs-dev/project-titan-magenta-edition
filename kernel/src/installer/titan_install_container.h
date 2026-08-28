#ifndef PROJECT_TITAN_INSTALL_CONTAINER_H
#define PROJECT_TITAN_INSTALL_CONTAINER_H

#include <tamtypes.h>

#define TITAN_TINS_MAGIC "TINS"
#define TITAN_TINS_FORMAT 1u
#define TITAN_TINS_HEADER_SIZE 64u
#define TITAN_TINS_ENTRY_SIZE 224u
#define TITAN_TINS_PATH_BYTES 192u
#define TITAN_TINS_MAX_FILES 192u
#define TITAN_TINS_FLAG_AUTONOMOUS 0x00000001u

#define TITAN_TINS_KIND_SYSTEM 1u
#define TITAN_TINS_KIND_BOOT 2u
#define TITAN_TINS_KIND_EXPLOIT 3u /* historical; not a BF30 FMCB release path */
#define TITAN_TINS_KIND_BOOTSTRAP 4u

typedef struct TitanInstallContainerHeader
{
    u32 file_count;
    u32 flags;
    u32 table_offset;
    u32 payload_offset;
    u32 total_size;
    u32 table_crc32;
} TitanInstallContainerHeader;

typedef struct TitanInstallContainerEntry
{
    u32 kind;
    u32 flags;
    u32 payload_offset;
    u32 size;
    u32 crc32;
    char path[TITAN_TINS_PATH_BYTES];
} TitanInstallContainerEntry;

int TitanInstallContainer_ReadHeader(int fd, TitanInstallContainerHeader *header);
int TitanInstallContainer_ReadEntry(
    int fd,
    const TitanInstallContainerHeader *header,
    unsigned int index,
    TitanInstallContainerEntry *entry
);
int TitanInstallContainer_ParseEntryRaw(
    const unsigned char raw[TITAN_TINS_ENTRY_SIZE],
    TitanInstallContainerEntry *entry
);
int TitanInstallContainer_ValidateTable(
    int fd,
    const TitanInstallContainerHeader *header,
    unsigned int (*crc_update)(unsigned int crc, const unsigned char *data, unsigned int length)
);
int TitanInstallContainer_ValidateBounds(
    const TitanInstallContainerHeader *header,
    const TitanInstallContainerEntry *entry
);

#endif
