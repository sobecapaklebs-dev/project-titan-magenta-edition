# BuildFix29 Recovery / Rehydration

## Historical checkpoint route

The development history can be reconstructed from:

1. BuildFix28 FINAL source archive, SHA-256 `978ab1e5072fd9cece54470d487050beff900d11e4996c3e4c6ef4324962d6b1`.
2. Apply BuildFix29 delta archives CP00 through CP11 in numeric order.
3. CP11 archive identity is `10f9a565a804c2bf063a021035ab7d01e6ee5f8e8818c9e2c3ba780fd1ab6fdd`.

Each checkpoint archive uses project-relative paths and is overlaid onto its parent tree.

## Final route

After final packaging, the authoritative recovery path is the monolithic `Project-Titan-BuildFix29-FINAL-SOURCE.tar.gz` plus its SHA-256 and internal final source manifest. The CP chain remains provenance/history and must not be required once the final independence gate passes.

No intermediate artifact is deleted automatically.
