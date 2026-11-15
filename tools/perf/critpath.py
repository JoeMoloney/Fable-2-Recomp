#!/usr/bin/env python3
# GPU Commands work split: while the 3D Engine waits (critical path) vs while it works.
# Usage: tools/perf/critpath.py s.txt <gpu_tid> <engine_tid>   (s.txt as for frames.py)
import sys,re,collections,bisect
samples=[]; cur=None
for line in open(sys.argv[1]):
    m=re.match(r'\s*(\d+)\s+([\d.]+):',line)
    if m:
        if cur: samples.append(cur)
        cur=[int(m.group(1)),float(m.group(2)),[]]
    elif cur and line.strip(): cur[2].append(line.strip().split(None,1)[-1])
if cur: samples.append(cur)
def w(s): return any('FrameLimiterWait' in f or 'GpuProgressCheck' in f for f in s[2])
GPU,ENG=int(sys.argv[2]),int(sys.argv[3])
eng=[s for s in samples if s[0]==ENG]; gpu=[s for s in samples if s[0]==GPU]
wakes=[eng[i][1] for i in range(1,len(eng)) if w(eng[i-1]) and not w(eng[i])]
sleeps=[eng[i][1] for i in range(1,len(eng)) if not w(eng[i-1]) and w(eng[i])]
# classify each GPU sample: in engine-wait phase (critical: engine blocked) or engine-work phase
ev=sorted([(t,'wake') for t in wakes]+[(t,'sleep') for t in sleeps])
times=[e[0] for e in ev]
crit=collections.Counter(); over=collections.Counter(); totc=toto=0
def tag(s):
    st=s[2]
    for k in ['IssueDraw_MemexportRead','RefreshGuestOutput','EndSubmission','UploadRanges','UpdateBindings','RequestTextures','IssueCopy','IssueDraw']:
        if any(k in f for f in st): return k
    return 'other'
for s in gpu:
    i=bisect.bisect_right(times,s[1])-1
    if i<0: continue
    if ev[i][1]=='sleep': crit[tag(s)]+=1; totc+=1
    else: over[tag(s)]+=1; toto+=1
print('GPU samples while engine WAITS (critical path):',totc,' while engine WORKS (overlapped):',toto)
for k in set(crit)|set(over): print(f'{k:26} crit {crit[k]:5}  overlapped {over[k]:5}')
