# Titan Neutrino In-Game Overlay — BuildFix28 CP08

The Neutrino backend now owns a resident Smart IGR/Overlay integration in its EE core. It does not load HOME or Theme Engine in game.

Launch policy is supplied by Titan with `-titan-igr=`, `-titan-igr-profile=` and `-titan-exit=`. UNKNOWN content remains fail-closed at RETURN_ONLY. A title/profile may enable MINIMAL/FULL only when the compatibility layer proves `CONTEXT2_SAFE`.

After Neutrino has loaded the title ELF, the EE core installs the PadOpen hook before `CleanExecPS2`. The hook observes the normalized DS2-compatible ingress (including PADEMU modern controllers), detects the Titan IGR chord, and wakes a resident worker. For a safe overlay profile the worker suspends title threads, renders the shared Titan Engine Overlay using GS context 2, consumes overlay input, and resumes exactly the tracked threads. The Resume path does not reset EE, GS or IOP.

Return uses Neutrino's existing resident `LoadExecPS2` re-entry. That controlled path wipes title memory, reboots the IOP into a clean emulation environment, reloads the configured Titan exit ELF and executes through `CleanExecPS2`. RETURN_ONLY content uses this path without opening a renderer.

Hardware/PCSX2 validation remains external; host contracts validate launch policy, source wiring, non-destructive Resume separation, Return cleanup ownership and the shared overlay state machine under ASan/UBSan.
