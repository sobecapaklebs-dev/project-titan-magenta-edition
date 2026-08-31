# Titan Repository Protocol v1 — Frozen Release Contract

Repository Protocol v1 is the initial Magenta Edition repository contract. Catalog parsing is bounded and target-aware. Packages declare identity/version, Titan API compatibility, location, byte size and SHA-256; optional descriptive/visual/dependency metadata is accepted only within parser limits.

The normal deployment model supports static hosting: the console fetches repository metadata and package URLs directly through Titan Network/Download Service. No PC or phone bridge is required by the protocol.

Download publication remains REQUEST -> QUEUE -> DOWNLOAD -> TEMP -> HASH/SIZE -> PACKAGE VALIDATION -> ATOMIC PUBLISH -> INSTALLER. HTTP Range resume is not claimed in v1 because BF29 explicitly did not implement it.
