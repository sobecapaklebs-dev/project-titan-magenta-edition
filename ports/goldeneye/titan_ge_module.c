#include "../../kernel/include/titan_app_sdk.h"
#include "../../kernel/include/titan_game_sdk.h"
#include "titan_ge_runtime.h"
#include "mgb64/src/boss.h"
#include "mgb64/src/init.h"
#include "mgb64/src/platform/rom_io.h"
#include "mgb64/src/platform/gfx_pc.h"
#include "mgb64/src/platform/fast3d/gfx_titan.h"
#include "mgb64/src/platform/audio_pc.h"

#include <stddef.h>
#include <string.h>

#define TITAN_GE_REQUIRED_APP_API_SIZE \
    (offsetof(TitanAppApiV1, game_end) + sizeof(((TitanAppApiV1 *)0)->game_end))
#define TITAN_GE_ROM_RELATIVE_PATH "rom/GoldenEye 007.z64"

extern int platformInitSDL(void);
extern void platformShutdownSDL(void);
extern void schedulerInitThread(void);

static const TitanAppApiV1 *s_app_api;
static char s_app_root[256];

/* game_begin() validates that both its input config and output pointer live in
 * Titan-owned app memory. titan_app_update() runs on the shell thread, so C
 * autos there live on the shell stack and must never cross the App ABI.
 * Keep the handshake in the module's writable .bss instead. */
static TitanGameSessionConfig s_game_config;
static const TitanGameApiV1 *s_game_api;

static int s_started;
static int s_game_active;
static int s_finished;

static void TitanGeModule_Notify(
    TitanAppNotificationKind kind,
    const char *message
)
{
    if (s_app_api != 0 && s_app_api->notify != 0)
        (void)s_app_api->notify(kind, "TitanEYE 007", message);
}

static const char *TitanGeModule_FailureMessage(void)
{
    const char *stage = TitanGeRuntime_GetFailureStage();

    if (stage != 0 && strcmp(stage, "E_ROM_FILE_TABLE_RANGE") == 0)
        return "Tabela de arquivos da ROM TitanEYE invalida.";
    if (stage != 0 &&
        (strncmp(stage, "E_MALLOC", 8u) == 0 ||
         strncmp(stage, "E_HEAP", 6u) == 0 ||
         strncmp(stage, "E_TEX_ARENA", 11u) == 0 ||
         strncmp(stage, "E_TEX_POOL", 10u) == 0 ||
         strncmp(stage, "E_DYN_STAGEPOOL", 15u) == 0))
        return "Memoria insuficiente durante a inicializacao do TitanEYE.";
    return "Falha interna no TitanEYE. Consulte data/titaneye_boot.trace.";
}

static void TitanGeModule_CloseGame(void)
{
    platformShutdownSDL();
    TitanGeRuntime_Detach();
    platformSetRomReader(0, 0, 0u);
    if (s_game_active && s_app_api != 0 && s_app_api->game_end != 0)
        s_app_api->game_end();
    s_game_active = 0;
    s_game_api = 0;
    memset(&s_game_config, 0, sizeof(s_game_config));
}

static int TitanGeModule_Run(void)
{
    char rom_path[512];

    memset(&s_game_config, 0, sizeof(s_game_config));
    s_game_api = 0;
    s_game_config.struct_size = sizeof(s_game_config);
    s_game_config.width = TITAN_GAME_DEFAULT_WIDTH;
    s_game_config.height = TITAN_GAME_DEFAULT_HEIGHT;
    /* TitanEYE native bootstrap can exceed the old 12 MiB logical cap.
     * 16 MiB is already the Titan Game Runtime maximum; allocations remain
     * demand-driven and are not reserved eagerly. */
    s_game_config.heap_limit_bytes = 16u * 1024u * 1024u;

    if (!s_app_api->game_begin(&s_game_config, &s_game_api) || s_game_api == 0)
    {
        TitanGeModule_Notify(TITAN_APP_NOTIFICATION_ERROR,
            "Nao foi possivel reservar o Titan Game Runtime.");
        return 0;
    }
    s_game_active = 1;

    if (!TitanGeRuntime_Attach(s_game_api))
    {
        TitanGeModule_Notify(TITAN_APP_NOTIFICATION_ERROR,
            "Falha ao anexar o Titan Game Runtime.");
        TitanGeModule_CloseGame();
        return 0;
    }
    TitanGeRuntime_TraceReset();
    TitanGeRuntime_Trace("01_TGR_ATTACHED");

    if (!TitanGeRuntime_SetAppRoot(s_app_root) ||
        !TitanGeRuntime_MakePath(
            TITAN_GE_ROM_RELATIVE_PATH, rom_path, sizeof(rom_path)))
    {
        TitanGeRuntime_Trace("E_ROOT_PATH");
        TitanGeModule_Notify(TITAN_APP_NOTIFICATION_ERROR,
            "Caminho da ROM TitanEYE invalido.");
        TitanGeModule_CloseGame();
        return 0;
    }

    TitanGeRuntime_Trace("02_ROM_OPEN_BEFORE");
    if (!TitanGeRuntime_OpenRom(rom_path))
    {
        TitanGeRuntime_Trace("E_ROM_OPEN");
        TitanGeModule_Notify(TITAN_APP_NOTIFICATION_ERROR,
            "ROM invalida ou ausente em rom/GoldenEye 007.z64.");
        TitanGeModule_CloseGame();
        return 0;
    }
    TitanGeRuntime_Trace("03_ROM_OPEN_AFTER");

    platformSetRomReader(
        TitanGeRuntime_RomRead, 0, TITAN_GE_ROM_SIZE_BYTES
    );
    platformPatchFileTable(0);
    TitanGeRuntime_Trace("04_PLATFORM_INIT_BEFORE");
    if (platformInitSDL() != 0)
    {
        TitanGeRuntime_Trace("E_PLATFORM_INIT");
        TitanGeModule_Notify(TITAN_APP_NOTIFICATION_ERROR,
            "Falha ao inicializar a plataforma nativa TitanEYE.");
        TitanGeModule_CloseGame();
        return 0;
    }
    TitanGeRuntime_Trace("05_PLATFORM_INIT_AFTER");

    gfx_titan_bind(TitanGeRuntime_GetPlatform());
    TitanGeRuntime_Trace("06_AUDIO_INIT_BEFORE");
    portAudioInit();
    TitanGeRuntime_Trace("07_AUDIO_INIT_AFTER");
    TitanGeRuntime_Trace("08_GFX_INIT_BEFORE");
    gfx_init();
    TitanGeRuntime_Trace("09_GFX_INIT_AFTER");
    TitanGeRuntime_Trace("10_SCHEDULER_INIT_BEFORE");
    schedulerInitThread();
    TitanGeRuntime_Trace("11_SCHEDULER_INIT_AFTER");
    TitanGeRuntime_Trace("12_BOSS_ENTRY_BEFORE");
    /* Persist boot diagnostics once, before the game takes the foreground
     * loop. Runtime traces remain RAM-only until teardown/failure. */
    TitanGeRuntime_TraceFlush();
    bossEntry();
    TitanGeRuntime_Trace("13_BOSS_ENTRY_RETURNED");

    if (TitanGeRuntime_HasFailed()) {
        TitanGeModule_Notify(TITAN_APP_NOTIFICATION_ERROR,
            TitanGeModule_FailureMessage());
        TitanGeModule_CloseGame();
        return 0;
    }

    TitanGeModule_CloseGame();
    return 1;
}

int titan_app_init(const TitanAppApiV1 *api, const char *app_root)
{
    size_t length;
    if (api == 0 || app_root == 0 || app_root[0] == '\0' ||
        api->abi_version != TITAN_APP_API_VERSION ||
        api->struct_size < TITAN_GE_REQUIRED_APP_API_SIZE ||
        api->game_begin == 0 || api->game_end == 0)
        return -1;
    length = strlen(app_root);
    if (length >= sizeof(s_app_root)) return -1;
    s_app_api = api;
    memcpy(s_app_root, app_root, length + 1u);
    return 0;
}

void titan_app_update(const TitanAppInput *input, uint32_t delta_ms)
{
    (void)input;
    (void)delta_ms;
    if (s_started || s_finished) return;
    s_started = 1;
    (void)TitanGeModule_Run();
    s_finished = 1;
    if (s_app_api != 0 && s_app_api->request_exit != 0)
        s_app_api->request_exit();
}

void titan_app_draw(void)
{
    /* Once TGR begins, the game owns presentation inside bossEntry(). */
}

void titan_app_shutdown(void)
{
    TitanGeModule_CloseGame();
    s_finished = 1;
}
