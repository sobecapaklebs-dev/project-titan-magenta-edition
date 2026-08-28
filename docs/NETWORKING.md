# Project Titan networking

Project Titan includes Ethernet and experimental Wi-Fi support as part of its
platform/networking work.

## Ethernet

Ethernet is appropriate for community testing after the system boots. Useful
reports include PS2 model, adapter or built-in Ethernet hardware, link state,
DHCP result, IPv4 address, gateway/DNS behavior and reconnect behavior.

## Wi-Fi

Wi-Fi is **experimental** in the current public release line. Adapter coverage
is not exhaustive; the RC1 installer must not be interpreted as a claim that
every USB Wi-Fi device is supported.

Development has included an Atheros AR9271 / ath9k_htc path. Firmware
redistribution is a separate licensing concern, so private development firmware
must not be assumed to be bundled in public media.

Useful Wi-Fi reports include:

- adapter chipset and VID:PID when known;
- detection and scan results;
- SSID visibility;
- authentication result;
- DHCP/IP/DNS result;
- disconnect/reconnect behavior;
- audio, storage or UI regressions while networking is active.

Do not post Wi-Fi passwords, credentials or private network dumps.

## RC1 boundary

`installer-test-rc1` supports the **Semi-autonomous / DEV1** installation path.
Networking tests are post-install system tests. The Autonomous / Free McBoot
installer path remains experimental and is not enabled in RC1 public media.
