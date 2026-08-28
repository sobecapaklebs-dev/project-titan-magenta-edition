# Confirmed deployed baseline — 2026-08-05

The source consolidation compared the USB-drive executables against development
candidates. The confirmed hashes were:

| Component | Canonical/observed path | SHA-256 |
|---|---|---|
| Titan Shell kernel | `/kernel.elf` | `7dc14dfbe25f2e56ec3fa00aaf8d17a0655d196396c128f5011ce6de4cb63da2` |
| Titan bootloader | `/titan.elf` | `aafae299177c1902eefb51ec771136e6ad5c3f7f19516a9fdacce79ccac02a76` |
| Titan UL Engine (legacy name) | `/TITAN/titan_ul_loader.elf` | `ec7e3a274bd20edb0bcf694c2b32f826c89c6cdbbb72702c4396342cb227d9eb` |
| Titan Neutrino Engine | `/neutrino/neutrino.elf` | `3f76675330490ef2137814266d5d8ab8c3938e0bf2e665e2c3237b59d8a1ad50` |
| UL catalog | `/ul.cfg` | `c71d438732bc40095632d9e4b59d5f782b47d95abbb0d9b5c939f0e83a479254` |

RC2 changes the canonical UL executable name to
`/TITAN/titan_ul_engine.elf`. The installer preserves and disables the legacy
name rather than using two active copies.
