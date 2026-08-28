#include "titan_ge_rom.h"

#include <string.h>

static void TitanGeRom_Normalize(
    unsigned char *bytes,
    unsigned int count,
    TitanGeRomOrder order
)
{
    unsigned int index;
    if (order == TITAN_GE_ROM_ORDER_V64)
    {
        for (index = 0u; index + 1u < count; index += 2u)
        {
            unsigned char temporary = bytes[index];
            bytes[index] = bytes[index + 1u];
            bytes[index + 1u] = temporary;
        }
    }
    else if (order == TITAN_GE_ROM_ORDER_N64)
    {
        for (index = 0u; index + 3u < count; index += 4u)
        {
            unsigned char a = bytes[index];
            unsigned char b = bytes[index + 1u];
            bytes[index] = bytes[index + 3u];
            bytes[index + 1u] = bytes[index + 2u];
            bytes[index + 2u] = b;
            bytes[index + 3u] = a;
        }
    }
}

static int TitanGeRom_ReadExact(
    TitanGeRom *rom,
    unsigned int offset,
    void *destination,
    unsigned int bytes
)
{
    unsigned char *cursor = (unsigned char *)destination;
    unsigned int remaining = bytes;
    if (rom->api->storage_seek(
            rom->descriptor, (int64_t)offset, TITAN_GAME_STORAGE_SEEK_SET
        ) != (int64_t)offset)
        return 0;
    while (remaining != 0u)
    {
        int64_t result = rom->api->storage_read(
            rom->descriptor, cursor, remaining
        );
        if (result <= 0 || (uint64_t)result > remaining) return 0;
        cursor += (unsigned int)result;
        remaining -= (unsigned int)result;
    }
    return 1;
}

static int TitanGeRom_IsGoldenEyeHeader(const unsigned char *header)
{
    unsigned int index;
    if (header[0] != 0x80u || header[1] != 0x37u ||
        header[2] != 0x12u || header[3] != 0x40u)
        return 0;
    for (index = 0x20u; index + 9u <= 0x34u; index++)
        if (memcmp(&header[index], "GOLDENEYE", 9u) == 0)
            return 1;
    return 0;
}

static TitanGeRomPage *TitanGeRom_LoadPage(
    TitanGeRom *rom,
    unsigned int page_base
)
{
    TitanGeRomPage *page = 0;
    unsigned int index;
    unsigned int oldest_stamp = 0xffffffffu;
    unsigned int bytes;

    for (index = 0u; index < TITAN_GE_ROM_PAGE_COUNT; index++)
    {
        if (rom->pages[index].valid && rom->pages[index].base == page_base)
        {
            rom->pages[index].stamp = ++rom->stamp;
            return &rom->pages[index];
        }
        if (!rom->pages[index].valid)
        {
            page = &rom->pages[index];
            break;
        }
        if (rom->pages[index].stamp < oldest_stamp)
        {
            oldest_stamp = rom->pages[index].stamp;
            page = &rom->pages[index];
        }
    }
    if (page == 0 || page->bytes == 0) return 0;
    bytes = TITAN_GE_ROM_SIZE_BYTES - page_base;
    if (bytes > TITAN_GE_ROM_PAGE_BYTES) bytes = TITAN_GE_ROM_PAGE_BYTES;
    if (!TitanGeRom_ReadExact(rom, page_base, page->bytes, bytes)) return 0;
    TitanGeRom_Normalize(page->bytes, bytes, rom->order);
    page->base = page_base;
    page->valid_bytes = bytes;
    page->stamp = ++rom->stamp;
    page->valid = 1;
    return page;
}

int TitanGeRom_Open(
    TitanGeRom *rom,
    const TitanGameApiV1 *api,
    const char *path
)
{
    TitanGameStorageEntry entry;
    unsigned char header[64];
    unsigned int index;

    if (rom == 0 || api == 0 || path == 0 ||
        api->abi_version < 2u || api->storage_stat == 0 ||
        api->storage_open == 0 || api->storage_read == 0 ||
        api->storage_seek == 0 || api->storage_close == 0 ||
        api->alloc == 0 || api->release == 0)
        return 0;
    memset(rom, 0, sizeof(*rom));
    rom->descriptor = -1;
    if (!api->storage_stat(path, &entry) ||
        (entry.flags & TITAN_GAME_STORAGE_ENTRY_DIRECTORY) != 0u ||
        entry.size != TITAN_GE_ROM_SIZE_BYTES)
        return 0;
    rom->api = api;
    rom->descriptor = api->storage_open(path, TITAN_GAME_STORAGE_READ);
    if (rom->descriptor < 0) goto fail;
    if (!TitanGeRom_ReadExact(rom, 0u, header, sizeof(header))) goto fail;
    if (header[0] == 0x80u && header[1] == 0x37u)
        rom->order = TITAN_GE_ROM_ORDER_Z64;
    else if (header[0] == 0x37u && header[1] == 0x80u)
        rom->order = TITAN_GE_ROM_ORDER_V64;
    else if (header[0] == 0x40u && header[1] == 0x12u)
        rom->order = TITAN_GE_ROM_ORDER_N64;
    else
        goto fail;
    TitanGeRom_Normalize(header, sizeof(header), rom->order);
    if (!TitanGeRom_IsGoldenEyeHeader(header)) goto fail;

    for (index = 0u; index < TITAN_GE_ROM_PAGE_COUNT; index++)
    {
        rom->pages[index].bytes = (unsigned char *)api->alloc(
            TITAN_GE_ROM_PAGE_BYTES, 64u
        );
        if (rom->pages[index].bytes == 0) goto fail;
    }
    return 1;

fail:
    TitanGeRom_Close(rom);
    return 0;
}

void TitanGeRom_Close(TitanGeRom *rom)
{
    unsigned int index;
    if (rom == 0) return;
    if (rom->api != 0)
    {
        if (rom->descriptor >= 0 && rom->api->storage_close != 0)
            (void)rom->api->storage_close(rom->descriptor);
        for (index = 0u; index < TITAN_GE_ROM_PAGE_COUNT; index++)
            if (rom->pages[index].bytes != 0)
                rom->api->release(rom->pages[index].bytes);
    }
    memset(rom, 0, sizeof(*rom));
    rom->descriptor = -1;
}

int TitanGeRom_Read(
    TitanGeRom *rom,
    unsigned int offset,
    void *destination,
    unsigned int bytes
)
{
    unsigned char *output = (unsigned char *)destination;
    unsigned int remaining = bytes;
    if (rom == 0 || rom->api == 0 || rom->descriptor < 0 ||
        destination == 0 || bytes == 0u ||
        offset > TITAN_GE_ROM_SIZE_BYTES ||
        bytes > TITAN_GE_ROM_SIZE_BYTES - offset)
        return 0;
    while (remaining != 0u)
    {
        unsigned int page_base = offset & ~(TITAN_GE_ROM_PAGE_BYTES - 1u);
        unsigned int in_page = offset - page_base;
        unsigned int amount;
        TitanGeRomPage *page = TitanGeRom_LoadPage(rom, page_base);
        if (page == 0 || in_page >= page->valid_bytes) return 0;
        amount = page->valid_bytes - in_page;
        if (amount > remaining) amount = remaining;
        memcpy(output, &page->bytes[in_page], amount);
        output += amount;
        offset += amount;
        remaining -= amount;
    }
    return 1;
}
