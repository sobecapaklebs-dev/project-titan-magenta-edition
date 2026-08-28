#!/bin/bash

# this is a partial extension of "make clean".
# this should be invoked along with regular make clean.
#
# arg 1: ALLOWED_COUNTRYCODE; this should be a string of country codes separated by space.
# arg 2: BUILD_DIR_BASE

ALLOWED_COUNTRYCODE=$1
BUILD_DIR_BASE=$2

# quote to allow space characters
if [ -z "${ALLOWED_COUNTRYCODE}" ]; then echo "$0: missing argument: ALLOWED_COUNTRYCODE"; exit 1; fi
if [ -z "${BUILD_DIR_BASE}" ]; then echo "$0: missing argument: BUILD_DIR_BASE"; exit 1; fi

echo "deleting build folders and files"

# dont quote to split on space characters
for cc in ${ALLOWED_COUNTRYCODE[@]}; do
    rm -r -f -d "${BUILD_DIR_BASE}/${cc}"
done

echo "deleting bin / rsp / asp"
rm -r -f -d bin/
rm -r -f -d assets/images/split/

# delete generated .bin assets according to the current directory structure.
# AUDIT-0014: the previous `rm -f "assets/.../*.bin"` QUOTED each glob, so the
# shell never expanded it -- rm looked for a literal file named "*.bin" (which
# never exists) and -f silently swallowed the error, leaving every generated
# binary in place. Enumerate each directory with find so the globs expand safely
# (quoted dir paths tolerate spaces, and a missing dir is skipped, not an error).
echo "deleting assets"
for d in \
    assets/music \
    assets/obseg/bg assets/obseg/brief assets/obseg/chr assets/obseg/gun \
    assets/obseg/prop \
    assets/obseg/setup assets/obseg/setup/e assets/obseg/setup/u assets/obseg/setup/j \
    assets/obseg/stan \
    assets/obseg/text assets/obseg/text/e assets/obseg/text/u assets/obseg/text/j \
    assets/ramrom assets/ramrom/e assets/ramrom/u assets/ramrom/j; do
    [ -d "$d" ] && find "$d" -maxdepth 1 -type f -name '*.bin' -delete
done