#include "titan_disc_engine.h"

#include "titan_applications.h"
#include "titan_igr_compatibility.h"
#include "titan_neutrino.h"
#include "titan_storage_scheduler.h"
#include "titan_time.h"
#include "titan_user_profile.h"
#include "titan_vmc.h"

#include <stdio.h>
#include <string.h>
#if defined(__EE__) || defined(_EE)
#include <delaythread.h>
#endif

static TitanDiscSession g_disc_session;
static char g_last_error[96];
static TitanDiscLaunchFn g_launch_fn;
static void *g_launch_context;

static int TitanDiscEngine_DefaultLaunch(const char *boot_path, const char *vmc_argument, void *context)
{
    (void)context;
    return TitanNeutrino_LaunchPhysicalDisc(boot_path, vmc_argument) == TITAN_NEUTRINO_OK ? 0 : -1;
}


static int TitanDiscEngine_QuiesceStorage(void)
{
#if defined(__EE__) || defined(_EE)
    uint64_t started;
    TitanStorageScheduler_SetTitanGameExclusiveMode(1);
    started = TitanTime_NowMilliseconds();
    while (!TitanStorageScheduler_IsTitanGameWorkerParked())
    {
        uint64_t now = TitanTime_NowMilliseconds();
        if (now >= started && now - started >= 2000u)
        {
            TitanStorageScheduler_SetTitanGameExclusiveMode(0);
            return 0;
        }
        DelayThread(1000);
    }
#else
    /* Host tests validate the contract structurally; target owns worker park. */
#endif
    return 1;
}

static void TitanDiscEngine_SetError(const char *message)
{
    snprintf(g_last_error, sizeof(g_last_error), "%s", message != 0 ? message : "Disc launch failed");
}

void TitanDiscEngine_Initialize(void)
{
    memset(&g_disc_session, 0, sizeof(g_disc_session));
    g_last_error[0] = '\0';
    g_launch_fn = TitanDiscEngine_DefaultLaunch;
    g_launch_context = 0;
}

void TitanDiscEngine_Shutdown(void)
{
    memset(&g_disc_session, 0, sizeof(g_disc_session));
    g_last_error[0] = '\0';
    g_launch_fn = TitanDiscEngine_DefaultLaunch;
    g_launch_context = 0;
}

void TitanDiscEngine_SetLaunchFunction(TitanDiscLaunchFn launch_fn, void *context)
{
    g_launch_fn = launch_fn != 0 ? launch_fn : TitanDiscEngine_DefaultLaunch;
    g_launch_context = context;
}

int TitanDiscEngine_Prepare(void)
{
    const TitanDiscInfo *disc;
    const TitanUserProfile *profile;
    TitanSessionDescriptor descriptor;
    TitanOverlayLevel overlay;
    uint32_t resolved_profile;
    TitanVmcStatus vmc_status;

    disc = TitanDiscService_GetInfo();
    if (disc == 0 || disc->state != TITAN_DISC_READY || !disc->media_present)
    {
        TitanDiscEngine_SetError("Disc is not ready");
        return 0;
    }

    memset(&g_disc_session, 0, sizeof(g_disc_session));
    g_disc_session.media_type = disc->media_type;
    g_disc_session.region = disc->region;
    g_disc_session.launch_state = TITAN_DISC_LAUNCH_PREPARED;
    snprintf(g_disc_session.title_id, sizeof(g_disc_session.title_id), "%s", disc->title_id);
    snprintf(g_disc_session.title, sizeof(g_disc_session.title), "%s", disc->title);
    snprintf(g_disc_session.boot_path, sizeof(g_disc_session.boot_path), "%s", disc->boot_path);
    snprintf(g_disc_session.source_device, sizeof(g_disc_session.source_device), "%s", "cdrom0:");
    snprintf(g_disc_session.launch_backend, sizeof(g_disc_session.launch_backend), "%s", "Titan-Neutrino/CDVD");

    profile = TitanUserProfiles_GetActive();
    if (profile != 0)
    {
        snprintf(g_disc_session.profile, sizeof(g_disc_session.profile), "%s", profile->name);
        /* Launch preparation must not perform a VMC filesystem transaction.
           A READY cached VMC may be handed to the resident backend; otherwise
           the original disc remains launchable with physical Memory Card semantics. */
        vmc_status = TitanVmc_GetCachedStatus(profile);
        if (vmc_status == TITAN_VMC_STATUS_READY &&
            TitanVmc_BuildNeutrinoArgument(profile, 0, g_disc_session.vmc_argument,
                sizeof(g_disc_session.vmc_argument)))
            g_disc_session.vmc_enabled = 1;
    }

    overlay = TITAN_OVERLAY_DISABLED;
    resolved_profile = 0u;
    g_disc_session.igr_compatibility = TitanIgrCompatibility_Evaluate(
        g_disc_session.title_id, TITAN_SESSION_ENGINE_DISC,
        TITAN_SESSION_CAP_SAFE_RETURN | TITAN_SESSION_CAP_INPUT_NORMALIZED |
            TITAN_SESSION_CAP_STORAGE_QUIESCE,
        disc->compatibility_profile_hint, &overlay, &resolved_profile);
    g_disc_session.overlay_level = overlay;
    g_disc_session.compatibility_profile = resolved_profile;

    memset(&descriptor, 0, sizeof(descriptor));
    snprintf(descriptor.session_id, sizeof(descriptor.session_id), "disc-%u",
        (unsigned int)TitanTime_NowMilliseconds());
    snprintf(descriptor.title_id, sizeof(descriptor.title_id), "%s", g_disc_session.title_id);
    snprintf(descriptor.launch_path, sizeof(descriptor.launch_path), "%s", g_disc_session.boot_path);
    strncpy(descriptor.profile, g_disc_session.profile, sizeof(descriptor.profile) - 1u);
    if (g_disc_session.vmc_enabled)
        strncpy(descriptor.vmc, profile != 0 ? profile->vmc_path : "", sizeof(descriptor.vmc) - 1u);
    descriptor.classification = TITAN_APPLICATION_CLASS_DISC_ENGINE;
    descriptor.engine = TITAN_SESSION_ENGINE_DISC;
    descriptor.launch_source = 3u; /* physical CD/DVD media */
    descriptor.compatibility_profile = resolved_profile;
    descriptor.runtime_capabilities = TITAN_SESSION_CAP_SAFE_RETURN |
        TITAN_SESSION_CAP_INPUT_NORMALIZED | TITAN_SESSION_CAP_STORAGE_QUIESCE;
    descriptor.igr_compatibility = g_disc_session.igr_compatibility;
    descriptor.overlay_level = g_disc_session.overlay_level;
    descriptor.start_time_ms = TitanTime_NowMilliseconds();

    TitanSession_Initialize();
    if (!TitanSession_Begin(&descriptor) || !TitanSession_Transition(TITAN_SESSION_LAUNCHING))
    {
        TitanSession_End();
        g_disc_session.launch_state = TITAN_DISC_LAUNCH_FAILED;
        TitanDiscEngine_SetError("Disc session could not be started");
        return 0;
    }
    g_last_error[0] = '\0';
    return 1;
}

int TitanDiscEngine_Launch(void)
{
    int result;
    if (g_disc_session.launch_state != TITAN_DISC_LAUNCH_PREPARED)
    {
        if (!TitanDiscEngine_Prepare()) return 0;
    }
    if (!TitanDiscEngine_QuiesceStorage())
    {
        TitanSession_Fail();
        TitanSession_End();
        g_disc_session.launch_state = TITAN_DISC_LAUNCH_FAILED;
        TitanDiscEngine_SetError("Storage worker did not quiesce");
        return 0;
    }
    TitanDiscService_MarkLaunching();
    g_disc_session.launch_state = TITAN_DISC_LAUNCH_HANDOFF;
    result = g_launch_fn(g_disc_session.boot_path,
        g_disc_session.vmc_enabled ? g_disc_session.vmc_argument : "", g_launch_context);
    if (result == 0)
    {
        TitanDiscService_MarkRunning();
        return 1;
    }
    TitanDiscService_MarkLaunchFailed();
#if defined(__EE__) || defined(_EE)
    TitanStorageScheduler_SetTitanGameExclusiveMode(0);
#endif
    TitanSession_Fail();
    TitanSession_End();
    g_disc_session.launch_state = TITAN_DISC_LAUNCH_FAILED;
    TitanDiscEngine_SetError("Disc backend handoff failed");
    return 0;
}

const TitanDiscSession *TitanDiscEngine_GetSession(void) { return &g_disc_session; }
const char *TitanDiscEngine_GetLastError(void) { return g_last_error; }
