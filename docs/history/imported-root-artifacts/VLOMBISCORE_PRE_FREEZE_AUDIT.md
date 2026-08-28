# VlombisCore Pre-Freeze Audit — BuildFix28

Status: HOST_AUDITED / TARGET_PERFORMANCE_PENDING.

BF28 replaces ambiguous capability naming with an append-only measured-participation taxonomy while preserving legacy source aliases. Canonical capability bits are:
- STORAGE_QOS
- BACKGROUND_QOS
- IO_COORDINATION
- RENDER_STATE_CACHE
- ASYNC_PACKAGE_IO
- LIBRARY_INCREMENTAL_SCAN
- AUDIO_BUFFER_QOS
- NETWORK_BACKGROUND_QOS
- FRAME_BUDGETING
- PIKAVR

A capability is never activated merely because code exists. `TitanVlombisCore_Record(0, ...)` is a no-op; active bits arise only from material producer events. Existing storage scheduler producers retain their old aliases, which map to the same canonical STORAGE_QOS/BACKGROUND_QOS bits without ABI churn.

Badge rule remains unchanged: capability declaration alone is insufficient; measured session participation is required. BF28 does not invent activity for package, library, audio, network, frame-budget or pikaVR domains until their corresponding producers record real behavior.
