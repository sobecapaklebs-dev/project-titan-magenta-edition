# Titan Session Service — BuildFix28

A fixed-capacity central lifecycle context records session id, title id, classification, engine, launch source/path, profile/VMC, video mode, input configuration, IGR/overlay policy, compatibility profile, runtime capabilities and start time when available. It does not replace launchers.

States are deterministic: `IDLE`, `PREPARING`, `LAUNCHING`, `RUNNING`, `OVERLAY_ACTIVE`, `RETURN_REQUESTED`, `RETURNING`, `FAILED`, `RECOVERING`. Invalid transitions are rejected. Native/Application Runtime, Neutrino and UL register a descriptor before handoff. The inactive path allocates nothing per frame.

Application launch failure releases resources, fails/ends the active Session and resets Overlay. Shutdown/reboot reset Overlay and shut Session before Theme/audio/input/IOP teardown.
