#include "titan_ge_rom.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static TitanGeRomOrder g_order;
static unsigned int g_position;

static unsigned char canonical_byte(unsigned int offset)
{
    static const unsigned char magic[4] = {0x80u,0x37u,0x12u,0x40u};
    static const char title[] = "GOLDENEYE";
    if (offset < 4u) return magic[offset];
    if (offset >= 0x20u && offset < 0x20u + 9u)
        return (unsigned char)title[offset - 0x20u];
    return (unsigned char)((offset * 37u + 11u) & 0xffu);
}

static unsigned char file_byte(unsigned int offset)
{
    unsigned int canonical_offset = offset;
    if (g_order == TITAN_GE_ROM_ORDER_V64) canonical_offset ^= 1u;
    if (g_order == TITAN_GE_ROM_ORDER_N64) canonical_offset ^= 3u;
    return canonical_byte(canonical_offset);
}

static void *fake_alloc(uint32_t bytes,uint32_t alignment)
{
    void *pointer = 0;
    assert(alignment >= sizeof(void *));
    if (posix_memalign(&pointer, alignment, bytes) != 0) return 0;
    return pointer;
}
static void fake_release(void *pointer){ free(pointer); }
static int fake_stat(const char *path,TitanGameStorageEntry *entry)
{
    (void)path; memset(entry,0,sizeof(*entry)); entry->size=TITAN_GE_ROM_SIZE_BYTES; return 1;
}
static int fake_open(const char *path,uint32_t flags)
{ (void)path; return flags==TITAN_GAME_STORAGE_READ?7:-1; }
static int fake_close(int descriptor){ return descriptor==7; }
static int64_t fake_seek(int descriptor,int64_t offset,int origin)
{
    if(descriptor!=7||origin!=TITAN_GAME_STORAGE_SEEK_SET||offset<0||offset>TITAN_GE_ROM_SIZE_BYTES)return -1;
    g_position=(unsigned int)offset; return offset;
}
static int64_t fake_read(int descriptor,void *buffer,uint32_t bytes)
{
    unsigned char *output=(unsigned char *)buffer;
    unsigned int index;
    unsigned int amount=bytes>4093u?4093u:bytes;
    if(descriptor!=7||g_position>TITAN_GE_ROM_SIZE_BYTES||amount>TITAN_GE_ROM_SIZE_BYTES-g_position)return -1;
    for(index=0u;index<amount;index++) output[index]=file_byte(g_position+index);
    g_position+=amount; return amount;
}

static void run_order(TitanGeRomOrder order)
{
    TitanGameApiV1 api;
    TitanGeRom rom;
    unsigned char sample[512];
    unsigned int index;
    memset(&api,0,sizeof(api));
    api.abi_version=TITAN_GAME_API_VERSION;
    api.struct_size=sizeof(api);
    api.alloc=fake_alloc; api.release=fake_release;
    api.storage_stat=fake_stat; api.storage_open=fake_open; api.storage_close=fake_close;
    api.storage_seek=fake_seek; api.storage_read=fake_read;
    g_order=order; g_position=0u;
    assert(TitanGeRom_Open(&rom,&api,"mass:/TITAN/games/titaneye/goldeneye.rom"));
    assert(rom.order==order);
    assert(TitanGeRom_Read(&rom,TITAN_GE_ROM_PAGE_BYTES-117u,sample,sizeof(sample)));
    for(index=0u;index<sizeof(sample);index++)
        assert(sample[index]==canonical_byte(TITAN_GE_ROM_PAGE_BYTES-117u+index));
    assert(!TitanGeRom_Read(&rom,TITAN_GE_ROM_SIZE_BYTES-4u,sample,8u));
    TitanGeRom_Close(&rom);
    assert(rom.descriptor==-1);
}

int main(void)
{
    run_order(TITAN_GE_ROM_ORDER_Z64);
    run_order(TITAN_GE_ROM_ORDER_V64);
    run_order(TITAN_GE_ROM_ORDER_N64);
    puts("titan-ge-rom: OK z64/v64/n64 streaming cache");
    return 0;
}
