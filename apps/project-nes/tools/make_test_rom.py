#!/usr/bin/env python3
"""Build the original Project Nes diagnostic NROM used by PROJECTNES.pkg."""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "data" / "roms" / "ProjectNesTest.nes"

prg = bytearray([0xEA] * 16384)
base = 0xC000
code = bytearray()
labels = {}
fix_rel = []
fix_abs = []

def label(name): labels[name] = base + len(code)
def emit(*values): code.extend(values)
def imm(op, value): emit(op, value & 0xFF)
def absop(op, address): emit(op, address & 0xFF, (address >> 8) & 0xFF)
def rel(op, name): emit(op, 0); fix_rel.append((len(code) - 1, name))
def jump(op, name): emit(op, 0, 0); fix_abs.append((len(code) - 2, name))

label("reset")
emit(0x78, 0xD8)  # SEI, CLD
imm(0xA2, 0x40); absop(0x8E, 0x4017)
imm(0xA2, 0xFF); emit(0x9A, 0xE8)
absop(0x8E, 0x2000); absop(0x8E, 0x2001); absop(0x8E, 0x4010)
label("wait1"); absop(0x2C, 0x2002); rel(0x10, "wait1")
label("wait2"); absop(0x2C, 0x2002); rel(0x10, "wait2")

# Palette upload.
imm(0xA9, 0x3F); absop(0x8D, 0x2006)
imm(0xA9, 0x00); absop(0x8D, 0x2006)
imm(0xA2, 0x00)
label("pal_loop")
emit(0xBD, 0, 0); fix_abs.append((len(code) - 2, "palette"))
absop(0x8D, 0x2007); emit(0xE8); imm(0xE0, 0x20); rel(0xD0, "pal_loop")

# Clear the first nametable (1024 bytes).
imm(0xA9, 0x20); absop(0x8D, 0x2006)
imm(0xA9, 0x00); absop(0x8D, 0x2006)
imm(0xA9, 0x00); imm(0xA2, 0x00); imm(0xA0, 0x04)
label("clear_loop")
absop(0x8D, 0x2007); emit(0xE8); rel(0xD0, "clear_loop"); emit(0x88); rel(0xD0, "clear_loop")

# "PROJECT NES".
imm(0xA9, 0x21); absop(0x8D, 0x2006)
imm(0xA9, 0xCA); absop(0x8D, 0x2006)
imm(0xA2, 0x00)
label("msg_loop")
emit(0xBD, 0, 0); fix_abs.append((len(code) - 2, "message"))
absop(0x8D, 0x2007); emit(0xE8); imm(0xE0, 11); rel(0xD0, "msg_loop")

# "READY NOW".
imm(0xA9, 0x22); absop(0x8D, 0x2006)
imm(0xA9, 0x0B); absop(0x8D, 0x2006)
imm(0xA2, 0x00)
label("sub_loop")
emit(0xBD, 0, 0); fix_abs.append((len(code) - 2, "subtitle"))
absop(0x8D, 0x2007); emit(0xE8); imm(0xE0, 9); rel(0xD0, "sub_loop")

# Enable NMI and background rendering.
imm(0xA9, 0x80); absop(0x8D, 0x2000)
imm(0xA9, 0x0A); absop(0x8D, 0x2001)

# Pulse 1 test tone. Project Nes approximates APU pulse/triangle output in v0.1.
imm(0xA9, 0x01); absop(0x8D, 0x4015)
imm(0xA9, 0x9F); absop(0x8D, 0x4000)
imm(0xA9, 0xFD); absop(0x8D, 0x4002)
imm(0xA9, 0x08); absop(0x8D, 0x4003)

label("main"); jump(0x4C, "main")

while len(code) < 0x100:
    emit(0xEA)

label("nmi")
emit(0x48, 0x8A, 0x48)  # PHA, TXA, PHA
emit(0xE6, 0x00)        # INC $00
emit(0xA5, 0x00); imm(0x29, 0x03); emit(0xAA)
imm(0xA9, 0x3F); absop(0x8D, 0x2006)
imm(0xA9, 0x01); absop(0x8D, 0x2006)
emit(0xBD, 0, 0); fix_abs.append((len(code) - 2, "cycle_colors"))
absop(0x8D, 0x2007)
emit(0x68, 0xAA, 0x68, 0x40)  # PLA, TAX, PLA, RTI

label("irq"); emit(0x40)
label("palette")
emit(*([
    0x0F,0x30,0x16,0x27, 0x0F,0x30,0x16,0x27,
    0x0F,0x30,0x16,0x27, 0x0F,0x30,0x16,0x27,
    0x0F,0x30,0x16,0x27, 0x0F,0x30,0x16,0x27,
    0x0F,0x30,0x16,0x27, 0x0F,0x30,0x16,0x27,
]))
label("cycle_colors"); emit(0x16, 0x27, 0x30, 0x21)
label("message"); emit(1,2,3,4,5,6,7,0,8,5,9)
label("subtitle"); emit(2,5,10,11,12,0,8,3,13)

for pos, name in fix_abs:
    address = labels[name]
    code[pos] = address & 0xFF
    code[pos + 1] = (address >> 8) & 0xFF
for pos, name in fix_rel:
    target = labels[name]
    next_pc = base + pos + 1
    delta = target - next_pc
    if not -128 <= delta <= 127:
        raise ValueError(f"branch out of range: {name} {delta}")
    code[pos] = delta & 0xFF

prg[:len(code)] = code
for offset, name in ((0x3FFA, "nmi"), (0x3FFC, "reset"), (0x3FFE, "irq")):
    address = labels[name]
    prg[offset] = address & 0xFF
    prg[offset + 1] = (address >> 8) & 0xFF

# Small 5x7 font stored in CHR-ROM.
chrrom = bytearray(8192)
font = {
    "P":[0b11110,0b10001,0b10001,0b11110,0b10000,0b10000,0b10000],
    "R":[0b11110,0b10001,0b10001,0b11110,0b10100,0b10010,0b10001],
    "O":[0b01110,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110],
    "J":[0b00111,0b00010,0b00010,0b00010,0b10010,0b10010,0b01100],
    "E":[0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b11111],
    "C":[0b01111,0b10000,0b10000,0b10000,0b10000,0b10000,0b01111],
    "T":[0b11111,0b00100,0b00100,0b00100,0b00100,0b00100,0b00100],
    "N":[0b10001,0b11001,0b10101,0b10011,0b10001,0b10001,0b10001],
    "S":[0b01111,0b10000,0b10000,0b01110,0b00001,0b00001,0b11110],
    "A":[0b01110,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001],
    "D":[0b11110,0b10001,0b10001,0b10001,0b10001,0b10001,0b11110],
    "Y":[0b10001,0b10001,0b01010,0b00100,0b00100,0b00100,0b00100],
    "W":[0b10001,0b10001,0b10001,0b10101,0b10101,0b10101,0b01010],
}
for tile_id, ch in enumerate(["P","R","O","J","E","C","T","N","S","A","D","Y","W"], 1):
    for y, row in enumerate(font[ch]):
        chrrom[tile_id * 16 + y] = (row << 2) & 0xFF

header = bytearray(b"NES\x1a") + bytes([1, 1, 0, 0]) + bytes(8)
OUT.parent.mkdir(parents=True, exist_ok=True)
OUT.write_bytes(header + prg + chrrom)
print(f"created {OUT} ({OUT.stat().st_size} bytes)")
