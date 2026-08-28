#!/bin/bash
#
# build_hashtable.sh -- emit the object-section md5 hashtable CSV for one
# retail version (u/j/e). Fail-closed rewrite (AUDIT-0015).
#
# Output format (consumed by scripts/test_files.sh, which reads
# IFS=',' MD5 SECTION FILE): one row per extracted section,
#   <32-hex md5>,<section>,<path>
# Escaped CSV is not supported: commas/quotes in object paths are rejected.
#
# Fail-closed contract:
#   - `-v` is normalized into one canonical COUNTRY_CODE; unsupported or
#     missing versions exit 2 BEFORE any output path is touched.
#   - The default output name derives from the validated code
#     (full_hashtable_u/j/e.csv).
#   - Every required object class must contain at least one .o file.
#   - Every objcopy/md5sum invocation is status-checked.
#   - Rows are written to a temp file in the destination directory, each row
#     is validated (32-hex digest, section, non-wildcard path), and the CSV
#     is published atomically via mv ONLY after full validation.
#   - Every failure path exits nonzero and leaves no completed CSV behind.
#
# Bash 3.2 compatible (macOS system bash): no ${var,,}, no mapfile, no
# associative arrays.
set -euo pipefail

COUNTRY_CODE=""
OUTFILE=""
TMP_BIN=""
TMP_CSV=""
ROWS=0

usage() {
    echo "$0 usage:"
    echo ""
    echo "$0 -v u [-o results_file]"
    echo ""
    echo "    -o        output filename. Optional. Defaults to full_hashtable_{version}.csv"
    echo "    -v        version. Supported options are: US,u, JP,j, EU,e"
    echo ""
}

die() {
    echo "$0: error: $*" >&2
    exit 1
}

cleanup() {
    if [ -n "${TMP_BIN}" ]; then rm -f "${TMP_BIN}"; fi
    if [ -n "${TMP_CSV}" ]; then rm -f "${TMP_CSV}"; fi
}
trap cleanup EXIT

if [ $# -eq 0 ]; then
    usage >&2
    echo "$0: error: missing required -v <version>" >&2
    exit 2
fi

while getopts "o:hv:" arg; do
    case "${arg}" in
        v) # version -- normalize into the ONE canonical COUNTRY_CODE
            ver=$(printf '%s' "${OPTARG}" | tr '[:upper:]' '[:lower:]')
            case "${ver}" in
                us|u) COUNTRY_CODE="u" ;;
                jp|j) COUNTRY_CODE="j" ;;
                eu|e) COUNTRY_CODE="e" ;;
                *)
                    echo "$0: error: unsupported version '${OPTARG}' (supported: US,u, JP,j, EU,e)" >&2
                    exit 2
                    ;;
            esac
            ;;
        o) # out file
            OUTFILE="${OPTARG}"
            ;;
        h)
            usage
            exit 0
            ;;
        *)
            usage >&2
            exit 2
            ;;
    esac
done

if [ -z "${COUNTRY_CODE}" ]; then
    echo "$0: error: missing required -v <version> (supported: US,u, JP,j, EU,e)" >&2
    exit 2
fi

for tool in mips-linux-gnu-objcopy md5sum; do
    if ! command -v "${tool}" >/dev/null 2>&1; then
        echo "command ${tool} not found" >&2
        exit 1
    fi
done

if [ -z "${OUTFILE}" ]; then
    OUTFILE="full_hashtable_${COUNTRY_CODE}.csv"
fi

# `mv tmp <dir>` would "succeed" by dropping the temp INSIDE a directory
# instead of publishing the named CSV -- reject directories up front.
if [ -d "${OUTFILE}" ]; then
    die "output path '${OUTFILE}' is a directory"
fi

BUILD_ROOT="build/${COUNTRY_CODE}"
if [ ! -d "${BUILD_ROOT}" ]; then
    die "missing build directory: ${BUILD_ROOT} (build version '${COUNTRY_CODE}' first)"
fi

# Stage the CSV in the destination directory so the final publish is an atomic
# same-filesystem rename; an unwritable destination fails here, before any work.
OUTDIR=$(dirname "${OUTFILE}")
TMP_CSV=$(mktemp "${OUTDIR}/.full_hashtable.${COUNTRY_CODE}.XXXXXX") \
    || die "cannot create temporary output in '${OUTDIR}' (destination not writable?)"
TMP_BIN=$(mktemp "${TMPDIR:-/tmp}/ge_hashtable_sec.XXXXXX") \
    || die "cannot create temporary section file"

# emit_class <subdir> <section>... -- hash the listed sections of every
# build/<code>/<subdir>/*.o into TMP_CSV. Requires >=1 matching object.
emit_class() {
    local dir="${BUILD_ROOT}/$1"
    shift
    local f sec digest
    if [ ! -d "${dir}" ]; then
        die "missing object directory: ${dir}"
    fi
    for f in "${dir}"/*.o; do
        # With no matches the glob stays literal and does not exist on disk:
        # an empty required class is an error, never a hashed wildcard row.
        if [ ! -e "${f}" ]; then
            die "no object files match ${dir}/*.o (empty required class)"
        fi
        case "${f}" in
            *'*'*|*'?'*|*'['*)
                die "wildcard character in object path '${f}'"
                ;;
            *','*|*'"'*)
                die "unsupported character (comma/quote) in object path '${f}'"
                ;;
        esac
        echo "adding ${f}"
        for sec in "$@"; do
            rm -f "${TMP_BIN}"
            mips-linux-gnu-objcopy -j "${sec}" -O binary "${f}" "${TMP_BIN}" \
                || die "objcopy failed for section '${sec}' of '${f}'"
            # A "successful" extractor that produced no output (e.g. a stub
            # tool) must not turn into a stale or missing-file digest.
            if [ ! -f "${TMP_BIN}" ]; then
                die "objcopy produced no output for section '${sec}' of '${f}'"
            fi
            digest=$(md5sum -b "${TMP_BIN}" | cut -c -32) \
                || die "md5sum failed for section '${sec}' of '${f}'"
            case "${digest}" in
                *[!0-9a-fA-F]*|'')
                    die "invalid digest '${digest}' for section '${sec}' of '${f}'"
                    ;;
            esac
            if [ "${#digest}" -ne 32 ]; then
                die "invalid digest length for section '${sec}' of '${f}'"
            fi
            printf '%s,%s,%s\n' "${digest}" "${sec}" "${f}" >> "${TMP_CSV}"
            ROWS=$((ROWS + 1))
        done
    done
}

emit_class "src"                .text .code .bss .data .rodata
emit_class "src/game"           .text .code .bss .data .rodata
emit_class "assets/obseg/bg"    .bss .data .rodata
emit_class "assets/obseg/brief" .bss .data .rodata
emit_class "assets/obseg/setup" .bss .data .rodata
emit_class "assets/obseg/stan"  .bss .data .rodata
emit_class "assets/obseg/text"  .bss .data .rodata

if [ "${ROWS}" -eq 0 ]; then
    die "no rows generated"
fi

# Full-file validation pass before publish: every row must be
# <32-hex>,<.section>,<non-empty non-wildcard path>.
CHECKED=0
while IFS=',' read -r row_md5 row_section row_path; do
    CHECKED=$((CHECKED + 1))
    case "${row_md5}" in
        *[!0-9a-fA-F]*|'') die "row ${CHECKED}: invalid digest '${row_md5}'" ;;
    esac
    if [ "${#row_md5}" -ne 32 ]; then
        die "row ${CHECKED}: digest is not 32 hex characters"
    fi
    case "${row_section}" in
        .?*) : ;;
        *) die "row ${CHECKED}: invalid section '${row_section}'" ;;
    esac
    if [ -z "${row_path}" ]; then
        die "row ${CHECKED}: empty path"
    fi
    case "${row_path}" in
        *'*'*) die "row ${CHECKED}: literal wildcard in path '${row_path}'" ;;
    esac
done < "${TMP_CSV}"

if [ "${CHECKED}" -ne "${ROWS}" ]; then
    die "row count mismatch: wrote ${ROWS}, validated ${CHECKED}"
fi

mv "${TMP_CSV}" "${OUTFILE}" || die "cannot publish '${OUTFILE}'"
TMP_CSV=""
echo "wrote ${ROWS} rows to ${OUTFILE}"
