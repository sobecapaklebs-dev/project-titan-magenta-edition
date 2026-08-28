# Project Titan Magenta Edition audio runtime

The six files in `sfx/` are system effects produced specifically for Project
Titan Magenta Edition and are included with the project runtime.

`magenta_melody.wav` is the owner-supplied menu track "Nostalgic Soundscapes
S02E03 Dimensions - PS2 Console inspired Retro Ambient", credited to Retrovex
Ambient. It was decoded from the supplied AAC file starting at exactly
`00:01:30.000`, resampled to 32,000 Hz stereo and stored as IMA-ADPCM. The
runtime decodes one bounded block at a time into the existing PCM ring buffer;
the complete track is never loaded into EE memory.

SHA-256 of the runtime music asset:

`047f1bf49c4d3355185bb6579b9eb029f545997a08f9f3648a68d988e6967ea0`

The six SFX remain 32,000 Hz, 16-bit stereo PCM. Replacement background music
may use the same PCM format or the bounded stereo IMA-ADPCM format documented
above. Rights in the supplied track remain with its creator; confirm permission
before redistributing a public build that contains it.
