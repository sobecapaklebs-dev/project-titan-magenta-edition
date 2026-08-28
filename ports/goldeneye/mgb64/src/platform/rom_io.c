/**
 * rom_io.c — ROM file loading and PC-specific DMA implementation.
 *
 * On N64, ROM data is accessed via the PI (Parallel Interface) using DMA.
 * On PC, we load the entire .z64 ROM into a malloc'd buffer and serve
 * all DMA requests via memcpy from that buffer.
 */
#include <ultra64.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rom_io.h"

/* ===== ROM buffer ===== */
u8  *g_romData = NULL;
u32  g_romSize = 0;
static PlatformRomReadFn g_romReader = NULL;
static void *g_romReaderContext = NULL;

void platformSetRomReader(
    PlatformRomReadFn reader, void *context, u32 rom_size
)
{
    g_romReader = reader;
    g_romReaderContext = context;
    g_romSize = rom_size;
}

int platformRomRead(u32 offset, void *destination, u32 bytes)
{
    if (destination == NULL || bytes == 0 || offset > g_romSize ||
        bytes > g_romSize - offset)
        return 0;
    if (g_romReader != NULL)
        return g_romReader(g_romReaderContext, offset, destination, bytes);
    if (g_romData == NULL) return 0;
    memcpy(destination, g_romData + offset, bytes);
    return 1;
}

/* GoldenEye 007 is a 12 MB (96 Mbit) cartridge in every region (U/E/J). */
#define GE007_ROM_SIZE_BYTES 0xC00000

/**
 * Load the .z64 ROM file into memory.
 */
int platformInitRom(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[ROM] Failed to open: %s\n", path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    if (size <= 0) {
        fprintf(stderr, "[ROM] Could not determine size of: %s\n", path);
        fclose(f);
        return -1;
    }

    /* GoldenEye 007 is a 12 MB (96 Mbit) cartridge in every region (U/E/J), so a
     * valid dump is exactly this size in any byte order. Anything else is the
     * wrong game, an over-dump with a copier header, or a truncated file — all of
     * which would otherwise sail past here and then read out of bounds when the
     * hardcoded file-table offsets (up to ~9.4 MB) are DMA'd. Fail loudly instead. */
    if (size != GE007_ROM_SIZE_BYTES) {
        fprintf(stderr,
                "[ROM] %s is %ld bytes, but a GoldenEye 007 ROM must be exactly "
                "%d bytes (12 MB).\n"
                "[ROM] This does not look like a valid ROM (wrong game, a headered/"
                "over-dumped file, or a truncated download).\n"
                "[ROM] Expected SHA-1s are in ge007.u.sha1 / ge007.e.sha1 / "
                "ge007.j.sha1.\n",
                path, size, GE007_ROM_SIZE_BYTES);
        fclose(f);
        return -1;
    }

    g_romData = (u8 *)malloc((size_t)size);
    if (!g_romData) {
        fprintf(stderr, "[ROM] Failed to allocate %ld bytes\n", size);
        fclose(f);
        return -1;
    }

    size_t read = fread(g_romData, 1, (size_t)size, f);
    fclose(f);

    if ((long)read != size) {
        fprintf(stderr, "[ROM] Short read: %zu of %ld bytes\n", read, size);
        free(g_romData);
        g_romData = NULL;
        return -1;
    }

    g_romSize = (u32)size;
    g_romReader = NULL;
    g_romReaderContext = NULL;

    /* Detect byte order. N64 ROMs come in 3 formats:
     * .z64 (big-endian):    80 37 12 40
     * .v64 (byte-swapped):  37 80 40 12
     * .n64 (little-endian): 40 12 37 80
     * We need big-endian (.z64 format). */
    if (g_romData[0] == 0x37 && g_romData[1] == 0x80) {
        /* .v64 — swap every 2 bytes */
        fprintf(stderr, "[ROM] Detected .v64 byte-swapped format, converting...\n");
        for (u32 i = 0; i + 1 < g_romSize; i += 2) {
            u8 tmp = g_romData[i];
            g_romData[i] = g_romData[i + 1];
            g_romData[i + 1] = tmp;
        }
    } else if (g_romData[0] == 0x40 && g_romData[1] == 0x12) {
        /* .n64 — swap every 4 bytes */
        fprintf(stderr, "[ROM] Detected .n64 little-endian format, converting...\n");
        for (u32 i = 0; i + 3 < g_romSize; i += 4) {
            u8 a = g_romData[i], b = g_romData[i+1];
            u8 c = g_romData[i+2], d = g_romData[i+3];
            g_romData[i] = d; g_romData[i+1] = c;
            g_romData[i+2] = b; g_romData[i+3] = a;
        }
    }

    /* After byte-order normalization the header must be the N64 big-endian magic
     * (80 37 12 40). A 12 MB file that is not an N64 ROM at all (e.g. a zero-
     * filled or corrupt image) previously slipped through with only a warning,
     * then had the hardcoded US file-table offsets patched onto it and faulted in
     * resource decompression at frame 0. Reject here — before platformPatchFileTable,
     * SDL, audio, or the renderer run — so a wrong file exits cleanly (AUDIT-0005). */
    if (g_romData[0] != 0x80 || g_romData[1] != 0x37 ||
        g_romData[2] != 0x12 || g_romData[3] != 0x40) {
        fprintf(stderr,
                "[ROM] %s is not a big-endian N64 ROM (header %02x %02x %02x %02x, "
                "expected 80 37 12 40). Refusing to boot.\n",
                path, g_romData[0], g_romData[1], g_romData[2], g_romData[3]);
        free(g_romData);
        g_romData = NULL;
        g_romSize = 0;
        return -1;
    }

    /* The internal cartridge title must be GoldenEye. A valid 12 MB N64 ROM of a
     * DIFFERENT game would otherwise get GoldenEye's US file offsets patched onto
     * it and crash the same way. Scan the 0x20..0x34 title window (as
     * looksLikeGoldeneyeRom does) to tolerate regional title placement. */
    {
        int is_goldeneye = 0;
        for (int i = 0x20; i + 9 <= 0x34; i++) {
            if (memcmp(&g_romData[i], "GOLDENEYE", 9) == 0) {
                is_goldeneye = 1;
                break;
            }
        }
        if (!is_goldeneye) {
            char title[21];
            memcpy(title, &g_romData[0x20], 20);
            title[20] = '\0';
            fprintf(stderr,
                    "[ROM] %s internal title is \"%s\" — not a GoldenEye 007 ROM. "
                    "Refusing to boot.\n",
                    path, title);
            free(g_romData);
            g_romData = NULL;
            g_romSize = 0;
            return -1;
        }
    }

    printf("[ROM] Loaded %u bytes (%.1f MB) from %s\n",
           g_romSize, (float)g_romSize / (1024.0f * 1024.0f), path);

    return 0;
}
