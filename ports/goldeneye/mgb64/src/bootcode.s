# MGB64 public placeholder for N64 IPL3 boot material.
#
# The native port does not use this file. The optional N64 matching target links
# a boot segment here, but the public source tree intentionally does not
# redistribute IPL3 boot ROM code or boot-font bytes. Matching-target work that
# needs a bootable ROM must provide the required IPL3/boot material locally from
# a legally obtained source and keep it out of git.

.set noat
.set noreorder
.set gp=64

.include "macros.inc"

.section .text, "ax"

glabel ipl3_font
/* MGB64: IPL3 boot-font data removed; supply locally for matching-target work. */
.space 1168
