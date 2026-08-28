# mktex provenance

`tools/mktex/` is texture-conversion tooling for local asset inspection and
debugging. It is not part of the native runtime and it should only process
texture data extracted from a ROM supplied by the user.

`src/libpdtex/reader.c` notes that its texture decompression routines were copied
from the Perfect Dark decompilation's `texdecompress.c`. The upstream Perfect
Dark decompilation is MIT-licensed; the local copy of that notice is
`LICENSE.perfect_dark`. Keep that attribution when modifying this area. The rest
of this directory is project-specific conversion glue unless a file-level notice
says otherwise.

Generated PNGs or binary texture inputs are ROM-derived artifacts and must not
be committed.
