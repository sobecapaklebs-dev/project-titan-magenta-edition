#ifndef GE007_AUDIO_PC_H
#define GE007_AUDIO_PC_H

#include <ultra64.h>

typedef struct PortSfxPlayParams_s {
    f32 volume;
    f32 pan;
    f32 pitch;
    s32 delaySamples;
    u8 useEnvelope;
    u8 attackVolume;
    u8 decayVolume;
    u8 fxMix;
    ALMicroTime attackTime;
    ALMicroTime decayTime;
} PortSfxPlayParams;

typedef struct PortSfxMixStats_s {
    u32 mixCalls;
    u32 voiceStarts;
    u32 voiceStops;
    u32 activeVoicesLast;
    u32 activeVoiceFrames;
    u32 sampleFramesMixed;
    u32 peakDeltaLast;
    u32 peakDeltaMax;
} PortSfxMixStats;

/* H1 output low-pass (docs/design/remaster-aaa/06-audio-remaster.md §4.2): the one-pole
 * DAC-coloration filter in audi_port.c, exposed as two LIVE settings registered by
 * portAudioRegisterConfig(). Defined in audi_port.c (the consumer), registered in
 * audio_pc.c. Default alpha = the W6.E1.T2 sweep's best-fit (mildest) alpha against
 * the ares boot reference (30000 ≈ 8.7 kHz rolloff, replacing the old guessed
 * 26840 which fit worse: 1.94 vs 1.47 dB high-band MAE). The filter itself stays
 * default-OFF (OutputFilter=0) and is NOT promoted to --remaster: the sweep showed
 * OFF already matches the reference within budget (rejection, AUDIO_QUALITY_PLAN
 * Phase 1). This default only applies when the knob is manually enabled. */
#define PORT_AUDIO_OUTPUT_FILTER_ALPHA_DEFAULT 30000
extern s32 g_portAudioOutputFilter;       /* Audio.OutputFilter: 0/1, default 0     */
extern s32 g_portAudioOutputFilterAlpha;  /* Audio.OutputFilterAlpha: Q15 one-pole   */

/* PERF-010: audio queue occupancy target, in audio frames. Default 1.5 is
 * byte-identical to the historical fixed target; exposed so platforms/users can
 * trade latency for stall-absorption. A higher web default (more absorption for
 * the main-thread SPN path) would go behind an __EMSCRIPTEN__ guard on both the
 * definition and the registration once validated on a real browser. */
extern f32 g_portAudioQueueTargetFrames;  /* Audio.QueueTargetFrames: occupancy target in frames, default 1.5 */

void portAudioRegisterConfig(void);
void portAudioInit(void);
void portAudioShutdown(void);
s32 portAudioIsReady(void);
u32 portAudioGetDeviceBufferBytes(void);

s32 portAudioPlaySfxDetailed(s16 soundIndex, const PortSfxPlayParams *params);
s32 portAudioPlaySound(s16 soundIndex, f32 volume, f32 pan, f32 pitch);
s32 portAudioPlaySoundDelayed(s16 soundIndex, f32 volume, f32 pan, f32 pitch,
                              s32 delaySamples);
s32 portAudioSubmitSfx(s16 soundIndex, f32 volume, f32 pan, f32 pitch);
s32 portAudioSubmitSfxAtPosition(s16 soundIndex, f32 volume, f32 pitch,
                                 f32 worldX, f32 worldY, f32 worldZ);
void portAudioComputeSpatialMix(f32 worldX, f32 worldY, f32 worldZ,
                                f32 *attenOut, f32 *panOut);

void portAudioStopAll(void);
void portAudioStopVoice(s32 voiceIdx);
void portAudioReleaseVoice(s32 voiceIdx, ALMicroTime releaseUsec);
s32 portAudioVoiceIsActive(s32 voiceIdx);
void portAudioUpdateVoiceMix(s32 voiceIdx, f32 volume, f32 pan);
void portAudioUpdateVoiceMixRamp(s32 voiceIdx, f32 volume, f32 pan,
                                 ALMicroTime rampUsec);
void portAudioUpdateVoicePitch(s32 voiceIdx, f32 pitch);
void portAudioUpdateVoiceFxMix(s32 voiceIdx, u8 fxMix);
void portAudioUpdateVoiceSpatial(s32 voiceIdx, f32 volume,
                                 f32 worldX, f32 worldY, f32 worldZ);
void portAudioUpdateVoiceSpatialRamp(s32 voiceIdx, f32 volume,
                                     f32 worldX, f32 worldY, f32 worldZ,
                                     ALMicroTime rampUsec);
void portAudioUpdateVoicePanPositionRamp(s32 voiceIdx, f32 volume,
                                         f32 worldX, f32 worldY, f32 worldZ,
                                         ALMicroTime rampUsec);
void portAudioMixSfxIntoBuffer(s16 *out, s32 numSamples);
void portAudioGetSfxMixStats(PortSfxMixStats *stats);
void portAudioTraceSfxJson(const char *fmt, ...);

/* WEB-045: live soft-mute. portAudioSetMuted() moves the ramp target (0/1);
 * portAudioApplyMuteRamp() applies the per-sample-frame gain slew in place and
 * is called from the AI queue path (stubs.c osAiSetNextBuffer) just before
 * SDL_QueueAudio. No-op at unity, so the un-muted native path is byte-identical. */
void portAudioSetMuted(int muted);
void portAudioApplyMuteRamp(s16 *samples, s32 sampleFrames);

#endif
