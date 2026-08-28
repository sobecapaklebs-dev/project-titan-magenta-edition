# gzipsrc

This directory contains **gzip** source, used by the asset build pipeline for
DEFLATE (de)compression of ROM-extracted data.

- **Upstream:** GNU gzip — https://www.gnu.org/software/gzip/
- **Authors:** Jean-loup Gailly, Mark Adler, and contributors.
- **License:** GNU General Public License, version 2 or later (GPL-2.0-or-later).

gzip is **not** original to MGB64 and is **not** covered by this project's MIT
license. The full text of the GNU GPL v2 is included in `COPYING`, matching the
per-file headers' `see the file COPYING` notice.

> Maintainer note: this vendored copy can be replaced by the system `gzip` to
> avoid bundling GPL sources; that is tracked as a possible cleanup.
