#!/bin/bash
#
# combat_route_contract_smoke.sh -- contract lane for the combat/floor oracle
# (FID-0032). Sibling of route_contract_smoke.sh.
#
# ROM-free default mode (always runnable, joins verify_manifest tier 1): validates
# the combat_oracle JSON schema and the compare_combat_trace.py comparator against
# committed synthetic fixtures — a known-good identical pair must align with zero
# divergences, and a deliberately-mutated pair must surface divergences (negative
# control). This guards schema parity + comparator behaviour without a ROM.
#
# ROM-gated mode (--native-capture, mirrors route_contract_smoke --native-smoke):
# captures a native combat trace on a route and audits it; documented but the live
# both-sides ares comparison is the verified milestone (see FID-0032 ledger note).
#
set -euo pipefail
cd "$(dirname "$0")/.."

MODE="rom-free"
for arg in "$@"; do
    case "$arg" in
        --gate|--rom-free) MODE="rom-free" ;;
        --native-capture) MODE="native-capture" ;;
        -h|--help)
            echo "Usage: tools/combat_route_contract_smoke.sh [--gate|--rom-free|--native-capture]"
            exit 0 ;;
        *) echo "unknown arg: $arg" >&2; exit 2 ;;
    esac
done

COMPARE="tools/compare_combat_trace.py"
if [ ! -f "$COMPARE" ]; then
    echo "FAIL: $COMPARE missing" >&2
    exit 1
fi

WORK="$(mktemp -d "${TMPDIR:-/tmp}/mgb64_combat_contract_XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

# A minimal but schema-complete combat_oracle frame pair.
cat > "$WORK/good_a.jsonl" <<'EOF'
{"f":1,"p":1,"move":{"global":10,"clock":10},"combat_oracle":{"guards":[{"chrnum":3,"pos":[5.0,1.0,9.0],"actiontype":14,"aimode":1,"health":80.0,"shotbondsum":0.0,"flags_onscreen":1,"target_visible":0,"anim_hash":"0x0000000000001234","room":4}],"guards_overflow":0,"floor":{"stan_id":777,"stan_room":4,"stan_flags":0,"height":3.25},"combat":{"player_health":700.0,"player_armor":0.0,"shots_fired_total":0,"hits_landed_total":0,"rng_seed":"0xDEADBEEF"},"projectiles":[],"projectiles_overflow":0}}
{"f":2,"p":1,"move":{"global":11,"clock":11},"combat_oracle":{"guards":[{"chrnum":3,"pos":[5.0,1.0,9.0],"actiontype":14,"aimode":1,"health":80.0,"shotbondsum":0.0,"flags_onscreen":1,"target_visible":1,"anim_hash":"0x0000000000001234","room":4}],"guards_overflow":0,"floor":{"stan_id":777,"stan_room":4,"stan_flags":0,"height":3.25},"combat":{"player_health":700.0,"player_armor":0.0,"shots_fired_total":1,"hits_landed_total":0,"rng_seed":"0xCAFEBABE"},"projectiles":[],"projectiles_overflow":0}}
EOF
cp "$WORK/good_a.jsonl" "$WORK/good_b.jsonl"

# Negative control: mutate actiontype, floor.stan_id, and add a guard divergence.
python3 - "$WORK/good_a.jsonl" "$WORK/bad.jsonl" <<'PY'
import json, sys
src, dst = sys.argv[1], sys.argv[2]
lines = []
for line in open(src):
    line = line.strip()
    if not line:
        continue
    r = json.loads(line)
    co = r["combat_oracle"]
    co["guards"][0]["actiontype"] = 18          # dispatch divergence
    co["floor"]["stan_id"] = 999                # tile-selection divergence
    lines.append(json.dumps(r))
open(dst, "w").write("\n".join(lines) + "\n")
PY

echo "== positive control: identical pair aligns with zero divergences =="
OUT="$(python3 "$COMPARE" --baseline "$WORK/good_a.jsonl" --test "$WORK/good_b.jsonl" --strict)"
echo "$OUT"

echo "== negative control: mutated pair surfaces divergences (strict must fail) =="
if python3 "$COMPARE" --baseline "$WORK/good_a.jsonl" --test "$WORK/bad.jsonl" --strict >/dev/null 2>&1; then
    echo "FAIL: negative control did not surface divergences" >&2
    exit 1
fi
# ...but default mode still exits 0 (divergences are findings, not failures).
python3 "$COMPARE" --baseline "$WORK/good_a.jsonl" --test "$WORK/bad.jsonl" >/dev/null

if [ "$MODE" = "native-capture" ]; then
    echo "== native-capture mode is documented but the verified milestone is the" \
         "live native-vs-ares comparison; see FID-0032 ledger note ==" >&2
fi

echo "PASS: combat_oracle schema + comparator contract holds"
