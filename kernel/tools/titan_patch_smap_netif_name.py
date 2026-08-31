#!/usr/bin/env python3
"""Create Titan's SMAP image with a NETMAN-safe 3-character interface name.

The frozen PS2SDK NETMAN ABI reserves only four bytes for a NetIF name, while
its EE helper also reserves the final byte for NUL. Upstream SMAP registers the
four-byte name "SMAP", so NetManSetMainIF("SMAP") is truncated to "SMA" and
cannot select it. Titan changes only the static NetManNetIF name field from
"SMAP" to "SMA\0"; module identity "SMAP_driver", code, ABI and all other data
remain byte-for-byte identical.
"""
from pathlib import Path
import hashlib
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: titan_patch_smap_netif_name.py INPUT_SMAP.IRX OUTPUT_SMAP.IRX")

src=Path(sys.argv[1])
dst=Path(sys.argv[2])
data=src.read_bytes()

needle=b"SMAP\x00\x00\x00\x00"
replacement=b"SMA\x00\x00\x00\x00\x00"

count=data.count(needle)
if count != 1:
    raise SystemExit(f"refusing SMAP patch: expected exactly one NetIF initializer, found {count}")

driver_before=data.count(b"SMAP_driver")
patched=data.replace(needle,replacement,1)
if patched.count(b"SMAP_driver") != driver_before:
    raise SystemExit("refusing SMAP patch: module identity changed")
if len(patched) != len(data):
    raise SystemExit("refusing SMAP patch: image size changed")
if patched.count(replacement) < 1:
    raise SystemExit("refusing SMAP patch: alias verification failed")

dst.parent.mkdir(parents=True,exist_ok=True)
dst.write_bytes(patched)

print("TITAN_SMAP_ALIAS_PATCH=PASS")
print("SOURCE_SHA256="+hashlib.sha256(data).hexdigest())
print("OUTPUT_SHA256="+hashlib.sha256(patched).hexdigest())
print("NETIF_NAME=SMA")
print("MODULE_IDENTITY=SMAP_driver")
