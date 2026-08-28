#!/usr/bin/env bash
#
# test_build_hashtable.sh -- ROM-free guard for AUDIT-0015: the object-section
# hashtable generator scripts/make/build_hashtable.sh must be fail-closed.
#
# No MIPS toolchain is required: a PATH-shimmed fake `mips-linux-gnu-objcopy`
# writes deterministic bytes ("SEC:<section>|SRC:<input-path>") to the requested
# output, and the oracle digests are computed INDEPENDENTLY with `md5 -q` /
# openssl (a different tool than the md5sum the generator uses).
#
# Positive cases (oracle-exact CSV, `md5,section,path` format):
#   - explicit -o into a subdirectory (the wrapper-caller form),
#   - documented no-`-o` default filename full_hashtable_<code>.csv,
#   - -v aliases (US -> u) and all three codes u/j/e.
# Negative cases -- EACH must exit nonzero AND leave no completed CSV:
#   (a) unsupported version (exit 2), (b) missing version / zero args (exit 2),
#   (c) missing build dir, (d) empty required class, (e) extractor failure
#   (exit-1 shim AND a /usr/bin/true-style silent no-output shim -- the original
#   false-success repro), (f) checksum failure, (g) unwritable destination.
#
# The generator is executed under BOTH /bin/bash (macOS 3.2.57) and the PATH
# bash -- the bash-3.2 execution test is an AUDIT-0015 acceptance criterion.
#
# Counts failures explicitly and exits nonzero on any (does NOT rely on assert;
# the ctest build is Release -DNDEBUG). Wired as ctest `build_hashtable_guard`.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
GEN="$ROOT/scripts/make/build_hashtable.sh"
[[ -f "$GEN" ]] || { echo "FATAL: not found: $GEN" >&2; exit 2; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/hashtable_guard_XXXXXX")"
trap 'chmod -R u+w "$WORK" 2>/dev/null || true; rm -rf "$WORK"' EXIT

pass=0; fail=0
ok()  { printf '  PASS %s\n' "$1"; pass=$((pass+1)); }
bad() { printf '  FAIL %s\n' "$1"; fail=$((fail+1)); }

# ---------------------------------------------------------------------------
# Shims. GOOD = deterministic fake objcopy; XFAIL = objcopy exits 1;
# XSILENT = objcopy exits 0 but writes NOTHING (the /usr/bin/true repro);
# MDFAIL = good objcopy + md5sum that always fails.
# ---------------------------------------------------------------------------
GOOD="$WORK/shim_good"; XFAIL="$WORK/shim_xfail"; XSILENT="$WORK/shim_xsilent"; MDFAIL="$WORK/shim_mdfail"
mkdir -p "$GOOD" "$XFAIL" "$XSILENT" "$MDFAIL"

cat > "$GOOD/mips-linux-gnu-objcopy" <<'SHIM'
#!/bin/bash
# fake objcopy: understands `-j SEC -O binary IN OUT`; emits deterministic bytes.
sec=""; in=""; out=""
while [ $# -gt 0 ]; do
  case "$1" in
    -j) sec="$2"; shift 2 ;;
    -O) shift 2 ;;
    -*) shift ;;
    *) if [ -z "$in" ]; then in="$1"; else out="$1"; fi; shift ;;
  esac
done
if [ -z "$in" ] || [ -z "$out" ]; then echo "fake-objcopy: bad args" >&2; exit 1; fi
if [ ! -f "$in" ]; then echo "fake-objcopy: '$in': No such file" >&2; exit 1; fi
printf 'SEC:%s|SRC:%s' "$sec" "$in" > "$out"
SHIM
printf '#!/bin/sh\necho "fake-objcopy: forced failure" >&2\nexit 1\n' > "$XFAIL/mips-linux-gnu-objcopy"
printf '#!/bin/sh\nexit 0\n' > "$XSILENT/mips-linux-gnu-objcopy"
cp "$GOOD/mips-linux-gnu-objcopy" "$MDFAIL/mips-linux-gnu-objcopy"
printf '#!/bin/sh\necho "fake-md5sum: forced failure" >&2\nexit 1\n' > "$MDFAIL/md5sum"
chmod +x "$GOOD/mips-linux-gnu-objcopy" "$XFAIL/mips-linux-gnu-objcopy" \
         "$XSILENT/mips-linux-gnu-objcopy" "$MDFAIL/mips-linux-gnu-objcopy" "$MDFAIL/md5sum"

# ---------------------------------------------------------------------------
# Oracle: digest of the shim's predetermined bytes, computed with md5/openssl
# (NOT md5sum, which is what the generator itself uses).
# ---------------------------------------------------------------------------
oracle_md5() {  # <section> <path>
  if command -v md5 >/dev/null 2>&1; then
    printf 'SEC:%s|SRC:%s' "$1" "$2" | md5 -q
  else
    printf 'SEC:%s|SRC:%s' "$1" "$2" | openssl dgst -md5 -r | cut -c -32
  fi
}

# Fixture: the seven object classes the generator iterates, incl. a filename
# with a space (path preservation). 2*5 + 1*5 + 5*3 = 30 rows expected.
make_tree() {  # <fixture-root> <code>
  local r="$1/build/$2" d
  mkdir -p "$r/src/game"
  for d in bg brief setup stan text; do mkdir -p "$r/assets/obseg/$d"; done
  printf 'obj' > "$r/src/a1.o"
  printf 'obj' > "$r/src/a2 space.o"
  printf 'obj' > "$r/src/game/g1.o"
  for d in bg brief setup stan text; do printf 'obj' > "$r/assets/obseg/$d/${d}1.o"; done
}

write_oracle() {  # <out-csv> <code>
  local o="$1" c="$2" f sec d
  : > "$o"
  for f in "build/$c/src/a1.o" "build/$c/src/a2 space.o" "build/$c/src/game/g1.o"; do
    for sec in .text .code .bss .data .rodata; do
      printf '%s,%s,%s\n' "$(oracle_md5 "$sec" "$f")" "$sec" "$f" >> "$o"
    done
  done
  for d in bg brief setup stan text; do
    f="build/$c/assets/obseg/$d/${d}1.o"
    for sec in .bss .data .rodata; do
      printf '%s,%s,%s\n' "$(oracle_md5 "$sec" "$f")" "$sec" "$f" >> "$o"
    done
  done
}

# ---------------------------------------------------------------------------
# Runner + assertion helpers.
# ---------------------------------------------------------------------------
RC=0
run_gen() {  # <cwd> <shimdir> <interp> [args...]
  local cwd="$1" shims="$2" interp="$3"; shift 3
  RC=0
  ( cd "$cwd" && PATH="$shims:$PATH" "$interp" "$GEN" "$@" ) >"$WORK/last.log" 2>&1 || RC=$?
}

assert_rc() {  # <want> <label>
  if [[ "$RC" -eq "$1" ]]; then ok "$2 (exit $RC)"
  else bad "$2 -- expected exit $1, got $RC [$(tail -1 "$WORK/last.log" 2>/dev/null || true)]"; fi
}

assert_fails() {  # <label>
  if [[ "$RC" -ne 0 ]]; then ok "$1 (exit $RC)"
  else bad "$1 -- expected nonzero exit, got 0 (false success)"; fi
}

assert_no_csv() {  # <dir> <label> -- no completed CSV and no temp litter left
  if find "$1" -maxdepth 1 \( -name 'full_hashtable*' -o -name '.full_hashtable*' \) 2>/dev/null | grep -q .; then
    bad "$2 -- CSV/temp left behind: $(find "$1" -maxdepth 1 -name '*hashtable*' | tr '\n' ' ')"
  else
    ok "$2 leaves no CSV"
  fi
}

assert_csv_matches() {  # <got> <oracle> <label>
  if [[ ! -f "$1" ]]; then bad "$3 -- output CSV missing: $1"; return; fi
  if diff -u "$2" "$1" >"$WORK/diff.log" 2>&1; then ok "$3 matches oracle (rows+digests)"
  else bad "$3 -- differs from oracle: $(head -4 "$WORK/diff.log" | tr '\n' ' ')"; fi
  if grep -q '\*' "$1"; then bad "$3 -- literal wildcard in a row"; else ok "$3 has no wildcard rows"; fi
  if grep -Evq '^[0-9a-fA-F]{32},\.[a-z]+,..*$' "$1"; then
    bad "$3 -- malformed row (empty digest / bad format)"
  else ok "$3 rows are md5,section,path with 32-hex digest"; fi
}

# ---------------------------------------------------------------------------
# The suite, run once per interpreter (bash 3.2 acceptance + PATH bash).
# ---------------------------------------------------------------------------
case_n=0
new_case_dir() { case_n=$((case_n+1)); CDIR="$WORK/case_$case_n"; mkdir -p "$CDIR"; }

run_suite() {  # <interp>
  local B="$1"
  # shellcheck disable=SC2016  # $BASH_VERSION must expand in the CHILD bash, not here
  echo "== build_hashtable guard under $B ($("$B" -c 'echo $BASH_VERSION')) =="

  # P1: wrapper-caller form -- explicit -o into a subdirectory, code u.
  new_case_dir; make_tree "$CDIR" u; mkdir -p "$CDIR/out"
  write_oracle "$WORK/oracle_u.csv" u
  run_gen "$CDIR" "$GOOD" "$B" -v u -o out/result.csv
  assert_rc 0 "explicit -o accepted"
  assert_csv_matches "$CDIR/out/result.csv" "$WORK/oracle_u.csv" "explicit -o CSV"
  if [[ "$(wc -l < "$CDIR/out/result.csv" 2>/dev/null | tr -d ' ')" == "30" ]]; then
    ok "explicit -o row count is 30"; else bad "explicit -o row count wrong"; fi

  # P2: documented no-`-o` default + uppercase alias US -> full_hashtable_u.csv.
  new_case_dir; make_tree "$CDIR" u
  run_gen "$CDIR" "$GOOD" "$B" -v US
  assert_rc 0 "no-o default (-v US) accepted"
  if [[ -e "$CDIR/full_hashtable_.csv" ]]; then bad "no-o default -- created full_hashtable_.csv (filename typo)"
  else ok "no-o default does not create full_hashtable_.csv"; fi
  assert_csv_matches "$CDIR/full_hashtable_u.csv" "$WORK/oracle_u.csv" "no-o default CSV (full_hashtable_u.csv)"

  # P3/P4: codes j and e, no-`-o` default names.
  new_case_dir; make_tree "$CDIR" j
  write_oracle "$WORK/oracle_j.csv" j
  run_gen "$CDIR" "$GOOD" "$B" -v j
  assert_rc 0 "-v j accepted"
  assert_csv_matches "$CDIR/full_hashtable_j.csv" "$WORK/oracle_j.csv" "-v j CSV (full_hashtable_j.csv)"

  new_case_dir; make_tree "$CDIR" e
  write_oracle "$WORK/oracle_e.csv" e
  run_gen "$CDIR" "$GOOD" "$B" -v e
  assert_rc 0 "-v e accepted"
  assert_csv_matches "$CDIR/full_hashtable_e.csv" "$WORK/oracle_e.csv" "-v e CSV (full_hashtable_e.csv)"

  # (a) unsupported version -> exit 2, before touching output.
  new_case_dir
  run_gen "$CDIR" "$GOOD" "$B" -v q
  assert_rc 2 "(a) unsupported version rejected"
  assert_no_csv "$CDIR" "(a) unsupported version"

  # (b) missing version: -o only, and zero args -> exit 2, no output.
  new_case_dir
  run_gen "$CDIR" "$GOOD" "$B" -o x.csv
  assert_rc 2 "(b) missing -v rejected"
  if [[ -e "$CDIR/x.csv" ]]; then bad "(b) missing -v -- x.csv created"; else ok "(b) missing -v leaves no x.csv"; fi
  assert_no_csv "$CDIR" "(b) missing -v"
  new_case_dir
  run_gen "$CDIR" "$GOOD" "$B"
  assert_rc 2 "(b2) zero args rejected"
  assert_no_csv "$CDIR" "(b2) zero args"

  # (c) missing build dir.
  new_case_dir
  run_gen "$CDIR" "$GOOD" "$B" -v u
  assert_fails "(c) missing build dir rejected"
  assert_no_csv "$CDIR" "(c) missing build dir"

  # (d) empty required class (assets/obseg/text exists but has no .o).
  new_case_dir; make_tree "$CDIR" u; rm -f "$CDIR/build/u/assets/obseg/text/"*.o
  run_gen "$CDIR" "$GOOD" "$B" -v u
  assert_fails "(d) empty required class rejected"
  assert_no_csv "$CDIR" "(d) empty required class"

  # (e1) extractor failure: objcopy exits 1.
  new_case_dir; make_tree "$CDIR" u
  run_gen "$CDIR" "$XFAIL" "$B" -v u
  assert_fails "(e1) objcopy exit-1 rejected"
  assert_no_csv "$CDIR" "(e1) objcopy exit-1"

  # (e2) extractor silent no-output (/usr/bin/true repro -- the AUDIT-0015 case).
  new_case_dir; make_tree "$CDIR" u
  run_gen "$CDIR" "$XSILENT" "$B" -v u
  assert_fails "(e2) silent no-output objcopy rejected"
  assert_no_csv "$CDIR" "(e2) silent no-output objcopy"

  # (f) checksum failure: md5sum always fails.
  new_case_dir; make_tree "$CDIR" u
  run_gen "$CDIR" "$MDFAIL" "$B" -v u
  assert_fails "(f) md5sum failure rejected"
  assert_no_csv "$CDIR" "(f) md5sum failure"

  # (g) unwritable destination directory.
  if [[ "$(id -u)" -eq 0 ]]; then
    ok "(g) skipped (running as root; chmod is not enforceable)"
  else
    new_case_dir; make_tree "$CDIR" u; mkdir -p "$CDIR/ro"; chmod 0555 "$CDIR/ro"
    run_gen "$CDIR" "$GOOD" "$B" -v u -o ro/out.csv
    assert_fails "(g) unwritable destination rejected"
    if [[ -e "$CDIR/ro/out.csv" ]]; then bad "(g) unwritable destination -- out.csv created"
    else ok "(g) unwritable destination leaves no out.csv"; fi
    assert_no_csv "$CDIR/ro" "(g) unwritable destination"
    chmod 0755 "$CDIR/ro"
  fi
  echo
}

run_suite /bin/bash
PATH_BASH="$(command -v bash)"
if [[ -n "$PATH_BASH" && "$PATH_BASH" != /bin/bash ]]; then
  run_suite "$PATH_BASH"
fi

echo "== build_hashtable guard: ${pass} passed, ${fail} failed =="
[[ "$fail" -eq 0 ]] || exit 1
