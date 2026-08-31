#!/usr/bin/env python3
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(errors='replace')
mk=read('Makefile')
scanner=read('iop/titan_ar9271/ar9271_scanner.c')
proto=read('iop/titan_ar9271/ar9271_scanner_protocol.h')
imports=read('iop/titan_ar9271/irx_imports.h')
net=read('src/services/titan_network.c')
theme=read('src/services/titan_theme.c')
pi=read('src/services/titan_package_installer.c')
ui=read('src/ui/titan_shell_scene.c')
installer=read('src/installer/titan_installer.c')
# Frozen installer / USB bootstrap invariants.
assert 'TITAN_INSTALLER_VERSION' in installer
for token in ('titan_bdm_irx','titan_bdmfs_fatfs_irx','titan_usbmass_bd_irx'):
    assert token in mk
# AR9271 is resident under the existing USBD; never reboots IOP or embeds USBD.
assert 'SifIopReset' not in scanner and 'SifIopSync' not in scanner
assert 'size_usbd' not in scanner and 'SifExecModuleBuffer' not in scanner
assert 'UsbRegisterDriver(&g_usb_driver)' in scanner
assert 'IRX_ID("titan_ar9271"' in scanner
assert 'TITAN_AR9271_SCAN_RPC_NETMAN_ACTIVE' in proto
assert '"WFI"' in scanner
# NetMan TX callback must be notification-only; USB TX belongs to worker context.
xmit=scanner.split('static void titan_netman_xmit(void)',1)[1].split('static int titan_netman_ioctl',1)[0]
assert 'send_llc_payload_netman' not in xmit
assert 'QueryIntrContext' in xmit and 'iSignalSema' in xmit and 'SignalSema' in xmit
assert 'static void titan_netman_tx_thread' in scanner
assert 'titan_netman_tx_drain' in scanner and 'send_llc_payload_netman' in scanner.split('static void titan_netman_tx_drain',1)[1].split('static void titan_netman_tx_thread',1)[0]
assert '#include <intrman.h>' in imports and '#include <netman.h>' in imports
for token in ('NetManRegisterNetIF','NetManTxPacketNext','NetManTxPacketDeQ'):
    assert token in scanner
# Both physical transports are built; exact proven AR9271 firmware is required externally.
for token in ('TITAN_PS2DEV9_IRX','TITAN_NETMAN_IRX','TITAN_SMAP_IRX','TITAN_AR9271_IRX','TITAN_AR9271_FW'):
    assert token in mk
assert '-lps2ip' in mk and '-lnetman' in mk
# EE network core: runtime SSID/PMK, PBKDF2 4096, PS2IP-owned NetMan init, DHCP.
assert 'iter<4096u' in net
assert 'TITAN_AR9271_SCAN_RPC_SET_CONFIG' in net
assert 'TITAN_AR9271_SCAN_RPC_NETMAN_ACTIVE' in net
assert 'ps2ipInit' in net and 'ps2ip_setconfig' in net
assert 'NetManSetMainIF' in net and '"SMA"' in net and '"WFI"' in net
initseg=net.split('static void TitanNetwork_InitInterfaces(void)',1)[1].split('static int TitanNetwork_BindWifiRpc',1)[0]
assert 'netman_init_result=NetManInit();' in initseg
assert initseg.index('netman_init_result=NetManInit();') < initseg.index('NetManSetMainIF("SMA")')
assert initseg.index('NetManSetMainIF("SMA")') < initseg.index('ps2ipInit(&ip,&nm,&gw)')
assert initseg.index('ps2ipInit(&ip,&nm,&gw)') < initseg.index('TitanNetwork_SelectMainInterface(')
assert 'NetManSetLinkMode(NETMAN_NETIF_ETH_LINK_MODE_AUTO)' not in initseg
assert 'TITAN_NETWORK_BOOT_GRACE_FRAMES' in net
assert 'TitanNetwork_EnsureWifiDriver' in net and 'TitanNetwork_EnsureEthernetDriver' in net
assert 'password' in net and 'pmk=' in net
assert 'g_wifi_autoconnect_attempted' in net and '(void)TitanNetwork_ConnectWifi();' in net
# Five native themes including all four renamed Ribbon variants; no package required.
for token in ('"SCARLETFLOW"','"FLOWWINE"','"FLOWCYAN"','"FLOWTWILIGHT"'):
    assert token in theme
assert 'g_theme_count=5u' in theme
# Current hardware-proven built-ins use the restored Snake Flow baseline.
for token in ('wave->reference_ribbon_enabled=0u','wave->snake_enabled=1u','wave->snake_extent_x=520.0f','wave->snake_extent_y=320.0f'):
    assert token in theme
for token in ('"Ribbon Scarlet"','"Ribbon Vinho"','"Ribbon Ciano"','"Ribbon Crepusculo"'):
    assert token in theme
assert 'TitanTheme_CanDelete' in theme and 'TitanTheme_Delete' in theme
assert 'TitanPackageInstaller_RefreshThemes' in pi
# Settings/UI requirements.
assert '#define TITAN_SETTINGS_CATEGORY_COUNT 10' in ui
assert '"Network"' in ui and 'TitanShellScene_RunNetworkAction' in ui
assert '"Add theme"' in ui and 'TitanShellScene_OpenThemeInstaller' in ui
assert 'TITAN_INPUT_BUTTON_TRIANGLE' in ui and 'TitanTheme_Delete' in ui
assert 'g_scene.settings_selected_category == TITAN_SETTINGS_SYSTEM &&\n                        g_scene.settings_selected_row == 0' in ui
assert 'TitanShellScene_OpenSystemUpdate();' in ui
print('network-theme-integration contract: PASS')
