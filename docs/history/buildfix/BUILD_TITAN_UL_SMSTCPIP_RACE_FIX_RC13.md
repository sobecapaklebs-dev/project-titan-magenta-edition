# RC13 Titan UL SMSTCPIP parallel-build correction

## Failure represented by the RC12 PS2DEV log

`SMSTCPIP.irx` reached its strip/fixup commands, but the outer Titan UL build
still returned `Makefile:237: all Error 2`. The displayed commands were not the
outer failing rule; they were the final output of a recursive build whose
legacy rebuild graph was unsafe under the inherited parallel jobserver.

## Root cause

The Titan UL parent invoked the module as:

```make
$(MAKE) $(SMSTCPIP_INGAME_CFLAGS) -C $< rebuild
```

The shared module rules define `rebuild: clean all`. `clean` and `all` are
independent prerequisites, not an ordered sequence. Under `make -j`, GNU Make
may therefore execute them concurrently, allowing `clean` to remove objects or
the IRX while `all` is compiling, stripping or fixing it.

## Correction

The disposable Titan UL source copy is already cleaned before the parallel
build starts. The parent now invokes only the module's `all` target. This keeps
parallelism for independent Titan UL components while removing the redundant
destructive prerequisite from the live build graph.

## Validation boundary

Host contracts, the complete source-validation suite, extracted-ZIP
validation and byte-level RC12 asset/runtime comparisons are required for
RC13. A complete PS2DEV build and PCSX2/physical-PS2 validation are unavailable
in this environment and must be performed by the user.
