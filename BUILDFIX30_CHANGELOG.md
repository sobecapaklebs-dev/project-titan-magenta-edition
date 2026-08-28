# BuildFix30 Changelog

- Added DateTime/RTC/timezone/manual formats and bounded SNTP with offline fallback.
- Finalized global Theme v4 lifecycle: pre-HOME Ribbon/palette, HOME_ENTER scene authorization, HOME-only music gate and RETURN_HOME.
- Added native Disc Service state machine, SYSTEM.CNF/boot/Title ID/region handling and physical PS2 CD/DVD identification.
- Added Disc Session/Disc Engine, temporary HOME tile, conservative Smart IGR, optional cached VMC and resident Neutrino physical-disc handoff.
- Added English canonical shell text plus English/Portuguese-BR/Spanish localization architecture and persistent Language setting.
- Preserved BuildFix6/wLaunchELF-order boot baseline and hardened TINS release identity with SHA-256/manifest metadata without changing TINS v1 on-target format.
- Revalidated transactional PUP auto-reboot and power quiesce including Disc/Store/Download/Session teardown.
- Froze Magenta v1 public App/Game/Theme API artifacts with deterministic SHA-256 manifest; froze Repository Protocol v1 foundation.
