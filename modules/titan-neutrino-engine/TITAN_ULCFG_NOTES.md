# Neutrino Titan v1.8.0 — USBExtreme file-chain extension

New loader option:

```text
-dvd-ul=<base path>,<parts>,<media>
```

Example:

```text
-dvd-ul=usb:ul.7D3BE5AE.SCES_555.55,2,20
-elf=cdrom0:\SCES_555.55;1
```

`media` accepts the values stored by USBExtreme:

- 18 / 0x12: PlayStation 2 CD
- 20 / 0x14: PlayStation 2 DVD

The extension is intentionally limited to the block-device FHI backend. File
ID backends cannot describe several independent files as one logical CDVD
handle with the current shared ABI.

Host test:

```sh
ee/loader/tests/run_ul_chain_host_test.sh
```

A real PS2DEV cross-build and hardware validation are still required.
