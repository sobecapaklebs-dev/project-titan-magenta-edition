# BuildFix26 — Native Port Matrix

| Component | Runtime class | Entry | Graphics ownership | Storage | Network | Return model | Major missing Titan API / blocker | BuildFix27 action |
|---|---|---|---|---|---|---|---|---|
| Internet / Browser | A TITAN_NATIVE system service | `@system-browser` | Shell/Titan | TitanStorageIo | Direct PS2IP inside kernel browser service | In-shell page | Reusable Titan Network Client/DNS/TCP/HTTP service | Refactor transport behind Titan Network Core service; keep Browser UI |
| wLaunchELF | A TITAN_NATIVE, partial parity | `.tmod` | Titan App API | list/stat/read/write/mkdir/remove/rename + ELF handoff | Not implemented in app | Titan app shutdown / stage2 for launched ELF | PFS/HDD/VMC/FTP capabilities; recursive ops; feature UI adapters | Vendor/adapt mature upstream algorithms behind Titan services |
| Project NES | A TITAN_NATIVE, incomplete core | `.tmod` | Titan media RGB565 | app sandbox/fixed ROM slot | none | `request_exit`, shell remains resident | ROM-library/save API; mature NES core; timing contracts | Keep frontend/backend; replace emulator core after R5900 gate |
| TitanEYE | A TITAN_NATIVE GAME | Titan game module/bridge | TGR/TitanVideo/pikaVR | game-mode storage ownership | Titan runtime state remains platform-owned | `game_end()` restore | hardware lifecycle proof only | Do not rewrite; hardware validate |
| Generic SAS/ELF | C EXTERNAL_ELF unless package declares native runtime | ELF/stage2 | External executable | external | external | Titan handoff/reboot semantics | N/A | Keep packaging classification independent from runtime class |

## SAS axis (independent)

The table above is runtime integration only. SAS metadata may be compliant, compatible/non-compliant, unknown or non-SAS for any package type where the packaging rules apply. No row gains TITAN_NATIVE status from SAS metadata.
