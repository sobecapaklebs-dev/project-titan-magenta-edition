#include "titan_kernel.h"

#include "titan_context.h"
#include "titan_audio.h"
#include "titan_browser.h"
#include "titan_dex.h"
#include "titan_diagnostics.h"
#include "titan_health_trace.h"
#include "titan_platform_overhead.h"
#include "titan_target_memory.h"
#include "titan_vram_boot_guard.h"
#include "titan_vlombiscore.h"
#include "titan_session_service.h"
#include "titan_input.h"
#include "titan_home_folders.h"
#include "titan_shell_layout.h"
#include "titan_library_scan.h"
#include "titan_package_installer.h"
#include "titan_system_update.h"
#include "titan_platform.h"
#include "titan_result.h"
#include "titan_shell_scene.h"
#include "titan_shell_busy.h"
#include "titan_shell_background.h"
#include "titan_avatar_picker.h"
#include "titan_applications.h"
#include "titan_app_assets.h"
#include "titan_app_runtime.h"
#include "titan_asset_service.h"
#include "titan_notifications.h"
#include "titan_system_error.h"
#include "titan_neutrino.h"
#include "titan_game_patches.h"
#include "titan_network.h"
#include "titan_profile_storage.h"
#include "titan_early_input.h"
#include "titan_controller_ux.h"
#include "titan_controller_assignment.h"
#include "titan_download_service.h"
#include "titan_datetime.h"
#include "titan_localization.h"
#include "titan_store_service.h"
#include "titan_disc_service.h"
#include "titan_disc_engine.h"
#include "titan_time.h"
#include "titan_hid_input.h"
#include "titan_storage_scheduler.h"
#include "titan_storage_inventory.h"
#include "titan_storage_mount.h"
#include "titan_storage_pfs_provider_ps2.h"
#include "titan_text_input.h"
#include "titan_theme.h"
#include "titan_ui_assets.h"
#include "titan_user_profile.h"
#include "titan_vmc.h"
#include "titan_video.h"
#include "titan_video_output.h"

#include <stdio.h>

#if defined(__EE__) || defined(_EE)
#include <delaythread.h>
#include <fcntl.h>
#include <sifrpc.h>
#include <unistd.h>
#endif

#define TITAN_KERNEL_AUDIO_RETRY_FRAMES 180u
#define TITAN_KERNEL_BACKGROUND_IO_GRACE_FRAMES 120u

/*
 * 1.0 RC1 hardware boot probe.  stage2 proves SifLoadElf + ExecPS2 handoff,
 * but the runtime deliberately resets the IOP before rebuilding USB/BDM.
 * Persist breadcrumbs while the bootloader mass: stack is still alive so a
 * black-screen/no-LED failure can be located without changing boot order.
 *
 * This is intentionally EE-only and best-effort: failure to write a marker is
 * never allowed to change kernel control flow.  The first marker truncates the
 * previous probe; later markers append.  After IOP reset the same helper may
 * write again once profile storage has rebuilt mass:.
 */
static void TitanKernel_BootProbe(const char *stage, int result)
{
#if defined(__EE__) || defined(_EE)
    static int first = 1;
    const char *primary = "mass:/TITAN/kernel_boot_probe.log";
    const char *fallback = "mass:/kernel_boot_probe.log";
    char line[160];
    int flags;
    int fd;
    int length;

    if (stage == 0)
        return;

    /* stage2 exits RPC before ExecPS2. Rebinding here is safe and the normal
     * IOP manager will reset/rebind it again at its established boundary. */
    SifInitRpc(0);
    flags = O_WRONLY | O_CREAT | (first ? O_TRUNC : O_APPEND);
    fd = open(primary, flags, 0666);
    if (fd < 0)
        fd = open(fallback, flags, 0666);
    if (fd < 0)
        return;

    length = snprintf(line, sizeof(line), "stage=%s result=%d\n", stage, result);
    if (length > 0)
    {
        if ((unsigned int)length >= sizeof(line))
            length = (int)sizeof(line) - 1;
        (void)write(fd, line, (size_t)length);
    }
    close(fd);
    first = 0;
#else
    (void)stage;
    (void)result;
#endif
}

#define TITAN_SERVICE_STEP_WARN_US 25000u
#define TITAN_SERVICE_STEP_STALL_US 250000u

static void TitanKernel_DiagSlowService(uint32_t service, uint64_t started_us, uint32_t frame)
{
    uint64_t now = TitanTime_NowMicroseconds();
    uint32_t elapsed = now > started_us ? (uint32_t)(now - started_us) : 0u;
    if (elapsed < TITAN_SERVICE_STEP_WARN_US) return;
    TitanDiagnostics_Event(
        elapsed >= TITAN_SERVICE_STEP_STALL_US ? TITAN_DIAG_ERROR : TITAN_DIAG_WARN,
        TITAN_DIAG_KERNEL, 0x0102u, 0, service, elapsed, frame, 0u);
}

static void TitanKernel_UpdateNetworkAndAssetUi(void)
{
    static int asset_activity_visible = 0;
    static int asset_activity_was_manual = 0;
    const TitanNetworkStatus *network = TitanNetwork_GetStatus();
    const TitanAssetServiceStatus *assets = TitanAssetService_GetStatus();
    int manual_active;
    int automatic_active;
    int active;
    unsigned int processed;
    unsigned int total;
    unsigned int percent;
    char message[112];

    if (network != 0 && network->state == TITAN_NETWORK_ONLINE && network->ip[0] != '\0')
        TitanDex_SetIpAddress(network->ip);
    else TitanDex_SetIpAddress(0);

    if (assets == 0) return;
    manual_active = assets->manual_session_active || assets->manual_start_queued;
    automatic_active = !manual_active && assets->automatic_download_enabled && assets->network_busy;
    active = manual_active || automatic_active;
    processed = assets->session_completed_assets + assets->session_failed_assets + assets->session_unavailable_assets;
    total = manual_active ? assets->session_total_assets : 0u;
    percent = total > 0u ? (processed * 100u) / total : 0u;
    if (percent > 100u) percent = 100u;

    if (active)
    {
        const TitanNeutrinoGame *game = assets->current_game_index < TitanNeutrino_GetGameCount()
            ? TitanNeutrino_GetGame(assets->current_game_index) : 0;
        if (!asset_activity_visible)
            TitanNotifications_BeginDownloadActivity(manual_active ? "Assets dos jogos" : "Assets automaticos",
                assets->detail);
        if (assets->manual_start_queued)
            snprintf(message, sizeof(message), "%s", assets->detail);
        else if (assets->recovering_previous_state)
            snprintf(message, sizeof(message), "Recuperando estado anterior - %.81s", assets->detail);
        else if (assets->network_busy && game != 0 && assets->current_kind == TITAN_ASSET_KIND_ICON)
            snprintf(message, sizeof(message), "%s - Icone", game->title);
        else if (assets->network_busy && game != 0 && assets->current_kind == TITAN_ASSET_KIND_BACKGROUND)
            snprintf(message, sizeof(message), "%s - Background", game->title);
        else snprintf(message, sizeof(message), "%s", assets->detail);
        TitanNotifications_UpdateDownloadActivity(message, percent, processed, total,
            assets->transfer_progress_percent, assets->transfer_bytes_received,
            assets->transfer_bytes_total, assets->transfer_bytes_per_second);
        asset_activity_visible = 1;
        asset_activity_was_manual = manual_active;
    }
    else if (asset_activity_visible)
    {
        if (asset_activity_was_manual && assets->manual_phase == TITAN_ASSET_MANUAL_COMPLETE &&
            assets->session_failed_assets == 0u)
        {
            if (assets->session_unavailable_assets > 0u)
            {
                snprintf(message, sizeof(message), "Concluido: %u baixados, %u sem arte disponivel",
                    assets->session_completed_assets, assets->session_unavailable_assets);
                TitanNotifications_CompleteDownloadActivity(message, processed, assets->session_total_assets);
            }
            else TitanNotifications_CompleteDownloadActivity("Todos os assets solicitados foram processados",
                processed, assets->session_total_assets);
        }
        else if (asset_activity_was_manual && assets->manual_phase == TITAN_ASSET_MANUAL_COMPLETE)
        {
            snprintf(message, sizeof(message), "Concluido com %u falha(s); execute novamente para retomar",
                assets->session_failed_assets);
            TitanNotifications_FailDownloadActivity(message, processed, assets->session_total_assets);
        }
        else if (asset_activity_was_manual)
            TitanNotifications_FailDownloadActivity("Operacao interrompida; o estado valido foi preservado para retomada",
                processed, assets->session_total_assets);
        else TitanNotifications_CompleteDownloadActivity("Download automatico pausado", processed, total);
        asset_activity_visible = 0;
        asset_activity_was_manual = 0;
    }
}

static void TitanKernel_Fail(
    TitanContext *context,
    TitanResult result,
    const char *stage
)
{
    if (context != 0)
    {
        context->state =
            TITAN_KERNEL_STATE_FAILED;

        context->last_result =
            result;
    }

    TitanPlatform_ShowFatalError(
        stage,
        result
    );
}

static TitanResult TitanKernel_RenderFrame(
    TitanContext *context
)
{
    TitanResult result;
    uint32_t trace_frame = context != 0 ? context->frame_count : 0u;

    TitanHealthTrace_Mark(TITAN_HT_RENDER_ENTER, trace_frame,
        (uint32_t)TitanShellScene_IsStartupActive(),
        (uint32_t)TitanAppRuntime_OwnsExclusiveVideo(), 0u);
    TitanDiagnostics_PhaseBegin(TITAN_DIAG_PHASE_RENDER);

    /* A Titan Native Game renders from its dedicated worker through TGR. The
     * shell must not issue BeginFrame/Clear/EndFrame against the same GS while
     * that worker owns the foreground lane. Keep the kernel ticker alive for
     * input/lifecycle/recovery and explicitly yield CPU time to the game. */
    if (TitanAppRuntime_OwnsExclusiveVideo())
    {
        TitanDiagnostics_PhaseEnd(TITAN_DIAG_PHASE_RENDER);
#if defined(__EE__) || defined(_EE)
        DelayThread(1000);
#endif
        if (context != 0) context->frame_count++;
        return TITAN_RESULT_OK;
    }

    TitanDex_BeginFrame();

    TitanHealthTrace_Mark(TITAN_HT_VIDEO_BEGIN_ENTER, trace_frame, 0u, 0u, 0u);
    result = TitanVideo_BeginFrame();
    TitanHealthTrace_Mark(TITAN_HT_VIDEO_BEGIN_EXIT, trace_frame, (uint32_t)result, 0u, 0u);

    if (result != TITAN_RESULT_OK)
    {
        return result;
    }

    /* BuildFix23 CP05 stale-frame ownership rule: the new draw target is
     * cleared before either transition endpoint or an overlay can draw. */
    TitanVideo_Clear(0u, 0u, 0u);
    TitanHealthTrace_Mark(TITAN_HT_VIDEO_CLEAR, trace_frame, 0u, 0u, 0u);

    TitanHealthTrace_Mark(TITAN_HT_SHELL_RENDER_ENTER, trace_frame, 0u, 0u, 0u);
    TitanShellScene_Render(
        context,
        TitanInput_GetState()
    );
    TitanHealthTrace_Mark(TITAN_HT_SHELL_RENDER_EXIT, trace_frame, 0u, 0u, 0u);

    TitanHealthTrace_Mark(TITAN_HT_OVERLAYS_ENTER, trace_frame, 0u, 0u, 0u);
    TitanAvatarPicker_Draw(
        context != 0 ? context->frame_count : 0u
    );

    TitanTextInput_Draw(
        context != 0 ? context->frame_count : 0u
    );

    TitanNotifications_Draw(
        context != 0 ? context->frame_count : 0u
    );

    TitanSystemError_Draw(
        context != 0 ? context->frame_count : 0u
    );
    TitanHealthTrace_Mark(TITAN_HT_OVERLAYS_EXIT, trace_frame, 0u, 0u, 0u);

    TitanDex_MarkEeComplete();

    TitanHealthTrace_Mark(TITAN_HT_VIDEO_END_ENTER, trace_frame, 0u, 0u, 0u);
    result = TitanVideo_EndFrame();
    TitanHealthTrace_Mark(TITAN_HT_VIDEO_END_EXIT, trace_frame, (uint32_t)result, 0u, 0u);

    if (result != TITAN_RESULT_OK)
    {
        return result;
    }

    TitanDex_EndFrame();
    TitanDiagnostics_PhaseEnd(TITAN_DIAG_PHASE_RENDER);
    TitanHealthTrace_Mark(TITAN_HT_RENDER_EXIT, trace_frame, 0u, 0u, 0u);

    if (context != 0)
    {
        context->frame_count++;
    }

    return TITAN_RESULT_OK;
}

static void TitanKernel_RenderBusyProgress(
    void *user_data
)
{
    TitanContext *context;

    context = (TitanContext *)user_data;

    /* Long storage operations (VMC creation/verification, ISO metadata and
     * package work) are cooperative.  Keep the realtime pieces alive every
     * time such an operation yields so animation and AUDSRV never starve. */
    TitanInput_Update();
    TitanAudio_Update();
    TitanKernel_RenderFrame(context);
}

static void TitanKernel_RenderInputProgress(
    void *user_data
)
{
    TitanContext *context;

    context =
        (TitanContext *)user_data;

    /*
     * O callback é apenas diagnóstico visual.
     * O resultado definitivo será tratado pelo loop
     * principal depois da inicialização.
     */
    TitanKernel_RenderFrame(
        context
    );
}

int TitanKernel_Run(
    int argc,
    char *argv[]
)
{
    TitanContext context;
    TitanResult result;
    unsigned int background_io_start_frame;
    int background_scan_started;

    TitanKernel_BootProbe("kernel-entry", 0);
    TitanDiagnostics_Initialize();
    TitanHealthTrace_Initialize();
    TitanOverhead_Initialize();
    TitanTargetMemory_Initialize();
    TitanVramBootGuard_Initialize();
    TitanVlombisCore_Initialize();
    TitanSession_Initialize();
    TitanKernel_BootProbe("core-services-ok", 0);
    TitanDiagnostics_Event(TITAN_DIAG_INFO, TITAN_DIAG_BOOT, 0x0010, 0, (uint32_t)argc, 0, 0, 0);

    TitanContext_Initialize(
        &context,
        argc,
        argv
    );

    if (!TitanContext_IsValid(&context))
    {
        TitanKernel_Fail(
            &context,
            TITAN_ERROR_INVALID_ARGUMENT,
            "context"
        );

        return 0;
    }

    context.state =
        TITAN_KERNEL_STATE_INITIALIZING;
    TitanKernel_BootProbe("context-ok", 0);

    TitanDiagnostics_Event(TITAN_DIAG_INFO, TITAN_DIAG_KERNEL, 0x0020, 0, 0, 0, 0, 0);
    TitanVramBootGuard_EnterPhase(TITAN_VRAM_PHASE_VIDEO_INIT);
    TitanKernel_BootProbe("platform-enter", 0);
    result = TitanPlatform_Initialize();

    if (result != TITAN_RESULT_OK)
    {
        TitanKernel_Fail(
            &context,
            result,
            "platform"
        );

        return 0;
    }

    TitanKernel_BootProbe("platform-ok", (int)result);
    TitanDiagnostics_Event(TITAN_DIAG_INFO, TITAN_DIAG_KERNEL, 0x0021, (int32_t)result, 0, 0, 0, 0);
    TitanVramBootGuard_EnterPhase(TITAN_VRAM_PHASE_CORE_UI);
    result = TitanUiAssets_Initialize();

    if (result != TITAN_RESULT_OK)
    {
        TitanKernel_Fail(
            &context,
            result,
            "ui atlas"
        );

        return 0;
    }

    TitanKernel_BootProbe("ui-assets-ok", (int)result);
    TitanAvatarPicker_Initialize();
    TitanTextInput_Initialize();
    TitanNotifications_Initialize();
    TitanControllerUx_Initialize();
    TitanSystemError_Initialize();
    TitanApplications_Initialize();
    TitanAppAssets_Initialize();
    TitanPackageInstaller_Initialize();
    TitanSystemUpdate_Initialize();
    TitanBrowser_Initialize();
    TitanNeutrino_Initialize();
    TitanGamePatches_Initialize();
    TitanVideoOutput_Initialize();
    TitanKernel_BootProbe("services-ok", 0);
    TitanVramBootGuard_EnterPhase(TITAN_VRAM_PHASE_WAVES_RIBBON);
    TitanTheme_Initialize();
    TitanKernel_BootProbe("theme-ok", 0);
    TitanVramBootGuard_EnterPhase(TITAN_VRAM_PHASE_THEME_BASE);
    TitanKernel_BootProbe("shell-enter", 0);
    TitanShellScene_Initialize();
    TitanKernel_BootProbe("shell-ok", 0);
    TitanVramBootGuard_EnterPhase(TITAN_VRAM_PHASE_WELCOME);
    TitanShellBusy_Initialize(
        TitanKernel_RenderBusyProgress,
        &context
    );

    /*
     * Mostra a interface antes de tocar no IOP. Assim,
     * mesmo uma falha de bootstrap do controle deixa um
     * estágio visível em vez de uma tela preta.
     */
    TitanKernel_BootProbe("first-frame-enter", 0);
    TitanDiagnostics_BeginFrame(context.frame_count);
    result = TitanKernel_RenderFrame(
        &context
    );
    TitanDiagnostics_EndFrame(context.frame_count);
    TitanKernel_BootProbe("first-frame-done", (int)result);

    if (result != TITAN_RESULT_OK)
    {
        TitanKernel_Fail(
            &context,
            result,
            "initial frame"
        );

        return 0;
    }

    /*
     * Falha de input não derruba o kernel. A tela INPUT
     * continuará acessível visualmente e mostrará o código
     * e o estágio em que o bootstrap parou.
     */
    TitanKernel_BootProbe("before-iop-reset", 0);
    result = TitanInput_Initialize(
        TitanKernel_RenderInputProgress,
        &context
    );

    /*
     * Input e serviços não compartilham o mesmo estado de disponibilidade.
     * O IOP pode estar pronto mesmo quando PADMAN, o controle ou a porta falha.
     * Armazenamento, DEX e catálogo continuam inicializando de forma degradada
     * para que uma falha de controle nunca derrube o restante do shell.
     */
    if (TitanInput_IsIopReady())
    {
        TitanProfileStorage_Initialize(
            TitanInput_UsesExtendedModules()
        );
        TitanKernel_BootProbe("storage-rebound", 0);
        /* Video preference lives on the same transactional storage root.
         * Apply it before the visible startup sequence advances so a confirmed
         * 720p selection becomes the normal shell mode on future boots. */
        (void)TitanVideoOutput_LoadPersistentSetting();
        (void)TitanTheme_LoadPersistentSetting();
        TitanHomeFolders_Initialize();
        TitanShellLayout_Initialize();
        TitanNetwork_Initialize();
        TitanDateTime_Initialize();
        TitanLocalization_Initialize();
        (void)TitanAudio_Initialize();
        TitanShellBackground_RefreshThemeAssets();
        /* BF28 Early Input: storage/USBD are ready, but the first Health &
         * Safety frame has not run yet.  Failure is deliberately non-fatal. */
        TitanControllerAssignment_Initialize();
        (void)TitanEarlyInput_Initialize();
        TitanHidInput_Initialize();
    }

    if (!TitanInput_IsIopReady())
    {
        TitanShellLayout_Initialize();
        TitanVramBootGuard_ArmRecovery();
    }

    TitanDiagnostics_Event(TITAN_DIAG_INFO, TITAN_DIAG_KERNEL, 0x0022, (int32_t)result, (uint32_t)TitanInput_IsIopReady(), 0, 0, 0);
    TitanDex_Initialize();
    TitanShellScene_ApplyDexLayout();

    /*
     * 0.3.90 Fast Boot Catalog: carrega o cache pequeno e entrega a Home
     * antes da verificação completa. A varredura só começa quando o IOP está
     * disponível; a estrutura do catálogo existe mesmo em modo degradado.
     */
    TitanLibraryScan_Initialize();
    TitanStorageMount_Initialize();
    (void)TitanStoragePfsProviderPs2_RegisterIfAvailable();
    (void)TitanStorageScheduler_Initialize();
    TitanStorageInventory_Initialize();
    (void)TitanDownloadService_Initialize();
    TitanStoreService_Initialize();
    TitanDiscService_Initialize();
    TitanDiscEngine_Initialize();
    TitanAssetService_Initialize();
    (void)TitanStorageScheduler_RequestAppAssetsRebuild();

    /* The health/safety transition must get real frames before USB discovery
     * starts.  The cached catalog is already available at this point; a full
     * refresh is deliberately delayed so boot animation and music win over
     * background I/O on USB 1.1 hardware. */
    background_io_start_frame =
        context.frame_count + TITAN_KERNEL_BACKGROUND_IO_GRACE_FRAMES;
    background_scan_started = 0;

    context.state =
        TITAN_KERNEL_STATE_RUNNING;

    for (;;)
    {
        int input_consumed;
        TitanHealthTrace_Mark(TITAN_HT_FRAME_BEGIN, context.frame_count,
            (uint32_t)TitanShellScene_IsStartupActive(), 0u, 0u);
        /* BF28 Early Input advances at most one backend activation phase per
         * frame. Failure/deadline expiry is non-fatal and DS2 remains live. */
        TitanHealthTrace_Mark(TITAN_HT_EARLY_INPUT_ENTER, context.frame_count, 0u, 0u, 0u);
        TitanEarlyInput_Pump(TitanTime_NowMilliseconds());
        TitanHealthTrace_Mark(TITAN_HT_EARLY_INPUT_EXIT, context.frame_count, 0u, 0u, 0u);

        TitanDiagnostics_BeginFrame(context.frame_count);
        TitanDiagnostics_PhaseBegin(TITAN_DIAG_PHASE_INPUT);
        TitanHealthTrace_Mark(TITAN_HT_INPUT_ENTER, context.frame_count, 1u, 0u, 0u);
        TitanInput_Update();
        TitanHealthTrace_Mark(TITAN_HT_INPUT_ENTER, context.frame_count, 2u, 0u, 0u);
        TitanControllerAssignment_Update();
        TitanHealthTrace_Mark(TITAN_HT_INPUT_ENTER, context.frame_count, 3u, 0u, 0u);
        TitanControllerUx_Update(!TitanShellScene_IsStartupActive());

        input_consumed = TitanSystemError_Update(
            TitanInput_GetState()
        );
        if (!input_consumed)
        {
            input_consumed = TitanNotifications_Update(
                TitanInput_GetState()
            );
        }

        TitanHealthTrace_Mark(TITAN_HT_INPUT_EXIT, context.frame_count, (uint32_t)input_consumed, 0u, 0u);
        TitanDiagnostics_PhaseEnd(TITAN_DIAG_PHASE_INPUT);

        TitanDiagnostics_PhaseBegin(TITAN_DIAG_PHASE_AUDIO);
        TitanHealthTrace_Mark(TITAN_HT_AUDIO_ENTER, context.frame_count,
            (uint32_t)TitanAudio_IsReady(), (uint32_t)TitanShellScene_IsStartupActive(), 0u);
        /* The realtime path runs before any storage background work.
         * Only the tiny startup-critical SFX may preload while the startup
         * sequence is visible; larger effects wait until Home is live. */
        TitanAudio_SetDeferredAssetIoAllowed(
            !TitanShellScene_IsStartupActive()
        );
        if (!TitanAudio_IsReady() &&
            context.frame_count > 0u &&
            (context.frame_count % TITAN_KERNEL_AUDIO_RETRY_FRAMES) == 0u)
        {
            if (TitanAudio_Initialize())
            {
                TitanAudio_StartMusic();
            }
        }
        TitanAudio_Update();
        TitanHealthTrace_Mark(TITAN_HT_AUDIO_EXIT, context.frame_count,
            (uint32_t)TitanAudio_IsReady(), 0u, 0u);
        TitanDiagnostics_PhaseEnd(TITAN_DIAG_PHASE_AUDIO);

        TitanDiagnostics_PhaseBegin(TITAN_DIAG_PHASE_SERVICES);
        TitanHealthTrace_Mark(TITAN_HT_SERVICES_ENTER, context.frame_count, 0u, 0u, 0u);
        /* Download/store/disc are service state machines, not input. Keep
         * their timing in the service bucket so diagnostics can attribute
         * frame stalls to the actual owner. Their heavy I/O remains worker
         * owned; these calls only consume snapshots/completions. */
        TitanHealthTrace_Mark(TITAN_HT_DOWNLOAD_ENTER, context.frame_count, 0u, 0u, 0u);
        { uint64_t started = TitanTime_NowMicroseconds(); TitanDownloadService_Update(); TitanKernel_DiagSlowService(1u, started, context.frame_count); }
        TitanHealthTrace_Mark(TITAN_HT_DOWNLOAD_EXIT, context.frame_count, 0u, 0u, 0u);
        TitanHealthTrace_Mark(TITAN_HT_STORE_ENTER, context.frame_count, 0u, 0u, 0u);
        { uint64_t started = TitanTime_NowMicroseconds(); TitanStoreService_Update(); TitanKernel_DiagSlowService(2u, started, context.frame_count); }
        TitanHealthTrace_Mark(TITAN_HT_STORE_EXIT, context.frame_count, 0u, 0u, 0u);
        TitanHealthTrace_Mark(TITAN_HT_DISC_ENTER, context.frame_count, 0u, 0u, 0u);
        { uint64_t started = TitanTime_NowMicroseconds(); TitanDiscService_UpdateMainThread(TitanTime_NowMilliseconds()); TitanKernel_DiagSlowService(3u, started, context.frame_count); }
        TitanHealthTrace_Mark(TITAN_HT_DISC_EXIT, context.frame_count, 0u, 0u, 0u);

        /* Consume only small completion/state messages here.  Blocking USB
         * discovery, profile commits and VMC work execute on the low-priority
         * storage worker instead of the realtime UI/audio path. */
        TitanHealthTrace_Mark(TITAN_HT_DIAG_ENTER, context.frame_count, 0u, 0u, 0u);
        { uint64_t started = TitanTime_NowMicroseconds(); TitanDiagnostics_UpdateMainThread(context.frame_count); TitanKernel_DiagSlowService(4u, started, context.frame_count); }
        TitanHealthTrace_Mark(TITAN_HT_DIAG_EXIT, context.frame_count, 0u, 0u, 0u);
        TitanHealthTrace_Mark(TITAN_HT_STORAGE_ENTER, context.frame_count, 0u, 0u, 0u);
        {
            uint64_t started = TitanTime_NowMicroseconds();
            uint64_t overhead_token = TitanOverhead_Begin(
                TITAN_OVERHEAD_STORAGE,
                TitanStorageScheduler_IsForegroundBusy() || TitanStorageScheduler_IsLibraryScanBusy()
            );
            TitanStorageScheduler_UpdateMainThread();
            TitanOverhead_End(
                TITAN_OVERHEAD_STORAGE, overhead_token,
                TitanStorageScheduler_IsForegroundBusy() || TitanStorageScheduler_IsLibraryScanBusy()
            );
            TitanKernel_DiagSlowService(5u, started, context.frame_count);
        }
        TitanHealthTrace_Mark(TITAN_HT_STORAGE_EXIT, context.frame_count, 0u, 0u, 0u);
        TitanHealthTrace_Mark(TITAN_HT_ASSET_ENTER, context.frame_count, 0u, 0u, 0u);
        { uint64_t started = TitanTime_NowMicroseconds(); TitanAssetService_UpdateMainThread(context.frame_count); TitanKernel_DiagSlowService(6u, started, context.frame_count); }
        TitanHealthTrace_Mark(TITAN_HT_ASSET_EXIT, context.frame_count, 0u, 0u, 0u);
        TitanHealthTrace_Mark(TITAN_HT_NETWORK_ENTER, context.frame_count, 0u, 0u, 0u);
        {
            const TitanNetworkStatus *network_status = TitanNetwork_GetStatus();
            int network_active = network_status != 0 && network_status->enabled;
            uint64_t started = TitanTime_NowMicroseconds();
            uint64_t overhead_token = TitanOverhead_Begin(TITAN_OVERHEAD_NETWORK, network_active);
            TitanNetwork_Update();
            TitanDateTime_Update(network_status != 0 && network_status->state == TITAN_NETWORK_ONLINE, TitanTime_NowMilliseconds());
            TitanOverhead_End(TITAN_OVERHEAD_NETWORK, overhead_token, network_active);
            TitanKernel_DiagSlowService(7u, started, context.frame_count);
        }
        TitanHealthTrace_Mark(TITAN_HT_NETWORK_EXIT, context.frame_count, 0u, 0u, 0u);
        TitanHealthTrace_Mark(TITAN_HT_NET_UI_ENTER, context.frame_count, 0u, 0u, 0u);
        { uint64_t started = TitanTime_NowMicroseconds(); TitanKernel_UpdateNetworkAndAssetUi(); TitanKernel_DiagSlowService(8u, started, context.frame_count); }
        TitanHealthTrace_Mark(TITAN_HT_NET_UI_EXIT, context.frame_count, 0u, 0u, 0u);
        TitanHealthTrace_Mark(TITAN_HT_BROWSER_ENTER, context.frame_count, 0u, 0u, 0u);
        { uint64_t started = TitanTime_NowMicroseconds(); TitanBrowser_Update(); TitanKernel_DiagSlowService(9u, started, context.frame_count); }
        TitanHealthTrace_Mark(TITAN_HT_BROWSER_EXIT, context.frame_count, 0u, 0u, 0u);
        TitanHealthTrace_Mark(TITAN_HT_SERVICES_EXIT, context.frame_count, 0u, 0u, 0u);
        TitanDiagnostics_PhaseEnd(TITAN_DIAG_PHASE_SERVICES);

        TitanDiagnostics_PhaseBegin(TITAN_DIAG_PHASE_UI);
        TitanHealthTrace_Mark(TITAN_HT_UI_ENTER, context.frame_count, (uint32_t)input_consumed, 0u, 0u);
        if (input_consumed)
        {
            /* Modal global bloqueia a tela abaixo. */
        }
        else if (TitanTextInput_IsOpen())
        {
            TitanTextInput_Update(
                TitanInput_GetState()
            );
        }
        else if (TitanAvatarPicker_IsOpen())
        {
            TitanAvatarPicker_Update(
                TitanInput_GetState()
            );
        }
        else
        {
            TitanHealthTrace_Mark(TITAN_HT_SHELL_UPDATE_ENTER, context.frame_count, 0u, 0u, 0u);
            TitanShellScene_Update(
                TitanInput_GetState()
            );
            TitanHealthTrace_Mark(TITAN_HT_SHELL_UPDATE_EXIT, context.frame_count, 0u, 0u, 0u);
        }

        TitanHealthTrace_Mark(TITAN_HT_UI_EXIT, context.frame_count, 0u, 0u, 0u);
        TitanDiagnostics_PhaseEnd(TITAN_DIAG_PHASE_UI);
        result = TitanKernel_RenderFrame(
            &context
        );

        if (result != TITAN_RESULT_OK)
        {
            TitanDiagnostics_Event(TITAN_DIAG_ERROR, TITAN_DIAG_VIDEO, 0x500, (int32_t)result, context.frame_count, 0, 0, 0);
            TitanKernel_Fail(
                &context,
                result,
                "main frame"
            );

            return 0;
        }
        TitanDiagnostics_EndFrame(context.frame_count);
        TitanHealthTrace_Mark(TITAN_HT_FRAME_END, context.frame_count, (uint32_t)result, 0u, 0u);
        /* Background storage gets only the time left after a complete visible
         * frame.  The busy-pump callback above keeps rendering/audio alive
         * inside any individual operation that takes longer than expected. */
        if (TitanInput_IsIopReady() &&
            !TitanShellScene_IsStartupActive() &&
            context.frame_count >= background_io_start_frame)
        {
            const TitanUserProfile *active_profile;

            active_profile = TitanUserProfiles_GetActive();
            if (active_profile != 0 &&
                TitanVmc_GetCachedStatus(active_profile) == TITAN_VMC_STATUS_CHECKING)
            {
                (void)TitanStorageScheduler_RequestVmcRefresh(active_profile);
            }

            if (!background_scan_started)
            {
                const TitanLibraryScanStatus *library_status =
                    TitanLibraryScan_GetStatus();
                TitanVramBootGuard_EnterPhase(TITAN_VRAM_PHASE_OPTIONAL_ASSETS);
                if (library_status != 0 && library_status->cache_loaded)
                {
                    /* The integrity-checked cache was already published during
                     * initialization. Trust it on normal boots; the explicit
                     * Reload Home Catalog action rescans after USB changes. */
                    background_scan_started = 1;
                    TitanDiagnostics_Event(
                        TITAN_DIAG_INFO, TITAN_DIAG_LIBRARY, 0x614, 0,
                        TitanNeutrino_GetGameCount(),
                        TitanApplications_GetCount(),
                        library_status->cache_loaded, context.frame_count
                    );
                }
                else
                {
                    int scan_request_ok = TitanStorageScheduler_RequestLibraryScan(0);
                    if (scan_request_ok)
                    {
                        background_scan_started = 1;
                        TitanDiagnostics_Event(
                            TITAN_DIAG_INFO, TITAN_DIAG_LIBRARY, 0x610, 0,
                            context.frame_count, 1u, 0u, 0u
                        );
                    }
                    else
                    {
                        /* Do not latch a failed request forever. The next frame
                         * retries after scheduler initialization/recovery. */
                        TitanDiagnostics_Event(
                            TITAN_DIAG_WARN, TITAN_DIAG_LIBRARY, 0x611, -1,
                            context.frame_count, 0u, 0u, 0u
                        );
                    }
                }
            }

            /* Package extraction, library/profile/VMC analysis and audio
             * asset reads all share the serialized storage worker. Nothing
             * below the completed frame is allowed to issue bulk fileXio. */
        }

    }
}
