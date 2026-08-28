#!/usr/bin/env python3
from pathlib import Path
r=Path(__file__).resolve().parents[2]
c=(r/'kernel/src/services/titan_datetime.c').read_text();h=(r/'kernel/src/services/titan_datetime.h').read_text();k=(r/'kernel/src/core/titan_kernel.c').read_text();shell=(r/'kernel/src/ui/titan_shell_scene.c').read_text();clock=(r/'kernel/src/platform/ps2/titan_system_clock.c').read_text();mk=(r/'kernel/Makefile').read_text()
def req(v,m):
    if not v: raise AssertionError(m)
req('src/services/titan_datetime.o' in mk,'datetime service not in target build')
req('sceCdReadClock' in c and 'configConvertToLocalTime' in c,'RTC raw/local separation missing')
req('TitanDateTime_ValidateCivil' in c and 'TitanDateTime_CivilToUnix' in c,'RTC/date validation core missing')
print('titan-rtc-contract: OK')
for token in ('Automatic Time','Time Zone','Clock Format','Date Format','Internet Time'):
    req(token in shell, f'date/time setting missing: {token}')
req('TitanDateTime_AdjustManualDays' in shell and 'TitanDateTime_AdjustManualSeconds' in shell,'manual date/time adjustment missing')
print('titan-date-time-settings-contract: OK')
req('timezone_minutes' in h and 'TitanDateTime_CycleTimezone' in c and 'UTC%c%02d:%02d' in c,'timezone model missing')
print('titan-timezone-contract: OK')
for token in ('SOCK_DGRAM','IPPROTO_UDP','gethostbyname','htons(123)','select(','TITAN_DATETIME_SNTP_TIMEOUT_MS','TitanDateTime_ParseSntpResponse'):
    req(token in c,f'SNTP bounded transport missing {token}')
req('TitanDateTime_StartWorker(TITAN_DATETIME_JOB_SNTP)' in c,'SNTP is not worker-owned')
req('TitanDateTime_RequestSync' in shell,'manual Internet Time request missing')
print('titan-internet-time-contract: OK')
req('TITAN_DATETIME_SYNC_WAIT_NETWORK' in c and 'if(!network_online)' in c,'offline fallback state missing')
req('TITAN_DATETIME_SOURCE_RTC' in c,'RTC fallback missing')
print('titan-internet-time-offline-contract: OK')
req('TitanDateTime_Update(network_status != 0 && network_status->state == TITAN_NETWORK_ONLINE' in k,'datetime update not integrated after nonblocking network update')
req('gethostbyname' not in k and 'recvfrom' not in k,'blocking time network work leaked into kernel frame path')
req('TitanDateTime_Shutdown();' in shell,'datetime lifecycle shutdown missing')
print('titan-time-boot-nonblocking-contract: OK')
req('TitanDateTime_GetSnapshot' in clock and 'sceCdReadClock' not in clock,'legacy shell clock still owns RTC polling')
print('titan-system-clock-single-owner-contract: OK')
