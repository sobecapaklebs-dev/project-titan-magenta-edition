# Titan Theme Dynamic Lifecycle — BuildFix30

BuildFix30 makes dynamic-theme lifetime a shell/platform policy rather than a theme-specific startup workaround.

Lifecycle states are `PRE_HOME`, `HOME_ENTER`, `HOME`, `TOP`, `SUBMENU`, `HOME_EXIT`, and `RETURN_HOME`. `PRE_HOME` keeps the active theme palette and official Ribbon while dynamic scene authorization, scene animation updates, rare-event work, and scene rendering remain disabled. `HOME_ENTER` is the first state that can request scene resources; its real shell progress is the scene reveal envelope. `RETURN_HOME` is a distinct re-entry state and does not replay the cold-boot Health/Welcome sequence.

Dynamic scene filesystem work is never performed by `TitanShellBackground_Draw()` or `TitanShellBackground_RefreshThemeAssets()`. HOME authorization queues the work to the existing serialized Titan storage worker. Until resources are ready the Ribbon remains the visual fallback. Theme v4 update/render is skipped entirely at zero lifecycle opacity.

`TOP` preserves the already-live scene and uses the existing top transition. Functional submenus keep the Ribbon contract. Theme API v1, v2, v3 and v4 validation rules remain supported.

Theme music may opt into `audio_music_home_only=1`. In that mode music requests are rejected before `HOME_ENTER` and become eligible when the lifecycle authorizes HOME. SFX remain governed by their existing independent paths.
