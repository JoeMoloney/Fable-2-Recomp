#!/usr/bin/env python3
"""Build the Theseus batch queue from the inventory (plans/THESEUS_BATCH.md, unit B0).

  python3 tools/theseus/batch_pool.py            # write the queue, print counts
  python3 tools/theseus/batch_pool.py --top 25 A # also list the next 25 queued in tier A

Reads out/theseus/functions.jsonl (run inventory.py first) and the ledger
tools/theseus/batch/ledger.tsv. Writes out/theseus/batch/queue.jsonl: every pool
function not yet in the ledger, ranked.

Pool: leaf, 6-80 instructions, has an address, no kernel imports, no indirect
calls, no global lock / timebase / mid-asm hooks, no hand-written override.
Tiers: A integer without stores, B integer with stores, C float/vector,
D no direct callers (reached through vtables or pointers; callers can't be
checked for scratch-register reads). Inside a tier: most callers first, then
smallest first.

The ledger holds only functions that left the queue (written, gated, shipped,
rejected, skipped); it is tracked in git, the queue is not.
"""
import argparse
import csv
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import target as target_mod  # noqa: E402

TARGET = target_mod.load()
LEDGER = HERE / "batch" / "ledger.tsv"
LEDGER_FIELDS = ["name", "tier", "status", "gate", "kill_rate", "batch", "note"]
STATUSES = {"written", "gated", "gate-fail", "reviewed", "shipped", "rejected", "skipped"}
# Cache-control instructions in a small leaf usually mean a speed-tuned copy/fill
# routine: a plain rewrite is correct but may cost frames (see memset in the pilot).
SPEED_HINT = re.compile(r"//\s*(dcbz|dcbz128|dcbt|dcbtst|dcbf)\b")


def read_ledger():
    if not LEDGER.exists():
        return {}
    with LEDGER.open() as f:
        rows = list(csv.DictReader(f, delimiter="\t"))
    for r in rows:
        if r["status"] not in STATUSES:
            sys.exit(f"{LEDGER}: {r['name']}: unknown status {r['status']!r}")
    return {r["name"]: r for r in rows}


def write_ledger(rows):
    LEDGER.parent.mkdir(parents=True, exist_ok=True)
    with LEDGER.open("w", newline="") as f:
        w = csv.DictWriter(f, LEDGER_FIELDS, delimiter="\t", lineterminator="\n")
        w.writeheader()
        for r in sorted(rows.values(), key=lambda r: r["name"]):
            w.writerow({k: r.get(k, "") for k in LEDGER_FIELDS})


def tier_of(f):
    if f["n_callers"] == 0:
        return "D"
    if f["uses_fp"] or f["uses_vmx"]:
        return "C"
    return "B" if f["stores"] else "A"


def in_pool(f):
    return (f["leaf"] and 6 <= f["insns"] <= 80 and f["addr"] and not f["imports"]
            and not f["indirect_calls"] and not f["global_lock"] and not f["timebase"]
            and not f["midasm_hooks"] and not f["hotfunc"])


def speed_tuned(names):
    """Names whose generated body contains cache-control instructions."""
    hits, cur = set(), None
    start = re.compile(r"^DEFINE_REX_FUNC\((\w+)\)")
    for path in TARGET.partitions():
        for line in path.open(errors="replace"):
            m = start.match(line)
            if m:
                cur = m.group(1) if m.group(1) in names else None
            elif cur and SPEED_HINT.search(line):
                hits.add(cur)
    return hits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--top", type=int, default=0, help="list the next N queued functions")
    ap.add_argument("tiers", nargs="*", default=[], help="tiers to list with --top (default all)")
    args = ap.parse_args()

    inv = TARGET.out / "functions.jsonl"
    if not inv.exists():
        sys.exit("run tools/theseus/inventory.py first")
    funcs = [json.loads(l) for l in inv.open()]
    pool = [f for f in funcs if in_pool(f)]

    ledger = read_ledger()
    rewrites = {p.stem for p in (HERE / "rewrites").glob("*.cpp")}
    unknown = sorted(n for n in rewrites if n not in ledger)
    if unknown:
        print(f"note: rewrites not in the ledger (add them): {', '.join(unknown)}")

    tuned = speed_tuned({f["name"] for f in pool})
    queue = []
    for f in pool:
        if f["name"] in ledger or f["name"] in rewrites:
            continue
        queue.append({
            "name": f["name"], "addr": f["addr"], "tier": tier_of(f), "insns": f["insns"],
            "n_callers": f["n_callers"], "stores": f["stores"], "loads": f["loads"],
            "uses_fp": f["uses_fp"], "uses_vmx": f["uses_vmx"], "named": f["named"],
            "speed_tuned": f["name"] in tuned, "file": f["file"],
        })
    queue.sort(key=lambda q: (q["tier"], -q["n_callers"], q["insns"], q["name"]))
    for i, q in enumerate(queue):
        q["rank"] = i

    out = TARGET.out / "batch"
    out.mkdir(parents=True, exist_ok=True)
    with (out / "queue.jsonl").open("w") as fh:
        for q in queue:
            fh.write(json.dumps(q) + "\n")

    counts = {}
    for q in queue:
        counts[q["tier"]] = counts.get(q["tier"], 0) + 1
    status = {}
    for r in ledger.values():
        status[r["status"]] = status.get(r["status"], 0) + 1
    print(f"pool {len(pool)}  queued {len(queue)}  ledger {len(ledger)} {status}")
    print("queued by tier: " + "  ".join(f"{t} {counts.get(t, 0)}" for t in "ABCD")
          + f"  (speed-tuned flagged: {sum(q['speed_tuned'] for q in queue)})")
    if args.top:
        sel = [q for q in queue if not args.tiers or q["tier"] in args.tiers][:args.top]
        for q in sel:
            flag = " speed?" if q["speed_tuned"] else ""
            print(f"  {q['tier']} {q['name']:<48} {q['insns']:3d} insns {q['n_callers']:5d} callers{flag}")


if __name__ == "__main__":
    main()
