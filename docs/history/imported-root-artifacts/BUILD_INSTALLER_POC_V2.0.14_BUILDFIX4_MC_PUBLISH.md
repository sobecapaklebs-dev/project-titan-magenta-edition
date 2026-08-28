# Titan Installer v2.0.14 BuildFix4 — Memory Card publish path

## Physical hardware evidence

BuildFix3 `INSTALLER(5).LOG` proved the previous permission fix:

- `OPEN_FLAG_TRANSLATION backend=libmc-iop posix=0x0 iop=0x1`.
- `mc0:/BOOT/BOOT.ELF.TITAN.NEW` was written completely.
- The full staging file was read back and CRC-verified successfully.
- Failure moved to publication at 90%.

The failed publish sequence was:

1. `mcRename(BOOT.ELF.TITAN.NEW -> BOOT.ELF)` -> `-1`.
2. Generic stream fallback used POSIX `stat()` to decide `BOOT.ELF` existed.
3. `mcDelete(BOOT.ELF)` -> `-4`.
4. Transaction rolled back before the stream destination was opened.

PS2SDK documents that `rom0:MCSERV` does not support `mcRename()`.

## BuildFix4 correction

For a Memory Card destination (`mcX:`):

- publishing bypasses the rename-first path;
- the generic delete-before-stream step is skipped;
- the already verified `.TITAN.NEW` staging file is opened as the source;
- the destination is opened through libmc with `O_WRONLY|O_CREAT|O_TRUNC`, translated to the IOP/FIO flag domain by BuildFix3;
- data is copied cooperatively using the existing asynchronous libmc state machine;
- staging cleanup remains best-effort after successful publication.

USB/FAT publication is unchanged and retains FileXio asynchronous rename first.

No TINS, UI, renderer, theme, boot path, installation selection, or transaction format change was made.
