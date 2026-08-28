#include <kernel.h>
#include <loadfile.h>
#include <stdio.h>
#include <string.h>

static int resolve_boot_path(char *out, unsigned int cap, int argc, char **argv)
{
    int i;
    const char *slot = "0";
    for (i = 0; i < argc; ++i) {
        if (argv && argv[i] && !strncmp(argv[i], "mc1:", 4)) { slot = "1"; break; }
    }
    return snprintf(out, cap, "mc%s:/BOOT/BOOT.ELF", slot) > 0;
}

int main(int argc, char **argv)
{
    char boot[64];
    char *args[2];
    if (!resolve_boot_path(boot, sizeof(boot), argc, argv)) return 2;
    args[0] = boot; args[1] = 0;
    LoadExecPS2(boot, 1, args);
    return 3;
}
