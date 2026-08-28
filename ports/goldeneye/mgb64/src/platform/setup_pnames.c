/*
 * setup_pnames.c — see setup_pnames.h (FID-0037).
 */
#include "setup_pnames.h"

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

uint32_t setupPnamesTableOffset(const uint8_t *base, int index, int legacy)
{
    if (base == NULL || legacy) {
        /* legacy: the port defect (prop.c:2538) left the table NULL. */
        return 0;
    }
    return be32(base + (uint32_t)index * 4u);
}

size_t setupPnamesCount(const uint8_t *base, uint32_t table_off, size_t limit)
{
    size_t n = 0;

    if (base == NULL || table_off == 0) {
        return 0;
    }
    while (n < limit) {
        if (be32(base + table_off + (uint32_t)(n * 4u)) == 0) {
            break; /* zero entry terminates the table */
        }
        n++;
    }
    return n;
}

const char *setupPnamesResolve(const uint8_t *base, uint32_t table_off, size_t i)
{
    uint32_t off;

    if (base == NULL || table_off == 0) {
        return NULL;
    }
    off = be32(base + table_off + (uint32_t)(i * 4u));
    if (off == 0) {
        return NULL;
    }
    return (const char *)(base + off);
}
