# Asset provenance and release policy

The active UI uses the curated asset tree restored from repository commit
`1d988cf`, the final pre-audit state. The protected file-by-file hashes are in
`kernel/assets/ui/CURATED_ASSET_SHA256SUMS`.

## Curated runtime assets

The restored set includes the original Project Titan icon atlas, 199-avatar
atlas and source cells, boot mark, DEX APP_HOME/FTP_HOME artwork, game
placeholder, alert icon and PS2 Slim power artwork. The accompanying notice
files inside `kernel/assets/ui` preserve the source descriptions available in
the project history.

RC8 adds only the owner-supplied Debug Settings two-arrow symbol. Its
background was removed, the mark was normalized as a white alpha mask and it
was placed in the previously unused twelfth icon-atlas cell. See
`kernel/assets/ui/DEBUG_SETTINGS_ICON_ASSET_NOTICE.txt`. Existing atlas cells
were not regenerated or replaced.

Run the following command to verify that none of these assets were replaced:

```sh
make -C kernel system-assets
```

That target verifies hashes; it does not regenerate or overwrite artwork.

## Procedural fallback assets

`kernel/tools/generate_system_assets.py` remains available only for fallback
and host-test output. By default it writes to `build/fallback-system-assets`.
It refuses to write into `kernel/assets/ui` unless
`--force-curated-overwrite` is passed explicitly.

## Font

`titan_ui_font.a8` was rasterized from Inter. RC8 uses the final unused atlas
cell for a deterministic geometric U+2605 mask; the 95 ASCII cells and their
kerning table remain unchanged. The original font binary is not included;
`FONT_LICENSE_INTER.txt` and `FONT_NOTICE.txt` retain the licensing and
regeneration instructions.

## Background music

RC11 includes the track supplied by the project owner as
`runtime/TITAN/audio/magenta_melody.wav`: "Nostalgic Soundscapes S02E03
Dimensions - PS2 Console inspired Retro Ambient", credited in the supplied
filename to Retrovex Ambient. The conversion begins at source timestamp
`00:01:30.000` and preserves the complete remaining duration as 32 kHz stereo
IMA-ADPCM. Its runtime SHA-256 is
`047f1bf49c4d3355185bb6579b9eb029f545997a08f9f3648a68d988e6967ea0`.

This provenance record identifies the supplied source and deterministic runtime
derivative; it does not grant redistribution rights. Confirm the creator's
licence or permission before publishing a build containing the track.

## Redistribution note

Some curated avatars and recognizable artwork were supplied for proof-of-concept
and personal testing. Rights in third-party characters, artwork and marks remain
with their respective owners. Confirm redistribution rights before publishing a
binary or source package containing those assets.
