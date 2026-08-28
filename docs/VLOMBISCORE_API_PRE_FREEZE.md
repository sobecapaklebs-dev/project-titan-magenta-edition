# VlombisCore API Pre-Freeze — BuildFix29

VlombisCore remains capability/evidence driven. Public code consumes Titan services; it does not call private scheduler/cache workers directly.

Stable behavior classes remain: RENDER_STATE_CACHE, STORAGE_QOS, ASYNC_PACKAGE_IO, LIBRARY_INCREMENTAL_SCAN, AUDIO_BUFFER_QOS, NETWORK_BACKGROUND_QOS, FRAME_BUDGETING and PIKAVR integration. A capability/badge is truthful only when the corresponding behavior produced material runtime evidence.

Beginner path: use Titan App/TGR/VFS/Network normally and receive safe scheduling automatically.

Advanced path: use documented TGR state, storage/network lifecycle and bounded service APIs. No private worker pointer, FileXio lane, DMA queue or cache internals become public ABI in BF29.
