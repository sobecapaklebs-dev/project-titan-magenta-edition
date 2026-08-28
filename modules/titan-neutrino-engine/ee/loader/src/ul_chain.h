#ifndef UL_CHAIN_H
#define UL_CHAIN_H

#include <stdint.h>

#define UL_CHAIN_MAX_PARTS 255u
#define UL_CHAIN_PATH_CAPACITY 256u
#define UL_CHAIN_TITLE_CAPACITY 33u
#define UL_CHAIN_STARTUP_CAPACITY 13u
#define UL_CHAIN_ELF_CAPACITY 40u

typedef struct UlChain
{
    char base_path[UL_CHAIN_PATH_CAPACITY];
    char title[UL_CHAIN_TITLE_CAPACITY];
    char startup[UL_CHAIN_STARTUP_CAPACITY];
    char elf_path[UL_CHAIN_ELF_CAPACITY];
    unsigned int part_count;
    unsigned int media;
    uint64_t part_sizes[UL_CHAIN_MAX_PARTS];
    uint64_t total_size;
} UlChain;

/* Legacy parser: <base path>,<part count>,<media byte>.
 * Example: usb:ul.89ABCDEF.SLUS_123.45,4,20
 */
int UlChain_Parse(const char *specification, UlChain *chain);

/* Native USBExtreme parser:
 * <ul.cfg path>,<startup>,<data-device prefix>
 * Example: mass:/ul.cfg,SLUS_123.45,usb:
 *
 * Reads the matching 64-byte ul.cfg record itself, preserves the exact
 * 32-byte title used by USBExtreme CRC32, and builds the physical part base.
 */
int UlChain_LoadConfig(const char *specification, UlChain *chain);

/* Build the USBExtreme slice filename: <base path>.<two-digit hex part>. */
int UlChain_BuildPartPath(
    const UlChain *chain,
    unsigned int part_index,
    char *destination,
    unsigned int capacity
);

/* Open every slice and cache its length. */
int UlChain_Probe(UlChain *chain);

/* Read from the concatenated logical image, crossing slice boundaries. */
int UlChain_ReadAt(
    const UlChain *chain,
    uint64_t logical_offset,
    void *destination,
    unsigned int size
);

#endif
