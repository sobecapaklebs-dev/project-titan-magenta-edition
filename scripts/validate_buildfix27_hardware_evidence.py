#!/usr/bin/env python3
"""BuildFix27 CP48 physical-PS2 evidence validator.

The validator does not infer hardware success from host logs. It consumes an
actual Titan exported debug report plus a JSONL event ledger recorded during
the physical test sequence. Missing evidence fails closed.
"""
from __future__ import annotations
import argparse, hashlib, json, re, sys
from pathlib import Path

REQ_EVENTS = ("cold_boot_home", "network_online", "storage_online", "pfs_mount_rw", "ftp_roundtrip", "vmc_export")


def sha256(p: Path) -> str:
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''): h.update(b)
    return h.hexdigest()


def parse_events(path: Path):
    rows=[]
    for n,line in enumerate(path.read_text(encoding='utf-8').splitlines(),1):
        if not line.strip(): continue
        try: row=json.loads(line)
        except json.JSONDecodeError as exc: raise ValueError(f"events line {n}: {exc}") from exc
        if not isinstance(row,dict) or not isinstance(row.get('event'),str):
            raise ValueError(f"events line {n}: event object required")
        rows.append(row)
    return rows


def main() -> int:
    ap=argparse.ArgumentParser()
    ap.add_argument('--debug-report',type=Path,required=True)
    ap.add_argument('--events',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--require-pass',action='store_true')
    args=ap.parse_args()
    checks=[]
    def check(name, ok, detail): checks.append({'name':name,'pass':bool(ok),'detail':str(detail)})
    try:
        report=args.debug_report.read_text(encoding='utf-8',errors='replace')
    except OSError as exc:
        report=''; check('debug_report_present',False,exc)
    else: check('debug_report_present',True,args.debug_report)
    try:
        events=parse_events(args.events)
    except (OSError,ValueError) as exc:
        events=[]; check('events_present',False,exc)
    else: check('events_present',True,args.events)

    check('report_identity','Project Titan Magenta Edition Debug Report' in report,'Titan debug report signature')
    check('video_720p',bool(re.search(r'^Video: .* / 720p$',report,re.M)),'progressive shell output')
    check('path_reference','720p path: REFERENCE' in report,'Reference composition required for release baseline')
    check('output_1280x720','Output resolution: 1280x720' in report,'native physical output dimensions')
    m=re.search(r'^EE RAM estimate: valid=(\d+) used=(\d+) free=(\d+) peak=(\d+) min_free=(\d+) alloc_fail=(\d+)$',report,re.M)
    check('ee_watermark',bool(m and m.group(1)=='1' and m.group(6)=='0'),m.groups() if m else 'missing')
    mi=re.search(r'^IOP RAM: valid=(\d+) used=(\d+) free=(\d+) min_free=(\d+) known_modules=(\d+)$',report,re.M)
    check('iop_watermark',bool(mi and mi.group(1)=='1' and int(mi.group(3))>0),mi.groups() if mi else 'missing')
    check('boot_history','BOOT VRAM HISTORY' in report and bool(re.search(r'^HOME .*fails=0 .*ee_valid=1 .*iop_valid=1',report,re.M)),'HOME watermark with no allocation failures')
    # A critical pressure state is 2 in the current Titan VRAM contract; release evidence may not contain it.
    critical=bool(re.search(r'^\S+ .*pressure=2(?:\s|$)',report,re.M))
    check('no_critical_vram_pressure',not critical,'no boot phase may reach CRITICAL pressure')

    successes={r.get('event') for r in events if r.get('success') is True}
    for ev in REQ_EVENTS: check('event_'+ev,ev in successes,ev)
    cycles=sorted({int(r.get('cycle')) for r in events if r.get('event')=='lifecycle_cycle' and r.get('success') is True and isinstance(r.get('cycle'),int) and 1<=r['cycle']<=10})
    check('lifecycle_10_cycles',cycles==list(range(1,11)),cycles)
    # PFS evidence must prove Titan ownership rather than a direct app mount.
    pfs=[r for r in events if r.get('event')=='pfs_mount_rw' and r.get('success') is True]
    check('pfs_titan_owned',any(r.get('owner')=='TitanStorageMount' and str(r.get('mount','')).startswith('pfs') for r in pfs),pfs)
    ftp=[r for r in events if r.get('event')=='ftp_roundtrip' and r.get('success') is True]
    check('ftp_transactional',any(r.get('retr') is True and r.get('stor_atomic') is True for r in ftp),ftp)
    vmc=[r for r in events if r.get('event')=='vmc_export' and r.get('success') is True]
    check('vmc_transactional',any(r.get('atomic') is True and r.get('read_only_source') is True for r in vmc),vmc)

    passed=all(c['pass'] for c in checks)
    result={'schema':1,'buildfix':27,'parent':'CP47','status':'PASS' if passed else 'BLOCKED','checks':checks,
            'inputs':{'debug_report':str(args.debug_report),'events':str(args.events)}}
    if args.debug_report.is_file(): result['inputs']['debug_report_sha256']=sha256(args.debug_report)
    if args.events.is_file(): result['inputs']['events_sha256']=sha256(args.events)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2,sort_keys=True)+'\n',encoding='utf-8')
    print('BUILDFIX27_HARDWARE_STATUS='+result['status'])
    for c in checks:
        print(('PASS ' if c['pass'] else 'BLOCKED ')+c['name']+': '+c['detail'])
    return 0 if passed else (2 if args.require_pass else 0)

if __name__=='__main__': raise SystemExit(main())
