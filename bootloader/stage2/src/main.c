#include "titan_boot_version.h"
#include "stage2_video.h"

/*
 * The low-memory second-stage strategy follows the PS2SDK elf-loader design.
 * PS2SDK is distributed under the Academic Free License 2.0; see
 * licenses/PS2SDK-AFL-2.0.txt and THIRD_PARTY_NOTICES.md.
 */

#include <debug.h>
#include <kernel.h>
#include <loadfile.h>
#include <sifrpc.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define TITAN_STAGE2_LOG_PATH "mass:/TITAN/stage2_handoff.log"
#define TITAN_STAGE2_LOG_FALLBACK "mass:/stage2_handoff.log"

static void TitanStage2_WriteLog(
    int truncate_file,
    const char *stage,
    const char *target,
    int result,
    unsigned int entry_point,
    unsigned int global_pointer
)
{
    char message[384];
    int flags;
    int descriptor;
    int length;

    flags = O_WRONLY | O_CREAT | (truncate_file ? O_TRUNC : O_APPEND);
    descriptor = open(TITAN_STAGE2_LOG_PATH, flags, 0666);
    if (descriptor < 0)
    {
        descriptor = open(TITAN_STAGE2_LOG_FALLBACK, flags, 0666);
    }
    if (descriptor < 0)
    {
        return;
    }

    length = snprintf(
        message,
        sizeof(message),
        "stage=%s\ntarget=%s\nresult=%d\nepc=0x%08x\ngp=0x%08x\n---\n",
        stage != 0 ? stage : "unknown",
        target != 0 ? target : "(null)",
        result,
        entry_point,
        global_pointer
    );
    if (length > 0)
    {
        if ((unsigned int)length >= sizeof(message))
        {
            length = (int)sizeof(message) - 1;
        }
        {
            size_t written = 0u;
            while (written < (size_t)length)
            {
                ssize_t amount = write(
                    descriptor,
                    message + written,
                    (size_t)length - written
                );
                if (amount <= 0)
                {
                    break;
                }
                written += (size_t)amount;
            }
        }
    }
    close(descriptor);
}

static void TitanStage2_WipeUserMemory(void)
{
    int address;

    for (
        address = 0x00100000;
        address < GetMemorySize();
        address += 64
    )
    {
        __asm__ __volatile__(
            "sq $0, 0(%0)\n"
            "sq $0, 16(%0)\n"
            "sq $0, 32(%0)\n"
            "sq $0, 48(%0)\n"
            :
            : "r"(address)
        );
    }
}

static void TitanStage2_Halt(
    const char *path,
    int load_result,
    unsigned int entry_point
)
{
    init_scr();

    scr_printf(
        "\nPROJECT TITAN BOOTLOADER %s\n"
        "----------------------------------------\n\n"
        "FALHA NO CARREGAMENTO DO EXECUTAVEL\n"
        "Caminho: %s\n"
        "SifLoadElf: %d\n"
        "Entry point: 0x%08x\n\n"
        "O sistema foi interrompido com seguranca.\n",
        TITAN_BOOTLOADER_VERSION_STRING,
        path != 0 ? path : "(null)",
        load_result,
        entry_point
    );

    for (;;)
    {
        SleepThread();
    }
}

int main(
    int argc,
    char *argv[]
)
{
    t_ExecData executable;
    int result;
    int video_result;

    if (
        argc < 1 ||
        argv == 0 ||
        argv[0] == 0 ||
        argv[0][0] == '\0'
    )
    {
        TitanStage2_Halt(
            "(missing executable path)",
            -EINVAL,
            0u
        );
    }

    memset(&executable, 0, sizeof(executable));

    /* Draw the handoff logo from stage2 itself before the long RAM wipe and
       ELF load. The bootloader framebuffer is not treated as persistent. */
    video_result = TitanStage2Video_ShowLogo();

    SifInitRpc(0);
    TitanStage2_WriteLog(
        1,
        "stage2-enter",
        argv[0],
        video_result,
        0u,
        0u
    );
    TitanStage2_WipeUserMemory();
    FlushCache(WRITEBACK_DCACHE);

    result = SifLoadFileInit();
    TitanStage2_WriteLog(
        0,
        "loadfile-init",
        argv[0],
        result,
        0u,
        0u
    );

    if (result < 0)
    {
        TitanStage2_Halt(
            argv[0],
            result,
            0u
        );
    }

    result = SifLoadElf(argv[0], &executable);
    TitanStage2_WriteLog(
        0,
        "load-elf",
        argv[0],
        result,
        executable.epc,
        executable.gp
    );
    SifLoadFileExit();

    if (result != 0 || executable.epc == 0u)
    {
        TitanStage2_Halt(
            argv[0],
            result,
            executable.epc
        );
    }

    SifExitRpc();
    FlushCache(WRITEBACK_DCACHE);
    FlushCache(INVALIDATE_ICACHE);

    return ExecPS2(
        (void *)executable.epc,
        (void *)executable.gp,
        argc,
        argv
    );
}
