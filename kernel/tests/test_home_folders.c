#include "titan_home_folders.h"
#include "titan_storage_io.h"
#include "titan_storage_paths.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void make_root(void)
{
    (void)system("rm -rf /tmp/project_titan_home_folders_test");
    assert(mkdir("/tmp/project_titan_home_folders_test", 0777) == 0);
    assert(mkdir(TITAN_STORAGE_DEVICE_ROOT, 0777) == 0);
}

int main(void)
{
    const char *first_paths[2] = {
        "mass:/DVD/SLUS_000.01.Game A.iso",
        "mass:/CD/SLUS_000.02.Game B.iso"
    };
    const char *updated_paths[2] = {
        "mass:/CD/SLUS_000.02.Game B.iso",
        "mass:/DVD/SLUS_000.03.Game C.iso"
    };
    const char *second_paths[1] = {
        "mass:/DVD/SLUS_000.01.Game A.iso"
    };
    unsigned int created_index;
    const TitanHomeFolder *folder;

    make_root();
    assert(TitanStorageIo_InitializeClient());

    TitanHomeFolders_Initialize();
    assert(TitanHomeFolders_GetStatus() == TITAN_HOME_FOLDERS_READY_EMPTY);
    assert(TitanHomeFolders_GetCount() == 0u);

    /* ul.cfg is one shared catalog file, so UL games must be keyed by their
       startup IDs rather than by the identical physical path. */
    {
        TitanNeutrinoGame first_ul;
        TitanNeutrinoGame second_ul;
        TitanNeutrinoGame alias_ul;
        TitanHomeGameKey first_key;
        TitanHomeGameKey second_key;

        memset(&first_ul, 0, sizeof(first_ul));
        memset(&second_ul, 0, sizeof(second_ul));
        memset(&alias_ul, 0, sizeof(alias_ul));
        strcpy(first_ul.path, "mass:/ul.cfg");
        strcpy(first_ul.startup, "SLUS_100.01");
        first_ul.format = TITAN_NEUTRINO_GAME_USBEXTREME;
        strcpy(second_ul.path, "mass:/ul.cfg");
        strcpy(second_ul.startup, "SLES_200.02");
        second_ul.format = TITAN_NEUTRINO_GAME_USBEXTREME;
        strcpy(alias_ul.path, "mass0:/ul.cfg");
        strcpy(alias_ul.startup, "SLUS_100.01");
        alias_ul.format = TITAN_NEUTRINO_GAME_USBEXTREME;

        first_key = TitanHomeFolders_MakeCatalogGameKey(&first_ul);
        second_key = TitanHomeFolders_MakeCatalogGameKey(&second_ul);
        assert(first_key != 0u);
        assert(second_key != 0u);
        assert(first_key != second_key);
        assert(first_key == TitanHomeFolders_MakeCatalogGameKey(&alias_ul));
        assert(TitanHomeFolders_MakeGameKey(first_ul.path) ==
            TitanHomeFolders_MakeGameKey(second_ul.path));
    }

    assert(TitanHomeFolders_Create("Favoritos", first_paths, 2u, &created_index));
    assert(created_index == 0u);
    assert(TitanHomeFolders_GetCount() == 1u);
    assert(TitanHomeFolders_FindForGamePath(first_paths[0]) == 0);
    assert(TitanHomeFolders_FindForGamePath(first_paths[1]) == 0);

    TitanHomeFolders_Initialize();
    assert(TitanHomeFolders_GetCount() == 1u);
    folder = TitanHomeFolders_Get(0u);
    assert(folder != 0);
    assert(strcmp(folder->name, "Favoritos") == 0);
    assert(folder->item_count == 2u);

    assert(TitanHomeFolders_Update(0u, "Demos", updated_paths, 2u));
    assert(TitanHomeFolders_FindForGamePath(first_paths[0]) < 0);
    assert(TitanHomeFolders_FindForGamePath(updated_paths[0]) == 0);
    assert(TitanHomeFolders_FindForGamePath(updated_paths[1]) == 0);

    /* Corrupt the primary copy. Initialize must recover the last known-good
       backup instead of accepting or propagating the damaged target. */
    {
        FILE *file;
        file = fopen(TITAN_STORAGE_DEVICE_ROOT "/HOMEFOLD.DAT", "wb");
        assert(file != 0);
        assert(fwrite("BAD", 1u, 3u, file) == 3u);
        assert(fclose(file) == 0);
    }
    TitanHomeFolders_Initialize();
    assert(TitanHomeFolders_GetCount() == 1u);
    folder = TitanHomeFolders_Get(0u);
    assert(folder != 0);
    assert(strcmp(folder->name, "Favoritos") == 0);

    assert(TitanHomeFolders_Update(0u, "Demos", updated_paths, 2u));
    assert(TitanHomeFolders_Create("Outros", second_paths, 1u, &created_index));
    assert(created_index == 1u);
    assert(TitanHomeFolders_GetCount() == 2u);

    /* System-shell items use the same compact 64-bit membership model as
       games, so moving Applications/Controller into a folder does not store
       long strings or duplicate assets. */
    {
        TitanHomeGameKey shell_key;

        shell_key = TitanHomeFolders_MakeShellKey(2u);
        assert(shell_key != 0u);
        assert(TitanHomeFolders_AddItemKey(1u, shell_key));
        assert(TitanHomeFolders_ContainsItemKey(1u, shell_key));
        assert(TitanHomeFolders_FindForItemKey(shell_key) == 1);
        assert(!TitanHomeFolders_AddItemKey(0u, shell_key));
        assert(TitanHomeFolders_RemoveItemKey(1u, shell_key));
        assert(TitanHomeFolders_FindForItemKey(shell_key) < 0);
    }

    /* A path can belong to only one folder. */
    assert(!TitanHomeFolders_AddGamePath(1u, updated_paths[0]));

    assert(TitanHomeFolders_Delete(0u));
    assert(TitanHomeFolders_GetCount() == 1u);
    folder = TitanHomeFolders_Get(0u);
    assert(folder != 0);
    assert(strcmp(folder->name, "Outros") == 0);
    assert(TitanHomeFolders_FindForGamePath(second_paths[0]) == 0);

    TitanHomeFolders_Initialize();
    assert(TitanHomeFolders_GetCount() == 1u);
    assert(TitanHomeFolders_GetStatus() == TITAN_HOME_FOLDERS_READY);

    puts("home-folders: OK");
    return 0;
}
