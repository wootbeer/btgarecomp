# Round 78 (STATUS.md): batch-split merged-function boundaries in the syms toml.
# Run from the repo root against freshly generated RecompiledFuncs/ (jump-table
# targets are read from it). Rewrites the syms toml and re-points displaced
# [[patches.hook]]/[[patches.instruction]] entries; regenerate afterwards.
# Usage: python3 tools/batch_split_merged_funcs.py <summary.json>
import re, struct, glob, capstone, sys, json
SYMS='BattleTanxGASyms/battletanxga.us.rev0.syms.toml'
TOML='battletanxga.us.rev0.toml'
rom=open('BattleTanx Global Assault (USA).z64','rb').read()
md=capstone.Cs(capstone.CS_ARCH_MIPS, capstone.CS_MODE_MIPS32+capstone.CS_MODE_BIG_ENDIAN)
syms=open(SYMS).read(); toml=open(TOML).read()

# ROM references: literal words and lui/addiu|ori constants
words=set(struct.unpack('>%dI'%(len(rom)//4),rom[:len(rom)//4*4]))
consts=set()
for off in range(0,len(rom)-32,4):
    w=struct.unpack('>I',rom[off:off+4])[0]
    if w>>26==0x0F:
        rt=(w>>16)&31; hi=w&0xFFFF
        for k in range(1,8):
            w2=struct.unpack('>I',rom[off+4*k:off+4*k+4])[0]
            if w2>>26 in (0x09,0x0D) and ((w2>>21)&31)==rt:
                lo=w2&0xFFFF
                consts.add(((hi<<16)+(lo if w2>>26==0x0D else (lo-0x10000 if lo&0x8000 else lo)))&0xFFFFFFFF); break
referenced=words|consts

# Resolved jump tables from current generated code: jr address -> case targets
jt={}
for f in glob.glob('RecompiledFuncs/funcs_*.c'):
    sw=None
    for line in open(f):
        m=re.search(r'switch \(jr_addend_([0-9A-F]{8})',line)
        if m: sw=int(m.group(1),16); jt.setdefault(sw,set()); continue
        m=re.search(r'case \d+: goto L_([0-9A-F]{8})',line)
        if m and sw is not None: jt[sw].add(int(m.group(1),16))

excluded=set(re.findall(r'"([^"]+)"', re.search(r'^stubs = \[(.*?)\]',toml,re.M).group(1)))
excluded|=set(re.findall(r'"([^"]+)"', re.search(r'^ignored = \[(.*?)\]',toml,re.M).group(1)))

line_re=re.compile(r'^(\s*)\{ name = "(func_[0-9A-F]{8})", vram = (0x[0-9a-fA-F]+), size = (0x[0-9a-fA-F]+) \},\s*$')
plan=[]; skipped_jt=[]
for line in syms.splitlines():
    m=line_re.match(line)
    if not m: continue
    ind,name,v,s=m.group(1),m.group(2),int(m.group(3),16),int(m.group(4),16)
    if name in excluded: continue
    off=v-0x80070000
    insns=list(md.disasm(rom[off:off+s],v))
    if len(insns)!=s//4: continue
    bounds=[i.address+8 for i in insns if i.mnemonic=='jr' and i.op_str=='$ra' and i.address+8<v+s]
    if not bounds: continue
    bad=set()
    for i in insns:
        mn=i.mnemonic
        if mn in('j','b') or (mn.startswith('b') and mn!='break'):
            try: t=int(i.op_str.split(', ')[-1],16)
            except: continue
            lo,hi=sorted((i.address,t))
            bad|={b for b in bounds if lo<b<=hi}
    unresolved=False
    for i in insns:
        if i.mnemonic=='jr' and i.op_str!='$ra':
            if i.address not in jt: unresolved=True; break
            grp=jt[i.address]|{i.address}
            lo,hi=min(grp),max(grp)
            bad|={b for b in bounds if lo<b<=hi}
    if unresolved: skipped_jt.append(name); continue
    cand=[b for b in bounds if b not in bad and b in referenced]
    # drop pure-padding pieces: a split whose piece up to the next kept split is all zero
    keep=[]
    pts=sorted(cand)
    for idx,b in enumerate(pts):
        end=pts[idx+1] if idx+1<len(pts) else v+s
        if all(x==0 for x in rom[b-0x80070000:end-0x80070000]): continue
        keep.append(b)
    if not keep: continue
    p=[v]+keep+[v+s]
    pieces=[(a,b-a) for a,b in zip(p,p[1:])]
    plan.append((line,ind,name,v,s,pieces))

# apply syms edits
newsyms=syms
for line,ind,name,v,s,pieces in plan:
    rep=[ind+'# Batch split (round 78, STATUS.md): clean jr-$ra boundaries whose start is ROM-referenced.']
    for a,sz in pieces:
        rep.append(f'{ind}{{ name = "func_{a:08X}", vram = {a:#x}, size = {sz:#x} }},')
    assert newsyms.count(line+'\n')==1, name
    newsyms=newsyms.replace(line+'\n','\n'.join(rep)+'\n')
open(SYMS,'w').write(newsyms)

# re-point hooks / instruction patches displaced into a new piece
owner={}
for line,ind,name,v,s,pieces in plan:
    owner[name]=pieces
moved=[]
def repoint(mo):
    kind,fn,addr=mo.group(1),mo.group(2),int(mo.group(4),16)
    if fn in owner:
        for a,sz in owner[fn]:
            if a<=addr<a+sz:
                nf=f'func_{a:08X}'
                if nf!=fn: moved.append((kind,fn,nf,hex(addr)))
                return mo.group(0).replace(f'func = "{fn}"',f'func = "{nf}"')
    return mo.group(0)
newtoml=re.sub(r'(\[\[patches\.(?:hook|instruction)\]\])\s*\nfunc = "([^"]+)"\s*\n(before_vram|vram) = (0x[0-9a-fA-F]+)',repoint,toml)
open(TOML,'w').write(newtoml)
summary={'entries_split':len(plan),'new_functions':sum(len(p[5])-1 for p in plan),'skipped_unresolved_jtable':skipped_jt,'repointed':moved}
json.dump(summary,open(sys.argv[1],'w'),indent=1)
print(json.dumps({k:(v if k!='skipped_unresolved_jtable' else len(v)) for k,v in summary.items()},indent=1))
