#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1]).resolve();sdk=(r/'kernel/include/titan_app_sdk.h').read_text(errors='replace');rt=(r/'kernel/src/services/titan_app_runtime.c').read_text(errors='replace');w=(r/'apps/wlaunchelf/src/wlaunchelf_titan.c').read_text(errors='replace')
def req(x,m):
 if not x: raise SystemExit('buildfix27-app-ftp-integration: FAIL: '+m)
req('ftp_start' in sdk and 'ftp_stop' in sdk and 'ftp_status' in sdk,'append-only FTP app API missing')
req(sdk==(r/'apps/wlaunchelf/include/titan_app_sdk.h').read_text() and sdk==(r/'apps/project-nes/include/titan_app_sdk.h').read_text(),'SDK copies diverged')
req('TitanAppApi_FtpStart' in rt and 'TitanAppRuntime_StoragePathSafe(absolute_root, 1)' in rt,'runtime writable-root policy missing')
req('TitanFtpService_Tick();' in rt and 'TitanFtpService_Stop();' in rt,'runtime FTP lifecycle containment missing')
req('TITAN_APP_BUTTON_L3' in w and 'wl_toggle_ftp' in w and 'api->ftp_start' in w and 'api->ftp_stop' in w,'wLaunchELF FTP UI integration missing')
req('socket(' not in w and 'fileXio' not in w,'wLaunchELF gained forbidden network/storage ownership')
print('buildfix27-app-ftp-integration: OK append-only lifecycle/status API + wLaunchELF L3 control')
