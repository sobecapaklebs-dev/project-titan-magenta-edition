# Titan Store Architecture — BuildFix29 Foundation

BuildFix29 provides Store infrastructure, not the final v1.0 storefront UI.

## Flow

`STATIC REPOSITORY -> Titan Repository Protocol v1 -> Store catalog -> Titan Download Service -> SHA-256 + TPKG validation -> mass:/TITAN/packages -> Package Installer`

The Store does not own HTTP/TLS, package parsing, installation or a second storage stack. It composes existing Titan services.

Browser integration recognizes two lightweight system links:

- `titan://repository?url=<encoded-http-or-https-url>` loads a static `.trepo` catalog asynchronously through BrowserNet/NetworkClient. Repository data is fetched directly; it does not require modern JavaScript or the HTML proxy bridge.
- `titan://package?id=<package-id>` enqueues the selected catalog package through Titan Download Service.

Repository package metadata includes title, version, R5900/PS2 target, SHA-256, size, description, optional icon/background, API level and dependencies. Dependency references must resolve inside the loaded catalog before a package is queued.

Download progress is surfaced through the existing Notification Service. Package installation remains an explicit Package Installer action after the file has been atomically published and validated.

## BF29 platform-closure idle cost

The Store foundation has no resident catalog allocation until a repository is actually accepted. The catalog is allocated on demand and released on shutdown. Store update work is bounded notification/catalog bookkeeping; download/network/storage ownership remains in Titan Download Service and the existing shared services.
