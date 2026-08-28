/**
 * stall_watchdog.h — BUG-1 silent-freeze capture rig.
 *
 * The Bunker field freeze (silent, no log, force-quit-only) survived a
 * 25-run headless repro campaign, so the next interactive occurrence must
 * capture its own evidence. A watchdog thread watches a once-per-frame
 * heartbeat published by the main (sim) loop; if the heartbeat stops
 * advancing while gameplay is supposed to be running, it dumps a stall
 * block + all-thread sample + breadcrumb ring and lets the process live
 * (the user force-quits as before — the evidence is already on disk).
 *
 * Default ON, near-zero cost (one atomic store + a bounded read-only state
 * snapshot per frame; one watchdog wake per second). GE007_NO_WATCHDOG=1
 * disables everything.
 *
 * Windows: clean no-op until the Windows diagnostics lane exists (backlog
 * M6.1 diag-log tee, M6.2 crash handler) — see stall_watchdog.c.
 */
#ifndef _PLATFORM_STALL_WATCHDOG_H_
#define _PLATFORM_STALL_WATCHDOG_H_

/* Main thread, once, after SDL init and before bossEntry(). Reads the
 * GE007_WATCHDOG_* flags (port_env is main-thread-only), caches savedir
 * paths, and starts the watchdog thread. */
void portWatchdogInit(void);

/* Stage (re)load started — suppress stall detection (loads legitimately
 * block the main loop for seconds). Called from bossMainloop. */
void portWatchdogLoadBegin(void);

/* Frame loop (re)entered — arm stall detection. */
void portWatchdogLoadEnd(void);

/* MC.7 solo-pause: a legitimate single-player pause (settings overlay open)
 * intentionally freezes the sim tick (g_ClockTimer=0) while rendering keeps
 * running. Suppress stall detection while paused so the frozen sim state can't
 * be misread as a wedge. Idempotent; called every frame from the sim loop with
 * the current pause state (1 = paused, 0 = running). */
void portWatchdogSetPaused(int paused);

/* Sim/main thread, once per completed main-loop frame: records the
 * breadcrumb for this frame and publishes the heartbeat. */
void portWatchdogFrameTick(void);

#endif /* _PLATFORM_STALL_WATCHDOG_H_ */
