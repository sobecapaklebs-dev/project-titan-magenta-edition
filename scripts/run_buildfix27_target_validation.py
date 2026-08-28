#!/usr/bin/env python3
"""BuildFix27 CP47 deterministic target build/evidence orchestrator.

Consumes an already materialized PS2DEV bundle. It never downloads toolchains,
never silently substitutes a host compiler, and writes all evidence outside the
source tree. PCSX2 execution is optional and only occurs when the caller
provides an explicit command template containing {kernel} if desired.
"""
from __future__ import annotations
import argparse, hashlib, json, os, shlex, shutil, subprocess, sys, time
from pathlib import Path


def run(cmd, *, cwd: Path, env: dict[str, str], log, timeout=900) -> int:
    log.write("$ " + " ".join(shlex.quote(str(x)) for x in cmd) + "\n")
    log.flush()
    try:
        cp = subprocess.run(cmd, cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT,
                            timeout=timeout, check=False)
        log.write(f"[exit={cp.returncode}]\n")
        log.flush()
        return cp.returncode
    except subprocess.TimeoutExpired:
        log.write(f"[timeout={timeout}]\n")
        log.flush()
        return 124


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def parse_env_file(path: Path) -> dict[str, str]:
    out: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or not line.startswith("export "):
            continue
        key, sep, value = line[7:].partition("=")
        if not sep:
            raise ValueError(f"malformed env line: {raw}")
        # CP45 emits shell-quoted scalar paths and PATH with a literal $PATH tail.
        if key == "PATH":
            prefix = value
            if prefix.endswith(':"$PATH"'):
                prefix = prefix[:-8]
            try:
                prefix = shlex.split(prefix)[0]
            except (ValueError, IndexError) as exc:
                raise ValueError(f"invalid PATH in env file: {exc}") from exc
            out[key] = prefix + os.pathsep + os.environ.get("PATH", "")
        else:
            vals = shlex.split(value)
            if len(vals) != 1:
                raise ValueError(f"invalid scalar env value for {key}")
            out[key] = vals[0]
    if not out.get("PS2DEV") or not out.get("PS2SDK"):
        raise ValueError("env file missing PS2DEV/PS2SDK")
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    ap.add_argument("--bundle-root", type=Path, required=True)
    ap.add_argument("--evidence-dir", type=Path, required=True)
    ap.add_argument("--pcsx2-cmd", help="explicit shell command; {kernel} expands to target kernel path")
    ap.add_argument("--timeout", type=int, default=900)
    ap.add_argument("--require-complete", action="store_true",
                    help="also require caller-supplied PCSX2 execution to pass")
    args = ap.parse_args()

    root = args.root.resolve()
    evidence = args.evidence_dir.resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    env_file = evidence / "bf27-target-env.sh"
    log_path = evidence / "target-validation.log"
    manifest_path = evidence / "target-validation.json"
    state: dict[str, object] = {
        "schema": 1, "buildfix": 27, "host_ready_parent": "CP46",
        "started_unix": int(time.time()), "target_build": "BLOCKED",
        "pcsx2": "NOT_EXECUTED", "artifacts": [], "steps": [],
    }

    def save() -> None:
        manifest_path.write_text(json.dumps(state, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    with log_path.open("w", encoding="utf-8") as log:
        intake = [sys.executable, str(root / "scripts/validate_ps2dev_bundle.py"),
                  str(args.bundle_root), "--require-ready", "--emit-env", str(env_file)]
        rc = run(intake, cwd=root, env=os.environ.copy(), log=log, timeout=60)
        state["steps"].append({"name": "bundle_intake", "exit": rc})
        if rc != 0 or not env_file.is_file():
            state["blocker"] = "PS2DEV_BUNDLE_NOT_READY"
            save(); print("BUILDFIX27_TARGET_STATUS=BLOCKED"); print(f"EVIDENCE={evidence}")
            return 2 if args.require_complete else 0

        try:
            target_vars = parse_env_file(env_file)
        except ValueError as exc:
            log.write(f"env parse error: {exc}\n"); state["blocker"] = "INVALID_CP45_ENV"; save(); return 3
        env = os.environ.copy(); env.update(target_vars)
        ee_cc = str(Path(target_vars["PS2DEV"]) / "ee/bin/mips64r5900el-ps2-elf-gcc")

        commands = [
            ("pfs_prereqs", [sys.executable, str(root / "scripts/validate_pfs_target_prereqs.py"), "--require-ready"]),
            ("wlaunchelf_build", ["make", "-C", "apps/wlaunchelf", "clean", "all", "verify", f"EE_CC={ee_cc}"]),
            ("project_nes_build", ["make", "-C", "apps/project-nes", "clean", "all", "verify", f"EE_CC={ee_cc}"]),
            ("kernel_build", ["make", "-C", "kernel", "clean", "all"]),
        ]
        for name, cmd in commands:
            rc = run(cmd, cwd=root, env=env, log=log, timeout=args.timeout)
            state["steps"].append({"name": name, "exit": rc})
            if rc != 0:
                state["blocker"] = f"STEP_FAILED:{name}"
                save(); print("BUILDFIX27_TARGET_STATUS=FAILED"); print(f"FAILED_STEP={name}")
                return rc if rc < 126 else 4

        artifacts = [
            root / "kernel/kernel.elf",
            root / "apps/wlaunchelf/build/WLAUNCHELF.pkg",
            root / "apps/wlaunchelf/build/source/wlaunchelf.tmod",
            root / "apps/project-nes/build/PROJECTNES.pkg",
            root / "apps/project-nes/build/source/project_nes.tmod",
        ]
        missing = [str(p) for p in artifacts if not p.is_file() or p.stat().st_size <= 0]
        if missing:
            state["blocker"] = "MISSING_BUILD_ARTIFACTS"; state["missing"] = missing; save(); return 5
        state["artifacts"] = [
            {"path": str(p.relative_to(root)), "size": p.stat().st_size, "sha256": sha256(p)} for p in artifacts
        ]
        state["target_build"] = "PASS"

        if args.pcsx2_cmd:
            kernel = str((root / "kernel/kernel.elf").resolve())
            command = args.pcsx2_cmd.replace("{kernel}", shlex.quote(kernel))
            log.write("$ [pcsx2 explicit] " + command + "\n"); log.flush()
            try:
                cp = subprocess.run(command, cwd=root, env=env, shell=True, stdout=log, stderr=subprocess.STDOUT,
                                    timeout=args.timeout, check=False)
                rc = cp.returncode
            except subprocess.TimeoutExpired:
                rc = 124
            state["steps"].append({"name": "pcsx2", "exit": rc})
            state["pcsx2"] = "PASS" if rc == 0 else "FAILED"
        elif args.require_complete:
            state["blocker"] = "PCSX2_COMMAND_NOT_SUPPLIED"; save();
            print("BUILDFIX27_TARGET_STATUS=PARTIAL"); print("PCSX2_STATUS=NOT_EXECUTED"); return 6

    save()
    complete = state["target_build"] == "PASS" and (state["pcsx2"] == "PASS" or not args.require_complete)
    print("BUILDFIX27_TARGET_STATUS=" + ("PASS" if complete else "PARTIAL"))
    print("TARGET_BUILD_STATUS=" + str(state["target_build"]))
    print("PCSX2_STATUS=" + str(state["pcsx2"]))
    print(f"EVIDENCE={evidence}")
    return 0 if complete else 7

if __name__ == "__main__":
    raise SystemExit(main())
