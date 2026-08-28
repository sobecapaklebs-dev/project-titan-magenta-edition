# Orbis-style game launch transition

Implemented in the Magenta shell for both Titan-Neutrino Engine and Titan UL Engine.

- Fixed-step timing: 60 Hz, 204 frames (~3.4 s) before the engine handoff.
- Header, title/action text and secondary Home UI disappear first.
- Non-selected tiles fade out before the selected game tile.
- The selected tile survives the carousel, moves toward the lower-left live-area position and stays visible through the engine-identification hold.
- The shell background is progressively dimmed during the live-area takeover; the final full black handoff is delayed until the end of the choreography.
- The approved compact `Powered by:` badge fades in after the live-area takeover and remains readable for roughly 1.5 s before fading out.
- Titan-Neutrino Engine uses the Magenta badge; Titan UL Engine uses the blue badge.
- Badge placement is inset from the lower-right corner by 24 px in the 640x448 UI coordinate space.
- The badge is vector-rendered to avoid extra texture/VRAM cost. The `ENGINE` word carries the engine color while the engine name remains white.
- Folder-launched games use the exact same transition path.
- `TitanShellScene_BeginGameReturn(game_index)` provides the reverse-return animation hook for the future quick-menu/overlay return path.

The actual game handoff begins only after the full visual sequence completes. No blocking sleep is used: the shell continues rendering at its normal fixed step while the handoff gate is held closed. If the handoff fails and returns, the shell cancels the launch state and reports the existing launch error notification.
