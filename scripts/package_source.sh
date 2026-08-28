#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
VERSION=$(cat "$ROOT/VERSION")
OUTPUT=${1:-"$ROOT/dist/Project-Titan-Magenta-Edition-$VERSION-source.tar.gz"}
mkdir -p "$(dirname "$OUTPUT")"

# Package the source tree that is actually present on disk. Using
# `git archive HEAD` here is unsafe for hotfix/release trees unpacked over an
# existing checkout because modified and untracked release files would vanish.
SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-$(stat -c %Y "$ROOT/VERSION" 2>/dev/null || date +%s)}
tar -C "$ROOT" \
    --sort=name \
    --mtime="@$SOURCE_DATE_EPOCH" \
    --owner=0 --group=0 --numeric-owner \
    --exclude='./.git' \
    --exclude='./build' \
    --exclude='./dist' \
    --exclude='./bootloader/generated' \
    --exclude='./bootloader/stage2/generated' \
    --exclude='./kernel/src/graphics/generated' \
    --exclude='./kernel/src/services/generated' \
    --exclude='./apps/project-nes/build' \
    --exclude='./apps/wlaunchelf/build' \
    --exclude='./ports/goldeneye/build' \
    --exclude='__pycache__' \
    --exclude='*.pyc' \
    --exclude='*.o' \
    --exclude='*.map' \
    --exclude='*.bak' \
    --exclude='*.orig' \
    --exclude='*.rej' \
    --transform='s,^\.$,Project Titan Magenta Edition,;s,^\./,Project Titan Magenta Edition/,' \
    -cf - . | gzip -n -9 > "$OUTPUT"
sha256sum "$OUTPUT" > "$OUTPUT.sha256"
echo "$OUTPUT"
