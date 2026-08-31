#include "titan_exec_handoff.h"

#include "generated/titan_launch_stage2_elf.h"
#include "titan_elf_validate.h"

#include <kernel.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TITAN_HANDOFF_PS2RD_MINIMUM_ADDRESS 0x00080000u
#define TITAN_HANDOFF_PS2RD_MAXIMUM_ADDRESS 0x00084000u
#define TITAN_HANDOFF_STAGE2_MINIMUM_ADDRESS 0x00094000u
#define TITAN_HANDOFF_STAGE2_MAXIMUM_ADDRESS 0x00100000u
#define TITAN_HANDOFF_PROGRAM_LOAD 1u
#define TITAN_HANDOFF_INTERNAL_PATCH_PREFIX "--titan-internal-patch="
#define TITAN_HANDOFF_PATCH_PATH_CAPACITY 256u
#define TITAN_HANDOFF_PATCH_ARGUMENT_CAPACITY \
    (sizeof(TITAN_HANDOFF_INTERNAL_PATCH_PREFIX) + TITAN_HANDOFF_PATCH_PATH_CAPACITY)
#define TITAN_HANDOFF_MAXIMUM_ARGUMENTS 31

static int TitanExecHandoff_IsPs2rdRange(
    uint32_t start,
    uint32_t size
)
{
    uint64_t end = (uint64_t)start + (uint64_t)size;
    return
        size > 0u &&
        start >= TITAN_HANDOFF_PS2RD_MINIMUM_ADDRESS &&
        end <= TITAN_HANDOFF_PS2RD_MAXIMUM_ADDRESS;
}

static int TitanExecHandoff_IsStage2Range(
    uint32_t start,
    uint32_t size
)
{
    uint64_t end = (uint64_t)start + (uint64_t)size;
    return
        size > 0u &&
        start >= TITAN_HANDOFF_STAGE2_MINIMUM_ADDRESS &&
        end <= TITAN_HANDOFF_STAGE2_MAXIMUM_ADDRESS;
}

static TitanExecHandoffResult TitanExecHandoff_CopyStage2(
    uint32_t *entry_point,
    int copy_ps2rd
)
{
    TitanElfValidationPolicy policy;
    TitanElfImageInfo info;
    TitanElfValidationResult validation;
    unsigned int index;
    unsigned int ps2rd_segments = 0u;
    unsigned int stage2_segments = 0u;

    if (entry_point == 0)
    {
        return TITAN_EXEC_HANDOFF_INVALID_STAGE2;
    }

    /* The embedded image deliberately contains two non-overlapping low-memory
     * regions: optional PS2RD at 0x00080000-0x00083fff and stage2 itself at
     * 0x00094000-0x000fffff. The gap is reserved for Neutrino ee_core. */
    policy.minimum_address = TITAN_HANDOFF_PS2RD_MINIMUM_ADDRESS;
    policy.maximum_address = TITAN_HANDOFF_STAGE2_MAXIMUM_ADDRESS;
    policy.maximum_program_headers = 32u;
    policy.require_executable_entry = 1;

    validation = TitanElf_ValidateMemory(
        titan_launch_stage2_elf,
        (size_t)size_titan_launch_stage2_elf,
        &policy,
        &info
    );
    if (validation != TITAN_ELF_VALID)
    {
        return TITAN_EXEC_HANDOFF_INVALID_STAGE2;
    }

    if (
        info.entry_point < TITAN_HANDOFF_STAGE2_MINIMUM_ADDRESS ||
        info.entry_point >= TITAN_HANDOFF_STAGE2_MAXIMUM_ADDRESS
    )
    {
        return TITAN_EXEC_HANDOFF_STAGE2_RANGE;
    }

    for (index = 0u; index < info.program_header_count; index++)
    {
        TitanElf32ProgramHeader program;
        int ps2rd_range;
        int stage2_range;

        if (!TitanElf_ReadMemoryProgramHeader(
            titan_launch_stage2_elf,
            (size_t)size_titan_launch_stage2_elf,
            index,
            &program
        ))
        {
            return TITAN_EXEC_HANDOFF_INVALID_STAGE2;
        }

        if (program.type != TITAN_HANDOFF_PROGRAM_LOAD)
        {
            continue;
        }

        ps2rd_range = TitanExecHandoff_IsPs2rdRange(
            program.virtual_address,
            program.memory_size
        );
        stage2_range = TitanExecHandoff_IsStage2Range(
            program.virtual_address,
            program.memory_size
        );
        if (!ps2rd_range && !stage2_range)
        {
            return TITAN_EXEC_HANDOFF_STAGE2_RANGE;
        }

        if (ps2rd_range)
        {
            ps2rd_segments++;
            if (!copy_ps2rd)
            {
                continue;
            }
        }
        else
        {
            stage2_segments++;
        }

        memcpy(
            (void *)(uintptr_t)program.virtual_address,
            titan_launch_stage2_elf + program.offset,
            program.file_size
        );

        if (program.memory_size > program.file_size)
        {
            memset(
                (void *)(uintptr_t)(
                    program.virtual_address + program.file_size
                ),
                0,
                program.memory_size - program.file_size
            );
        }
    }

    if (ps2rd_segments == 0u || stage2_segments == 0u)
    {
        return TITAN_EXEC_HANDOFF_INVALID_STAGE2;
    }

    *entry_point = info.entry_point;
    return TITAN_EXEC_HANDOFF_OK;
}

static TitanExecHandoffResult TitanExecHandoff_ExecStage2(
    int argc,
    char *argv[],
    int copy_ps2rd
)
{
    TitanExecHandoffResult result;
    uint32_t stage2_entry;
    int exec_result;

    if (
        argc < 1 ||
        argv == 0 ||
        argv[0] == 0 ||
        argv[0][0] == '\0'
    )
    {
        return TITAN_EXEC_HANDOFF_INVALID_ARGUMENT;
    }

    result = TitanExecHandoff_CopyStage2(&stage2_entry, copy_ps2rd);
    if (result != TITAN_EXEC_HANDOFF_OK)
    {
        return result;
    }

    FlushCache(WRITEBACK_DCACHE);
    FlushCache(INVALIDATE_ICACHE);

    exec_result = ExecPS2(
        (void *)(uintptr_t)stage2_entry,
        0,
        argc,
        argv
    );
    (void)exec_result;
    return TITAN_EXEC_HANDOFF_EXEC_RETURNED;
}

TitanExecHandoffResult TitanExecHandoff_Run(
    int argc,
    char *argv[]
)
{
    return TitanExecHandoff_ExecStage2(argc, argv, 0);
}

TitanExecHandoffResult TitanExecHandoff_RunWithPatch(
    int argc,
    char *argv[],
    const char *patch_payload_path
)
{
    char marker[TITAN_HANDOFF_PATCH_ARGUMENT_CAPACITY];
    char *handoff_argv[TITAN_HANDOFF_MAXIMUM_ARGUMENTS + 1];
    int written;
    int index;

    if (
        argc < 1 ||
        argc > TITAN_HANDOFF_MAXIMUM_ARGUMENTS ||
        argv == 0 ||
        patch_payload_path == 0 ||
        patch_payload_path[0] == '\0'
    )
    {
        return TITAN_EXEC_HANDOFF_INVALID_ARGUMENT;
    }

    written = snprintf(
        marker,
        sizeof(marker),
        "%s%s",
        TITAN_HANDOFF_INTERNAL_PATCH_PREFIX,
        patch_payload_path
    );
    if (written <= 0 || (unsigned int)written >= sizeof(marker))
    {
        return TITAN_EXEC_HANDOFF_PATCH_ARGUMENT;
    }

    handoff_argv[0] = marker;
    for (index = 0; index < argc; ++index)
    {
        if (argv[index] == 0)
        {
            return TITAN_EXEC_HANDOFF_INVALID_ARGUMENT;
        }
        handoff_argv[index + 1] = argv[index];
    }

    return TitanExecHandoff_ExecStage2(argc + 1, handoff_argv, 1);
}
