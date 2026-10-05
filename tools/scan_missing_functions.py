# Finds functions merged into the symbol before them that the game calls
# through a pointer -- these crash at runtime with "Failed to find function at
# 0x..." (STATUS.md round 129). A candidate is an 'addiu $sp, $sp, -N'
# shortly after a 'jr $ra' inside one symbol; it matters only if the address is
# stored as a pointer (a ROM data word or a lui/addiu pair in code). Known false
# positive: 0x8010FFF8 (mid-alInit; its one reference is a coincidence).
# Run from the repo root: python3 tools/scan_missing_functions.py

import re, struct
r=open('BattleTanx Global Assault (USA).z64','rb').read()
def word(v): o=v-0x80071000+0x1000; return struct.unpack('>I',r[o:o+4])[0]
s=open('BattleTanxGASyms/battletanxga.us.rev0.syms.toml').read()
fs=sorted((int(m.group(2),16),int(m.group(3),16),m.group(1)) for m in re.finditer(r'name = "(\w+)", vram = (0x[0-9a-fA-F]+), size = (0x[0-9a-fA-F]+)',s))
starts={v for v,_,_ in fs}
cands=[]
for v,sz,n in fs:
    a=v+4
    while a < v+sz:
        w=word(a)
        if (w>>16)==0x27BD and (w&0x8000):          # addiu sp,sp,-N
            # a jr $ra within the previous 4 words (delay slot + padding/data)
            if any(word(a-4*k)==0x03E00008 for k in range(2,6)) and a not in starts:
                cands.append((n,v,sz,a))
        a+=4
print(len(cands),'candidates')

# Which candidates does the game reference as pointers?
cand_addrs={a for _,_,_,a in cands}
rom_words=set()
data_refs={}
for o in range(0x1000, 0x101000, 4):
    w=struct.unpack('>I',r[o:o+4])[0]
    if w in cand_addrs: data_refs.setdefault(w,[]).append(o-0x1000+0x80071000)
# lui/addiu (or ori) pairs within code: track last lui per register
code_refs={}
for v,sz,n in fs:
    lui={}
    for a in range(v, v+sz, 4):
        w=word(a); op=w>>26
        if op==0x0F: lui[(w>>16)&31]=(w&0xFFFF)<<16
        elif op in (0x09,0x0D):  # addiu / ori
            rs=(w>>21)&31
            if rs in lui:
                imm=w&0xFFFF
                val=(lui[rs]+(imm-0x10000 if (op==0x09 and imm&0x8000) else imm))&0xFFFFFFFF
                if val in cand_addrs: code_refs.setdefault(val,[]).append(a)
print('\nReferenced candidates:')
for n,v,sz,a in cands:
    if a in data_refs or a in code_refs:
        print(f'  {a:08X} inside {n}: data refs {[hex(x) for x in data_refs.get(a,[])][:4]} code refs {[hex(x) for x in code_refs.get(a,[])][:4]}')
