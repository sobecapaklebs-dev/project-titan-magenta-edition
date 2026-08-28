# ido5.3_recomp local placeholder

The public MGB64 repository does not vendor IDO static-recompilation source
or any SGI IDO/IRIX compiler input files.

The N64 byte-matching target can use a local static recompilation of Silicon
Graphics' **IDO 5.3** C compiler for period-accurate codegen. If you are working
on that path, populate this ignored directory from an external copy of the
tooling you are legally comfortable using, then provide your own legally obtained
IDO/IRIX compiler input files under `tools/irix/root` (for example
`tools/irix/root/usr/bin/cc` and
`tools/irix/root/usr/lib/err.english.cc`).

Both the external recompilation tooling you place here and the IDO/IRIX compiler
input files are local-only. They are not part of the public checkout, not
covered by MGB64's MIT license, and must not be committed or redistributed here.

The native CMake port does not need any of this.
