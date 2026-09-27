# Known behavioral patches

Instruction-level patches this port is expected to need, discovered from
research rather than from running the recompiled game yet. N64Recomp's TOML
config supports patching specific instructions at known addresses -- that's
the intended home for these once we have real addresses in *our* ROM.

## USA boot-timing race (`osContInit` vs. VI timer list)

Both the original *BattleTanx* (N64/PS1) and *BattleTanx: Global Assault*
USA ROMs have a documented boot-time race condition: the game calls
`osContInit` before ~500ms have elapsed at boot, before libultra's
VI-manager timer list has finished initializing. The resulting wait call
follows a null pointer in the timer chain and faults at address `0x10`.

- Confirmed independently by two emulator projects that had to work around
  it: [n64js PR #123](https://github.com/hulkholden/n64js/pull/123) patches
  a single conditional branch, right after the IPL3 checksum check, to skip
  the faulty timer-based wait. [mupen64plus-core issue #283](https://github.com/mupen64plus/mupen64plus-core/issues/283)
  documents the user-visible symptom (game "restarts" if you try to skip the
  intro) without root-causing it -- the n64js PR has the actual mechanism.
- The EUR version of Global Assault initializes timers in a different order
  and does **not** need this workaround, which is good corroborating
  evidence this is a genuine bug in the USA build's boot code, not a general
  emulation inaccuracy.
- Why this matters for a *static recompile* specifically: this is a real
  race in the original game code, not an emulation quirk, so it should be
  expected to reproduce (or get worse -- native execution timing will be
  very different from either real N64 hardware or an emulator's timing
  model) once boot code actually runs on real hardware speed. Plan to find
  the equivalent branch in our ELF/symbol map early and patch it the same
  way the emulators did, rather than debugging a boot hang/crash from
  scratch.

Status: not yet located in our own symbol map -- needs the real disassembly
before this can be turned into an actual TOML patch entry.
