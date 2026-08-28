#!/bin/bash

usage()
{
    echo "Rare 1172 decompression script"
    echo "Usage: $0 input output" 1>&2;
    exit 1;
}

if [ -z "$1" ]; then
    usage
fi;

if [ -z "$2" ]; then
    usage
fi;

# input file to decompress
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
    echo "can not read input file: ${INPUT_FILE}"
    usage
fi

# explanation of commands:
# use process substitution to cat contents together to send to gzip.
# the first cat argument supplies the standard gzip header
# the next cat argument prints the input file, and chops the "1172" prefix
# this is sent to the gzip command for decompression, and the expected "unexpected end of file" error is filtered out

cat <(echo -n -e \\x1F\\x8B\\x08\\x00\\x00\\x00\\x00\\x00\\x02\\x03) <(cat "${INPUT_FILE}" | tail --bytes=+3) | "${GZ}" --decompress 2> >(sed '/gzip: stdin: unexpected end of file/d' >&2) > "${OUTPUT_FILE}"
echo "Successfully Decompressed ${INPUT_FILE}"
