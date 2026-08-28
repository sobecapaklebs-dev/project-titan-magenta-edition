#ifndef PROJECT_TITAN_GOLDENEYE_ROM_H
#define PROJECT_TITAN_GOLDENEYE_ROM_H

#include "../../kernel/include/titan_game_sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TITAN_GE_ROM_SIZE_BYTES 0x00c00000u
#define TITAN_GE_ROM_PAGE_BYTES 65536u
#define TITAN_GE_ROM_PAGE_COUNT 4u

typedef enum TitanGeRomOrder
{
    TITAN_GE_ROM_ORDER_UNKNOWN = 0,
    TITAN_GE_ROM_ORDER_Z64 = 1,
    TITAN_GE_ROM_ORDER_V64 = 2,
    TITAN_GE_ROM_ORDER_N64 = 3
} TitanGeRomOrder;

typedef struct TitanGeRomPage
{
    unsigned char *bytes;
    unsigned int base;
    unsigned int valid_bytes;
    unsigned int stamp;
    int valid;
} TitanGeRomPage;

typedef struct TitanGeRom
{
    const TitanGameApiV1 *api;
    int descriptor;
    TitanGeRomOrder order;
    unsigned int stamp;
    TitanGeRomPage pages[TITAN_GE_ROM_PAGE_COUNT];
} TitanGeRom;

int TitanGeRom_Open(
    TitanGeRom *rom,
    const TitanGameApiV1 *api,
    const char *path
);
void TitanGeRom_Close(TitanGeRom *rom);
int TitanGeRom_Read(
    TitanGeRom *rom,
    unsigned int offset,
    void *destination,
    unsigned int bytes
);

#ifdef __cplusplus
}
#endif

#endif
