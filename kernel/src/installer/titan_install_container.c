#include "titan_install_container.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

static u32 read_le32(const unsigned char *p)
{
    return ((u32)p[0]) |
           ((u32)p[1] << 8) |
           ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

static int read_exact(int fd, void *buffer, unsigned int length)
{
    unsigned char *out = (unsigned char *)buffer;
    unsigned int done = 0u;
    while (done < length) {
        int got = read(fd, out + done, length - done);
        if (got <= 0) return 0;
        done += (unsigned int)got;
    }
    return 1;
}

int TitanInstallContainer_ReadHeader(int fd, TitanInstallContainerHeader *header)
{
    unsigned char raw[TITAN_TINS_HEADER_SIZE];
    u32 format;
    u32 header_size;
    u32 entry_size;

    if (fd < 0 || header == 0) return 0;
    /* Hardware contract: callers open a fresh descriptor positioned at byte 0.
     * Do not lseek() USB streams here. Project Titan already learned on real PS2
     * hardware (TPUP BuildFix25) that seek can corrupt/fail otherwise valid
     * sequential mass: reads. */
    if (!read_exact(fd, raw, sizeof(raw))) return 0;
    if (memcmp(raw, TITAN_TINS_MAGIC, 4u) != 0) return 0;

    format = read_le32(raw + 4u);
    header_size = read_le32(raw + 8u);
    entry_size = read_le32(raw + 12u);
    if (format != TITAN_TINS_FORMAT ||
        header_size != TITAN_TINS_HEADER_SIZE ||
        entry_size != TITAN_TINS_ENTRY_SIZE)
        return 0;

    memset(header, 0, sizeof(*header));
    header->file_count = read_le32(raw + 16u);
    header->flags = read_le32(raw + 20u);
    header->table_offset = read_le32(raw + 24u);
    header->payload_offset = read_le32(raw + 28u);
    header->total_size = read_le32(raw + 32u);
    header->table_crc32 = read_le32(raw + 36u);

    if (header->file_count == 0u ||
        header->file_count > TITAN_TINS_MAX_FILES ||
        header->table_offset != TITAN_TINS_HEADER_SIZE ||
        header->payload_offset != TITAN_TINS_HEADER_SIZE + header->file_count * TITAN_TINS_ENTRY_SIZE ||
        header->total_size < header->payload_offset)
        return 0;

    return 1;
}

int TitanInstallContainer_ParseEntryRaw(
    const unsigned char raw[TITAN_TINS_ENTRY_SIZE],
    TitanInstallContainerEntry *entry
)
{
    u32 path_length;

    if (raw == 0 || entry == 0) return 0;
    memset(entry, 0, sizeof(*entry));
    entry->kind = read_le32(raw + 0u);
    entry->flags = read_le32(raw + 4u);
    entry->payload_offset = read_le32(raw + 8u);
    entry->size = read_le32(raw + 12u);
    entry->crc32 = read_le32(raw + 16u);
    path_length = read_le32(raw + 20u);

    if (path_length == 0u || path_length >= TITAN_TINS_PATH_BYTES) return 0;
    {
        unsigned int i;
        for (i = 0u; i < path_length; i++)
            if (raw[24u + i] == 0u) return 0;
    }
    memcpy(entry->path, raw + 24u, path_length);
    entry->path[path_length] = '\0';
    return 1;
}

int TitanInstallContainer_ReadEntry(
    int fd,
    const TitanInstallContainerHeader *header,
    unsigned int index,
    TitanInstallContainerEntry *entry
)
{
    unsigned char raw[TITAN_TINS_ENTRY_SIZE];
    unsigned int offset;

    if (fd < 0 || header == 0 || entry == 0 || index >= header->file_count) return 0;
    offset = header->table_offset + index * TITAN_TINS_ENTRY_SIZE;
    if (lseek(fd, (off_t)offset, SEEK_SET) < 0) return 0;
    if (!read_exact(fd, raw, sizeof(raw))) return 0;
    return TitanInstallContainer_ParseEntryRaw(raw, entry);
}

int TitanInstallContainer_ValidateTable(
    int fd,
    const TitanInstallContainerHeader *header,
    unsigned int (*crc_update)(unsigned int crc, const unsigned char *data, unsigned int length)
)
{
    unsigned char buffer[1024];
    unsigned int remaining;
    unsigned int crc = 0u;

    if (fd < 0 || header == 0 || crc_update == 0) return 0;
    remaining = header->file_count * TITAN_TINS_ENTRY_SIZE;
    if (lseek(fd, (off_t)header->table_offset, SEEK_SET) < 0) return 0;

    while (remaining > 0u) {
        unsigned int want = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
        int got = read(fd, buffer, want);
        if (got <= 0) return 0;
        crc = crc_update(crc, buffer, (unsigned int)got);
        remaining -= (unsigned int)got;
    }
    return crc == header->table_crc32;
}

int TitanInstallContainer_ValidateBounds(
    const TitanInstallContainerHeader *header,
    const TitanInstallContainerEntry *entry
)
{
    u32 payload_size;
    if (header == 0 || entry == 0) return 0;
    payload_size = header->total_size - header->payload_offset;
    if (entry->payload_offset > payload_size) return 0;
    if (entry->size > payload_size - entry->payload_offset) return 0;
    return 1;
}
