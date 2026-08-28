#include "titan_app_runtime.h"
#include "titan_dex.h"
#include "titan_exec_handoff.h"
#include "titan_storage_scheduler.h"

#include <stdio.h>

int TitanAppRuntime_ValidateModuleFile(const char *path)
{
    FILE *file;
    if (path == 0) return 0;
    file = fopen(path, "rb");
    if (file == 0) return 0;
    fclose(file);
    return 1;
}

int TitanAppRuntime_ValidateRuntimeModuleFile(const char *path)
{
    return TitanAppRuntime_ValidateModuleFile(path);
}

int TitanAppRuntime_ValidateGameModuleFile(const char *path)
{
    return TitanAppRuntime_ValidateModuleFile(path);
}

int TitanAppRuntime_Open(
    const char *title,
    const char *root_path,
    const char *module_path,
    const char *background_path,
    int trusted_game
)
{
    (void)title;
    (void)root_path;
    (void)module_path;
    (void)background_path;
    (void)trusted_game;
    return 1;
}

void TitanAppRuntime_Update(const TitanInputState *input, unsigned int delta_ms)
{
    (void)input;
    (void)delta_ms;
}

void TitanAppRuntime_Draw(unsigned int frame) { (void)frame; }
void TitanAppRuntime_Close(void) { }
int TitanAppRuntime_IsActive(void) { return 0; }
int TitanAppRuntime_IsLaunching(void) { return 0; }
int TitanAppRuntime_OwnsShell(void) { return 0; }
const char *TitanAppRuntime_GetTitle(void) { return "Host Titan App"; }
const char *TitanAppRuntime_GetLastError(void) { return "host stub"; }

TitanExecHandoffResult TitanExecHandoff_Run(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    return TITAN_EXEC_HANDOFF_EXEC_RETURNED;
}

int TitanDex_IsEnabled(void) { return 1; }
int TitanStorageScheduler_RequestLibraryScan(int force_rebuild)
{
    (void)force_rebuild;
    return 1;
}

void TitanAppAssets_RequestRebuild(void)
{
}
