# Legal Disclaimer

**MGB64 is a decompilation and native source port project, created for
research, preservation, and educational purposes. It is an unofficial fan
project and is not affiliated with, endorsed by, or sponsored by any rights
holder.**

## What this repository contains

- **Decompiled game code** — C and assembly that reproduces the behavior of the
  original 1997 Nintendo 64 first-person shooter. As with the
  [Super Mario 64](https://github.com/n64decomp/sm64) and
  [Ocarina of Time](https://github.com/zeldaret/oot) decompilation projects,
  this is a transformative, human-readable reimplementation of the game's logic.
  It includes the small, code-coupled data tables that are inseparable from that
  logic (AI scripts, collision tables, level/room index tables, display lists).
- **Original first-party work** — the native platform/port layer (rendering,
  audio, input, file I/O), the build system, extraction tooling, and
  documentation. This first-party work is offered under the [MIT License](LICENSE).

## What this repository does NOT contain

It contains **no** game ROM and **no** bulk copyrighted assets — no textures,
audio, music, models, fonts, level/scene data, image tables, or logos. It
contains no proprietary SDK or library binaries. None of that data is present
in the repository or anywhere in its git history.

To build or run anything useful you must **provide your own copy of the
original game that you legally own and dumped yourself.** The build extracts the
assets it needs from that copy on your machine. Those extracted files are
git-ignored and must **never** be committed or redistributed.

## Trademarks and rights

The original game — its title, characters, code, music, and all related assets —
is the property of its respective rights holders, which may include (without
limitation) **Nintendo**, **Rare** / **Microsoft**, **MGM**, **Danjaq LLC**,
**EON Productions**, and **Activision**. All trademarks are the property of
their respective owners.

"MGB64" is an unofficial project codename and is not a product name of any
rights holder.

## Acceptable use

- **Do** use this project with a ROM you legally own, for personal, educational,
  preservation, or research purposes.
- **Do not** use this project to obtain, share, or distribute the game or any of
  its assets.
- **Do not** redistribute builds that bundle copyrighted ROM-derived data.
- **Do not** commercially use, sell, or promote the original game, its assets,
  its trademarks, or any build that bundles ROM-derived data.

## No warranty

This software is provided "as is", without warranty of any kind. See
[LICENSE](LICENSE). You are solely responsible for ensuring your use complies
with the laws of your jurisdiction.

If you are a rights holder and have a concern about this repository, please open
an issue or contact the maintainers, and it will be addressed promptly.
