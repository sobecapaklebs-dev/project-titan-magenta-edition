# Theme Engine — BuildFix23 SDK notes

BuildFix23 does not introduce a public Theme API v4. Theme API v1, v2 and Theme Shell API v3 remain compatible and existing packages are not auto-converted.

The runtime now has an internal compiled-scene capability with bounded layers, timelines and residency. Authoring work should happen on the host. `kernel/tools/titan_theme_asset.py compile-manifest` validates layer types, frame count, per-frame 40–2000ms duration, missing assets and duplicates, estimates peak CURRENT/NEXT/RECENT residency and emits a compact JSON runtime manifest. Package authors should treat the runtime working set—not total package size—as the VRAM model.

Themes remain declarative: ELF, IRX, TMOD, SO and VSM executable payloads are not theme capabilities. Dynamic parallax receives normalized shell values only. Decorative particles are budgeted and may degrade under pressure. Reserved 720p headroom is not automatically available to themes.
