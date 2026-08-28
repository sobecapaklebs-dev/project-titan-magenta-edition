#!/usr/bin/env python3
"""Prepare pinned TLS build inputs for Project Titan (host-only)."""
from __future__ import annotations
import argparse, hashlib, io, os, shutil, tarfile, tempfile, urllib.request
from pathlib import Path

BEARSSL_URL = "https://bearssl.org/bearssl-0.6.tar.gz"
ATHENA_CA_URL = "https://raw.githubusercontent.com/DanielSant0s/AthenaEnv/main/src/net/ta_cabundle.c"
ATHENA_CA_SHA256 = "c91316e5e83e480b5275be02fdfb810ae834c47704b6eb8d1d36a9da4569779d"
USER_AGENT = "Project-Titan-TLS-Dependency-Builder/1.0"

def fetch(url: str, limit: int) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=30) as response:
        data = response.read(limit + 1)
    if len(data) > limit:
        raise SystemExit(f"dependency exceeds safety limit: {url}")
    return data

def safe_extract_tar_gz(blob: bytes, destination: Path) -> Path:
    with tarfile.open(fileobj=io.BytesIO(blob), mode="r:gz") as archive:
        members = archive.getmembers(); roots: set[str] = set(); dest = destination.resolve()
        for member in members:
            name = member.name.replace("\\", "/")
            if member.issym() or member.islnk():
                raise SystemExit("BearSSL archive contains an unsupported link")
            if name.startswith("/") or ".." in Path(name).parts:
                raise SystemExit("unsafe BearSSL archive path")
            roots.add(Path(name).parts[0])
            target = (destination / name).resolve()
            if target != dest and dest not in target.parents:
                raise SystemExit("BearSSL archive escapes destination")
        if len(roots) != 1:
            raise SystemExit("unexpected BearSSL archive layout")
        archive.extractall(destination)
        return destination / next(iter(roots))

def atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(prefix=path.name + ".", dir=str(path.parent))
    try:
        with os.fdopen(fd, "wb") as handle: handle.write(data)
        os.replace(tmp, path)
    finally:
        if os.path.exists(tmp): os.unlink(tmp)

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, required=True)
    ap.add_argument("--ca-output", type=Path, required=True)
    ap.add_argument("--marker", type=Path, required=True)
    ns = ap.parse_args()
    root = ns.root.resolve(); bearssl = root / "bearssl-0.6"; root.mkdir(parents=True, exist_ok=True)
    if not (bearssl / "inc" / "bearssl.h").is_file():
        blob = fetch(BEARSSL_URL, 2 * 1024 * 1024)
        staging = root / ".bearssl-extract"
        if staging.exists(): shutil.rmtree(staging)
        staging.mkdir(parents=True)
        extracted = safe_extract_tar_gz(blob, staging)
        if not (extracted / "inc" / "bearssl.h").is_file():
            raise SystemExit("downloaded BearSSL 0.6 has unexpected layout")
        if bearssl.exists(): shutil.rmtree(bearssl)
        shutil.move(str(extracted), str(bearssl)); shutil.rmtree(staging, ignore_errors=True)
    ca = fetch(ATHENA_CA_URL, 1024 * 1024)
    digest = hashlib.sha256(ca).hexdigest()
    if digest != ATHENA_CA_SHA256:
        raise SystemExit("AthenaEnv CA bundle changed; refusing unpinned TLS trust data: " + digest)
    if b"br_x509_trust_anchor TAs" not in ca or b"#define TAs_NUM" not in ca:
        raise SystemExit("AthenaEnv CA bundle format is not compatible with BearSSL")
    atomic_write(ns.ca_output.resolve(), ca)
    atomic_write(ns.marker.resolve(), (f"BearSSL=0.6\nBearSSL-URL={BEARSSL_URL}\nCA-URL={ATHENA_CA_URL}\nCA-SHA256={ATHENA_CA_SHA256}\n").encode())
    print("Titan TLS dependencies: READY")
    return 0
if __name__ == "__main__": raise SystemExit(main())
