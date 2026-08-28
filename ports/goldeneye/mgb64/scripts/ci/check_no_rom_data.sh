#!/usr/bin/env bash
#
# check_no_rom_data.sh — guard against committing ROMs, ROM-derived assets, or
# opaque bundled media.
#
# This is the safeguard that keeps MGB64 legally publishable. It fails if any
# tracked file looks like a ROM, an extracted asset, a compiled binary, an
# embedded binary/data-URI asset, or a direct SDK/devkit source-path copy note.
# Run locally before pushing or opening a PR.
#
set -euo pipefail

if root="$(git rev-parse --show-toplevel 2>/dev/null)"; then
  cd "$root"
  HAVE_GIT=1
else
  script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  cd "${script_dir}/../.."
  HAVE_GIT=0
fi

fail=0
note() { printf '  \033[31m[VIOLATION]\033[0m %s\n' "$1"; fail=1; }
warn() { printf '  \033[33m[WARN]\033[0m %s\n' "$1"; }

echo "== MGB64 contamination guard =="

public_file_list() {
  if [ "$HAVE_GIT" -eq 1 ]; then
    git ls-files
    return
  fi

  find . \
    \( -path './.git' -o -path './build' -o -path './build-*' -o -path './dist' -o -path './__pycache__' \) -prune \
    -o -type f -print \
    | sed 's#^\./##' \
    | sort
}

grep_public_files() {
  local pattern="$1"
  local exclude_path="${2:-}"

  while IFS= read -r f; do
    [ -n "$exclude_path" ] && [ "$f" = "$exclude_path" ] && continue
    grep -InE "$pattern" "$f" 2>/dev/null | sed "s#^#$f:#" || true
  done < <(public_file_list)
}

# 1) Forbidden file types (ROMs, extracted asset binaries, build/vendor binaries)
#    Exception: branding/appicon-source.png + branding/appicon-windows.ico are
#    hand-authored project branding art (not ROM-derived) — the single tracked
#    source every platform icon is generated/wrapped from at build/package
#    time. See the .gitignore comment above this same exception.
if [ "$HAVE_GIT" -eq 1 ]; then
  forbidden=$(git ls-files \
    '*.z64' '*.n64' '*.v64' '*.rom' '*.bin' '*.rz' '*.eeprom' '*.cdata*' \
    '*.ctl' '*.tbl' '*.sbk' '*.seq' '*.aifc' '*.aiff' '*.seg' \
    'baserom*' '*.o' '*.a' '*.so' '*.dll' '*.dylib' '*.exe' \
    '*.bmp' '*.png' '*.jpg' '*.jpeg' '*.gif' '*.webp' '*.ico' '*.icns' '*.ppm' '*.pgm' '*.pnm' \
    '*.raw' '*.wav' '*.mp3' '*.ogg' '*.flac' '*.m4a' '*.aac' \
    '*.mp4' '*.mov' '*.m4v' '*.mkv' '*.avi' '*.webm' '*.jsonl' \
    '*.dmg' '*.zip' '*.7z' '*.tar' '*.tgz' '*.gz' \
    ':!branding/appicon-source.png' ':!branding/appicon-windows.ico' 2>/dev/null || true)
else
  forbidden=$(public_file_list \
    | grep -E '(^|/)baserom|\.((z64|n64|v64|bin|rz|eeprom|ctl|tbl|sbk|seq|aifc|aiff|seg|o|a|so|dll|dylib|exe|bmp|png|jpe?g|gif|webp|ico|icns|ppm|pgm|pnm|raw|wav|mp3|ogg|flac|m4a|aac|mp4|mov|m4v|mkv|avi|webm|jsonl|dmg|zip|7z|tar|tgz|gz))$|\.cdata($|\.)' \
    | grep -v -E '^branding/appicon-(source\.png|windows\.ico)$' \
    || true)
fi
if [ -n "$forbidden" ]; then
  while IFS= read -r f; do note "tracked binary/ROM/asset file: $f"; done <<< "$forbidden"
fi

# 2) ROM-derived BULK DATA files that must never be committed (loaded/extracted
#    from your ROM at build/runtime). NOTE: GlobalImageTable.c and the .inc.c
#    model-header/skeleton glue are kept in-tree on purpose (code-coupled, no
#    pixels/audio) and are intentionally NOT listed here.
if [ "$HAVE_GIT" -eq 1 ]; then
  romtables=$(git ls-files \
    'assets/animationtable_data.c' 'assets/animationtable_entries.c' \
    'assets/rarewarelogo.c' \
    'assets/font_chardata*.c' 'assets/font/font*.c' \
    'assets/obseg/bg/*_all_p.c' 2>/dev/null || true)
else
  romtables=$(public_file_list \
    | grep -E '^assets/(animationtable_data\.c|animationtable_entries\.c|rarewarelogo\.c|font_chardata.*\.c|font/font.*\.c|obseg/bg/.*_all_p\.c)$' \
    || true)
fi
if [ -n "$romtables" ]; then
  while IFS= read -r f; do note "tracked ROM-derived data table: $f"; done <<< "$romtables"
fi

# 3) Oversized tracked files that are not known source (cheap heuristic for
#    accidentally-committed blobs). Threshold: 3 MB.
while IFS= read -r f; do
  [ -f "$f" ] || continue
  sz=$(wc -c < "$f")
  if [ "$sz" -gt 3145728 ]; then
    case "$f" in
      *.c|*.h|*.cpp|*.s) : ;;  # large source is allowed
      scripts/ge007.*-test_basis.csv|scripts/filelist.*.csv|scripts/filediff.*.csv) : ;;
      *) note "tracked file larger than 3 MB (possible blob): $f ($sz bytes)";;
    esac
  fi
done < <(public_file_list)

# 4) Embedded base64/data-URI payloads hide binary provenance inside otherwise
#    reviewable source files. These are not allowed in the public tree; use
#    external, licensed source assets or generated runtime data instead.
if [ "$HAVE_GIT" -eq 1 ]; then
  embedded_payloads=$(git grep -nE 'data:(file|font|application|image)/|;base64,' -- . ':!scripts/ci/check_no_rom_data.sh' 2>/dev/null || true)
else
  embedded_payloads=$(grep_public_files 'data:(file|font|application|image)/|;base64,' 'scripts/ci/check_no_rom_data.sh')
fi
if [ -n "$embedded_payloads" ]; then
  while IFS= read -r hit; do note "tracked embedded binary/data-URI payload: $hit"; done <<< "$embedded_payloads"
fi

# 5) Direct SDK/devkit/demo source-path comments are not acceptable in public
#    source. Keep provenance in THIRD_PARTY.md/docs and replace copied
#    explanatory text with project-owned descriptions.
if [ "$HAVE_GIT" -eq 1 ]; then
  sdk_reference_comments=$(git grep -nEi 'n64devkit|libsrc[\\/]+libultra|usr[\\/]+src[\\/]+pr|PR[\\/]+libultra[\\/]+|demos_old|audioMgr[.]c([^[:alnum:]_]|$)|audioMgr[[:space:]]+demo|audiomgr[[:space:]]+example|(^|[^[:alnum:]_])devkit([^[:alnum:]_]|$)' -- \
    '*.c' '*.h' '*.s' '*.S' '*.def' 'CMakeLists.txt' 'Makefile' \
    ':!scripts/ci/check_no_rom_data.sh' \
    ':!src/libultra/*' ':!src/libultrare/*' 2>/dev/null || true)
else
  sdk_reference_comments=$(
    while IFS= read -r f; do
      case "$f" in
        *.c|*.h|*.s|*.S|*.def|CMakeLists.txt|Makefile)
          case "$f" in
            scripts/ci/check_no_rom_data.sh|src/libultra/*|src/libultrare/*) continue ;;
          esac
          grep -InEi 'n64devkit|libsrc[\\/]+libultra|usr[\\/]+src[\\/]+pr|PR[\\/]+libultra[\\/]+|demos_old|audioMgr[.]c([^[:alnum:]_]|$)|audioMgr[[:space:]]+demo|audiomgr[[:space:]]+example|(^|[^[:alnum:]_])devkit([^[:alnum:]_]|$)' "$f" 2>/dev/null | sed "s#^#$f:#" || true
          ;;
      esac
    done < <(public_file_list)
  )
fi
if [ -n "$sdk_reference_comments" ]; then
  while IFS= read -r hit; do note "direct SDK/devkit source reference in tracked file: $hit"; done <<< "$sdk_reference_comments"
fi

# 6) Proprietary SDK notice text is not allowed in the public tree. SDK-lineage
#    compatibility material is tracked in THIRD_PARTY.md, but notices and copied
#    license-restriction blocks must not re-enter source.
if [ "$HAVE_GIT" -eq 1 ]; then
  sdk_notice_paths=$(git grep -liE 'UNPUBLISHED[[:space:]]+PROPRIETARY|may not be disclosed|without the prior written (permission|consent)|RESTRICTED[[:space:]]+RIGHTS|subparagraph \(c\)\(1\)\(ii\) of the Rights' -- . ':!scripts/ci/check_no_rom_data.sh' ':!scripts/ci/check_release_ready.sh' 2>/dev/null || true)
else
  sdk_notice_paths=$(
    while IFS= read -r f; do
      case "$f" in
        scripts/ci/check_no_rom_data.sh|scripts/ci/check_release_ready.sh) continue ;;
      esac
      if grep -Iq . "$f" 2>/dev/null && grep -IqE 'UNPUBLISHED[[:space:]]+PROPRIETARY|may not be disclosed|without the prior written (permission|consent)|RESTRICTED[[:space:]]+RIGHTS|subparagraph \(c\)\(1\)\(ii\) of the Rights' "$f" 2>/dev/null; then
        printf '%s\n' "$f"
      fi
    done < <(public_file_list)
  )
fi
sdk_notice_count=0
if [ -n "$sdk_notice_paths" ]; then
  while IFS= read -r f; do
    sdk_notice_count=$((sdk_notice_count + 1))
    case "$f" in
      include/PR/R4300.h|include/PR/abi.h|include/PR/gbi.h|include/PR/gu.h|include/PR/libaudio.h|include/PR/mbi.h|include/PR/os.h|include/PR/os_internal.h|include/PR/rcp.h|include/PR/region.h|include/PR/ultratypes.h)
        note "proprietary notice in clean-room PR compatibility header: $f"
        ;;
      include/assert.h|include/bstring.h|include/limits.h|include/math.h|include/sgidefs.h|include/stddef.h|include/stdlib.h|include/svr4_math.h)
        note "proprietary notice in clean-room top-level compatibility header: $f"
        ;;
      src/libultra/audio/synthInternals.h|src/libultra/gu/guint.h|src/libultra/io/viint.h)
        note "proprietary notice in clean-room libultra compatibility header: $f"
        ;;
      src/libultra/audio/clean_compat.c|src/libultra/gu/align.c|src/libultra/gu/cosf.c|src/libultra/gu/coss.c|src/libultra/gu/lookat.c|src/libultra/gu/lookatref.c|src/libultra/gu/mtxutil.c|src/libultra/gu/normalize.c|src/libultra/gu/ortho.c|src/libultra/gu/perspective.c|src/libultra/gu/rotate.c|src/libultra/gu/scale.c|src/libultra/gu/sinf.c|src/libultra/gu/sins.c|src/libultra/gu/translate.c|src/libultra/io/viblack.c)
        note "proprietary notice in clean-room libultra compatibility source: $f"
        ;;
      include/PR/*|src/libultra/*|src/libultrare/*)
        note "proprietary notice in SDK/libultra-lineage compatibility path: $f"
        ;;
      *)
        note "proprietary notice outside known SDK compatibility paths: $f"
        ;;
    esac
  done <<< "$sdk_notice_paths"
fi

if [ "$fail" -ne 0 ]; then
  echo
  echo "Contamination guard FAILED. Do not commit ROMs or ROM-derived assets."
  echo "See CONTRIBUTING.md and DISCLAIMER.md."
  exit 1
fi
echo "  OK — no ROM or ROM-derived data found in tracked files."
