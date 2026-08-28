/**
 * rom_io.h — ROM file I/O for the PC port.
 *
 * Loads the N64 .z64 ROM into memory and provides the infrastructure
 * for romCopy() and osPiStartDma() to read from it.
 */
#ifndef ROM_IO_H
#define ROM_IO_H

#include <ultra64.h>

/* The loaded ROM image */
extern u8  *g_romData;
extern u32  g_romSize;

/* Backend-neutral random-access ROM lane. Titan binds this to the bounded
 * four-page TGR cache instead of allocating the complete 12 MiB cartridge. */
typedef int (*PlatformRomReadFn)(
    void *context, u32 offset, void *destination, u32 bytes
);
void platformSetRomReader(
    PlatformRomReadFn reader, void *context, u32 rom_size
);
int platformRomRead(u32 offset, void *destination, u32 bytes);

/**
 * Open and load the .z64 ROM file into memory.
 * Returns 0 on success, -1 on failure.
 */
int platformInitRom(const char *path);

/**
 * Patch file_resource_table hw_address pointers to reference
 * correct offsets within the loaded ROM buffer.
 * (Implemented in rom_offsets.c, generated from N64 ELF.)
 */
void platformPatchFileTable(u8 *romData);

/**
 * Initialize segment symbol values to real ROM offsets
 * extracted from the N64 ELF.
 */
void platformInitSegmentOffsets(void);

#endif /* ROM_IO_H */
