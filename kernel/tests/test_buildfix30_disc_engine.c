#include "titan_disc_engine.h"
#include "titan_applications.h"
#include "titan_neutrino.h"
#include "titan_user_profile.h"
#include "titan_vmc.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static TitanDiscInfo g_disc;
static TitanSessionDescriptor g_descriptor;
static TitanSessionState g_session_state;
static TitanUserProfile g_profile;
static int g_launch_called;
static char g_launch_boot[128];
static char g_launch_vmc[128];

const TitanDiscInfo *TitanDiscService_GetInfo(void) { return &g_disc; }
int TitanDiscService_IsReady(void) { return g_disc.state == TITAN_DISC_READY; }
void TitanDiscService_MarkLaunching(void) { g_disc.state = TITAN_DISC_LAUNCHING; }
void TitanDiscService_MarkRunning(void) { g_disc.state = TITAN_DISC_RUNNING; }
void TitanDiscService_MarkLaunchFailed(void) { g_disc.state = TITAN_DISC_ERROR; g_disc.last_error = TITAN_DISC_ERROR_LAUNCH_FAILED; }

const TitanUserProfile *TitanUserProfiles_GetActive(void) { return &g_profile; }
TitanVmcStatus TitanVmc_GetCachedStatus(const TitanUserProfile *profile) { assert(profile == &g_profile); return TITAN_VMC_STATUS_READY; }
int TitanVmc_BuildNeutrinoArgument(const TitanUserProfile *profile, int port, char *dst, unsigned int cap)
{
    (void)cap; assert(profile == &g_profile && port == 0); strcpy(dst, "-mc0=mass:/TITAN/users/u1/profile.vmc"); return 1;
}
uint64_t TitanTime_NowMilliseconds(void) { return 12345u; }
TitanIgrCompatibility TitanIgrCompatibility_Evaluate(const char *title_id, TitanSessionEngine engine,
    uint32_t caps, uint32_t requested_profile, TitanOverlayLevel *overlay, uint32_t *resolved)
{
    (void)title_id; (void)caps; (void)requested_profile;
    assert(engine == TITAN_SESSION_ENGINE_DISC);
    *overlay = TITAN_OVERLAY_RETURN_ONLY; *resolved = 0u; return TITAN_IGR_UNKNOWN;
}
void TitanSession_Initialize(void) { g_session_state = TITAN_SESSION_IDLE; }
int TitanSession_Begin(const TitanSessionDescriptor *d) { g_descriptor = *d; g_session_state=TITAN_SESSION_PREPARING; return 1; }
int TitanSession_Transition(TitanSessionState next) { g_session_state=next; return 1; }
void TitanSession_Fail(void) { g_session_state=TITAN_SESSION_FAILED; }
void TitanSession_End(void) { g_session_state=TITAN_SESSION_IDLE; }
TitanNeutrinoResult TitanNeutrino_LaunchPhysicalDisc(const char *boot, const char *vmc) { (void)boot;(void)vmc; return TITAN_NEUTRINO_EXEC_FAILED; }

static int fake_launch(const char *boot, const char *vmc, void *context)
{
    assert(context == (void*)0x1234); g_launch_called++;
    strcpy(g_launch_boot, boot); strcpy(g_launch_vmc, vmc); return 0;
}

int main(void)
{
    const TitanDiscSession *s;
    memset(&g_disc,0,sizeof(g_disc)); memset(&g_profile,0,sizeof(g_profile));
    g_disc.state=TITAN_DISC_READY; g_disc.media_present=1; g_disc.media_type=TITAN_DISC_MEDIA_PS2_DVD;
    g_disc.region=TITAN_DISC_REGION_NTSC_U; strcpy(g_disc.title_id,"SLUS_123.45");
    strcpy(g_disc.title,"Disc Test"); strcpy(g_disc.boot_path,"cdrom0:\\SLUS_123.45;1");
    strcpy(g_profile.name,"Player 1"); strcpy(g_profile.vmc_path,"mass:/TITAN/users/u1/profile.vmc");
    TitanDiscEngine_Initialize(); TitanDiscEngine_SetLaunchFunction(fake_launch,(void*)0x1234);
    assert(TitanDiscEngine_Prepare());
    s=TitanDiscEngine_GetSession();
    assert(s->media_type==TITAN_DISC_MEDIA_PS2_DVD && s->region==TITAN_DISC_REGION_NTSC_U);
    assert(strcmp(s->title_id,"SLUS_123.45")==0 && strcmp(s->boot_path,"cdrom0:\\SLUS_123.45;1")==0);
    assert(s->igr_compatibility==TITAN_IGR_UNKNOWN && s->overlay_level==TITAN_OVERLAY_RETURN_ONLY);
    assert(s->vmc_enabled==1 && strstr(s->vmc_argument,"-mc0=")==s->vmc_argument);
    assert(g_descriptor.engine==TITAN_SESSION_ENGINE_DISC);
    assert(g_descriptor.classification==TITAN_APPLICATION_CLASS_DISC_ENGINE);
    assert(g_descriptor.overlay_level==TITAN_OVERLAY_RETURN_ONLY);
    assert(TitanDiscEngine_Launch());
    assert(g_launch_called==1 && strcmp(g_launch_boot,"cdrom0:\\SLUS_123.45;1")==0);
    assert(strstr(g_launch_vmc,"-mc0=")==g_launch_vmc);
    assert(g_disc.state==TITAN_DISC_RUNNING);
    puts("buildfix30-disc-engine-host: OK");
    return 0;
}
