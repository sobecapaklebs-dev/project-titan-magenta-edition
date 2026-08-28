#!/usr/bin/env python3
"""Build/verify Project Titan TPUP v1 USB update packages."""
from __future__ import annotations
import argparse, binascii, struct
from pathlib import Path
MAGIC=b'TPUP'; FORMAT=1; HEADER=64; ENTRY=144; MAX_FILES=16
ALLOWED=('kernel.elf','kernel')
def safe_target(s:str)->bool:
    return s in ALLOWED or s.startswith('TITAN/') or s.startswith('bootloader/')
def crc32(data:bytes)->int: return binascii.crc32(data)&0xffffffff

def build(args):
    files=[]
    for spec in args.file:
        if '=' not in spec: raise SystemExit('--file requires target=local-path')
        target,local=spec.split('=',1); p=Path(local)
        if not safe_target(target) or not p.is_file(): raise SystemExit(f'invalid update file: {spec}')
        data=p.read_bytes(); files.append((target,data))
    if not files or len(files)>MAX_FILES: raise SystemExit('PUP requires 1..16 files')
    payload_off=HEADER+len(files)*ENTRY
    table=bytearray(); payload=bytearray(); off=0
    for target,data in files:
        raw=target.encode('utf-8');
        if len(raw)>=128: raise SystemExit('target path too long')
        e=bytearray(ENTRY); e[:len(raw)]=raw
        struct.pack_into('<IIII',e,128,off,len(data),crc32(data),0)
        table+=e; payload+=data; off+=len(data)
    ver=args.version.encode('utf-8')[:31]
    head=bytearray(HEADER); head[:4]=MAGIC
    struct.pack_into('<IIIIII',head,4,FORMAT,args.build,len(files),payload_off,payload_off+len(payload),crc32(table))
    head[28:28+len(ver)]=ver
    out=Path(args.output); out.write_bytes(head+table+payload)
    print(f'PUP: {out} build={args.build} version={args.version} files={len(files)} bytes={out.stat().st_size}')

def verify(args):
    data=Path(args.package).read_bytes()
    if len(data)<HEADER or data[:4]!=MAGIC: raise SystemExit('invalid TPUP magic')
    fmt,buildn,count,payload_off,size,table_crc=struct.unpack_from('<IIIIII',data,4)
    if fmt!=FORMAT or count<1 or count>MAX_FILES or size!=len(data) or payload_off!=HEADER+count*ENTRY: raise SystemExit('invalid TPUP header')
    table=data[HEADER:payload_off]
    if crc32(table)!=table_crc: raise SystemExit('invalid TPUP table checksum')
    version=data[28:60].split(b'\0',1)[0].decode('utf-8','replace')
    for i in range(count):
        e=table[i*ENTRY:(i+1)*ENTRY]; target=e[:128].split(b'\0',1)[0].decode(); off,sz,cs,flags=struct.unpack_from('<IIII',e,128)
        if not safe_target(target) or payload_off+off+sz>len(data) or crc32(data[payload_off+off:payload_off+off+sz])!=cs: raise SystemExit(f'invalid entry {i}')
        print(f'  {target}: {sz} bytes')
    print(f'valid TPUP v1: build={buildn} version={version} files={count}')

def main():
    ap=argparse.ArgumentParser(); sp=ap.add_subparsers(dest='cmd',required=True)
    b=sp.add_parser('build'); b.add_argument('--output',required=True); b.add_argument('--build',type=int,required=True); b.add_argument('--version',required=True); b.add_argument('--file',action='append',required=True); b.set_defaults(fn=build)
    v=sp.add_parser('verify'); v.add_argument('package'); v.set_defaults(fn=verify)
    a=ap.parse_args(); a.fn(a)
if __name__=='__main__': main()
