# Titan standalone developer SDK

The public **Titan App SDK + Titan Game Runtime (TGR) SDK** is distributed separately from the Project Titan Magenta Edition system source tree.

Release validated for this source baseline:

- standalone package: `Project-Titan-SDK-2026-08-28-FINAL-R2.zip`
- minimum system runtime: **Project Titan 0.3.93-rc17**
- native target ABI: **R5900 N32 / EF_MIPS_ABI2**
- Titan App API: v1
- Titan Game API (TGR): v2

The main system repository intentionally does **not** vendor the public
developer SDK, examples, porting guides or ABI reference bundle.

`kernel/include/titan_app_sdk.h` and `kernel/include/titan_game_sdk.h` remain in
this repository because they are the canonical runtime ABI definitions compiled
by the kernel and first-party native ports. They are not a second SDK
distribution.

First-party ports may keep private byte-identical copies of the Titan App
header where their isolated build layout requires it. Release contracts verify
those copies against `kernel/include/titan_app_sdk.h`.

pikaVR and VlombisCore remain implementation layers below the public Titan
interfaces; there is no public direct `pikaVR_*` or VlombisCore ABI.

Historical BuildFix documents under `docs/history/` are archival records and
may mention the former embedded `sdk/titan-app` / `sdk/titan-game` layout.
Those paths are not active distribution locations.
