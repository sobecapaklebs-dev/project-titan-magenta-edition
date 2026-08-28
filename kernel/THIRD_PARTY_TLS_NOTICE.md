# Titan native HTTPS build dependencies

The direct Internet Asset Service uses **BearSSL 0.6** for TLS 1.2. BearSSL is
published under the MIT License by Thomas Pornin. Project Titan does not vendor
BearSSL in its source archive; `kernel/tools/titan_tls_deps.py` downloads the
released 0.6 archive from the official BearSSL site during the first build.

The same build helper downloads the generated BearSSL trust-anchor bundle used
by AthenaEnv and verifies its pinned SHA-256 before it is accepted. The bundle
is generated build input and is not stored in Project Titan's source archive.

The BearSSL MIT license notice shipped with this source tree is preserved at
`kernel/third_party/bearssl_ps2/BEARSSL_LICENSE.txt`.

Provider artwork remains property of its respective owners. The Asset Service
retrieves public artwork at runtime and caches a small transformed copy locally.
