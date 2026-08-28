#ifndef TITAN_PS2_MAC_H
#define TITAN_PS2_MAC_H

/* User-supplied fixed MAC read from the PlayStation 2 BIOS/network menu.
 * V10.10 deliberately keeps the AR9271 station identity to this value for the
 * entire Wi-Fi stack (hardware STA registers, VIF/node, 802.11, WPA2, DHCP). */
#define TITAN_PS2_BIOS_MAC_BYTES 0xfcu,0x0fu,0xe6u,0x5bu,0xeau,0xf9u
#define TITAN_PS2_BIOS_MAC_TEXT  "FC:0F:E6:5B:EA:F9"

#endif
