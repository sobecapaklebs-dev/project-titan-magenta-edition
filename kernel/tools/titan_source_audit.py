#!/usr/bin/env python3
"""Project Titan source-release safety audit.

Fail-closed checks for payloads that must not silently enter a future public
source release.  This tool does not publish or delete anything.
"""
from __future__ import annotations
import argparse, hashlib, json, re, sys
from pathlib import Path

ROM_EXTS={'.nes','.fds','.unf','.unif','.z64','.n64','.v64','.iso','.cso','.chd','.elf','.irx'}
SENSITIVE_EXTS={'.pem','.key','.p12','.pfx','.keystore','.jks'}
BINARY_EXTS={'.bin'}
TEXT_SUFFIXES={'.c','.h','.cpp','.hpp','.cc','.py','.sh','.md','.txt','.mk','.ini','.cfg','.json','.yml','.yaml','.toml','.xml','.cmake','.vsm','.s'}
SECRET_PATTERNS=[
    ('private-key', re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----')),
    ('aws-access-key', re.compile(rb'AKIA[0-9A-Z]{16}')),
    ('github-token', re.compile(rb'gh[pousr]_[A-Za-z0-9_]{30,}')),
    ('generic-secret-assignment', re.compile(rb'(?i)(?:password|passwd|secret|api[_-]?key|access[_-]?token)\s*[:=]\s*["\'][^"\'\r\n]{12,}["\']')),
]
ALLOW_PAYLOADS={
    'apps/project-nes/data/roms/ProjectNesTest.nes': {
        'sha256':'1c93186e908aff0f89ce763e728b4b901faf519c6d0656810c61f50e1beae9f8',
        'requires':['apps/project-nes/tools/make_test_rom.py','apps/project-nes/LICENSE'],
        'reason':'project-owned deterministic diagnostic homebrew ROM'
    },
    'kernel/src/graphics/vu1/titan_wave_vu1_micro.bin': {
        'sha256':'dfab213de4a5d7eebd9a86153db597221fd797022ab27bb3a9aef615eb8180bb',
        'requires':['kernel/src/graphics/vu1/titan_wave_vu1.vsm','kernel/Makefile'],
        'reason':'generated VU1 microprogram binary with source present'
    },
    'third_party/fmcb/upstream/installer/irx/compiled/bdm.irx': {
        'sha256':'d658f84662f3e23b468d5c84f18c7f2c53e320068cdbd744ed48730fbba3831b',
        'requires':['BUILDFIX30_FMCB_PROVENANCE.md','third_party/fmcb/upstream/README.upstream.md'],
        'reason':'identity-locked FMCB upstream payload from the user-supplied source archive'
    },
    'third_party/fmcb/upstream/installer/irx/compiled/bdmfs_fatfs.irx': {
        'sha256':'d10d9b45ffe01347ed91fbc30ee17de569f0fe23a85dd53cb58e6fd73352c9d1',
        'requires':['BUILDFIX30_FMCB_PROVENANCE.md','third_party/fmcb/upstream/README.upstream.md'],
        'reason':'identity-locked FMCB upstream payload from the user-supplied source archive'
    },
    'third_party/fmcb/upstream/installer/irx/compiled/mctools.irx': {
        'sha256':'5f57301a905b1072ac18b0b01f832bad3c1cc9252e469b8e7e92c93b773c2166',
        'requires':['BUILDFIX30_FMCB_PROVENANCE.md','third_party/fmcb/upstream/README.upstream.md'],
        'reason':'identity-locked FMCB upstream payload from the user-supplied source archive'
    },
    'third_party/fmcb/upstream/installer/irx/compiled/secrman.irx': {
        'sha256':'7e809a80c6528d126885cd85bee6e35d2d65ef169d27c7e86408a3ba3f4c702e',
        'requires':['BUILDFIX30_FMCB_PROVENANCE.md','third_party/fmcb/upstream/README.upstream.md'],
        'reason':'identity-locked FMCB upstream payload from the user-supplied source archive'
    },
    'third_party/fmcb/upstream/installer/irx/compiled/secrsif.irx': {
        'sha256':'db7c1e55511b3f97d43f5b45682f478487168529de5b862d3e5c625eb1e2256b',
        'requires':['BUILDFIX30_FMCB_PROVENANCE.md','third_party/fmcb/upstream/README.upstream.md'],
        'reason':'identity-locked FMCB upstream payload from the user-supplied source archive'
    },
    'third_party/fmcb/upstream/installer/irx/compiled/usbmass_bd.irx': {
        'sha256':'08f5b20c70d1498f8cd735a4803b5f30e09f378a201b739ad0954681bdf1aa23',
        'requires':['BUILDFIX30_FMCB_PROVENANCE.md','third_party/fmcb/upstream/README.upstream.md'],
        'reason':'identity-locked FMCB upstream payload from the user-supplied source archive'
    },
    'third_party/fmcb/upstream/installer_res/1966/INSTALL/SYS-CONF/FMCB_CFG.ELF': {
        'sha256':'7881ad9a3171cd4c675b0bc7b1311254a1c2f86c62ba64292de4f1c70f1cfff1',
        'requires':['BUILDFIX30_FMCB_PROVENANCE.md','third_party/fmcb/upstream/README.upstream.md'],
        'reason':'identity-locked FMCB upstream payload from the user-supplied source archive'
    },
    'third_party/fmcb/upstream/installer_res/1966/INSTALL/SYS-CONF/USBD.IRX': {
        'sha256':'775e90bb6cfafa1d13536d035eac11112db75b9249c9e65e05ffe31a6d1a383d',
        'requires':['BUILDFIX30_FMCB_PROVENANCE.md','third_party/fmcb/upstream/README.upstream.md'],
        'reason':'identity-locked FMCB upstream payload from the user-supplied source archive'
    },
    'third_party/fmcb/upstream/installer_res/1966/INSTALL/SYS-CONF/USBHDFSD.IRX': {
        'sha256':'a59647c14428086a4c4d440ea5a538f2d0eeb7bed224a9ae38d0334024f3fd6e',
        'requires':['BUILDFIX30_FMCB_PROVENANCE.md','third_party/fmcb/upstream/README.upstream.md'],
        'reason':'identity-locked FMCB upstream payload from the user-supplied source archive'
    },
    'third_party/fmcb/upstream/installer_res/1966/INSTALL/SYSTEM/ATAD.IRX': {
        'sha256':'d872f23b6eaebbdc392a9c4a9ffb65d872a77bf6e38055e57024fa6154a20395',
        'requires':['BUILDFIX30_FMCB_PROVENANCE.md','third_party/fmcb/upstream/README.upstream.md'],
        'reason':'identity-locked FMCB upstream payload from the user-supplied source archive'
    },
    'third_party/fmcb/upstream/installer_res/1966/INSTALL/SYSTEM/DEV9.IRX': {
        'sha256':'962bedf3ab211424409026e16c21437ce9b41edad01d7bc505b5b9037f0b469e',
        'requires':['BUILDFIX30_FMCB_PROVENANCE.md','third_party/fmcb/upstream/README.upstream.md'],
        'reason':'identity-locked FMCB upstream payload from the user-supplied source archive'
    },
    'third_party/fmcb/upstream/installer_res/1966/INSTALL/SYSTEM/HDDLOAD.IRX': {
        'sha256':'2c743ed4e87f517a974aef7c0b6a23361ee752912db0199008a406689ac1e266',
        'requires':['BUILDFIX30_FMCB_PROVENANCE.md','third_party/fmcb/upstream/README.upstream.md'],
        'reason':'identity-locked FMCB upstream payload from the user-supplied source archive'
    },

    'third_party/fmcb/resources/INSTALL/SYS-CONF/FMCB_CFG.ELF': {
        'sha256':'7881ad9a3171cd4c675b0bc7b1311254a1c2f86c62ba64292de4f1c70f1cfff1',
        'requires':['BUILDFIX30_FMCB_PROVENANCE.md','third_party/fmcb/TITAN_FMCB_SOURCE_IDENTITY.txt'],
        'reason':'identity-locked FMCB 1.966 runtime resource copied from audited upstream input'
    },
    'third_party/fmcb/resources/INSTALL/SYS-CONF/USBD.IRX': {
        'sha256':'775e90bb6cfafa1d13536d035eac11112db75b9249c9e65e05ffe31a6d1a383d',
        'requires':['BUILDFIX30_FMCB_PROVENANCE.md','third_party/fmcb/TITAN_FMCB_SOURCE_IDENTITY.txt'],
        'reason':'identity-locked FMCB 1.966 runtime resource copied from audited upstream input'
    },
    'third_party/fmcb/resources/INSTALL/SYS-CONF/USBHDFSD.IRX': {
        'sha256':'a59647c14428086a4c4d440ea5a538f2d0eeb7bed224a9ae38d0334024f3fd6e',
        'requires':['BUILDFIX30_FMCB_PROVENANCE.md','third_party/fmcb/TITAN_FMCB_SOURCE_IDENTITY.txt'],
        'reason':'identity-locked FMCB 1.966 runtime resource copied from audited upstream input'
    },
    'third_party/fmcb/resources/INSTALL/SYSTEM/ATAD.IRX': {
        'sha256':'d872f23b6eaebbdc392a9c4a9ffb65d872a77bf6e38055e57024fa6154a20395',
        'requires':['BUILDFIX30_FMCB_PROVENANCE.md','third_party/fmcb/TITAN_FMCB_SOURCE_IDENTITY.txt'],
        'reason':'identity-locked FMCB 1.966 runtime resource copied from audited upstream input'
    },
    'third_party/fmcb/resources/INSTALL/SYSTEM/DEV9.IRX': {
        'sha256':'962bedf3ab211424409026e16c21437ce9b41edad01d7bc505b5b9037f0b469e',
        'requires':['BUILDFIX30_FMCB_PROVENANCE.md','third_party/fmcb/TITAN_FMCB_SOURCE_IDENTITY.txt'],
        'reason':'identity-locked FMCB 1.966 runtime resource copied from audited upstream input'
    },
    'third_party/fmcb/resources/INSTALL/SYSTEM/HDDLOAD.IRX': {
        'sha256':'2c743ed4e87f517a974aef7c0b6a23361ee752912db0199008a406689ac1e266',
        'requires':['BUILDFIX30_FMCB_PROVENANCE.md','third_party/fmcb/TITAN_FMCB_SOURCE_IDENTITY.txt'],
        'reason':'identity-locked FMCB 1.966 runtime resource copied from audited upstream input'
    },
}
PROVENANCE_REQUIREMENTS={
    'third_party/fceumm/modern':['third_party/fceumm/modern/Copying','external/fceumm/FCEUMM_IMPORTED_SHA256SUMS'],
    'kernel/third_party/bearssl_ps2':['kernel/third_party/bearssl_ps2/BEARSSL_LICENSE.txt'],
    'modules/titan-neutrino-engine':['modules/titan-neutrino-engine/LICENSE'],
    'modules/titan-ul-engine':['modules/titan-ul-engine/LICENSE'],
    'ports/goldeneye/mgb64':['ports/goldeneye/mgb64/LICENSE','ports/goldeneye/mgb64/NOTICE.md'],
    'third_party/fmcb':['BUILDFIX30_FMCB_PROVENANCE.md','third_party/fmcb/upstream/README.upstream.md'],
}
SKIP_DIRS={'.git','.hg','.svn','build','__pycache__','.mypy_cache','.pytest_cache'}
SECRET_SCAN_EXEMPT={'kernel/tests/test_network_persistence_bf17_contract.py'}  # contains negative-test plaintext token literals

def sha256(p:Path)->str:
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''): h.update(b)
    return h.hexdigest()

def iter_files(root:Path):
    for p in root.rglob('*'):
        if not p.is_file(): continue
        rel=p.relative_to(root)
        if any(part in SKIP_DIRS for part in rel.parts): continue
        yield p, rel.as_posix()

def audit(root:Path):
    errors=[]; warnings=[]; allowed=[]; licenses=[]
    for p,rel in iter_files(root):
        suffix=p.suffix.lower()
        if suffix in ROM_EXTS or suffix in SENSITIVE_EXTS or suffix in BINARY_EXTS:
            allow=ALLOW_PAYLOADS.get(rel)
            if not allow:
                errors.append(f'UNAPPROVED_PAYLOAD {rel}')
            else:
                got=sha256(p)
                if got != allow['sha256']:
                    errors.append(f'ALLOWLIST_HASH_MISMATCH {rel} expected={allow["sha256"]} actual={got}')
                for req in allow['requires']:
                    if not (root/req).is_file(): errors.append(f'ALLOWLIST_PROVENANCE_MISSING {rel} requires={req}')
                allowed.append({'path':rel,'sha256':got,'reason':allow['reason']})
        name=p.name.lower()
        if name.startswith('license') or name.startswith('copying') or 'notice' in name:
            licenses.append(rel)
        # Avoid false positives in vendored/minified/binary content; scan project text only.
        if suffix in TEXT_SUFFIXES and p.stat().st_size <= 2*1024*1024 and rel not in SECRET_SCAN_EXEMPT and not rel.startswith(('third_party/','ports/goldeneye/mgb64/')):
            data=p.read_bytes()
            for label,rx in SECRET_PATTERNS:
                if rx.search(data): errors.append(f'SECRET_PATTERN {label} {rel}')
    for area,reqs in PROVENANCE_REQUIREMENTS.items():
        if (root/area).exists():
            for req in reqs:
                if not (root/req).is_file(): errors.append(f'PROVENANCE_MISSING {area} requires={req}')
    # Expected public-readiness documents are themselves part of the gate.
    for req in ['docs/TITAN_OPEN_SOURCE_READINESS.md','CONTRIBUTING.md']:
        if not (root/req).is_file(): errors.append(f'RELEASE_DOC_MISSING {req}')
    return {'ok':not errors,'errors':sorted(set(errors)),'warnings':sorted(set(warnings)),
            'allowed_payloads':allowed,'license_notice_files':sorted(licenses)}

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('root',nargs='?',default='.')
    ap.add_argument('--json',action='store_true'); args=ap.parse_args()
    root=Path(args.root).resolve(); result=audit(root)
    if args.json: print(json.dumps(result,indent=2,sort_keys=True))
    else:
        print(f'TITAN_SOURCE_AUDIT={"PASS" if result["ok"] else "FAIL"}')
        print(f'ALLOWED_PAYLOADS={len(result["allowed_payloads"])}')
        print(f'LICENSE_NOTICE_FILES={len(result["license_notice_files"])}')
        for x in result['allowed_payloads']: print(f'ALLOW {x["path"]} {x["sha256"]} # {x["reason"]}')
        for e in result['errors']: print(f'ERROR {e}')
    return 0 if result['ok'] else 2
if __name__=='__main__': raise SystemExit(main())
