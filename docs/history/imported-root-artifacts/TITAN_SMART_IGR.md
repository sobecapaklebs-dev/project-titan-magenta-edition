# Titan Smart IGR — BuildFix28

Smart IGR is capability/profile driven, not universal blind injection. Native/Project NES use Application Runtime. UL and Neutrino use resident EE-core backends and a minimal engine overlay; HOME and Theme Engine are never loaded inside a title.

For safe profiles, the trigger is non-destructive: tracked title threads are suspended, Overlay runs, and Resume restores the same threads without ResetEE/GS/IOP. Return to Titan intentionally enters each engine's proven cleanup/return pipeline. UNKNOWN titles remain fail-closed as RETURN_ONLY/legacy until a safe profile exists.

VMC/storage safety wins. If framebuffer/GS/thread/resource restoration cannot be proven for a title, visual overlay is demoted rather than forced.
