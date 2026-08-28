#include "titan_exec_handoff.h"

#include "generated/titan_launch_stage2_elf.h"
#include "titan_elf_validate.h"

#include <kernel.h>

#include <stdint.h>
#include <string.h>

#define TITAN_HANDOFF_STAGE2_MINIMUM_ADDRESS 0x00094000u
#define TITAN_HANDOFF_STAGE2_MAXIMUM_ADDRESS 0x00100000u
#define TITAN_HANDOFF_PROGRAM_LOAD 1u

static TitanExecHandoffResult TitanExecHandoff_CopyStage2(
    uint32_t *entry_point
)
{
    TitanElfValidationPolicy policy;
    TitanElfImageInfo info;
    TitanElfValidationResult validation;
    unsigned int index;

    if (entry_point == 0)
    {
        return TITAN_EXEC_HANDOFF_INVALID_STAGE2;
    }

    policy.minimum_address = TITAN_HANDOFF_STAGE2_MINIMUM_ADDRESS;
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

    for (index = 0u; index < info.program_header_count; index++)
    {
        TitanElf32ProgramHeader program;

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

    *entry_point = info.entry_point;
    return TITAN_EXEC_HANDOFF_OK;
}

TitanExecHandoffResult TitanExecHandoff_Run(
    int argc,
    char *argv[]
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

    result = TitanExecHandoff_CopyStage2(&stage2_entry);
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
