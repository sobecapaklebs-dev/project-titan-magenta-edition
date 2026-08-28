#ifndef PROJECT_TITAN_STORAGE_PFS_IOP_H
#define PROJECT_TITAN_STORAGE_PFS_IOP_H

#include <stdint.h>

typedef struct TitanStoragePfsIopImage
{
    const char *module_name;
    const unsigned char *data;
    uint32_t size;
} TitanStoragePfsIopImage;

typedef struct TitanStoragePfsIopBackend
{
    int (*module_resident)(void *context, const char *module_name);
    int (*exec_module)(void *context, const unsigned char *data, uint32_t size);
    int (*devices_ready)(void *context);
} TitanStoragePfsIopBackend;

/* Ensures dependency order without re-executing resident modules. Images are
 * DEV9, ATAD, APA/HDD and PFS in that order. Returns true only when hdd+pfs
 * devices are visible after the sequence. */
int TitanStoragePfsIop_Ensure(
    const TitanStoragePfsIopBackend *backend,
    void *context,
    const TitanStoragePfsIopImage images[4]
);

#endif
