#!/usr/bin/env python3
# Per-frame 3D Engine wait/work medians from a perf script dump.
# Usage: perf script -i <dir>/perf-kernel.data -F tid,time,ip,sym --tid <gpu>,<engine> > s.txt
#        tools/perf/frames.py s.txt <engine_tid>
import sys,re,statistics
eng_tid=int(sys.argv[2]); samples=[]; cur=None
for line in open(sys.argv[1]):
    m=re.match(r'\s*(\d+)\s+([\d.]+):',line)
    if m:
        if cur and cur[0]==eng_tid: samples.append(cur)
        cur=[int(m.group(1)),float(m.group(2)),False]
    elif cur and ('FrameLimiterWait' in line or 'GpuProgressCheck' in line): cur[2]=True
# runs of wait / work, merging gaps
runs=[]; 
for s in samples:
    if runs and runs[-1][0]==s[2]: runs[-1][2]=s[1]
    else: runs.append([s[2],s[1],s[1]])
# keep runs >2ms to drop noise; merge
clean=[]
for r in runs:
    if r[2]-r[1]<0.002 and clean: clean[-1][2]=r[2]; continue
    if clean and clean[-1][0]==r[0]: clean[-1][2]=r[2]
    else: clean.append(r)
waits=[r[2]-r[1] for r in clean if r[0]]; works=[r[2]-r[1] for r in clean if not r[0]]
starts=[r[1] for r in clean if r[0]]
per=[b-a for a,b in zip(starts,starts[1:]) if b-a<0.2]
print(f'frames {len(per)}  period med {1000*statistics.median(per):.1f} ms ({1/statistics.median(per):.1f} fps)  engine wait med {1000*statistics.median(waits):.1f} ms  engine work med {1000*statistics.median(works):.1f} ms')
