# Titan In-Game Overlay Lite — BuildFix28

Minimum UI: `PROJECT TITAN`, current title/session, `Resume`, `Return to Titan`. Optional fields remain capability-gated. Native/Project NES consume Titan Unified Input. UL/Neutrino use their normalized resident input ingress; overlay code never parses DS2/DS3/DS4/DualSense/Xbox protocols itself.

Native overlay is Application Runtime/kernel UI. UL and Neutrino use a small resident GS overlay with no HOME/Theme Engine. Resume is non-destructive only for safe profiles; Return deliberately invokes engine cleanup. Input suppression prevents the opening/closing chord from sticking into the resumed title.
