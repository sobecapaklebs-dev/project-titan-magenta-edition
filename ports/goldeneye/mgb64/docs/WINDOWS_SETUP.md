# MGB64 on Windows — install & setup

The Windows download is a portable `.zip`: unzip it and run `ge007.exe`. It ships
**no game data** — you bring your own legally-dumped GoldenEye 007 ROM at runtime.

## 1. Where to install

Unzip to a **writable** folder you own, for example `C:\Games\MGB64`.

**Do not** unzip into `C:\Program Files` (or `Program Files (x86)`). Those
directories are read-only for normal users, so Windows silently redirects any
files the app writes there (config, saves, logs, optional HD texture packs) into
a per-user *VirtualStore*, which is confusing to find later. A plain folder under
your user profile or a `C:\Games` directory avoids all of that.

The zip contains:

| File | What it is |
|---|---|
| `ge007.exe` | The game (launcher + engine in one binary). |
| `SDL2.dll` | Required runtime library — keep it next to `ge007.exe`. |
| `WINDOWS_SETUP.md` | This file. |
| `README.md`, `LICENSE`, `RUN_ME.txt` | Project info + license. |

## 2. Add your ROM

MGB64 contains no ROM or copyrighted assets. Supply your own:

1. Double-click `ge007.exe` to open the launcher.
2. Click **Choose ROM…** and pick your legally-dumped GoldenEye 007 ROM
   (`.z64` / `.n64`). The path is remembered for next time.
3. Pick a level / options and start playing.

## 3. First run

- Double-clicking `ge007.exe` opens the **launcher window** — no console/terminal
  window appears (the release build uses the Windows GUI subsystem).
- The launcher is **fully controller-navigable**: D-pad or left stick to move,
  **A** to select, **B** to go back. A mouse and keyboard work too.
- In-game, press **F1** on the keyboard or the gamepad **View/Back** button to
  open the in-game overlay for live settings and quit-to-launcher.

## 4. Adding MGB64 to a game library

`ge007.exe` is a normal Windows executable, so any launcher that can add an
arbitrary program will list it. For example, in Steam:

> **Games → Add a Non-Steam Game… → Browse…** and select `ge007.exe`.

It appears in your library as **MGB64** with the app icon (read from the exe's
embedded version resource). Other device game libraries expose the same idea as
"Add app" / "Add a program" — point them at `ge007.exe`. Once added, launching it
from the library enables that launcher's overlay and controller handling.

If your launcher or device distinguishes a **gamepad/controller mode** from a
desktop/mouse mode, run MGB64 in the standard controller mode: MGB64 reads
controllers through the SDL GameController (XInput) API, the standard Windows
controller path, so a normally-presented Xbox-style pad just works.

## 5. Save data

By default, saves and config go to **`%APPDATA%\ge007\`**:

- `ge007_eeprom.bin` — in-game save file (EEPROM).
- `ge007.ini` — settings written by the launcher/overlay.

To make the install **fully portable** (saves kept beside the exe instead of in
`%APPDATA%`), create an empty file named `ge007.ini` in the install folder before
the first launch; MGB64 then uses the exe's folder as its save directory. (You can
also pass `--savedir <path>` on the command line.)

## 6. Logs & diagnostics

If something crashes or stalls, these files help diagnose it:

- **`mgb64.log`** — the full engine output for the session, at
  **`%APPDATA%\MGB64\MGB64\mgb64.log`**. The previous run is rotated to
  `mgb64.prev.log`. The app captures this via an stdout/stderr log tee whether or
  not a console window is shown, and crash diagnostics (`[CRASH]` blocks) are
  written here too.
- **`stall_watchdog.log`** — if the simulation stalls, a breadcrumb dump is
  appended here, in your save directory (`%APPDATA%\ge007\stall_watchdog.log` by
  default).

For **live** console output while debugging, use a console-subsystem build
(configure with `-DMGB64_WIN_CONSOLE=ON`) or run the GUI build from a shell with
output redirection (`ge007.exe … > out.txt 2>&1`).
