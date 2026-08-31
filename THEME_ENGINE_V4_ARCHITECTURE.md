# Theme Engine v4 Architecture — BF28 CP12

`scene-v4` is declarative and available only with `shell_api=4`. It does not replace or reinterpret API v1-v3.

Authoring source is JSON for convenience; `titan_theme_asset.py compile-v4` converts it into a bounded line manifest consumed by the target runtime. The manifest declares up to 32 small RGB565 layers, z-order, transforms, alpha, X/Y parallax, HOME/TOP context, priority and timeline mode. Executable theme payloads remain forbidden.

Budget envelope is explicit in both `theme.cfg` and the scene manifest. Runtime defaults are 512 KiB theme VRAM, 2 MiB EE RAM, and 256 KiB reserved headroom; package/runtime validation reject invalid envelopes. Layer assets are currently bounded to 256x128 to fit the existing adaptive image scratch path instead of reserving a second fullscreen texture system.

CP12 closes schema/parser/validator/tooling. The target renderer/residency path is the next material unit and is not claimed complete by this checkpoint.
