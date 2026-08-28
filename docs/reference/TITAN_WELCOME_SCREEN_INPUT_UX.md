# Titan Welcome Screen Input UX — BuildFix29

Welcome renders a real lightweight rounded button/glyph with pulse/press feedback rather than raw instruction text. Candidate mapping is DS2 → X; DS3/DS4/DualSense → PS; Xbox → HOME. The candidate may change before confirmation without reloading the screen.

When multiple devices are available Titan does not alternate endlessly: the candidate follows deterministic connection/activity policy and the first valid confirmation claims Player 1. After confirmation HOME becomes active and any deferred connection/assignment notification can be emitted exactly once.
