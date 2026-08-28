# RT1 BuildFix3 — PS4-style Network Settings / automatic Wi-Fi scan

This build keeps the official BF6 bootloader and the RT1 BuildFix2 deferred-network boot safety unchanged, and changes only the Network Core / AR9271 discovery path / Network Settings UI.

## Wi-Fi discovery

- AR9271 attachment now starts a passive 2.4 GHz scan even when no SSID or PMK has been configured.
- The existing TT1426 beacon parser and AP cache are used directly; no second Wi-Fi scanner was introduced.
- Scan results expose SSID, BSSID, RSSI, channel and security through the resident RPC/Network Core.
- A configured Wi-Fi boot completes a full first channel pass before reconnecting, so Settings receives a useful network list instead of stopping immediately at the saved SSID.
- `Triangle` on the Wi-Fi list requests a fresh scan when the adapter is not currently associated.
- Manual SSID entry remains only as `Outra rede...` for hidden/not-listed networks.

## Settings > Network

Root page follows the supplied PS4 reference flow:

1. Conectar-se à Internet
2. Configurar conexão à Internet
3. Testar conexão à Internet
4. Exibir status da conexão
5. Exibir status dos serviços de rede

Setup flow:

`Configurar conexão à Internet -> Usar Wi-Fi / Usar cabo LAN -> Fácil / Personalizada -> Wi-Fi network list (for Wi-Fi)`

The supplied video demonstrates the Easy Wi-Fi path. BuildFix3 preserves a Personalizada branch in the hierarchy but does not invent manual IPv4/DNS screens that were not exercised in the supplied reference clip.

## Boot / IOP safety

- Network bootstrap remains deferred by 120 shell frames from BuildFix2.
- The official BF6 bootloader is untouched.
- AR9271 still shares the resident USBD with BDM/USB mass and does not reset IOP/USBD.
- Network enable/disable is persistent and re-enable is idempotent: already resident NETMAN/ps2ip/backend modules are not loaded a second time.

## Validation boundary

Host/source contracts validate syntax, RPC wiring, automatic-scan state transitions and UI hierarchy. Real PS2 validation is still required for the combined BF2 boot fix + BuildFix3 automatic discovery flow.
