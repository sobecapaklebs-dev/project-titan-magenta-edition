# extractor provenance

`tools/extractor/` contains the local ROM-slicing utility used by the asset
extraction scripts. Most of the utility is project-local glue for reading the
CSV file lists and writing extracted, git-ignored build artifacts.

The bundled `puff.c` / `puff.h` inflate implementation is third-party code by
Mark Adler. It carries its own zlib-style license notice in `puff.h`; the local
copy is marked as modified because this project writes the output length through
an argument pointer.

The extractor must only be run against a ROM supplied by the user. Its outputs
are ROM-derived local artifacts and must not be committed.
