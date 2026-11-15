#!/usr/bin/env python3
"""Wait profile of the running game, no root needed.

Usage: waitprof.py <out_dir> [seconds]

For every game thread that uses CPU it records, over the window:
  * user / kernel CPU time (from /proc/<tid>/stat),
  * on-CPU time and run-queue wait (time ready but not scheduled, /proc/<tid>/schedstat),
  * voluntary / involuntary context switches,
  * a 100 Hz sample of the thread state: R = running or ready, S = sleeping, D = disk/uninterruptible.
A thread at ~1.0 core with state R ~100% is saturated: anything waiting on it is waiting on CPU work.
"""
import os, sys, time, collections

out = sys.argv[1]
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 20.0
pid = int(os.popen("pgrep -x fable_2").read().split()[0])
tck = os.sysconf("SC_CLK_TCK")


def rd(path):
    try:
        with open(path) as f:
            return f.read()
    except OSError:
        return None


def snap():
    res = {}
    for tid in os.listdir(f"/proc/{pid}/task"):
        base = f"/proc/{pid}/task/{tid}"
        stat, comm = rd(base + "/stat"), rd(base + "/comm")
        if not stat or not comm:
            continue
        f = stat[stat.rfind(")") + 2:].split()  # fields after the comm, state first
        sched = (rd(base + "/schedstat") or "0 0 0").split()
        st = rd(base + "/status") or ""
        vol = int(next((l.split()[1] for l in st.splitlines() if l.startswith("voluntary_ctxt")), 0))
        inv = int(next((l.split()[1] for l in st.splitlines() if l.startswith("nonvoluntary_ctxt")), 0))
        res[tid] = dict(comm=comm.strip(), ut=int(f[11]), st=int(f[12]),
                        run=int(sched[0]), wait=int(sched[1]), vol=vol, inv=inv)
    return res


a = snap()
t0 = time.monotonic()
states = collections.defaultdict(collections.Counter)
n = 0
while time.monotonic() - t0 < secs:
    for tid in a:
        s = rd(f"/proc/{pid}/task/{tid}/stat")
        if s:
            states[tid][s[s.rfind(")") + 2]] += 1
    n += 1
    time.sleep(0.01)
el = time.monotonic() - t0
b = snap()
maps_lines = len((rd(f"/proc/{pid}/maps") or "").splitlines())

rows = []
for tid, x in b.items():
    y = a.get(tid)
    if not y:
        continue
    cpu = (x["ut"] - y["ut"] + x["st"] - y["st"]) / tck / el
    if cpu < 0.03:
        continue
    sc = states[tid]
    tot = sum(sc.values()) or 1
    rows.append((cpu, x["comm"], tid,
                 (x["ut"] - y["ut"]) / tck / el, (x["st"] - y["st"]) / tck / el,
                 (x["wait"] - y["wait"]) / 1e9 / el,
                 (x["vol"] - y["vol"]) / el, (x["inv"] - y["inv"]) / el,
                 100 * sc["R"] / tot, 100 * sc["S"] / tot, 100 * sc["D"] / tot))
rows.sort(reverse=True)
lines = [f"pid={pid} window={el:.1f}s samples={n} /proc/self/maps lines={maps_lines}",
         f"{'thread':<18}{'tid':>8}{'cores':>7}{'user':>6}{'kern':>6}{'rq-wait':>8}"
         f"{'vol/s':>8}{'invol/s':>8}{'R%':>5}{'S%':>5}{'D%':>4}"]
for r in rows:
    lines.append(f"{r[1]:<18}{r[2]:>8}{r[0]:>7.2f}{r[3]:>6.2f}{r[4]:>6.2f}{r[5]:>8.3f}"
                 f"{r[6]:>8.0f}{r[7]:>8.0f}{r[8]:>5.0f}{r[9]:>5.0f}{r[10]:>4.0f}")
txt = "\n".join(lines) + "\n"
open(os.path.join(out, "waitprof.txt"), "w").write(txt)
print(txt)
