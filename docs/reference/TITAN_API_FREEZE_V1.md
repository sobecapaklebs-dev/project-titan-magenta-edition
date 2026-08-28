# Titan API Freeze v1 — Magenta Edition

The Magenta v1 public ABI is frozen by `TITAN_API_FREEZE_V1.json`. The freeze covers the Titan App SDK, Titan Game SDK, their ABI references, Theme v4 authoring contract and BF30 Theme lifecycle contract. Any post-freeze edit changes SHA-256 and fails the verification tool until an intentional API revision is recorded.

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
