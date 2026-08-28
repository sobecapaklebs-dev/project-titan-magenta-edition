#!/usr/bin/env python3
from pathlib import Path
root=Path(__file__).resolve().parents[1]
def read(rel): return (root/rel).read_text(errors='replace')
proto=read('iop/titan_ar9271/ar9271_scanner_protocol.h')
iop=read('iop/titan_ar9271/ar9271_scanner.c')
net=read('src/services/titan_network.c')
hdr=read('src/services/titan_network.h')
ui=read('src/ui/titan_shell_scene.c')
assert '#define TITAN_AR9271_SCAN_RPC_START_SCAN 8' in proto
assert 'automatic Wi-Fi scan starting' in iop
assert 'run_passive_scan_phase' in iop
assert 'g_info.ap_count=0' in iop and 'memset(g_info.aps,0,sizeof(g_info.aps))' in iop
assert 'if (g_wifi_configured)' in iop and 'SignalSema(g_work_sema);' in iop
assert 'TITAN_AR9271_SCAN_RPC_START_SCAN' in net
assert 'TitanNetwork_RequestWifiScan' in net
assert 'TitanNetwork_GetWifiNetworkCount' in net
assert 'TitanNetwork_GetWifiSignalPercent' in net
assert 'wifi_adapter_present' in hdr and 'wifi_scanning' in hdr
for labels in [
    ('Conectar-se à Internet','Connect to the Internet'),
    ('Configurar conexão à Internet','Set Up Internet Connection'),
    ('Testar conexão à Internet','Test Internet Connection'),
    ('Exibir status da conexão','View Connection Status'),
    ('Exibir status dos serviços de rede','View Network Services Status'),
    ('Usar Wi-Fi','Use Wi-Fi'),
    ('Usar cabo LAN','Use LAN Cable'),
    ('Fácil','Easy'),
    ('Personalizada','Custom'),
    ('Qual rede Wi-Fi deseja usar?','Which Wi-Fi network do you want to use?'),
    ('Outra rede...','Other network...')
]:
    assert any(label in ui for label in labels), labels
assert 'TitanNetwork_GetWifiNetwork(' in ui
assert 'TitanNetwork_GetWifiSecurityName' in ui
assert 'TitanNetwork_GetWifiSignalPercent' in ui
assert 'TitanShellScene_OpenWifiSsidInput' in ui  # hidden/other network fallback only
# Manual SSID must no longer be a root Network Settings row.
case=ui.split('case TITAN_SETTINGS_NETWORK:',1)[1].split('case TITAN_SETTINGS_VIDEO:',1)[0]
assert 'Wi-Fi Network' not in case
assert 'Wi-Fi Password' not in case
print('network PS4-style auto-scan UI BF3 contract: PASS')
