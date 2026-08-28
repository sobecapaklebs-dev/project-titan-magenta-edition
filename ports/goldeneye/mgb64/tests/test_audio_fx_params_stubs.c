/*
 * test_audio_fx_params_stubs.c — link-only stubs for tests/test_audio_fx_params.c.
 *
 * audi_port.c (the real, unmodified source under test) references the
 * libultra audio thread/DMA/mixer subsystem. This test never exercises any
 * of that — it only reads the file-local FX-param table through
 * portAudioTestGetCustomFxParams() — but the linker still requires every
 * symbol audi_port.c's object file references to be defined somewhere.
 * These are trivial link-satisfying bodies, never called in practice; none
 * of them fake real behavior that this test depends on.
 */
#include <ultra64.h>
#include "audi.h"
#include "platform/mixer.h"
#include "platform/audio_pc.h"
#include "snd.h"

int g_deterministic = 0;
u64 osClockRate = 46875000u;
static u32 s_testOsCount;
static u32 s_testQueuedAudioBytes;
u32 osGetCount(void) { return s_testOsCount; }
void portAudioTestStubSetCount(u32 value) { s_testOsCount = value; }
void portAudioTestStubSetQueuedAudioBytes(u32 value) { s_testQueuedAudioBytes = value; }
int port_env_bool(const char *name, int default_on, const char *help) {
    (void)name; (void)help;
    return default_on;
}

void alUnlink(ALLink *element) { (void)element; }
void alLink(ALLink *element, ALLink *after) { (void)element; (void)after; }
void *alHeapDBAlloc(u8 *file, s32 line, ALHeap *hp, s32 num, s32 size) {
    (void)file; (void)line; (void)hp; (void)num; (void)size;
    return NULL;
}
void alInit(ALGlobals *glob, ALSynConfig *c) { (void)glob; (void)c; }
Acmd *alAudioFrame(Acmd *cmdList, s32 *cmdLen, s16 *outBuf, s32 outLen) {
    (void)outBuf; (void)outLen;
    if (cmdLen) *cmdLen = 0;
    return cmdList;
}

void mixerInit(void) {}
void mixerGetStats(PortMixerStats *out) { (void)out; }

u32 osAiGetLength(void) { return s_testQueuedAudioBytes; }
s32 osAiSetNextBuffer(void *buf, u32 size) { (void)buf; (void)size; return 0; }
void osCreateMesgQueue(OSMesgQueue *mq, OSMesg *msg, s32 count) {
    (void)mq; (void)msg; (void)count;
}
s32 osPiStartDma(OSIoMesg *mb, s32 pri, s32 direction, u32 devAddr,
                 void *dramAddr, u32 size, OSMesgQueue *mq) {
    (void)mb; (void)pri; (void)direction; (void)devAddr;
    (void)dramAddr; (void)size; (void)mq;
    return 0;
}
s32 osRecvMesg(OSMesgQueue *mq, OSMesg *msg, s32 flags) {
    (void)mq; (void)msg; (void)flags;
    return 0;
}

u32 portAiGetDroppedBufferCount(void) { return 0; }
void portAiGetStats(PortAiStats *stats) { (void)stats; }
u32 portAudioGetDeviceBufferBytes(void) { return 0; }
s32 portAudioGetMasterVolumeQ15(void) { return 32768; }
void portAudioGetSfxMixStats(PortSfxMixStats *stats) { (void)stats; }
void portAudioMixSfxIntoBuffer(s16 *out, s32 numSamples) { (void)out; (void)numSamples; }
int portAudioIsMuted(void) { return 0; }  /* PERF-060 live-mute gate; never muted here */
void portMusicAudioDump(const void *buf, unsigned int size) { (void)buf; (void)size; }
void sndGetPlayerStats(PortSndPlayerStats *stats) { (void)stats; }

/* PERF-052: audi_port.c references the synth-worker handshake (defined in
 * audio_pc.c, which this test does not link). The worker never runs here — this
 * test only reads the FX-param table — so these are link-only no-ops that report
 * "no worker" (portAudioFrame then takes the synchronous path). */
int  portAudioSynthWorkerRunning(void) { return 0; }
void portAudioSynthWorkerKick(s16 *buf, s32 frameSamples, int skip) {
    (void)buf; (void)frameSamples; (void)skip;
}
void portAudioSynthWorkerJoin(void) { }
/* PERF-052: the worker job body brackets with the (reified) interrupt mask;
 * single-threaded here, so the historical no-op shape is correct. */
OSIntMask osSetIntMask(OSIntMask mask) { (void)mask; return 0; }
