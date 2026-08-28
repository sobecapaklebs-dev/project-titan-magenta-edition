# Backend compatibility matrix

| Storage/game form | Discovery in Titan Shell | Launch backend | VMC handoff |
|---|---:|---|---:|
| Local `.iso` | Yes | Titan Neutrino Engine | Explicit active profile VMC |
| Local USBExtreme/UL (`ul.cfg` + parts) | Yes | Titan UL Engine | Explicit `-vmc0=` override |
| Local `.zso` / `.cso` | No claim in this release | Not exposed by local scanner | N/A |
| Local `.chd` | No | No local PS2 backend | N/A |
| Network compressed image via Neutrino PC helper | Helper-dependent | Titan Neutrino Engine sees a virtual ISO | Backend-dependent |

The project must not advertise local CHD support merely because the optional PC
UDPFS helper can decompress CHD. Hardware compatibility remains per-game and
must be recorded from a physical PS2. Marvel vs. Capcom 2 has launched through
the UL chain; that result is not a blanket compatibility guarantee.
