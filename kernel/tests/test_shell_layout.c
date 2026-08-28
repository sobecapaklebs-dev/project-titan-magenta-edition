#include "titan_shell_layout.h"
#include "titan_storage_io.h"
#include "titan_storage_paths.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

static void make_root(void)
{
    (void)system("rm -rf /tmp/project_titan_shell_layout_test");
    assert(mkdir("/tmp/project_titan_shell_layout_test", 0777) == 0);
    assert(mkdir(TITAN_STORAGE_DEVICE_ROOT, 0777) == 0);
}

static void assert_defaults(void)
{
    unsigned int index;

    for (index = 0u; index < TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT; index++)
    {
        assert(TitanShellLayout_GetHomeCatalogIndex(index) == index);
        assert(TitanShellLayout_GetHomeSlot(index) == (int)index);
        assert(!TitanShellLayout_IsHomeHidden(index));
    }

    for (index = 0u; index < TITAN_SHELL_LAYOUT_SYSTEM_ITEM_COUNT; index++)
    {
        assert(TitanShellLayout_GetSystemCatalogIndex(index) == index);
        assert(TitanShellLayout_GetSystemSlot(index) == (int)index);
    }
}

int main(void)
{
    FILE *file;

    make_root();
    assert(TitanStorageIo_InitializeClient());

    TitanShellLayout_Initialize();
    assert(TitanShellLayout_GetStatus() == TITAN_SHELL_LAYOUT_READY_DEFAULTS);
    assert_defaults();

    /* Library (catalog index 3) is the permanent Home tail. It cannot be
     * hidden or moved, and other items cannot cross into its final slot. */
    assert(!TitanShellLayout_SetHomeHidden(3u, 1));
    assert(!TitanShellLayout_MoveHome(3u, -1));
    assert(TitanShellLayout_GetHomeSlot(3u) == 3);

    assert(TitanShellLayout_MoveHome(2u, -1));
    assert(TitanShellLayout_GetHomeSlot(2u) == 1);
    assert(TitanShellLayout_GetHomeCatalogIndex(1u) == 2u);
    assert(TitanShellLayout_SetHomeHidden(1u, 1));
    assert(TitanShellLayout_IsHomeHidden(1u));

    assert(TitanShellLayout_MoveSystem(4u, -1));
    assert(TitanShellLayout_GetSystemSlot(4u) == 3);
    assert(TitanShellLayout_Save());

    TitanShellLayout_Initialize();
    assert(TitanShellLayout_GetStatus() == TITAN_SHELL_LAYOUT_READY);
    assert(TitanShellLayout_GetHomeSlot(2u) == 1);
    assert(TitanShellLayout_IsHomeHidden(1u));
    assert(TitanShellLayout_GetSystemSlot(4u) == 3);

    assert(TitanShellLayout_Export());
    /* Re-export must atomically replace the old export rather than fail when
       SHELLLAY.EXP already exists. */
    assert(TitanShellLayout_Export());

    assert(TitanShellLayout_ResetDefaults());
    assert_defaults();
    assert(TitanShellLayout_Import());
    assert(TitanShellLayout_GetHomeSlot(2u) == 1);
    assert(TitanShellLayout_IsHomeHidden(1u));
    assert(TitanShellLayout_GetSystemSlot(4u) == 3);

    /* Leave a known-good backup, then damage the primary. Initialize must
       recover from SHELLLAY.BAK without accepting the corrupted target. */
    assert(TitanShellLayout_Save());
    file = fopen(TITAN_STORAGE_DEVICE_ROOT "/SHELLLAY.DAT", "wb");
    assert(file != 0);
    assert(fwrite("BAD", 1u, 3u, file) == 3u);
    assert(fclose(file) == 0);

    TitanShellLayout_Initialize();
    assert(TitanShellLayout_GetHomeSlot(2u) == 1);
    assert(TitanShellLayout_IsHomeHidden(1u));
    assert(TitanShellLayout_GetSystemSlot(4u) == 3);

    puts("shell-layout: OK");
    return 0;
}
