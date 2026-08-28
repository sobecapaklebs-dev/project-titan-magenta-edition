# Titan integrated FMCB backend

This directory turns the retained FMCB 1.966 installation/signing engine into a static EE library linked into `titan-installer.elf`. It intentionally does **not** compile or package the standalone FMCB installer `main.c`/UI as a release executable.

The adapter owns the IOP bootstrap needed by the FMCB MagicGate/KELF path and maps upstream progress/error UI hooks to Titan callbacks. `PerformInstallation()` remains the authoritative install engine.
