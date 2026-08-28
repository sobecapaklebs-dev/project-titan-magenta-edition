# Titan Neutrino Engine changes

This maintained derivative integrates Neutrino with Project Titan Magenta
Edition while retaining the upstream licence and authorship.

Titan-specific changes in this source line include:

- shell-controlled ISO launch arguments and active-profile VMC handoff;
- hardened UL fragment-chain parsing used by experimental compatibility paths;
- exact partial-record reads, bounded delayed open retries and wide size checks;
- FHI allocation based on the maximum occupied extent rather than fragment
  counts, with staged validation and rollback before publication;
- catalog/launch diagnostics suitable for Titan stage2 handoff;
- source/build integration under `modules/titan-neutrino-engine/`.

The canonical local UL backend is Titan UL Engine. Neutrino UL experiments are
not advertised as a blanket compatibility path.
