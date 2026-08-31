# Titan Controller Battery — BuildFix29

Unified Input represents battery as UNSUPPORTED, UNKNOWN, LEVEL, CHARGING or FULL plus an independent `battery_valid` percentage flag and raw backend value. FULL normalizes to 100%. CHARGING may legitimately have no percentage. Unsupported devices reject bogus percentage data.

Telemetry is event/report driven where available or sampled through the existing PADEMU snapshot slow path (nominally 4 Hz), never queried synchronously per frame. USB does not imply battery percentage: each backend exposes only data the device protocol actually provides.
