#include "titan_system_update.h"
#include "titan_storage_io.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define ROOT "/tmp/project_titan_system_update_test"

int main(void)
{
    const TitanSystemUpdateInfo *info;
    const TitanSystemUpdateStatus *status;
    FILE *f;
    char data[32];
    size_t n;
    unsigned int guard;
    unsigned int index;
    unsigned int build37 = 0xffffffffu;
    unsigned int build38 = 0xffffffffu;
    unsigned int build39 = 0xffffffffu;

#if defined(TITAN_STORAGE_IO_TESTING)
    TitanStorageIo_TestSetMaxTransfer(3u);
#endif
    TitanSystemUpdate_Initialize();
    assert(TitanSystemUpdate_Refresh() == 3u);
    for (index = 0u; index < TitanSystemUpdate_GetCount(); index++)
    {
        info = TitanSystemUpdate_Get(index);
        assert(info != 0);
        if (info->build_number == 37u) build37 = index;
        if (info->build_number == 38u) build38 = index;
        if (info->build_number == 39u) build39 = index;
    }
    assert(build37 != 0xffffffffu);
    assert(build38 != 0xffffffffu);
    assert(build39 != 0xffffffffu);
    info = TitanSystemUpdate_Get(build37);
    assert(info->file_count == 2u);

    assert(TitanSystemUpdate_Begin(build37) == TITAN_SYSTEM_UPDATE_OK);
    guard = 0u;
    while (TitanSystemUpdate_IsActive() && guard++ < 4096u)
        TitanSystemUpdate_Update(7u);
    assert(guard < 4096u);
    status = TitanSystemUpdate_GetStatus();
    assert(status->result == TITAN_SYSTEM_UPDATE_OK);
    assert(status->progress_percent == 100u);

    f = fopen(ROOT "/kernel.elf", "rb");
    assert(f != 0);
    n = fread(data,1u,sizeof(data)-1u,f);
    fclose(f);
    data[n]='\0';
    assert(strcmp(data,"new-kernel-build37") == 0);
    f = fopen(ROOT "/TITAN/settings/update-test.cfg", "rb");
    assert(f != 0);
    n = fread(data,1u,sizeof(data)-1u,f);
    fclose(f);
    data[n]='\0';
    assert(strcmp(data,"new-settings-build37") == 0);

    /* Build38 publishes kernel.elf first, then deliberately fails because
     * ROOT/bootloader is a file rather than a directory. The updater must
     * roll kernel.elf back to the Build37 version. */
    assert(TitanSystemUpdate_Begin(build38) == TITAN_SYSTEM_UPDATE_OK);
    guard = 0u;
    while (TitanSystemUpdate_IsActive() && guard++ < 4096u)
        TitanSystemUpdate_Update(7u);
    assert(guard < 4096u);
    status = TitanSystemUpdate_GetStatus();
    assert(status->result == TITAN_SYSTEM_UPDATE_PUBLISH_ERROR);
    f = fopen(ROOT "/kernel.elf", "rb");
    assert(f != 0);
    n = fread(data,1u,sizeof(data)-1u,f);
    fclose(f);
    data[n]='\0';
    assert(strcmp(data,"new-kernel-build37") == 0);

    /* Build39 has a valid TPUP header/table but one payload byte was changed
     * after packaging. It must be rejected in the read-only VERIFYING phase,
     * before any staging/publish mutation occurs. */
    assert(TitanSystemUpdate_Begin(build39) == TITAN_SYSTEM_UPDATE_OK);
    guard = 0u;
    while (TitanSystemUpdate_IsActive() && guard++ < 4096u)
        TitanSystemUpdate_Update(7u);
    assert(guard < 4096u);
    status = TitanSystemUpdate_GetStatus();
    assert(status->result == TITAN_SYSTEM_UPDATE_CHECKSUM_ERROR);
    assert(strstr(status->message, "CRC stream PUP invalido") != 0);
    f = fopen(ROOT "/kernel.elf", "rb");
    assert(f != 0);
    n = fread(data,1u,sizeof(data)-1u,f);
    fclose(f);
    data[n]='\0';
    assert(strcmp(data,"new-kernel-build37") == 0);

    puts("system-update-transaction: OK forward-only TPUP stream + short I/O + staged reread + rollback");
    return 0;
}
