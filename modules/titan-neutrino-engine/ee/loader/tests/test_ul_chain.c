#include "ul_chain.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int write_part(const char *path, const char *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    if (fwrite(data, 1, size, f) != size) { fclose(f); return 0; }
    return fclose(f) == 0;
}

static int write_ul_cfg(void)
{
    unsigned char entry[64];
    FILE *f;

    memset(entry, 0, sizeof(entry));
    memcpy(&entry[0], "Marvel vs. capcom 2 - new age of", 32);
    memcpy(&entry[32], "ul.", 3);
    memcpy(&entry[35], "SLUS_204.86", 11);
    entry[47] = 1;
    entry[48] = 0x12;

    f = fopen("/tmp/titan-ul.cfg", "wb");
    if (!f) return 0;
    if (fwrite(entry, 1, sizeof(entry), f) != sizeof(entry)) {
        fclose(f);
        return 0;
    }
    return fclose(f) == 0;
}

int main(void)
{
    UlChain chain;
    char output[12];
    char path[300];
    const char *base = "/tmp/titan-ul-test";

    if (!write_part("/tmp/titan-ul-test.00", "ABCDE", 5) ||
        !write_part("/tmp/titan-ul-test.01", "FGHI", 4) ||
        !write_part("/tmp/titan-ul-test.02", "JKLMN", 5))
        return 1;

    if (!UlChain_Parse("/tmp/titan-ul-test,3,20", &chain)) return 2;
    if (strcmp(chain.base_path, base) != 0 || chain.part_count != 3 || chain.media != 20) return 3;
    if (!UlChain_BuildPartPath(&chain, 2, path, sizeof(path)) || strcmp(path, "/tmp/titan-ul-test.02") != 0) return 4;
    if (!UlChain_Probe(&chain) || chain.total_size != 14) return 5;
    memset(output, 0, sizeof(output));
    if (!UlChain_ReadAt(&chain, 3, output, 10)) return 6;
    if (memcmp(output, "DEFGHIJKLM", 10) != 0) return 7;
    if (UlChain_Parse("usb:test,0,20", &chain)) return 8;
    if (UlChain_Parse("usb:test,2,19", &chain)) return 9;

    if (!write_ul_cfg()) return 10;
    if (!UlChain_LoadConfig(
            "/tmp/titan-ul.cfg,SLUS_204.86,/tmp/",
            &chain)) return 11;
    if (strcmp(chain.title, "Marvel vs. capcom 2 - new age of") != 0) return 12;
    if (strcmp(chain.startup, "SLUS_204.86") != 0) return 13;
    if (strcmp(chain.base_path, "/tmp/ul.0CB7255A.SLUS_204.86") != 0) return 14;
    if (strcmp(chain.elf_path, "cdrom0:\\SLUS_204.86;1") != 0) return 15;
    if (chain.part_count != 1 || chain.media != 0x12) return 16;
    if (!UlChain_BuildPartPath(&chain, 0, path, sizeof(path)) ||
        strcmp(path, "/tmp/ul.0CB7255A.SLUS_204.86.00") != 0) return 17;
    if (UlChain_LoadConfig(
            "/tmp/titan-ul.cfg,SLUS_999.99,/tmp/",
            &chain)) return 18;

    remove("/tmp/titan-ul-test.00");
    remove("/tmp/titan-ul-test.01");
    remove("/tmp/titan-ul-test.02");
    remove("/tmp/titan-ul.cfg");
    puts("ul_chain native ul.cfg tests passed");
    return 0;
}
