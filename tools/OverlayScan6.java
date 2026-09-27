// OverlayScan6.java -- Ghidra Java GhidraScript for BattleTanx: Global Assault (GlobalRecomp)
//
// Plain Java GhidraScript -- Ghidra compiles it on the fly from Script
// Manager, no extra setup needed. Run from Window > Script Manager with
// the battletanx_ga_usa.z64 CodeBrowser open and focused.
//
// IMPORTANT: keep this as the ONLY .java file in this script directory.
// Ghidra compiles an entire script directory together as one build unit,
// so a stray old/broken .java file sitting alongside this one will block
// this one from running too, even though they're unrelated files. If you
// see a "cannot find symbol" error mentioning some OTHER file name,
// that's why -- delete every other .java file from this folder first.
//
// Context: this project has spent a long time hunting for how BattleTanx:
// Global Assault loads its "overlay" code/data blob (confirmed to live
// around VRAM 0x800F8000-0x80112000) from ROM into RAM at runtime. Earlier
// scripts (OverlayScan.java through OverlayScan5.java) exhaustively ruled
// out: a direct `jal`/`j` call to the loader-shaped functions, the loader
// address stored anywhere as a raw data pointer, and an indirect call via
// a 2-instruction register load -- all searched across the ENTIRE ROM,
// including bytes Ghidra hasn't disassembled as code yet. Separately, this
// project already confirmed the four real N64 hardware DMA-trigger
// registers (PI_DRAM_ADDR, PI_CART_ADDR, PI_RD_LEN, PI_WR_LEN) have ZERO
// references anywhere in this ROM -- so whatever copies the overlay bytes
// in is NOT using the hardware PI DMA engine at all.
//
// The standing hypothesis: the overlay copy is a plain SOFTWARE loop that
// reads directly from the memory-mapped cartridge address space
// (KSEG1, 0xB0000000-0xB0FFFFFF covers a 16MB cart window, comfortably
// larger than this ~8MB ROM) rather than triggering real DMA hardware.
// A much earlier manual "Search for Scalars" pass only checked for the
// upper half-word being the single exact value 0xB000 (i.e. only the
// FIRST 64KB of cart space) and found just 6 hits, all unrelated false
// positives. That was far too narrow: a source address anywhere else in
// the 8MB ROM would have a different upper half-word (0xB001, 0xB002, ...
// up to roughly 0xB080 for an 8MB ROM) and would have been invisible to
// that search entirely.
//
// This script generalizes that into a real range check across the whole
// cart window:
//
//   9. Scans for `lui` immediately followed (within a few instructions)
//      by an `ori`/`addiu` using the same register, reconstructs the full
//      32-bit address, and reports every one landing in
//      0xB0000000-0xB0800000 (the whole ~8MB cart window, with margin).
//      This is section [1]'s technique from earlier scripts, generalized
//      from a narrow destination-range check to this much wider
//      source-range check.
//
//  10. Scans every 4-byte-aligned word in every initialized memory block
//      for a raw value in that same range -- i.e. looks for a cart
//      address sitting in a DATA TABLE (as opposed to being constructed
//      fresh in code). If the overlay system uses a table of
//      {rom_source, dest, size}-shaped descriptors, the rom_source field
//      would show up here.
//
// Everything prints to the Script Manager console (Window > Console in
// the main CodeBrowser tool, NOT the Script Manager's own "Running/
// Finished" popup). Please copy/paste the full console output back.

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.listing.Program;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryAccessException;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.scalar.Scalar;

public class OverlayScan6 extends GhidraScript {

    // Whole KSEG1 cart window, with margin beyond an 8MB ROM.
    private static final long CART_LO = 0xB0000000L;
    private static final long CART_HI = 0xB0800000L;
    private static final int MAX_HITS = 500;

    private Listing listing;
    private Memory memory;

    @Override
    protected void run() throws Exception {
        Program program = currentProgram;
        listing = program.getListing();
        memory = program.getMemory();

        scanCartAddressPairs();
        scanCartDataWords();

        println("");
        println("=== done ===");
    }

    private String regName(Instruction instr, int idx) {
        try {
            Register r = instr.getRegister(idx);
            return r == null ? null : r.getName();
        } catch (Exception e) {
            return null;
        }
    }

    private Scalar getScalarOp(Instruction instr, int idx) {
        try {
            return instr.getScalar(idx);
        } catch (Exception e) {
            return null;
        }
    }

    private void scanCartAddressPairs() throws Exception {
        println(String.format("=== [9] lui+ori/addiu pairs building an address anywhere in the KSEG1 cart window 0x%08X-0x%08X ===", CART_LO, CART_HI));
        int pairHits = 0;

        Instruction instr = listing.getInstructionAt(currentProgram.getMinAddress());
        if (instr == null) {
            instr = listing.getInstructionAfter(currentProgram.getMinAddress());
        }

        while (instr != null) {
            if (monitor.isCancelled()) break;
            String mnem = instr.getMnemonicString();
            if (mnem.equals("lui")) {
                String destReg = regName(instr, 0);
                Scalar upperScalar = getScalarOp(instr, 1);
                if (destReg != null && upperScalar != null) {
                    long upperVal = upperScalar.getUnsignedValue() & 0xFFFFL;
                    Instruction look = instr;
                    for (int i = 0; i < 4; i++) {
                        Instruction nxt = listing.getInstructionAfter(look.getAddress());
                        if (nxt == null) break;
                        look = nxt;
                        String lm = look.getMnemonicString();
                        if (lm.equals("ori") || lm.equals("addiu")) {
                            String srcReg = regName(look, 1);
                            if (destReg.equals(srcReg)) {
                                Scalar loScalar = getScalarOp(look, 2);
                                if (loScalar != null) {
                                    long loVal = loScalar.getValue() & 0xFFFFL;
                                    long full = ((upperVal << 16) | loVal) & 0xFFFFFFFFL;
                                    if (full >= CART_LO && full < CART_HI) {
                                        println(String.format("  PAIR: lui@0x%08X + %s@0x%08X (reg %s) -> 0x%08X (ROM offset ~0x%X)",
                                                instr.getAddress().getOffset(), lm, look.getAddress().getOffset(), destReg, full, full - CART_LO));
                                        pairHits++;
                                    }
                                }
                            }
                            break;
                        }
                        String clobber = regName(look, 0);
                        if (destReg.equals(clobber)) break;
                    }
                }
            }
            instr = listing.getInstructionAfter(instr.getAddress());
        }
        println("  total pair hits: " + pairHits);
        println("");
    }

    private void scanCartDataWords() throws Exception {
        println(String.format("=== [10] raw 4-byte-aligned words anywhere in memory equal to a value in the KSEG1 cart window 0x%08X-0x%08X ===", CART_LO, CART_HI));
        int wordHits = 0;
        boolean truncated = false;

        for (MemoryBlock block : memory.getBlocks()) {
            if (monitor.isCancelled()) break;
            if (!block.isInitialized()) continue;
            Address start = block.getStart();
            Address end = block.getEnd();
            long off = start.getOffset();
            Address addr = (off % 4 == 0) ? start : start.add(4 - (off % 4));
            while (addr.getOffset() + 3 <= end.getOffset()) {
                try {
                    long val = memory.getInt(addr) & 0xFFFFFFFFL;
                    if (val >= CART_LO && val < CART_HI) {
                        if (wordHits < MAX_HITS) {
                            println(String.format("  WORD @ 0x%08X = 0x%08X (ROM offset ~0x%X)", addr.getOffset(), val, val - CART_LO));
                        } else {
                            truncated = true;
                        }
                        wordHits++;
                    }
                } catch (MemoryAccessException e) {
                    // ignore unreadable spots
                }
                addr = addr.add(4);
            }
        }
        if (truncated) {
            println("  ... truncated, " + wordHits + " total hits found (raise MAX_HITS to see more)");
        } else {
            println("  total word hits: " + wordHits);
        }
        println("");
    }
}
