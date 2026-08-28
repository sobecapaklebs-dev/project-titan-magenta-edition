# BuildFix30 — Autonomous Boot Chain

The release autonomous chain is Free McBoot only and is provisioned from inside the Project Titan installer process. There is no user-visible FMCB installer handoff.

```text
POWER ON
  -> Free McBoot
  -> mc?:/TITAN/BOOT/TITAN_FMCB_ENTRY.ELF
  -> mc?:/BOOT/BOOT.ELF
  -> Titan Bootloader (validated BuildFix6 lineage)
  -> integrity/system lookup
  -> Titan kernel/system
  -> Health & Safety -> Welcome/Profile -> HOME
```

Eligibility is fail-closed: product autonomous gate is ROM <= 2.20 plus known compatible console identity. Unsupported or unknown consoles expose only the semi-autonomous DEV1/Matrix/Modbo installation path.
