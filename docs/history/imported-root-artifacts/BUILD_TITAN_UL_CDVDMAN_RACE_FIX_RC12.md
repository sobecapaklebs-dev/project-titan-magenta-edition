# RC12 Titan UL cdvdman parallel-build correction

## Failure represented by the RC11 PS2DEV log

The displayed `hdd_cdvdman.irx` and `smb_cdvdman.irx` commands completed
successfully. The final `Makefile:237: all` error only propagated a failure from
one of the concurrently running recursive makes, whose diagnostic was emitted
earlier in the interleaved output.

## Root cause

The BDM, BDM-ATA, SMB, HDD and HDPro variants select distinct `obj.*`
directories for their ordinary sources. Their two ISO helper entries are
spelled as `obj.<variant>/../../isofs/zso.o` and
`obj.<variant>/../../isofs/lz4.o`, however, so every path normalizes to the same
pair of files. Parallel recursive makes can link a shared object while another
variant is still writing it.

## Correction

The five affected IRX targets now form an order-only dependency chain. Only
those recursive builds are serialized; languages, frontend objects, unrelated
IOP modules and the rest of the Titan UL Engine remain parallel. Recursive
output is synchronized as a unit so any future fatal error remains adjacent to
the responsible target.

## Validation boundary

Host contracts, the complete source-validation suite, extracted-ZIP validation
and byte-level asset comparisons are required for RC12. A complete PS2DEV build
and PCSX2/physical-PS2 validation are unavailable in this environment and must
be performed by the user.
