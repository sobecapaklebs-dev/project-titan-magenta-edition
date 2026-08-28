#!/usr/bin/env python3
import os, pathlib, subprocess, sys, tempfile
root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[2])
script = root / "scripts/validate_ps2dev_bundle.py"
assert script.is_file()
with tempfile.TemporaryDirectory() as td:
    base = pathlib.Path(td) / "ps2dev"
    sdk = base / "ps2sdk"
    for d in (base / "ee/bin", base / "iop/bin", base / "dvp/bin", sdk / "iop/irx",
              sdk / "iop/dev9/atad", sdk / "iop/hdd/apa", sdk / "iop/hdd/pfs"):
        d.mkdir(parents=True, exist_ok=True)
    for rel in ("ee/bin/mips64r5900el-ps2-elf-gcc", "ee/bin/mips64r5900el-ps2-elf-ld",
                "iop/bin/mipsel-none-elf-gcc", "dvp/bin/dvp-as"):
        p = base / rel; p.write_text("#!/bin/sh\necho fake-tool 1.0\n"); p.chmod(0o755)
    for name in ("ps2atad.irx", "ps2hdd.irx", "ps2fs.irx"):
        (sdk / "iop/irx" / name).write_bytes(b"IRX" + name.encode())
    for rel in ("iop/dev9/atad/Makefile", "iop/hdd/apa/Makefile", "iop/hdd/pfs/Makefile"):
        (sdk / rel).write_text("# fixture\n")
    env = pathlib.Path(td) / "target-env.sh"
    cp = subprocess.run([sys.executable, str(script), str(base), "--require-ready", "--emit-env", str(env)],
                        text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    assert cp.returncode == 0, cp.stdout
    assert "PS2DEV_BUNDLE_READY=1" in cp.stdout
    text = env.read_text()
    assert "export PS2DEV=" in text and "export PS2SDK=" in text
    # Remove one required IRX: readiness must fail closed and no new env file is emitted.
    (sdk / "iop/irx/ps2fs.irx").unlink()
    env2 = pathlib.Path(td) / "bad-env.sh"
    cp = subprocess.run([sys.executable, str(script), str(base), "--require-ready", "--emit-env", str(env2)],
                        text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    assert cp.returncode == 2, cp.stdout
    assert "PS2DEV_BUNDLE_READY=0" in cp.stdout and not env2.exists()
print("buildfix27-ps2dev-bundle-intake: OK deterministic external target environment gate")
