# Project Titan Magenta Edition 0.3.90-rc11

- Kernel: `0.3.90-rc11`
- Titan UI: `0.9.31`
- Bootloader metadata: `0.1.11-rc11`

## Scope

RC11 changes only the audio runtime/event wiring, the DEX metadata text
alignment and synchronized release metadata. The RC10 renderer, CRTC timing,
storage paths, profiles, DEX persistence, Home catalogue, Package Installer,
engine sources and curated visual assets are preserved.

## Audio correction

PS2SDK `audsrv_set_volume()` uses the SPU2 range ending at `MAX_VOLUME`, not a
0-100 percentage. RC10 passed `100`; RC11 passes `MAX_VOLUME` and retains the
existing software music/SFX levels before the final SPU2 output stage.

Directional events are routed once by context: Home/folder tiles use the Home
effect, the top row uses the top-menu effect, and pages/overlays use the
interface effect. Shell tile movement keeps its success-only event and is
excluded from the generic router.

## Menu music

The supplied source file is decoded from exactly `00:01:30.000`, resampled to
32 kHz stereo and stored as IMA-ADPCM at:

```text
runtime/TITAN/audio/magenta_melody.wav
```

The existing `audsrv` PCM mixer decodes at most one bounded ADPCM block per
update into its 32 KiB ring. The full 58-minute remainder is never loaded into
RAM. The music request begins on the transition into the health screen, not on
profile confirmation.

## Title ID alignment

DEX metadata is measured, reduced only when necessary, measured again at the
final scale and centered within the tile strip. This applies to both Home tiles
and tiles inside folders because they share the same renderer.

## Validation boundary

Host tests cover WAV/IMA parsing, decoding, partial `audsrv` queues, gain,
startup trigger, event-routing contracts, music hash/geometry and the existing
release suite. PS2DEV compilation, PCSX2 playback and physical PS2 audio output
must still be validated in the user's environment.
