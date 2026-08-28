#!/usr/bin/env python3
"""Extract one relocation-free VU microprogram symbol interval from an ELF object.

This is intentionally fail-closed.  Project Titan embeds DVP output as ordinary
C bytes, so unresolved relocations inside the selected microprogram would become
silent bad instructions after objcopy.  The extractor accepts only a single
PROGBITS section range delimited by explicit global start/end symbols, rejects
relocations touching the range, and writes atomically.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import os
import struct
import sys
import tempfile

ELF_MAGIC=b'\x7fELF'
ET_REL=1
SHT_PROGBITS=1
SHT_SYMTAB=2
SHT_RELA=4
SHT_NOBITS=8
SHT_REL=9
SHT_DYNSYM=11
SHN_UNDEF=0
SHN_XINDEX=0xffff

class ElfError(ValueError): pass

@dataclass(frozen=True)
class Section:
    index:int; name_offset:int; typ:int; flags:int; address:int; offset:int
    size:int; link:int; info:int; align:int; entsize:int; name:str=''

@dataclass(frozen=True)
class Symbol:
    name:str; value:int; size:int; info:int; other:int; shndx:int

@dataclass(frozen=True)
class Reloc:
    target:int; offset:int; typ:int; sym:int; section_name:str

class Elf:
    def __init__(self,data:bytes):
        self.data=data
        if len(data)<16 or data[:4]!=ELF_MAGIC: raise ElfError('input is not ELF')
        cls,enc=data[4],data[5]
        if cls not in (1,2) or enc not in (1,2): raise ElfError('unsupported ELF class/encoding')
        self.bits=32 if cls==1 else 64
        self.endian='<' if enc==1 else '>'
        hfmt=self.endian+('16sHHIIIIIHHHHHH' if self.bits==32 else '16sHHIQQQIHHHHHH')
        if len(data)<struct.calcsize(hfmt): raise ElfError('truncated ELF header')
        f=struct.unpack_from(hfmt,data,0)
        self.etype=f[1]
        shoff=f[6]; shentsize=f[11]; shnum=f[12]; shstrndx=f[13]
        self.sfmt=self.endian+('IIIIIIIIII' if self.bits==32 else 'IIQQQQIIQQ')
        need=struct.calcsize(self.sfmt)
        if shoff==0 or shentsize<need: raise ElfError('invalid section table')
        zero=self._section(0,shoff,shentsize)
        if shnum==0: shnum=zero.size
        if shstrndx==SHN_XINDEX: shstrndx=zero.link
        if shnum<=0 or shoff+shentsize*shnum>len(data): raise ElfError('section table out of bounds')
        raw=[self._section(i,shoff+i*shentsize,shentsize) for i in range(shnum)]
        if not 0<=shstrndx<len(raw): raise ElfError('bad section-name table index')
        names=self.bytes(raw[shstrndx])
        self.sections=[]
        for s in raw:
            name=self.cstr(names,s.name_offset)
            self.sections.append(Section(s.index,s.name_offset,s.typ,s.flags,s.address,s.offset,s.size,s.link,s.info,s.align,s.entsize,name))
    def _section(self,index,off,entsize):
        need=struct.calcsize(self.sfmt)
        if off<0 or off+need>len(self.data): raise ElfError('truncated section header')
        return Section(index,*struct.unpack_from(self.sfmt,self.data,off))
    @staticmethod
    def cstr(table:bytes,off:int)->str:
        if off<0 or off>=len(table):
            if off==0 and not table:return ''
            raise ElfError('bad string-table offset')
        end=table.find(b'\0',off)
        if end<0:raise ElfError('unterminated ELF string')
        return table[off:end].decode('utf-8','replace')
    def bytes(self,s:Section)->bytes:
        if s.typ==SHT_NOBITS:raise ElfError(f'{s.name} has no file payload')
        end=s.offset+s.size
        if s.offset<0 or end<s.offset or end>len(self.data):raise ElfError(f'{s.name} out of file bounds')
        return self.data[s.offset:end]
    def symbols(self):
        fmt=self.endian+('IIIBBH' if self.bits==32 else 'IBBHQQ')
        need=struct.calcsize(fmt); out=[]
        for sec in self.sections:
            if sec.typ not in (SHT_SYMTAB,SHT_DYNSYM):continue
            if not 0<=sec.link<len(self.sections):raise ElfError('bad symtab string link')
            strings=self.bytes(self.sections[sec.link]); payload=self.bytes(sec); step=sec.entsize or need
            if step<need or len(payload)%step:raise ElfError('malformed symbol table')
            for off in range(0,len(payload),step):
                f=struct.unpack_from(fmt,payload,off)
                if self.bits==32:n,v,z,info,other,shndx=f
                else:n,info,other,shndx,v,z=f
                out.append(Symbol(self.cstr(strings,n),v,z,info,other,shndx))
        return out
    def relocs(self):
        out=[]
        for sec in self.sections:
            if sec.typ not in (SHT_REL,SHT_RELA):continue
            payload=self.bytes(sec)
            if self.bits==32:
                fmt=self.endian+('II' if sec.typ==SHT_REL else 'IIi'); mask=0xff; shift=8
            else:
                fmt=self.endian+('QQ' if sec.typ==SHT_REL else 'QQq'); mask=0xffffffff; shift=32
            need=struct.calcsize(fmt); step=sec.entsize or need
            if step<need or len(payload)%step:raise ElfError(f'malformed relocation section {sec.name}')
            for off in range(0,len(payload),step):
                r_off,r_info,*_=struct.unpack_from(fmt,payload,off)
                out.append(Reloc(sec.info,r_off,r_info&mask,r_info>>shift,sec.name))
        return out

def choose(elf:Elf,name:str):
    vals=[s for s in elf.symbols() if s.name==name and s.shndx not in (SHN_UNDEF,SHN_XINDEX)]
    if not vals:raise ElfError(f'missing/undefined symbol: {name}')
    return vals[0]

def loc(elf:Elf,s:Symbol):
    if not 0<=s.shndx<len(elf.sections):raise ElfError(f'{s.name}: invalid section index')
    sec=elf.sections[s.shndx]
    off=s.value if elf.etype==ET_REL else s.value-sec.address
    if off<0 or off>sec.size:raise ElfError(f'{s.name}: outside section {sec.name}')
    return sec,off

def atomic_write(path:Path,data:bytes):
    path.parent.mkdir(parents=True,exist_ok=True)
    fd,tmp=tempfile.mkstemp(prefix='.'+path.name+'.',dir=str(path.parent))
    try:
        with os.fdopen(fd,'wb') as f:
            f.write(data);f.flush();os.fsync(f.fileno())
        os.replace(tmp,path)
    except Exception:
        try:os.unlink(tmp)
        except FileNotFoundError:pass
        raise

def main():
    p=argparse.ArgumentParser()
    p.add_argument('input',type=Path);p.add_argument('output',type=Path)
    p.add_argument('start_symbol');p.add_argument('end_symbol')
    p.add_argument('--max-size',type=int,default=16*1024)
    a=p.parse_args()
    elf=Elf(a.input.read_bytes()); start=choose(elf,a.start_symbol); end=choose(elf,a.end_symbol)
    ssec,soff=loc(elf,start); esec,eoff=loc(elf,end)
    if ssec.index!=esec.index:raise ElfError('start/end symbols are in different sections')
    if ssec.typ!=SHT_PROGBITS:raise ElfError('microprogram section is not PROGBITS')
    if eoff<=soff:raise ElfError('invalid start/end symbol interval')
    bad=[r for r in elf.relocs() if r.target==ssec.index and soff<=r.offset<eoff]
    if bad:
        detail=', '.join(f'0x{r.offset:x}/type={r.typ}/sym={r.sym}' for r in bad[:8])
        raise ElfError('pending relocation inside microprogram: '+detail)
    code=elf.bytes(ssec)[soff:eoff]
    if not code:raise ElfError('empty microprogram')
    if len(code)%8:raise ElfError(f'microprogram size {len(code)} is not multiple of 8')
    if a.max_size<=0 or len(code)>a.max_size:raise ElfError(f'microprogram too large: {len(code)} > {a.max_size}')
    atomic_write(a.output,code)
    print(f'extract_vu_micro.py: PASS bytes={len(code)} instructions={len(code)//8} relocations_in_range=0')

if __name__=='__main__':
    try:main()
    except (OSError,ElfError,ValueError) as e:
        print(f'extract_vu_micro.py: FAIL {e}',file=sys.stderr);sys.exit(1)
