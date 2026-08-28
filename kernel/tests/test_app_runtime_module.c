#include "titan_app_runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define ELF32_E_FLAGS_OFFSET 36L
#define EF_MIPS_ABI2 0x00000020u
#define EF_MIPS_ABI_MASK 0x0000f000u

static int make_n32_fixture(const char *source, const char *target)
{
    FILE *in = fopen(source, "rb");
    FILE *out;
    unsigned char buffer[4096];
    size_t amount;
    uint32_t flags;

    if (in == 0) return 0;
    out = fopen(target, "wb");
    if (out == 0) { fclose(in); return 0; }
    while ((amount = fread(buffer, 1u, sizeof(buffer), in)) != 0u)
        if (fwrite(buffer, 1u, amount, out) != amount) { fclose(in); fclose(out); return 0; }
    fclose(in);
    fclose(out);

    out = fopen(target, "r+b");
    if (out == 0 || fseek(out, ELF32_E_FLAGS_OFFSET, SEEK_SET) != 0 ||
        fread(&flags, sizeof(flags), 1u, out) != 1u)
    {
        if (out != 0) fclose(out);
        return 0;
    }
    flags &= ~EF_MIPS_ABI_MASK;
    flags |= EF_MIPS_ABI2;
    if (fseek(out, ELF32_E_FLAGS_OFFSET, SEEK_SET) != 0 ||
        fwrite(&flags, sizeof(flags), 1u, out) != 1u)
    {
        fclose(out);
        return 0;
    }
    fclose(out);
    return 1;
}

int main(int argc, char **argv)
{
    const char *n32_safe = "/tmp/hello_titan_r5900_n32_contract.tmod";
    const char *n32_bad = "/tmp/hello_titan_r5900_n32_bad_cop1.tmod";

    if (argc != 4)
    {
        fprintf(stderr, "usage: %s generic.tmod r5900_o32_safe.tmod r5900_o32_bad_cop1.tmod\n", argv[0]);
        return 2;
    }
    if (!make_n32_fixture(argv[2], n32_safe) || !make_n32_fixture(argv[3], n32_bad))
    {
        fprintf(stderr, "failed to materialize N32 fixture copies\n");
        return 2;
    }

    if (!TitanAppRuntime_ValidateModuleFile(argv[1]))
    {
        fprintf(stderr, "compiler-produced MIPS ET_REL fixture was rejected structurally\n");
        return 1;
    }
    if (TitanAppRuntime_ValidateRuntimeModuleFile(argv[1]))
    {
        fprintf(stderr, "generic MIPS32r2 fixture was incorrectly accepted for R5900 execution\n");
        return 1;
    }
    if (TitanAppRuntime_ValidateRuntimeModuleFile(argv[2]))
    {
        fprintf(stderr, "legacy R5900 O32 fixture was incorrectly accepted by N32 Titan runtime\n");
        return 1;
    }
    if (!TitanAppRuntime_ValidateRuntimeModuleFile(n32_safe))
    {
        fprintf(stderr, "R5900 N32 single-float ET_REL fixture failed runtime validation\n");
        return 1;
    }
    if (!TitanAppRuntime_ValidateModuleFile(n32_bad) ||
        TitanAppRuntime_ValidateRuntimeModuleFile(n32_bad))
    {
        fprintf(stderr, "R5900 N32-marked double-COP1 fixture was not safely rejected\n");
        return 1;
    }

    remove(n32_safe);
    remove(n32_bad);
    puts("Titan App ET_REL/R5900 N32 ISA guard validation passed");
    return 0;
}
