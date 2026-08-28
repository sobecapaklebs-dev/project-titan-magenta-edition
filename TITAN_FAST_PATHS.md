# Titan Fast Paths — BF28 CP10

- `TitanOverhead`: disabled -> branch/return, zero clock reads.
- `TitanSession_Update`: IDLE -> return.
- `TitanOverlay_Update`: closed -> normalized chord check only; no layout/render.
- `TitanOverlay_Draw`: closed -> return.
- `TitanPikaVr_TryReuseTexture`: Reference/disabled -> return before texture-registry scan.
- `TitanVlombisCore_Record`: capability mask 0 -> return.
- `TitanNetwork_Update`: disabled/already offline -> no repeated detail copy; no stack bootstrap.
- diagnostics percentile and service telemetry are sampled rather than recomputed each frame.

All paths use fixed state; no new per-frame heap allocation was introduced.
