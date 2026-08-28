# Titan Disc Engine — BuildFix30

Titan Disc Engine owns game sessions whose source is physical PS2 CD/DVD media. The Shell only presents a temporary Disc tile and the normal Titan launch transition; it never becomes a CD/DVD loader.

## Launch backend
The first production backend reuses the already integrated Titan-Neutrino resident EE core in physical-CDVD mode. The command is built with `-bsd=no`, `-dvd=no`, a validated `-elf=cdrom0:\TITLE_ID;1`, the Titan exit path, and compatibility-first IGR policy. This does not make the logical session a Neutrino ISO session: Session Service records `TITAN_SESSION_ENGINE_DISC` and the Disc Engine remains owner of media/session state.

## Ownership and quiesce
Disc identification runs only through the shared storage worker. Before target handoff the Disc Engine parks that worker through Titan's proven game-exclusive storage lane, preventing metadata scan, package/download filesystem work, VMC work, or another storage client from racing CD/DVD ownership. Audio uses the normal launch handoff preparation in the Shell.

## VMC
A VMC is optional for physical-disc launch. Only a VMC already known READY in the cache is passed to Neutrino. Disc launch never creates/formats/verifies a VMC during preparation. If no READY VMC exists, the original game remains launchable with physical Memory Card semantics.

## Compatibility
Unknown physical titles default to `IGR=UNKNOWN`, `Overlay=RETURN_ONLY`. The resident core provides the proven Titan exit path and keeps the DS2 chord/System Button ingress. MINIMAL/FULL overlay must be enabled only by a materially proven compatibility profile; it is never inferred globally.
