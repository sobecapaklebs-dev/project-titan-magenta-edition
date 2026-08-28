#include <ultra64.h>
#include <stdio.h>
#include <string.h>

#include "sdl_titan_compat.h"
#include "sched.h"
#include "../../../titan_ge_runtime.h"

/* Public native-port settings normally owned by platform_sdl.c.  The Titan
 * target is deliberately faithful-first: desktop-only post effects and debug
 * overlays stay disabled, while gameplay-affecting defaults remain unchanged. */
float g_pcCamX=0.0f,g_pcCamY=50.0f,g_pcCamZ=-400.0f;
float g_pcCamYaw=0.0f,g_pcCamPitch=0.0f;
int g_pcDebugFlyCamera=0;
f32 g_pcVideoGamma=1.0f,g_pcRenderScale=1.0f,g_pcFovY=50.0f,g_pcCutsceneFovY=60.0f;
s32 g_pcMsaaSamples=0; int g_pcTextureAnisotropy=1;
f32 g_pcVideoSaturation=1.0f,g_pcVideoContrast=1.0f,g_pcVideoBrightness=0.0f;
s32 g_pcOutputDither=0; f32 g_pcVignette=0.0f;
s32 g_pcBloom=0; f32 g_pcBloomThreshold=0.8f,g_pcBloomIntensity=0.0f;
s32 g_pcSsao=0,g_pcSsaoMode=1,g_pcSsaoHalfRes=0,g_pcSsaoBlur=0;
f32 g_pcSsaoRadius=0.5f,g_pcSsaoIntensity=1.0f,g_pcSsaoBias=0.15f;
f32 g_pcSsaoPower=3.0f,g_pcSsaoFarCutoff=800.0f,g_pcSsaoNearCut=0.02f;
f32 g_pcSsaoSkyCut=0.9999f,g_pcSsaoBlurDepthSharp=8.0f;
s32 g_pcFxaa=0,g_pcSmaa=0; f32 g_pcSharpen=0.0f,g_pcFogDensity=1.0f;
s32 g_pcSmoothSky=0,g_pcEnvSmoothNormals=0; f32 g_pcEnvRelightBlend=0.0f;
s32 g_pcSunShadow=0,g_pcSunShadowRes=1024; f32 g_pcSunShadowRadius=500.0f;
f32 g_pcSunShadowBias=0.0015f,g_pcSunShadowUmbra=0.55f;
s32 g_pcPerPixelLight=0,g_pcSceneDecor=0,g_pcFontUpscale=1;
char g_pcSceneDecorDir[1024]=""; f32 g_pcViewmodelFov=50.0f;
s32 g_pcGradePresets=0,g_pcTonemap=0,g_pcRemasterFX=0,g_pcFpsOverlay=0;
char g_pcTexturePack[1024]="";
f32 g_pcGradeLevelSat=1.0f,g_pcGradeLevelCon=1.0f;
f32 g_pcGradeLevelTintR=1.0f,g_pcGradeLevelTintG=1.0f,g_pcGradeLevelTintB=1.0f;
s32 g_pcRetroFilterMode=0;

int g_autoScreenshotFrame=-1,g_autoScreenshotGameTimer=-1,g_autoScreenshotExit=0;
int g_screenshotFrameSessionActive=0,g_freezeInput=0;
int g_pcEscapePressed=0,g_pcCrouchToggle=0,g_pcMouseRegrabFrame=0;
float g_pcMouseSensitivity=0.15f,g_pcMouseSensAim=0.05f;
int g_pcInvertY=0; float g_pcGamepadLookSpeed=8.0f,g_pcGamepadLookCurve=1.5f;
float g_pcGamepadDeadzone=0.15f; int g_pcGamepadRadialDeadzone=1,g_pcGamepadFpsScale=1;
s32 g_pcSteadyView=1,g_pcRumble=1,g_pcRumbleIntensity=100;
s32 g_cfgMenuToggleKey=0,g_cfgMenuToggleButton=0,g_cfgFpsToggleKey=0;
s32 g_pcAdsEnabled=0,g_pcFireRateAuthentic=1,g_pcFireRateN64FrameCost=3;
f32 g_pcAdsSensitivity=1.0f; s32 g_pcAdsFovCoupleSens=1,g_pcAdsCenterCrosshair=1;
s32 g_pcAdsSpreadEnabled=1,g_pcAdsMovePenalty=1; f32 g_pcAdsMoveScale=1.0f;
f32 g_pcAdsStrafeScale=1.0f; s32 g_pcAdsFaithfulZoom=0,g_pcAdsModelPose=1;
s32 g_pcAdsModernReticle=0,g_pcAdsSteadyView=1; f32 g_pcAdsBobFloor=0.15f;
f32 g_pcAdsRecoilReduce=0.0f,g_pcViewmodelSway=1.0f;
s32 g_pcModernCrosshair=0,g_pcHitMarkers=0,g_pcReticleTargetFeedback=0;
s32 g_pcMinimapEnabled=0,g_pcMinimapMode=0,g_pcMinimapObjectives=0;
s32 g_pcMinimapEnemyFireReveal=0,g_pcMinimapShowAllEnemies=0;
f32 g_pcMinimapOpacity=0.85f,g_pcMinimapSize=1.0f;
s32 g_pcMinimapSharpOverlay=0,g_pcIntroSkipStyle=0,g_pcCheckForUpdates=0;

SDL_GameController *g_gameController=0;
SDL_Window *g_sdlWindow=0;
static u64 g_titan_rumble_until[TITAN_GAME_MAX_PADS];
extern void portAudioFrame(void);
bool gfx_backend_use_opengl(void){return false;}
bool gfx_backend_use_webgpu(void){return false;}
bool gfx_backend_use_metal(void){return false;}
bool texture_pack_enabled(void){return false;}
bool texture_pack_try_load(int token,u8 **rgba,int *width,int *height)
{(void)token;if(rgba)*rgba=0;if(width)*width=0;if(height)*height=0;return false;}
void platformOverlayRender(void){}
void minimap_overlay_draw_queued_frames(void){}
void platformOverlayProcessEvent(const void *event){(void)event;}
int platformOverlayWantsInput(void){return 0;}

static const TitanGamePadState *TitanPlatform_Pad(unsigned int index)
{
    TitanGePlatform *platform=TitanGeRuntime_GetPlatform();
    return platform != 0 ? TitanGePlatform_GetPad(platform,index) : 0;
}

static int TitanPlatform_Axis(unsigned char value)
{
    int centered=(int)value-128;
    int scaled=centered*258;
    if(scaled>32767)scaled=32767;
    if(scaled<-32767)scaled=-32767;
    return scaled;
}

int platformInitSDL(void)
{
    TitanGePlatform *platform=TitanGeRuntime_GetPlatform();
    if(platform==0)return -1;
    TitanGePlatform_Poll(platform);
    return 0;
}

void platformShutdownSDL(void){ TitanGeRuntime_AudioEnd(); }
void platformPollEvents(void)
{
    TitanGePlatform *platform=TitanGeRuntime_GetPlatform();
    if(platform!=0)TitanGePlatform_Poll(platform);
}
void platformDrainEvents(void){ platformPollEvents(); }
void portLoadYield(void){platformPollEvents();portAudioFrame();}
int platformShouldQuit(void)
{
    const TitanGameApiV1 *api=TitanGeRuntime_GetApi();
    return api==0 || TitanGeRuntime_HasFailed() || api->exit_requested();
}
void platformSetWindowTitle(const char *title){ (void)title; }
void platformRegisterConfig(void){}
int platformApplyFaithfulPreset(void){return 0;}
int platformApplyRemasterPreset(void){return 0;}
int platformApplyFaithfulHdPreset(void){return 0;}
int platformPrintDisplays(FILE *file)
{
    if(file!=0)fprintf(file,"Project Titan GS 640x448i\n");
    return 1;
}
void platformSetScreenshotLabel(const char *label){(void)label;}
int platformScreenshotCapturePendingForGles(void){return 0;}
void platformSaveScreenshot(void){}
void *platformGetMetalLayer(void){return 0;}
void *platformGetSdlWindow(void){return 0;}
int platformWebGpuWindowInfo(void *window,void **a,void **b,int *c,int *d)
{(void)window;(void)a;(void)b;(void)c;(void)d;return 0;}

void platformGetMouseDelta(int *dx,int *dy){if(dx)*dx=0;if(dy)*dy=0;}
int platformGetMouseWheel(void){return 0;}
void platformGetRightStick(int *rx,int *ry)
{
    const TitanGamePadState *pad=TitanPlatform_Pad(0u);
    if(rx)*rx=pad&&pad->connected?TitanPlatform_Axis(pad->right_stick_x):0;
    if(ry)*ry=pad&&pad->connected?TitanPlatform_Axis(pad->right_stick_y):0;
}
int platformGetPadCount(void)
{
    const TitanGameApiV1 *api=TitanGeRuntime_GetApi();
    return api?(int)api->connected_pad_count():0;
}
int platformGetPad0InstanceId(void){return TitanPlatform_Pad(0u)?0:-1;}
int platformGetPlayerCount(void){return platformGetPadCount();}

unsigned int platformGetPadButtons(int index)
{
    const TitanGamePadState *pad=TitanPlatform_Pad((unsigned int)index);
    unsigned int out=0u,held;
    if(!pad||!pad->connected)return 0u;
    held=pad->held;
    if(held&TITAN_GAME_BUTTON_CROSS)out|=1u<<SDL_CONTROLLER_BUTTON_A;
    if(held&TITAN_GAME_BUTTON_CIRCLE)out|=1u<<SDL_CONTROLLER_BUTTON_B;
    if(held&TITAN_GAME_BUTTON_SQUARE)out|=1u<<SDL_CONTROLLER_BUTTON_X;
    if(held&TITAN_GAME_BUTTON_START)out|=1u<<SDL_CONTROLLER_BUTTON_START;
    if(held&TITAN_GAME_BUTTON_L1)out|=1u<<SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
    if(held&TITAN_GAME_BUTTON_R1)out|=1u<<SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
    if(held&TITAN_GAME_BUTTON_UP)out|=1u<<SDL_CONTROLLER_BUTTON_DPAD_UP;
    if(held&TITAN_GAME_BUTTON_DOWN)out|=1u<<SDL_CONTROLLER_BUTTON_DPAD_DOWN;
    if(held&TITAN_GAME_BUTTON_LEFT)out|=1u<<SDL_CONTROLLER_BUTTON_DPAD_LEFT;
    if(held&TITAN_GAME_BUTTON_RIGHT)out|=1u<<SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
    return out;
}
void platformGetPadLeftStick(int index,int *x,int *y)
{
    const TitanGamePadState *pad=TitanPlatform_Pad((unsigned int)index);
    if(x)*x=pad&&pad->connected?TitanPlatform_Axis(pad->left_stick_x):0;
    if(y)*y=pad&&pad->connected?TitanPlatform_Axis(pad->left_stick_y):0;
}
void platformGetPadRightStick(int index,int *x,int *y)
{
    const TitanGamePadState *pad=TitanPlatform_Pad((unsigned int)index);
    if(x)*x=pad&&pad->connected?TitanPlatform_Axis(pad->right_stick_x):0;
    if(y)*y=pad&&pad->connected?TitanPlatform_Axis(pad->right_stick_y):0;
}
void platformGetPadTriggers(int index,int *left,int *right)
{
    const TitanGamePadState *pad=TitanPlatform_Pad((unsigned int)index);
    unsigned int held=pad&&pad->connected?pad->held:0u;
    if(left)*left=(held&TITAN_GAME_BUTTON_L2)?32767:0;
    if(right)*right=(held&TITAN_GAME_BUTTON_R2)?32767:0;
}

int platformTitanFillN64Pads(OSContPad *pads,int capacity)
{
    int index;
    if(pads==0||capacity<=0)return 0;
    platformPollEvents();
    memset(pads,0,(size_t)capacity*sizeof(*pads));
    for(index=0;index<capacity&&index<(int)TITAN_GAME_MAX_PADS;index++)
    {
        const TitanGamePadState *pad=TitanPlatform_Pad((unsigned int)index);
        unsigned int held;
        int x,y;
        if(!pad||!pad->connected){pads[index].errnum=1;continue;}
        held=pad->held;
        if(held&(TITAN_GAME_BUTTON_CROSS|TITAN_GAME_BUTTON_SQUARE))pads[index].button|=A_BUTTON;
        if(held&(TITAN_GAME_BUTTON_CIRCLE|TITAN_GAME_BUTTON_TRIANGLE))pads[index].button|=B_BUTTON;
        if(held&TITAN_GAME_BUTTON_START)pads[index].button|=START_BUTTON;
        if(held&TITAN_GAME_BUTTON_R2)pads[index].button|=Z_TRIG;
        if(held&TITAN_GAME_BUTTON_L2)pads[index].button|=R_TRIG;
        if(held&TITAN_GAME_BUTTON_L1)pads[index].button|=L_TRIG;
        if(held&TITAN_GAME_BUTTON_UP)pads[index].button|=U_JPAD;
        if(held&TITAN_GAME_BUTTON_DOWN)pads[index].button|=D_JPAD;
        if(held&TITAN_GAME_BUTTON_LEFT)pads[index].button|=L_JPAD;
        if(held&TITAN_GAME_BUTTON_RIGHT)pads[index].button|=R_JPAD;
        x=TitanPlatform_Axis(pad->left_stick_x)*80/32767;
        y=-TitanPlatform_Axis(pad->left_stick_y)*80/32767;
        if(x>80)x=80;
        if(x<-80)x=-80;
        if(y>80)y=80;
        if(y<-80)y=-80;
        pads[index].stick_x=(s8)x;
        pads[index].stick_y=(s8)y;
        pads[index].errnum=0;
    }
    return 0;
}

void platformRumblePlayer(s32 player,f32 duration)
{
    const TitanGameApiV1 *api=TitanGeRuntime_GetApi();
    u32 strength;
    if(api==0||api->set_pad_rumble==0||player<0||player>=(s32)TITAN_GAME_MAX_PADS||
       !g_pcRumble)return;
    if(duration<0.0f)duration=0.0f;
    strength=(u32)(g_pcRumbleIntensity<0?0:g_pcRumbleIntensity);
    if(strength>100u)strength=100u;
    strength=strength*255u/100u;
    if(api->set_pad_rumble((u32)player,0u,(u8)strength))
        g_titan_rumble_until[player]=api->uptime_ms()+(u64)(duration*1000.0f);
}
void platformRumbleStopAll(void)
{
    const TitanGameApiV1 *api=TitanGeRuntime_GetApi();
    u32 index;
    if(api==0||api->set_pad_rumble==0)return;
    for(index=0u;index<TITAN_GAME_MAX_PADS;index++){
        (void)api->set_pad_rumble(index,0u,0u);
        g_titan_rumble_until[index]=0u;
    }
}

extern void platformCheckTimers(void);
void platformFrameStatsTick(void){}
void platformFrameSync(void)
{
    OSScClient *client;
    const TitanGameApiV1 *api=TitanGeRuntime_GetApi();
    u32 rumble_index;
    platformPollEvents();
    if(api!=0&&api->set_pad_rumble!=0){
        u64 now=api->uptime_ms();
        for(rumble_index=0u;rumble_index<TITAN_GAME_MAX_PADS;rumble_index++)
            if(g_titan_rumble_until[rumble_index]!=0u&&now>=g_titan_rumble_until[rumble_index]){
                (void)api->set_pad_rumble(rumble_index,0u,0u);
                g_titan_rumble_until[rumble_index]=0u;
            }
    }
    platformCheckTimers();
    portAudioFrame();
    os_scheduler.frameCount++;
    for(client=os_scheduler.clientList;client!=0;client=client->next)
        osSendMesg(client->msgQ,(OSMesg)&os_scheduler.retraceMsg,OS_MESG_NOBLOCK);
}
