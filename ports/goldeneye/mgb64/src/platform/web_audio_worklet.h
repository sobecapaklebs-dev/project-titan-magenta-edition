/*
 * web_audio_worklet.h — web-only low-latency audio output backend.
 *
 * On the browser, the default SDL emscripten audio path is a MAIN-THREAD
 * ScriptProcessorNode: the once-per-frame SDL_QueueAudio pump AND the SPN
 * callback both run on the main thread, so any GC / WebGPU pipeline compile /
 * level-load stall starves audio, which forces a deep (2048-sample, ~43 ms)
 * device buffer as glitch insurance. That buffer is a hard latency floor.
 *
 * This backend moves the DRAIN off the main thread: a plain-JS
 * AudioWorkletProcessor owns a ring buffer and pulls 128-sample render quanta
 * (~2.7 ms) at audio-thread priority, immune to main-thread stalls. The engine
 * still synthesises on the main thread (it is welded to the 60 Hz sim tick) and
 * posts finished PCM into the ring via postMessage — so this needs NO
 * SharedArrayBuffer, NO cross-origin isolation, and does NOT touch the WebGPU
 * build. It is web-only and behind webAudioOutputActive(); if the browser lacks
 * AudioWorklet the caller falls back to the SDL ScriptProcessorNode path.
 *
 * See docs/WEB.md (audio) and FID-0141 / WEB-069.
 */
#ifndef WEB_AUDIO_WORKLET_H
#define WEB_AUDIO_WORKLET_H

#ifdef __EMSCRIPTEN__

/* Start the AudioWorklet output backend. srcRate = the engine PCM rate (22050),
 * channels = 2. Returns 1 if the backend was started (browser supports
 * AudioWorklet), 0 to fall back to the SDL ScriptProcessorNode path. The worklet
 * module loads asynchronously; pushes before it is ready are briefly buffered. */
int webAudioOutputInit(int srcRate, int channels);

/* 1 once webAudioOutputInit has selected this backend (module may still be
 * loading). 0 if init was never called or the browser refused it. */
int webAudioOutputActive(void);

/* Push final stereo s16 PCM (size bytes, 4 bytes/frame) to the ring. Returns 0
 * on success to mirror SDL_QueueAudio's contract. */
int webAudioOutputPush(const void *buf, unsigned size);

/* Estimated ring occupancy in bytes, for the occupancy controller. Derived on
 * the main thread from the worklet's last reported ring count plus the audio
 * clock (ctx.currentTime), so it is synchronous and needs no shared memory. */
unsigned webAudioOutputQueuedBytes(void);

/* Resume the AudioContext (autoplay policy needs a user gesture). Safe to call
 * repeatedly; also reachable via the shell's existing SDL2.audioContext resume. */
void webAudioOutputResume(void);

#endif /* __EMSCRIPTEN__ */
#endif /* WEB_AUDIO_WORKLET_H */
