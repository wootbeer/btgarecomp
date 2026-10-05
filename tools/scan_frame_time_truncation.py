# Lists every place the recompiled game code loads one of the frame-time
# globals (dt at 0x803A5948, the governor step at 0x80219488) and converts a
# value derived from it to an integer. A per-frame result below 1 at 30 fps
# is a bug here even if hardware (slower frames) gave >= 1 -- see STATUS.md
# rounds 125 and 127. Run from the repo root after N64Recomp:
#   python3 tools/scan_frame_time_truncation.py
import re, glob, collections
# Scan the recompiled sources' disassembly comments for loads of the two
# frame-time globals followed (within a window, same function) by a float->int
# conversion. Report function, load address, ops in between.
ins_re = re.compile(r'//\s+(0x[0-9A-F]{8}):\s+(\S+)\s*(.*)')
func_re = re.compile(r'^RECOMP_FUNC void (func_[0-9A-F]+|\w+)\(')
results = []
for path in sorted(glob.glob('RecompiledFuncs/funcs_*.c')):
    func = None; insns = []; seen=set()
    def flush():
        if not func: return
        n=len(insns)
        for i,(a,op,args) in enumerate(insns):
            if op!='lwc1': continue
            m=re.match(r'\$(f\d+), (-?0x[0-9A-F]+)\(\$at\)', args)
            if not m: continue
            off=m.group(2)
            # find preceding lui $at
            hi=None
            for j in range(i-1,max(i-6,-1),-1):
                if insns[j][1]=='lui' and insns[j][2].startswith('$at,'):
                    hi=int(insns[j][2].split(',')[1],16); break
            if hi is None: continue
            addr=((hi<<16)+int(off,16))&0xFFFFFFFF
            if addr not in (0x803A5948,0x80219488): continue
            window=insns[i:i+28]
            conv=[w for w in window if w[1] in ('trunc.w.s','cvt.w.s','floor.w.s','round.w.s','ceil.w.s','trunc.l.s')]
            if not conv: continue
            muls=[w for w in window[:window.index(conv[0])] if w[1] in ('mul.s','div.s','add.s','sub.s','cvt.s.w')]
            key=(func,addr,conv[0][0])
            if key in seen: continue
            seen.add(key)
            results.append((func,a,'dt' if addr==0x803A5948 else 'step',conv[0][1],[f"{w[1]} {w[2]}" for w in muls]))
    for line in open(path):
        m=func_re.match(line)
        if m:
            flush(); func=m.group(1); insns=[]; continue
        m=ins_re.search(line)
        if m:
            a,op,args=m.groups()
            if insns and insns[-1][0]==a: continue  # skip duplicated delay-slot comments
            insns.append((a,op,args))
    flush()
for r in results: print(r[0], r[1], r[2], r[3], ' | '.join(r[4]))
print(len(results), 'sites')
