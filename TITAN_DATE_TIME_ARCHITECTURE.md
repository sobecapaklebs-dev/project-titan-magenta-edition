# Titan Date / Time Architecture — BuildFix30

## Ownership

BuildFix30 introduces one Titan DateTime Service. `titan_time` remains monotonic uptime only; `TitanDateTime` owns civil time policy; `TitanSystemClock` is now a shell display adapter and no longer polls CDVD RTC independently.

## Clock model

- Raw PS2 RTC is read once as the boot fallback when IOP/CDVD is available.
- The OSD local conversion is sampled only to derive the existing local offset so BF30 does not silently change historical display behavior.
- Titan keeps an internal UTC anchor plus monotonic uptime delta. Timezone is applied separately for presentation.
- Internet synchronization and manual adjustment update the software UTC anchor. BF30 deliberately does not rewrite the physical RTC automatically.
- Invalid RTC civil fields never become a valid Titan wall clock.

## Settings

System Settings expose Date, Time, Automatic Time, Time Zone, Clock Format, Date Format and Internet Time. When Automatic Time is disabled, LEFT/RIGHT on Date adjusts one day and on Time adjusts one minute. Timezone is bounded to UTC-12:00..UTC+14:00 in 30-minute steps. Settings changes are persisted to `mass:/TITAN/system/time.cfg` by the service worker rather than by recurring frame-path I/O.

## Failure policy

No wall-clock failure is a boot blocker. Missing/invalid RTC yields an unavailable clock until a valid source exists. Offline/DNS/socket/timeout/invalid-SNTP failures leave the last valid RTC/manual/SNTP anchor intact and emit bounded diagnostics.

## Validation truth

Host civil-time/SNTP parser and architecture contracts pass. PS2 target compile and physical RTC/network behavior remain external gates.
