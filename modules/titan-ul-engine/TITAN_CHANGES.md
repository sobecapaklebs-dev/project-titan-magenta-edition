# Titan UL Engine changes

This maintained Open PS2 Loader derivative is the direct USBExtreme/UL backend
for Project Titan Magenta Edition. Upstream licence, credits and authorship are
preserved.

Titan-specific changes in this source line include:

- headless direct launch selected by the `titan-ul` argument;
- explicit startup identifier and `-vmc0=<path>` parsing;
- validation of the supplied VMC path, expected image length and superblock;
- active-profile VMC application in the BDM launch path;
- exact/partial `ul.cfg` record handling and bounded title lookup;
- explicit resource cleanup and launch diagnostics on Titan error exits;
- disposable-copy build script that never patches or cleans an external OPL
  checkout or the maintained source tree;
- canonical runtime name `TITAN/titan_ul_engine.elf`.
