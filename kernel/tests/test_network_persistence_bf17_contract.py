#!/usr/bin/env python3
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
net=(ROOT/'src/services/titan_network.c').read_text(errors='replace')
ui=(ROOT/'src/ui/titan_shell_scene.c').read_text(errors='replace')
checks={
 'backup-path':'TITAN_NETWORK_SETTINGS_BACKUP' in net and 'network.bak' in net,
 'atomic-replace-with-backup':'TITAN_NETWORK_SETTINGS_BACKUP))' in net and 'TITAN_NETWORK_SETTINGS_PATH,\n        TITAN_NETWORK_SETTINGS_BACKUP' in net,
 'not-backupless':'TITAN_NETWORK_SETTINGS_TEMP,TITAN_NETWORK_SETTINGS_PATH,NULL' not in net.replace(' ',''),
 'ensure-root':'TitanStorageIo_CreateDirectory(TITAN_STORAGE_DEVICE_ROOT)' in net,
 'flush-before-publish':'TitanStorageIo_Flush(fd)' in net,
 'v3-format':'version=3\\nenabled=%d\\npreference=%s\\nssid=%s\\npmk=%s\\npmk_valid=%d' in net,
 'v2-migration':'version<2||version>3' in net,
 'no-plaintext-password':'password=' not in net and 'passphrase=' not in net,
 'pmk-persisted':'pmk=%s' in net and 'g_wifi_pmk_valid' in net,
 'backup-load':'TitanNetwork_LoadPath(TITAN_NETWORK_SETTINGS_BACKUP)' in net,
 'backup-self-heal':'if(source==2)(void)TitanNetwork_Save();' in net,
 'boot-retry':'TITAN_NETWORK_SETTINGS_LOAD_MAX_ATTEMPTS 4u' in net and 'TITAN_NETWORK_SETTINGS_LOAD_RETRY_FRAMES 30u' in net,
 'save-result-propagated':'return TitanNetwork_Save();}' in net,
 'auto-connect-preserved':'g_network.ssid[0] && g_wifi_pmk_valid && !g_wifi_autoconnect_attempted' in net,
 'save-diagnostic':'TITAN_DIAG_NETWORK,0x230' in net,
 'load-diagnostic':'TITAN_DIAG_NETWORK,0x231' in net,
 'ui-storage-error':('Nao foi possivel salvar a credencial Wi-Fi no storage do Titan' in ui or 'Could not save the Wi-Fi credential to Titan storage' in ui),
}
failed=[]
for name,ok in checks.items():
 print(f'network-bf17: {"PASS" if ok else "FAIL"} {name}')
 if not ok: failed.append(name)
if failed: raise SystemExit('failed: '+', '.join(failed))
print(f'network-bf17: PASS {len(checks)}/{len(checks)}')
