#!/usr/bin/env sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out=${TMPDIR:-/tmp}/neutrino_ul_chain_test
cc -std=c99 -Wall -Wextra -Werror \
  -I"$root/src" \
  "$root/src/ul_chain.c" \
  "$root/tests/test_ul_chain.c" \
  -o "$out"
"$out"
rm -f "$out"
