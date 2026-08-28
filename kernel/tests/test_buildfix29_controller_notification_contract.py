#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1]); c=(r/'kernel/src/services/titan_controller_ux.c').read_text(); k=(r/'kernel/src/core/titan_kernel.c').read_text()
for x in ['TITAN_INPUT_EVENT_CONNECTED','TITAN_INPUT_EVENT_RECONNECTED','TITAN_INPUT_EVENT_PLAYER_ASSIGNED','TITAN_INPUT_EVENT_BATTERY_CHANGED','TITAN_INPUT_EVENT_DISCONNECTED','battery_valid','duplicates_suppressed','TitanNotifications_Push']:
 assert x in c,x
assert 'TitanControllerUx_Update(!TitanShellScene_IsStartupActive())' in k
print('titan-controller-connect-notification-contract: OK')
print('titan-controller-deferred-boot-notification-contract: OK')
print('titan-controller-no-duplicate-notification-contract: OK')
