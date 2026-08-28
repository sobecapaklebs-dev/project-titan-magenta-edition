# Titan API Freeze v1 — Magenta Edition

The Magenta v1 public ABI is frozen by `TITAN_API_FREEZE_V1.json`. The developer-facing Titan App SDK and Titan Game SDK are distributed as the separate standalone SDK bundle; the canonical runtime ABI headers remain in `kernel/include/`. The freeze also covers Theme v4 authoring and the BF30 Theme lifecycle contract. Any post-freeze ABI edit changes SHA-256 and fails the verification tool until an intentional API revision is recorded.

## Levels

The supported public API levels are HIGH, MID and ADVANCED.

- HIGH: Titan App/Game UI, files, HTTP/network, audio, normalized input and graphics APIs.
- MID: documented Runtime/TGR/VFS/network/pikaVR/session/theme services used by platform integrations; not every internal header is exported to ordinary applications.
- ADVANCED: explicit PS2/VU/DMA/GS/IOP integration surfaces; developers opt in and remain responsible for target constraints.

## Input

BF29 normalized device identity, player assignment, connection, capabilities, honest battery state/validity and Titan-owned System Button policy are preserved. System/Home is not exposed as an ordinary game button while Titan owns the session.

## Disc

Normal applications do not receive raw CD/DVD ownership. Disc Service/Disc Engine remain platform-arbitrated. Public application code should consume session/content abstractions instead of touching CDVD hardware directly.

## Compatibility rule

v1 changes after this freeze must be append-only or versioned; removing/reordering ABI fields or silently changing ownership semantics is prohibited.

## Distribution boundary

The active system source tree does not vendor `sdk/titan-app` or `sdk/titan-game`. Developer examples, porting guidance and ABI reference material ship in `Project-Titan-SDK-2026-08-28-FINAL-R2.zip`. Historical BuildFix records may still mention the former embedded layout.
