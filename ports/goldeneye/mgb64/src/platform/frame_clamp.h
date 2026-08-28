/*
 * frame_clamp.h — speedgraphframes spike clamp (mirrors lvl.c g_ClockTimer cap).
 *
 * Pure (ROM-free) so the runtime assignment site (game/unk_0C0A70.c) and a
 * ROM-free unit test share one implementation and can guard it (FID-0017 /
 * M2.4). HUD/watch timers (bondview.c, watch.c) read speedgraphframes directly,
 * so a stall (alt-tab, asset-load spike) must not make them jump ahead when the
 * sim's own g_ClockTimer is already capped. The policy mirrors lvl.c exactly:
 *   - RAMROM playback: raw deltaFrames preserved (replay timing fidelity).
 *   - first tick after load: capped to 1.
 *   - otherwise: capped to 4.
 */
#ifndef MGB64_FRAME_CLAMP_H
#define MGB64_FRAME_CLAMP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Max delta-frames the sim integrates per tick outside the first-tick case
 * (mirrors lvl.c: "Cap to 4 ticks (~133ms at 30fps)"). */
#define FRAME_SPIKE_CAP 4

/* Clamp a raw delta-frame count using the lvl.c cap policy.
 *   is_ramrom     != 0 -> return deltaFrames unchanged (replay exemption).
 *   is_first_tick != 0 -> cap to 1 (first frame after load).
 *   otherwise          -> cap to FRAME_SPIKE_CAP. */
int clampSpeedgraphFrames(int deltaFrames, int is_ramrom, int is_first_tick);

#ifdef __cplusplus
}
#endif

#endif /* MGB64_FRAME_CLAMP_H */
