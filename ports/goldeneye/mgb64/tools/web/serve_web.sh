#!/usr/bin/env bash
# Local static server for manual web-shell testing. Usage: tools/web/serve_web.sh [port]
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
exec python3 -m http.server "${1:-8000}" --directory "$ROOT/dist/web" --bind 127.0.0.1
