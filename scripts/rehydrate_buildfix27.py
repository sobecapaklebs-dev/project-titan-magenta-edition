#!/usr/bin/env python3
"""BuildFix27 final deterministic recovery.

Reconstructs the frozen BF27 source exclusively from artifacts that are
persisted in the Project Titan/BuildFix27 Library folder:
  CP00 full source -> CP10R -> CP11..CP49 -> real FCEUmm integration ->
  final closure delta.

The historical CP14 full archive is intentionally NOT used because it was
never persisted. The final tree is accepted only when it matches the supplied
final SHA-256 manifest with zero missing, unexpected, or mismatched files.
"""
import argparse, hashlib, os, shutil, sys, tarfile, tempfile
from pathlib import Path

BASE = ("Project-Titan-0.3.92-rc16-TitanEYE-Ribbon-BuildFix27-CP00-source.tar.gz",
        "3a54226f820e5dc013377dc50e4c8da618523c15d47dda653419ca8a0af7ac5d")
LOCKED = [
("BUILDFIX27_CP10R_INCREMENTAL.tar.gz","73323bfd31e9002abf8d0be9a526cca5b2c8dba8344dd522ef985524c4819894"),
("BUILDFIX27_CP11_INCREMENTAL.tar.gz","c8bdef8e9124f6c1dc6704594b784179c3e20761d21e31b4685894803d8a5694"),
("BUILDFIX27_CP12_INCREMENTAL.tar.gz","adfdc53fa315fcba37f56b4639e0e9070e030669fb08425a4ca61ed2f57a79b2"),
("BUILDFIX27_CP13_INCREMENTAL.tar.gz","6e90ba0598ba30830921b960688aacbfe81290f89f21f7db27205ed55d9a8972"),
("BUILDFIX27_CP14_INCREMENTAL.tar.gz","c1f0ab760c760005247f7c8902173f10fa4e869c8d175903e98941abbc1818c1"),
("BUILDFIX27_CP15_INCREMENTAL.tar.gz","1cd526545034f238a1768f75ef1f1b4592e8a9ab60914627179d3bad39fb348f"),
("BUILDFIX27_CP16_INCREMENTAL.tar.gz","16ff288a2792dc1bd84e98565bcd7c5dcacbebf8b9c329446c486008e1fc30bd"),
("BUILDFIX27_CP17_INCREMENTAL.tar.gz","d92cdf4982e8399db61a0a3114bfad4992a442101e139d03a53d1ac17059b527"),
("BUILDFIX27_CP18_INCREMENTAL.tar.gz","cb15bd16020bdb38195dc221257656b7c65d5684f981a692b6658292560b99d9"),
("BUILDFIX27_CP19_INCREMENTAL.tar.gz","32f55f489c8d6da7840b2445543d568027ee36dd3b86ad3748a6e43a6b62223c"),
("BUILDFIX27_CP20_INCREMENTAL.tar.gz","1ff16e9917c31c62614f7e67f069e3887985debbce6e6d64c7af793255d46858"),
("BUILDFIX27_CP21_INCREMENTAL.tar.gz","6e5378599e13c0a933a07bf121aba0723bb1439cc5e2d7ee002a9ae873d70412"),
("BUILDFIX27_CP22_INCREMENTAL.tar.gz","5dda7e02f83d2904b9f7d0f7da10fbb762cc27d16ac90240137f48507a6b70a3"),
("BUILDFIX27_CP23_INCREMENTAL.tar.gz","778a018df2331f75c9c0f7adbfc7a31e439ca395b13a8832b11add94b7605b7a"),
("BUILDFIX27_CP24_INCREMENTAL.tar.gz","23065d5ba35c088856d75843e1d712a0df6d3934c226849d3888d897f111fae6"),
("BUILDFIX27_CP25_INCREMENTAL.tar.gz","14a9dfdb2c801364945d673f3c4c521d056a4af4a9c5e595309c914540695f7e"),
("BUILDFIX27_CP26_INCREMENTAL.tar.gz","d5a0a10986a6fb9b9774e0e2466bae402eeb0d47f3cc1c63b14bac39e3732c2d"),
("BUILDFIX27_CP27_INCREMENTAL.tar.gz","8068ba452b820fefbf117ebf56a050af9db4586723542f1cdf3b8f62bc82e3aa"),
("BUILDFIX27_CP28_INCREMENTAL.tar.gz","50954ded9d45aa50724c6571fa5a41b33eaec48a71c7bfe5cda4e8233758e38e"),
("BUILDFIX27_CP29_INCREMENTAL.tar.gz","8dda3ace2bb497e7c6e22610d2a73b77647f511ec30f02298f2b0435b42c9aa5"),
("BUILDFIX27_CP30_INCREMENTAL.tar.gz","86332b836f84d639004cb262c41f96ca1b474f11045db8fb76a7f457d798bbdd"),
("BUILDFIX27_CP31_INCREMENTAL.tar.gz","9d76810746c3076de5d52e7c6382f402ba50cb2c24cb9fddb6e7a95d27ce0ad6"),
("BUILDFIX27_CP32_INCREMENTAL.tar.gz","1707f255e629a43075ccefbec397e1cd1706abfe4d7908e764e43ac6598e146d"),
("BUILDFIX27_CP33_INCREMENTAL.tar.gz","3b0424455d5cd0ba8ae21b434092679ea856bb6a68db45cbfac74f7cb1958b47"),
("BUILDFIX27_CP34_INCREMENTAL.tar.gz","43fe567fcba673b2468dc25755a0181345526b49ba0a0e20d230c53cadc63bd6"),
("BUILDFIX27_CP35_INCREMENTAL.tar.gz","696d836f366b8e0f7e52196b1c10776e7d224e2d061ad58276be9cabda2885a3"),
("BUILDFIX27_CP36_INCREMENTAL.tar.gz","d64dfccc0ae6bd203c0026f48e68e7f2a147b1ecbf93b1074aa9b0d0dbbde58b"),
("BUILDFIX27_CP37_INCREMENTAL.tar.gz","23816b3f93997a0330fdbafedf66cbcb3706ceb22b183b0f9b2dec551dc4a88c"),
("BUILDFIX27_CP38_INCREMENTAL.tar.gz","7cd755a48b3c21246ef239cf3235fcdd80dce8e4494e803c250f16b6af6816f3"),
("BUILDFIX27_CP39_INCREMENTAL.tar.gz","b63cf3255e7a814ace8d47b4901901c69a23f1ea9c8c0b916f36d9aafda54eef"),
("BUILDFIX27_CP40_INCREMENTAL.tar.gz","bb4a39d7f3d650ae112050ba82f8c6faab08f3076b3096c276c6bda806d16fa1"),
("BUILDFIX27_CP41_INCREMENTAL.tar.gz","d1eff73e7723dc098812ef31832e48eb89acabe01c92a1f8bed6fb856277f5eb"),
("BUILDFIX27_CP42_INCREMENTAL.tar.gz","efadfadf026c9eab323636387d3fb9aa49bc2f70c6a6586be4e5a0b3dcbcbaf4"),
("BUILDFIX27_CP43_INCREMENTAL.tar.gz","aece167885a4a74eebd2d6d33ccc31efe9f36ac7fc2c182039d8b4068ce32f36"),
("BUILDFIX27_CP44_INCREMENTAL.tar.gz","f70ad7eed30d751ddcc40ee6b2bb3d0de3008138521d96712acf24e45b8fc078"),
("BUILDFIX27_CP45_INCREMENTAL.tar.gz","df5a7afa8cd18550dbcefbc6545b7cae3cd2aea1aae62bc7ba1a59de0e8e3307"),
("BUILDFIX27_CP46_INCREMENTAL.tar.gz","421920762655b28c9571520ee7cdc2bfce397ec8147c98cc3f5f76fdd438e196"),
("BUILDFIX27_CP47_INCREMENTAL.tar.gz","25fc123a2bd40bc50e49a1bb6fed5867a0d16b94c07cbc9b06691873d4963a6a"),
("BUILDFIX27_CP48_INCREMENTAL.tar.gz","ec0de73beb9ac528e34871faa1e2c2b8a264c7248288e0f58bbd0e97bbf1677d"),
("BUILDFIX27_CP49_INCREMENTAL.tar.gz","5fdf5a85a0f09032ddae32f8b39865eed52507f2c2cdcc23f59966a0cd01a17e"),
("BUILDFIX27_FCEUMM_REAL_INTEGRATION.tar.gz","8a2977db81ad68954a231f0f29d03e174fa640d871dc8ebd54cb023cc226bf1e"),
]
CLOSURE = "BUILDFIX27_FINAL_CLOSURE_DELTA.tar.gz"
DEFAULT_MANIFEST = "BUILDFIX27_FINAL_MANIFEST.txt"
EPHEMERAL = (
    "FINAL_CLOSURE_DELTA_SHA256SUMS",
    "apps/project-nes/build/host",
    "apps/wlaunchelf/build/host",
    "kernel/tests/__pycache__",
    "tools/__pycache__",
)

class Error(RuntimeError): pass

def digest(path):
    h=hashlib.sha256()
    with open(path,'rb') as f:
        for chunk in iter(lambda:f.read(1024*1024),b''): h.update(chunk)
    return h.hexdigest()

def safe_extract(path, dest, strip_root=False):
    with tarfile.open(path,'r:gz') as tf:
        members=[]; root=None
        for m in tf.getmembers():
            p=Path(m.name)
            if p.is_absolute() or '..' in p.parts or m.issym() or m.islnk() or m.isdev():
                raise Error(f"unsafe tar member {path.name}: {m.name}")
            parts=tuple(x for x in p.parts if x not in ('','.'))
            if strip_root:
                if not parts: continue
                root = root or parts[0]
                if parts[0] != root: raise Error("CP00 archive has multiple roots")
                parts=parts[1:]
                if not parts: continue
            members.append(m.replace(name=str(Path(*parts)),deep=False))
        tf.extractall(dest,members=members,filter='data')

def load_manifest(path):
    out={}
    for line in path.read_text(encoding='utf-8').splitlines():
        if not line.strip(): continue
        try: h, rel=line.split('  ',1)
        except ValueError: raise Error(f"bad manifest line: {line!r}")
        if rel.startswith('./'): rel=rel[2:]
        out[rel]=h.lower()
    return out

def compare(root, manifest):
    expected=load_manifest(manifest); actual={}
    for p in root.rglob('*'):
        if p.is_file(): actual[p.relative_to(root).as_posix()]=digest(p)
    missing=sorted(set(expected)-set(actual))
    unexpected=sorted(set(actual)-set(expected))
    mismatch=sorted(k for k in set(expected)&set(actual) if expected[k]!=actual[k])
    return len(expected),len(actual),missing,unexpected,mismatch

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--material-dir',required=True,type=Path)
    ap.add_argument('--output',required=True,type=Path)
    ap.add_argument('--manifest',type=Path,default=None)
    ap.add_argument('--overwrite',action='store_true')
    a=ap.parse_args(); material=a.material_dir.resolve(); output=a.output.resolve()
    manifest=(a.manifest or (material/DEFAULT_MANIFEST)).resolve()
    required=[BASE,*LOCKED]
    for name,want in required:
        p=material/name
        if not p.is_file(): raise Error(f"missing persisted material: {name}")
        got=digest(p)
        if got!=want: raise Error(f"SHA mismatch {name}: expected={want} actual={got}")
    closure=material/CLOSURE
    if not closure.is_file(): raise Error(f"missing persisted material: {CLOSURE}")
    if not manifest.is_file(): raise Error(f"missing final manifest: {manifest}")
    if output.exists():
        if not a.overwrite: raise Error(f"output exists: {output}")
        shutil.rmtree(output)
    output.parent.mkdir(parents=True,exist_ok=True)
    stage=Path(tempfile.mkdtemp(prefix='.bf27-final-rehydrate-',dir=output.parent))
    try:
        safe_extract(material/BASE[0],stage,strip_root=True)
        for name,_ in LOCKED: safe_extract(material/name,stage)
        safe_extract(closure,stage)
        # Closure tar carries its own generation metadata and may inherit host
        # outputs from the validation workspace. They are explicitly non-source.
        for rel in EPHEMERAL:
            p=stage/rel
            if p.is_dir(): shutil.rmtree(p,ignore_errors=True)
            elif p.exists(): p.unlink()
        # Manifests are delivered beside the source archive and validated here;
        # do not count a copy extracted from a delta as source content.
        for rel in ("BUILDFIX27_FINAL_SOURCE_MANIFEST.sha256","BUILDFIX27_FINAL_MANIFEST.txt"):
            p=stage/rel
            if p.exists(): p.unlink()
        exp,act,missing,unexpected,mismatch=compare(stage,manifest)
        print(f"EXPECTED={exp} ACTUAL={act} MISSING={len(missing)} UNEXPECTED={len(unexpected)} HASH_MISMATCH={len(mismatch)}")
        if missing or unexpected or mismatch:
            if missing: print("MISSING_FILES="+"\n".join(missing[:50]),file=sys.stderr)
            if unexpected: print("UNEXPECTED_FILES="+"\n".join(unexpected[:50]),file=sys.stderr)
            if mismatch: print("HASH_MISMATCH_FILES="+"\n".join(mismatch[:50]),file=sys.stderr)
            raise Error("final manifest verification failed")
        os.replace(stage,output)
    except Exception:
        shutil.rmtree(stage,ignore_errors=True); raise
    print(f"BUILDFIX27_FINAL_REHYDRATE_OK output={output}")

if __name__=='__main__':
    try: main()
    except Error as e:
        print(f"BUILDFIX27_FINAL_REHYDRATE_ERROR: {e}",file=sys.stderr); raise SystemExit(2)
