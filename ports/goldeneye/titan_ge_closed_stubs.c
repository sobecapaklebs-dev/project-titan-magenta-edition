/* Closed replacements for desktop-only diagnostics, launch automation and
 * configuration UI hooks.  None of these are gameplay services.  Keeping them
 * local prevents a trusted Titan game from importing process, host-file,
 * dynamic-loader, OpenGL or thread capabilities through legacy port code. */
#include <ultra64.h>
#include <bondconstants.h>

#include "mgb64/src/platform/frame_stats.h"
#include "mgb64/src/platform/input_tape.h"
#include "mgb64/src/platform/settings.h"
#include "mgb64/src/game/minimap.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

int g_pcStartLevel = -1;
int g_pcStartDifficulty = DIFFICULTY_AGENT;
const char *g_pcStartRamrom = 0;
const char *g_simStateHashOut = 0;
int g_pcDirectBootLevelActive = 0;
int g_pcFaithfulSim = 1;
int g_pcStartMultiplayer = 0;
int g_pcStartMpPlayers = 2;
int g_pcStartMpStage = MP_STAGE_TEMPLE;
int g_pcStartMpScenario = SCENARIO_NORMAL;
int g_pcStartMpTimeLimitSecs = 0;

volatile int g_gfxRecoveryActive = 0;
volatile int g_portWatchdogAiChrnum = -1;
volatile int g_portWatchdogAiOpcode = -1;
volatile int g_portWatchdogDynVtxUsed = 0;

void debugDumpRequest(void) {}
int debugDumpOverlayActive(void) { return 0; }
void debugDumpExecute(void) {}
void debugDumpOverlayTick(void) {}

void portWatchdogInit(void) {}
void portWatchdogLoadBegin(void) {}
void portWatchdogLoadEnd(void) {}
void portWatchdogSetPaused(int paused) { (void)paused; }
void portWatchdogFrameTick(void) {}

static const PlatformFrameStats s_titan_frame_stats = {0};
const PlatformFrameStats *platformFrameStatsGet(void)
{ return &s_titan_frame_stats; }

int gfx_opengl_max_offscreen_dim(void) { return 2048; }
void gfx_opengl_draw_synth_frame(unsigned int frame) { (void)frame; }

void portAudioDump(const void *data, unsigned int bytes)
{ (void)data; (void)bytes; }
void portMusicAudioDump(const void *data, unsigned int bytes)
{ (void)data; (void)bytes; }

InputTapeWriter *inputTapeWriterOpen(
    const char *path, const InputTapeHeader *header)
{ (void)path; (void)header; return 0; }
int inputTapeWriterAppendTick(
    InputTapeWriter *writer, uint32_t tick,
    const InputTapePad *pads, uint32_t players)
{ (void)writer; (void)tick; (void)pads; (void)players; return -1; }
int inputTapeWriterClose(InputTapeWriter *writer) { (void)writer; return -1; }
InputTape *inputTapeRead(const char *path) { (void)path; return 0; }
void inputTapeFree(InputTape *tape) { (void)tape; }
void inputTapeConfigureRecord(const char *path) { (void)path; }
void inputTapeConfigurePlayback(const char *path) { (void)path; }
void inputTapeSetSessionParams(int level, int difficulty)
{ (void)level; (void)difficulty; }
int inputTapeIsRecordingRequested(void) { return 0; }
int inputTapeIsPlaybackRequested(void) { return 0; }
void inputTapeInstallHooks(void) {}

void minimap_stage_reset(LEVEL_INDEX stage) { (void)stage; }
void minimap_build_level_cache(LEVEL_INDEX stage) { (void)stage; }
void minimap_setup_ready(void) {}
void minimap_tick(void) {}
void minimap_note_guard_fired(ChrRecord *chr, s32 hand, s32 item, s32 audible)
{ (void)chr; (void)hand; (void)item; (void)audible; }
void minimap_queue_current_player_snapshot(void) {}
const MinimapLevelCache *minimap_get_level_cache(void) { return 0; }
const MinimapFrameQueue *minimap_get_frame_queue(void) { return 0; }
void minimap_clear_frame_queue(void) {}
s32 minimap_is_enabled(void) { return 0; }

static void TitanSetting_SetInt(s32 *value, s32 fallback)
{ if (value != 0) *value = fallback; }
static void TitanSetting_SetUInt(u32 *value, u32 fallback)
{ if (value != 0) *value = fallback; }
static void TitanSetting_SetFloat(f32 *value, f32 fallback)
{ if (value != 0) *value = fallback; }

void settingsRegisterInt(
    const char *key, s32 *value, s32 fallback, s32 min, s32 max,
    SettingScope scope, const char *env, const char *cli,
    const char *label, const char *help)
{
    (void)key; (void)min; (void)max; (void)scope; (void)env; (void)cli;
    (void)label; (void)help; TitanSetting_SetInt(value, fallback);
}
void settingsRegisterUInt(
    const char *key, u32 *value, u32 fallback, u32 min, u32 max,
    SettingScope scope, const char *env, const char *cli,
    const char *label, const char *help)
{
    (void)key; (void)min; (void)max; (void)scope; (void)env; (void)cli;
    (void)label; (void)help; TitanSetting_SetUInt(value, fallback);
}
void settingsRegisterFloat(
    const char *key, f32 *value, f32 fallback, f32 min, f32 max,
    SettingScope scope, const char *env, const char *cli,
    const char *label, const char *help)
{
    (void)key; (void)min; (void)max; (void)scope; (void)env; (void)cli;
    (void)label; (void)help; TitanSetting_SetFloat(value, fallback);
}
void settingsRegisterEnum(
    const char *key, s32 *value, s32 fallback,
    const ConfigEnumOption *options, s32 count, SettingScope scope,
    const char *env, const char *cli, const char *label, const char *help)
{
    (void)key; (void)options; (void)count; (void)scope; (void)env; (void)cli;
    (void)label; (void)help; TitanSetting_SetInt(value, fallback);
}
void settingsRegisterString(
    const char *key, char *value, size_t capacity, const char *fallback,
    SettingScope scope, const char *env, const char *cli,
    const char *label, const char *help)
{
    size_t bytes;
    (void)key; (void)scope; (void)env; (void)cli; (void)label; (void)help;
    if (value == 0 || capacity == 0u) return;
    if (fallback == 0) fallback = "";
    bytes = strlen(fallback);
    if (bytes >= capacity) bytes = capacity - 1u;
    memcpy(value, fallback, bytes);
    value[bytes] = '\0';
}
s32 settingsCount(void) { return 0; }
const Setting *settingsAt(s32 index) { (void)index; return 0; }
const Setting *settingsFind(const char *key) { (void)key; return 0; }
const char *settingsTypeName(SettingType type) { (void)type; return ""; }
const char *settingsScopeName(SettingScope scope) { (void)scope; return ""; }
const char *settingsOverrideSourceName(SettingOverrideSource source)
{ (void)source; return ""; }
const char *settingsEnumTokenForValue(const Setting *setting, s32 value)
{ (void)setting; (void)value; return 0; }
void settingsFormatEnumOptions(const Setting *setting, char *out, size_t bytes)
{ (void)setting; if (out != 0 && bytes != 0u) out[0] = '\0'; }
void settingsMarkCliOverride(const char *key) { (void)key; }
SettingOverrideSource settingsGetOverrideSource(const char *key)
{ (void)key; return SETTING_OVERRIDE_NONE; }
void settingsSetOverrideSource(const char *key, SettingOverrideSource source)
{ (void)key; (void)source; }
void settingsNoteDurableEdit(const char *key) { (void)key; }
void settingsMarkAdvanced(const char *key) { (void)key; }
s32 settingsApplyFaithfulValue(const char *key, const char *value)
{ (void)key; (void)value; return 0; }
s32 settingsApplyRemasterValue(const char *key, const char *value)
{ (void)key; (void)value; return 0; }
void settingsApplyEnvOverrides(void) {}
void settingsResetAllToDefaults(void) {}
void settingsPrintList(FILE *file) { (void)file; }
void settingsPrintDump(FILE *file) { (void)file; }

/* The trace hooks are observational only.  Old-style empty parameter lists are
 * intentional: upstream calls do not share a single public trace header. */
s32 portTraceBeginShot(void) { return -1; }
void portTraceEndShot() {}
void portTraceShotEvent() {}
void portTraceBondIntroRendered() {}
void portTraceBulletImpactCreate() {}
void portTraceBulletImpactMaterial() {}
void portTraceBulletImpactRender() {}
void portTraceProjectileImpactReposition() {}
void portTraceProjectileMotionEnd() {}
void portTraceForcedGuardHitEvent() {}
void portTraceGuardHitAnimation() {}
void portTraceGuardHitApply() {}
void portTraceGuardHitImpact() {}
void portTraceGuardSpawnEvent() {}
void portTraceLatchChrReaction() {}
void portTraceGuardDropHeldBegin() {}
void portTraceGuardDropHeldChild() {}
void portTraceGuardDropHeldClear() {}
void portTraceDisplayCastAnim() {}
void portTraceDisplayCastFollow() {}
void portTraceDisplayCastFollowStep() {}
void portTraceDisplayCastLoad() {}
void portTraceDisplayCastMatrix() {}
void portTraceDisplayCastModel() {}
void portTraceDisplayCastRootData() {}
void portTraceDisplayCastState() {}
void portTraceGoldenEyeLogoRender() {}
