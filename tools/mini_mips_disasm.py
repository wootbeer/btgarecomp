#!/usr/bin/env python3
"""
Minimal standalone MIPS (R4300i/N64) disassembler.

Built because PyPI (and therefore capstone/keystone/etc.) is blocked from
this sandbox, but we have the raw normalized ROM staged locally and need to
manually inspect a handful of specific functions the automated splat runs
never reached. Covers the common integer/float subset IDO-compiled N64
code actually uses -- not a complete MIPS III implementation, but enough to
read real code and spot library-call patterns (stack args, DMA-shaped
calls, etc).

Usage: mini_mips_disasm.py <rom.z64> <rom_offset_hex> <vram_hex> <num_words>
"""
import sys

GPR = ["zero","at","v0","v1","a0","a1","a2","a3",
       "t0","t1","t2","t3","t4","t5","t6","t7",
       "s0","s1","s2","s3","s4","s5","s6","s7",
       "t8","t9","k0","k1","gp","sp","fp","ra"]
FPR = [f"f{i}" for i in range(32)]

def se16(v):
    return v - 0x10000 if v & 0x8000 else v

def hx(v):
    return f"0x{v:X}" if v >= 0 else f"-0x{-v:X}"

def disasm_one(word, addr):
    op = (word >> 26) & 0x3F
    rs = (word >> 21) & 0x1F
    rt = (word >> 16) & 0x1F
    rd = (word >> 11) & 0x1F
    sa = (word >> 6) & 0x1F
    funct = word & 0x3F
    imm = word & 0xFFFF
    simm = se16(imm)
    target = word & 0x3FFFFFF

    def r(i): return "$" + GPR[i]
    def f(i): return "$" + FPR[i]

    if word == 0:
        return "nop"

    if op == 0:  # SPECIAL
        if funct == 0x00: return f"sll        {r(rd)}, {r(rt)}, {sa}" if word else "nop"
        if funct == 0x02: return f"srl        {r(rd)}, {r(rt)}, {sa}"
        if funct == 0x03: return f"sra        {r(rd)}, {r(rt)}, {sa}"
        if funct == 0x04: return f"sllv       {r(rd)}, {r(rt)}, {r(rs)}"
        if funct == 0x06: return f"srlv       {r(rd)}, {r(rt)}, {r(rs)}"
        if funct == 0x07: return f"srav       {r(rd)}, {r(rt)}, {r(rs)}"
        if funct == 0x08: return f"jr         {r(rs)}"
        if funct == 0x09: return f"jalr       {r(rd)}, {r(rs)}" if rd != 31 else f"jalr       {r(rs)}"
        if funct == 0x0C: return "syscall"
        if funct == 0x0D: return "break"
        if funct == 0x10: return f"mfhi       {r(rd)}"
        if funct == 0x11: return f"mthi       {r(rs)}"
        if funct == 0x12: return f"mflo       {r(rd)}"
        if funct == 0x13: return f"mtlo       {r(rs)}"
        if funct == 0x14: return f"dsllv      {r(rd)}, {r(rt)}, {r(rs)}"
        if funct == 0x18: return f"mult       {r(rs)}, {r(rt)}"
        if funct == 0x19: return f"multu      {r(rs)}, {r(rt)}"
        if funct == 0x1A: return f"div        {r(rs)}, {r(rt)}"
        if funct == 0x1B: return f"divu       {r(rs)}, {r(rt)}"
        if funct == 0x20: return f"add        {r(rd)}, {r(rs)}, {r(rt)}"
        if funct == 0x21: return f"addu       {r(rd)}, {r(rs)}, {r(rt)}"
        if funct == 0x22: return f"sub        {r(rd)}, {r(rs)}, {r(rt)}"
        if funct == 0x23: return f"subu       {r(rd)}, {r(rs)}, {r(rt)}"
        if funct == 0x24: return f"and        {r(rd)}, {r(rs)}, {r(rt)}"
        if funct == 0x25:
            if rt == 0: return f"or         {r(rd)}, {r(rs)}, $zero  # move"
            return f"or         {r(rd)}, {r(rs)}, {r(rt)}"
        if funct == 0x26: return f"xor        {r(rd)}, {r(rs)}, {r(rt)}"
        if funct == 0x27: return f"nor        {r(rd)}, {r(rs)}, {r(rt)}"
        if funct == 0x2A: return f"slt        {r(rd)}, {r(rs)}, {r(rt)}"
        if funct == 0x2B: return f"sltu       {r(rd)}, {r(rs)}, {r(rt)}"
        if funct == 0x0F: return "sync"
        return f".word 0x{word:08X} /* SPECIAL funct=0x{funct:02X} */"

    if op == 0x01:  # REGIMM
        names = {0x00:"bltz",0x01:"bgez",0x02:"bltzl",0x03:"bgezl",
                 0x10:"bltzal",0x11:"bgezal"}
        nm = names.get(rt)
        tgt = addr + 4 + (simm << 2)
        if nm: return f"{nm:10} {r(rs)}, 0x{tgt:08X}"
        return f".word 0x{word:08X} /* REGIMM rt=0x{rt:02X} */"

    if op == 0x02:
        tgt = (addr & 0xF0000000) | (target << 2)
        return f"j          0x{tgt:08X}"
    if op == 0x03:
        tgt = (addr & 0xF0000000) | (target << 2)
        return f"jal        0x{tgt:08X}"

    if op == 0x04:
        tgt = addr + 4 + (simm << 2)
        if rs == rt: return f"b          0x{tgt:08X}"
        return f"beq        {r(rs)}, {r(rt)}, 0x{tgt:08X}"
    if op == 0x05:
        tgt = addr + 4 + (simm << 2)
        if rt == 0: return f"bnez       {r(rs)}, 0x{tgt:08X}"
        return f"bne        {r(rs)}, {r(rt)}, 0x{tgt:08X}"
    if op == 0x06:
        tgt = addr + 4 + (simm << 2)
        return f"blez       {r(rs)}, 0x{tgt:08X}"
    if op == 0x07:
        tgt = addr + 4 + (simm << 2)
        return f"bgtz       {r(rs)}, 0x{tgt:08X}"

    if op == 0x08: return f"addi       {r(rt)}, {r(rs)}, {hx(simm)}"
    if op == 0x09:
        if rs == 0: return f"li         {r(rt)}, {hx(simm)}"
        return f"addiu      {r(rt)}, {r(rs)}, {hx(simm)}"
    if op == 0x0A: return f"slti       {r(rt)}, {r(rs)}, {hx(simm)}"
    if op == 0x0B: return f"sltiu      {r(rt)}, {r(rs)}, {hx(simm)}"
    if op == 0x0C: return f"andi       {r(rt)}, {r(rs)}, 0x{imm:X}"
    if op == 0x0D: return f"ori        {r(rt)}, {r(rs)}, 0x{imm:X}"
    if op == 0x0E: return f"xori       {r(rt)}, {r(rs)}, 0x{imm:X}"
    if op == 0x0F: return f"lui        {r(rt)}, 0x{imm:X}"

    if op == 0x10:  # COP0
        fmt = rs
        if fmt == 0x00: return f"mfc0       {r(rt)}, $cop0[{rd}]"
        if fmt == 0x04: return f"mtc0       {r(rt)}, $cop0[{rd}]"
        return f".word 0x{word:08X} /* COP0 */"
    if op == 0x11:  # COP1 (FPU)
        fmt = rs
        if fmt == 0x00: return f"mfc1       {r(rt)}, {f(rd)}"
        if fmt == 0x02: return f"cfc1       {r(rt)}, $fcr{rd}"
        if fmt == 0x04: return f"mtc1       {r(rt)}, {f(rd)}"
        if fmt == 0x06: return f"ctc1       {r(rt)}, $fcr{rd}"
        if fmt == 0x08:
            tgt = addr + 4 + (simm << 2)
            names={0:"bc1f",1:"bc1t",2:"bc1fl",3:"bc1tl"}
            return f"{names.get(rt,'bc1?'):10} 0x{tgt:08X}"
        # fmt 16=S(single) 17=D(double) 20=W(int) 21=L
        fmtname = {16:"s",17:"d",20:"w",21:"l"}.get(fmt, f"fmt{fmt}")
        fd = rd; fs = (word>>11)&0x1F; ft=(word>>16)&0x1F
        # standard layout: fmt(5) ft(5) fs(5) fd(5) funct(6)
        ft2=(word>>16)&0x1F; fs2=(word>>11)&0x1F; fd2=(word>>6)&0x1F
        fc = funct
        fnames = {0x00:"add",0x01:"sub",0x02:"mul",0x03:"div",0x04:"sqrt",
                  0x05:"abs",0x06:"mov",0x07:"neg",
                  0x20:"cvt.s",0x21:"cvt.d",0x24:"cvt.w",0x25:"cvt.l",
                  0x30:"c.f",0x31:"c.un",0x32:"c.eq",0x33:"c.ueq",
                  0x3C:"c.lt",0x3E:"c.le"}
        nm = fnames.get(fc)
        if nm:
            if nm.startswith("c."):
                return f"{nm}.{fmtname:<6} {f(fs2)}, {f(ft2)}"
            if fc in (0x05,0x06,0x07,0x04) or nm.startswith("cvt"):
                return f"{nm}.{fmtname:<6} {f(fd2)}, {f(fs2)}"
            return f"{nm}.{fmtname:<6} {f(fd2)}, {f(fs2)}, {f(ft2)}"
        return f".word 0x{word:08X} /* COP1 fmt={fmt} funct=0x{funct:02X} */"

    if op == 0x14: return f"beql       {r(rs)}, {r(rt)}, imm=0x{imm:X}"
    if op == 0x15: return f"bnel       {r(rs)}, {r(rt)}, imm=0x{imm:X}"

    if op == 0x20: return f"lb         {r(rt)}, {hx(simm)}({r(rs)})"
    if op == 0x21: return f"lh         {r(rt)}, {hx(simm)}({r(rs)})"
    if op == 0x22: return f"lwl        {r(rt)}, {hx(simm)}({r(rs)})"
    if op == 0x23: return f"lw         {r(rt)}, {hx(simm)}({r(rs)})"
    if op == 0x24: return f"lbu        {r(rt)}, {hx(simm)}({r(rs)})"
    if op == 0x25: return f"lhu        {r(rt)}, {hx(simm)}({r(rs)})"
    if op == 0x26: return f"lwr        {r(rt)}, {hx(simm)}({r(rs)})"
    if op == 0x28: return f"sb         {r(rt)}, {hx(simm)}({r(rs)})"
    if op == 0x29: return f"sh         {r(rt)}, {hx(simm)}({r(rs)})"
    if op == 0x2A: return f"swl        {r(rt)}, {hx(simm)}({r(rs)})"
    if op == 0x2B: return f"sw         {r(rt)}, {hx(simm)}({r(rs)})"
    if op == 0x2E: return f"swr        {r(rt)}, {hx(simm)}({r(rs)})"
    if op == 0x2F: return f"cache      0x{rt:X}, {hx(simm)}({r(rs)})"
    if op == 0x31: return f"lwc1       {f(rt)}, {hx(simm)}({r(rs)})"
    if op == 0x39: return f"swc1       {f(rt)}, {hx(simm)}({r(rs)})"
    if op == 0x30: return f"ll         {r(rt)}, {hx(simm)}({r(rs)})"
    if op == 0x38: return f"sc         {r(rt)}, {hx(simm)}({r(rs)})"

    return f".word 0x{word:08X} /* op=0x{op:02X} */"

def main():
    romfile, rom_off_hex, vram_hex, n = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
    rom_off = int(rom_off_hex, 16)
    vram = int(vram_hex, 16)
    with open(romfile, "rb") as fh:
        fh.seek(rom_off)
        data = fh.read(n * 4)
    for i in range(0, len(data), 4):
        word = int.from_bytes(data[i:i+4], "big")
        addr = vram + i
        romaddr = rom_off + i
        try:
            txt = disasm_one(word, addr)
        except Exception as e:
            txt = f".word 0x{word:08X} /* decode error: {e} */"
        print(f"/* {romaddr:06X} {addr:08X} {word:08X} */  {txt}")

if __name__ == "__main__":
    main()
