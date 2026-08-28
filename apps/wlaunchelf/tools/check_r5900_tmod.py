#!/usr/bin/env python3
"""Reject Titan App modules containing COP1 instructions unsupported by EE/R5900."""
from pathlib import Path
import struct
import sys

SHT_PROGBITS=1
SHT_SYMTAB=2
SHF_EXECINSTR=0x4
STT_FUNC=2
SHN_UNDEF=0
EF_MIPS_ARCH_MASK=0xF0000000
EF_MIPS_ARCH_3=0x20000000
EF_MIPS_MACH_MASK=0x00FF0000
EF_MIPS_MACH_5900=0x00920000
EF_MIPS_FP64=0x00000200
EF_MIPS_NAN2008=0x00000400


def fail(msg):
    raise SystemExit(f"r5900-isa: FAIL: {msg}")

def u32(buf, off):
    return struct.unpack_from('<I',buf,off)[0]

def cop1_ok(insn):
    rs=(insn>>21)&31
    rt=(insn>>16)&31
    fn=insn&63
    if rs in (0,2,4,6): return True
    if rs==8: return rt<=3
    if rs==20: return fn==32
    if rs!=16: return False
    return fn in {0,1,2,3,4,5,6,7,22,24,25,26,28,29,30,31,36,40,41,48,50,52,54}

def insn_ok(insn):
    op=insn>>26
    if op in (0x35,0x3d): return False # LDC1 / SDC1 absent on R5900
    if op==0x11: return cop1_ok(insn)
    return True

def main(path):
    b=Path(path).read_bytes()
    if len(b)<52 or b[:4]!=b'\x7fELF' or b[4]!=1 or b[5]!=1:
        fail('expected ELF32 little-endian')
    flags=u32(b,36)
    if (flags&EF_MIPS_ARCH_MASK)!=EF_MIPS_ARCH_3 or (flags&EF_MIPS_MACH_MASK)!=EF_MIPS_MACH_5900:
        fail(f'not stamped MIPS3/R5900: flags=0x{flags:08x}')
    if flags&(EF_MIPS_FP64|EF_MIPS_NAN2008):
        fail('FP64/NAN2008 ABI flags are incompatible')
    shoff=u32(b,32); shentsize=struct.unpack_from('<H',b,46)[0]; shnum=struct.unpack_from('<H',b,48)[0]
    if shentsize!=40 or shoff+shentsize*shnum>len(b): fail('bad section table')
    sections=[struct.unpack_from('<IIIIIIIIII',b,shoff+i*40) for i in range(shnum)]
    symtabs=[(i,s) for i,s in enumerate(sections) if s[1]==SHT_SYMTAB]
    if len(symtabs)!=1: fail('expected exactly one symbol table')
    _,sym=symtabs[0]
    if sym[9]!=16 or sym[5]%16 or sym[4]+sym[5]>len(b): fail('bad symbol table')
    symbols=[]
    for off in range(sym[4],sym[4]+sym[5],16):
        name,value,size=struct.unpack_from('<III',b,off)
        info=b[off+12]; other=b[off+13]; shndx=struct.unpack_from('<H',b,off+14)[0]
        symbols.append((name,value,size,info,other,shndx))
    checked=0
    for i,(_,value,size,info,_,shndx) in enumerate(symbols):
        if (info&15)!=STT_FUNC or shndx==SHN_UNDEF or shndx>=len(sections): continue
        sec=sections[shndx]
        if not (sec[2]&SHF_EXECINSTR) or sec[1]!=SHT_PROGBITS or value>=sec[5] or value&3: continue
        end=value+size if size and size<=sec[5]-value else sec[5]
        if not size:
            for _,nv,_,ni,_,ns in symbols:
                if (ni&15)==STT_FUNC and ns==shndx and value<nv<end: end=nv
        end &= ~3
        if sec[4]+end>len(b): fail('function exceeds file')
        for pos in range(value,end,4):
            insn=u32(b,sec[4]+pos)
            if not insn_ok(insn):
                fail(f'unsupported COP1 opcode 0x{insn:08x} in function symbol #{i} +0x{pos-value:x}')
            checked+=1
    if checked==0: fail('no function instructions checked')
    print(f'r5900-isa: OK ({checked} instructions checked)')

if __name__=='__main__':
    if len(sys.argv)!=2: fail('usage: check_r5900_tmod.py MODULE.tmod')
    main(sys.argv[1])
