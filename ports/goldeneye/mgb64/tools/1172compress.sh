#!/bin/bash

usage()
{
    echo "Rare 1172 compression script"
    echo "Usage: $0 input output" 1>&2;
    exit 1;
}

if [ -z "$1" ]; then
    usage
fi;

if [ -z "$2" ]; then
    usage
fi;

# input file to compress
INPUT_FILE="$1"
# output file result
OUTPUT_FILE="$2"
# gzip command.
GZ=${GZ:-'gzipsrc/gzip'}

gzip_is_usable()
{
    "$1" --version >/dev/null 2>&1
}

resolve_gzip()
{
    if gzip_is_usable "${GZ}"; then
        return
    fi

    if [ "${GZ}" != "tools/gzipsrc/gzip" ] && gzip_is_usable "tools/gzipsrc/gzip"; then
        GZ="tools/gzipsrc/gzip"
        return
    fi

    echo "decomp gzip bin not found or not runnable, falling back to system gzip" >&2
    GZ="gzip"
    if ! gzip_is_usable "${GZ}"; then
        echo "can not find runnable gzip" >&2
        exit 2
    fi
}

resolve_gzip

# make sure input file exists
if [ ! -f "${INPUT_FILE}" ]; then
    echo "can not read input file"
    usage
fi

# create
echo -n -e \\x11\\x72 > "${OUTPUT_FILE}"
cat "${INPUT_FILE}" | "${GZ}" --no-name --best | tail --bytes=+11 | head --bytes=-8 >> "${OUTPUT_FILE}"
