import re
runfile = '/workspaces/extractor_sierra/tmp/run91.txt'
rbt = '/workspaces/extractor_sierra/ScummVM/rbt/91.RBT'
with open(runfile,'rb') as f:
    txt = f.read().decode(errors='ignore')
rec_lines = re.findall(r'record\[[0-9]+\]=[0-9]+ videoSize=[0-9]+ packetSize=[0-9]+', txt)
out = []
for i,line in enumerate(rec_lines[:12]):
    m = re.search(r'record\[([0-9]+)\]=([0-9]+) videoSize=([0-9]+)', line)
    if not m: continue
    idx = int(m.group(1)); pos = int(m.group(2)); vs = int(m.group(3))
    off = pos+vs
    with open(rbt,'rb') as g:
        g.seek(off)
        data = g.read(64)
    hexs = ' '.join(['%02x'%b for b in data[:64]])
    out.append((idx,pos,vs,off,hexs,data))
for idx,pos,vs,off,hexs,data in out:
    print('frame=%d record=%d videoSize=%d offset=%d'%(idx,pos,vs,off))
    print(bytes(data[:64]).hex(' '))
    print('')
