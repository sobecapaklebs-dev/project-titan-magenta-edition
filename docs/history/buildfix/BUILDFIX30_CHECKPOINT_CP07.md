# BuildFix30 CP07 — PUP / Shutdown / Reboot Finalization

Parent: CP06 Boot / Exploit / System Packaging Hardening.
Parent delta SHA-256: `ad0ca132fec74178f82ad034f7e6081a874aa1989efa8cd380d80bea9bb0a770`.

Material unit: transactional PUP publication/rollback audit; staged-file verification gate; successful-update package-lane ownership preserved through automatic reboot; bounded storage-worker quiesce; Disc Service/Disc Engine/Store/Download/Session/DateTime/Theme teardown before power boundary; power/update UX canonical-English cleanup; BF28 lifecycle/recovery regression.

Target/physical shutdown, reboot and PUP auto-reboot remain external validation.

NEXT: Store/Download/Repository v1 release revalidation + SDK/API v1 freeze.
