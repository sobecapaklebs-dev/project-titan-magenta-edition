# Titan Installer v2.0.14 BuildFix5 — MC transport isolation

## Physical-hardware evidence

BuildFix4 reached `Publicando por streaming: titan.elf` and the PS2 then hard-froze: UI wave/rendering stopped and USB activity stopped. BuildFix4's publication path had already validated the Memory Card staging file, then opened the staging file on the same Memory Card for reading and `BOOT.ELF` for writing and alternated libmc RPCs between them.

The USB LED going idle during that BF4 state was expected because the copy was MC -> MC. The UI/render freeze was not expected and makes the MC -> MC transport path unacceptable for the installer.

## BuildFix5 rule

**No transactional copy is allowed to use Memory Card as both source and destination.**

For an MC-bound manifest entry:

1. payload staging is created on the installer USB as `.MC_STAGE_NNN.TITAN.NEW`;
2. source CRC and staging CRC are still verified before publication;
3. final publication streams USB -> MC using libmc only for the destination;
4. existing MC files are preserved through a USB `.MC_BACKUP_NNN.TMP` transport hop;
5. rollback restores USB -> MC with `O_TRUNC`, without an MC rename or a pre-delete;
6. synchronous `stat(mcX:)` is not used to validate completion of the MC publish path; exact transferred byte counts and successful close are authoritative.

This retains the BF2 `NEWLIB_PORT_AWARE` header-order fix and BF3 O_* -> FIO_O_* libmc translation.

## Scope retained

No change to:

- TINS v1 format or payload layout;
- installer UI/theme/wave geometry;
- USB FileXio rename publication for normal USB destinations;
- manifest selection;
- bootloader payload contents;
- package/update formats.

## Recovery note after a BF4 freeze

BF4 opened `mc0:/BOOT/BOOT.ELF` with `O_TRUNC` immediately before the visible streaming-publish state. After a hard reset, that destination must be treated as potentially partial. Launch the installer through a separate known-working boot path rather than assuming that `BOOT.ELF` is still usable.

`TITAN_SYSTEM.TINS` is unchanged. Only the installer ELF needs to be rebuilt/replaced for BuildFix5.
