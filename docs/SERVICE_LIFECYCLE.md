# Service lifecycle

`TitanIopServiceManager` owns SIF RPC, bounded IOP reset/sync and LOADFILE.
Input is a dependent client and no longer owns the global IOP lifecycle. A
PADMAN or controller failure therefore does not disable profile storage,
library scanning, audio or DEX.

Initialization order:

1. platform and GS renderer;
2. UI assets and shell diagnostics;
3. IOP service manager;
4. input client;
5. storage/profile, audio and catalog clients when their dependencies exist;
6. DEX and UI state.

Each service keeps an explicit ready/failed state. Audio and avatar assets have
retryable initialization. Handoff shuts down audio before transferring control.
The IOP manager releases EE-side LOADFILE/RPC clients on shutdown; a subsequent
retry performs a new bounded reset and increments its generation.
