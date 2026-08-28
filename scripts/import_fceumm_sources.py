#!/usr/bin/env python3
"""BuildFix27 deterministic importer for the two frozen FCEUmm snapshots.

This tool never downloads. It accepts a caller-supplied GitHub-style tar.gz,
verifies the frozen commit identity plus selected Git blob fingerprints and GPL
license material, rejects unsafe archive entries, then extracts atomically.
"""
from __future__ import annotations
import argparse, hashlib, os, shutil, tarfile, tempfile
from pathlib import Path, PurePosixPath

PROFILES = {
    "modern": {
        "commit": "b5e3566515c27dc66c9c20572171673126532e06",
        "repo": "libretro-fceumm",
        "critical": {
            "Copying": "afd5a9471ab054bf72bb0d2403f417a5fa1b6f75",
            "Makefile.common": "4ce8e8c1eff507a0d44600630bbc042ee3337f8b",
            "src/x6502.c": "6250f2a46fd10f1b16531238fb3b42cced7f30d8",
            "src/ppu.c": "fb78963e07cee28619550ed6b572c015db70c378",
        },
    },
    "ps2": {
        "commit": "dc77e3ca4baad08f0023f61dba9846550735b74d",
        "repo": "Fceumm-PS2",
        "critical": {
            "Copying": "afd5a9471ab054bf72bb0d2403f417a5fa1b6f75",
            "src/x6502.c": "bf1e49d5923f5b0f45b055b3e40bedba4425ffc7",
            "src/cart.c": "9dd4328879b5d9f6a31c47da70c546852985a8c5",
        },
    },
}

def git_blob_sha(data: bytes) -> str:
    return hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\0" + data).hexdigest()

def _safe_member(member: tarfile.TarInfo) -> bool:
    p = PurePosixPath(member.name)
    if p.is_absolute() or ".." in p.parts:
        return False
    # Frozen source snapshot is source material only. Links/devices can redirect
    # extraction outside the verified tree and are unnecessary here.
    return member.isdir() or member.isfile()

def verify_archive(archive: Path, profile_name: str) -> tuple[str, list[tarfile.TarInfo]]:
    profile = PROFILES[profile_name]
    commit = profile["commit"]
    with tarfile.open(archive, "r:gz") as tf:
        members = tf.getmembers()
        if not members or any(not _safe_member(m) for m in members):
            raise ValueError("unsafe or unsupported tar member")
        roots = {PurePosixPath(m.name).parts[0] for m in members if PurePosixPath(m.name).parts}
        if len(roots) != 1:
            raise ValueError("archive must have one source root")
        root = next(iter(roots))
        if commit not in root:
            raise ValueError("archive root does not identify frozen commit")
        by_name = {m.name: m for m in members}
        for rel, expected in profile["critical"].items():
            name = f"{root}/{rel}"
            m = by_name.get(name)
            if m is None or not m.isfile():
                raise ValueError(f"critical file missing: {rel}")
            f = tf.extractfile(m)
            if f is None:
                raise ValueError(f"cannot read critical file: {rel}")
            data = f.read()
            got = git_blob_sha(data)
            if got != expected:
                raise ValueError(f"Git blob mismatch for {rel}: {got} != {expected}")
            if rel == "Copying" and b"GNU GENERAL PUBLIC LICENSE" not in data:
                raise ValueError("GPL-2.0 license text not recognized")
        return root, members

def extract_verified(archive: Path, profile_name: str, destination: Path) -> None:
    root, _ = verify_archive(archive, profile_name)
    destination = destination.resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    temp_parent = Path(tempfile.mkdtemp(prefix="fceumm-import-", dir=str(destination.parent)))
    try:
        with tarfile.open(archive, "r:gz") as tf:
            for member in tf.getmembers():
                if not _safe_member(member):
                    raise ValueError("unsafe member changed after verification")
            tf.extractall(temp_parent, filter="data")
        extracted = temp_parent / root
        if not extracted.is_dir():
            raise ValueError("verified source root not extracted")
        staged = destination.with_name(destination.name + ".new")
        if staged.exists():
            shutil.rmtree(staged)
        shutil.move(str(extracted), str(staged))
        old = destination.with_name(destination.name + ".old")
        if old.exists():
            shutil.rmtree(old)
        if destination.exists():
            destination.rename(old)
        staged.rename(destination)
        if old.exists():
            shutil.rmtree(old)
    finally:
        shutil.rmtree(temp_parent, ignore_errors=True)

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("profile", choices=sorted(PROFILES))
    ap.add_argument("archive", type=Path)
    ap.add_argument("--destination", type=Path)
    ap.add_argument("--verify-only", action="store_true")
    args = ap.parse_args()
    if not args.archive.is_file():
        raise SystemExit(f"archive not found: {args.archive}")
    root, _ = verify_archive(args.archive, args.profile)
    print(f"verified profile={args.profile} root={root} commit={PROFILES[args.profile]['commit']}")
    if args.verify_only:
        return 0
    if args.destination is None:
        raise SystemExit("--destination is required unless --verify-only is used")
    extract_verified(args.archive, args.profile, args.destination)
    print(f"imported {args.destination}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
