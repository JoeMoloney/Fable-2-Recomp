#!/usr/bin/env python3
"""Evidence packet for one queued function (plans/THESEUS_BATCH.md, unit B0).

  python3 tools/theseus/batch_packet.py sub_82D51980     # one function
  python3 tools/theseus/batch_packet.py --next 5 A       # the next 5 queued in tier A

Writes out/theseus/batch/<name>/packet.md: everything the rewriter (an LLM or a
person) needs, with no other lookups: what the function is in the inventory,
the naming evidence, strings and RTTI pointing at it, its callers and how they
use r3, the original recompiled body, the rewrite contract, the rules
vocabulary and the closest pilot rewrite to copy the style from.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import target as target_mod  # noqa: E402
from difftest import extract  # noqa: E402

TARGET = target_mod.load()
OUT = TARGET.out
ROOT = TARGET.root

# The pilot rewrite to imitate for each tier (style, comments, how r3 is kept).
EXAMPLES = {
    "A": ["FNV1Hash_821F3C28", "sub_821E3E10"],
    "B": ["StringCopyN_822CD828", "GrowAndZeroBuffer_82BC55B8"],
    "C": ["ComputeWrapped_8222C3E8", "NormalizeVector_82260BC8"],
    "D": ["FNV1Hash_821F3C28", "StringCopyN_822CD828"],
}

CONTRACT = """\
## The task

Write `tools/theseus/rewrites/{name}.cpp` defining
`extern "C" REX_FUNC(rewrite_{name})`: readable C++ that does what the
original does, with a header comment saying what it is (the C/C++ it most likely
came from), its arguments and return value, and the evidence for the name.
Add `tools/theseus/rewrites/{name}.rules` whenever random registers would not
reach the interesting paths (pointers, lengths, structs, tables, float edges).
Propose a name (`Name_{addr}`) with its evidence; don't rename anything yet.

**Contract (checked by `difftest.py`):** identical full 64-bit r3; identical
non-volatile registers (r1, r2, r13-r31, f14-f31, cr2-cr4, lr) and memory,
including every page either version touches and the stack from r1 up.
Scratch registers (r0, r4-r12, ctr, xer, cr0, cr1, cr5-cr7) may differ only if
no caller reads them (see "Callers" below). Debug traps (`ppc_trap`) must
happen in the same cases. Floats must match bit for bit: keep fused
multiply-adds, the operation order, flush-to-zero switches and NaN handling.

**Rules vocabulary** (one per line in `.rules`, `#` comments; details in
`tools/theseus/README.md`): `rN ptr|ptr_or_null|int|u32|any`, `rN len <max>`,
`rN slen <max>`, `rN fixed <value>`, `rN near rM <max>`,
`mem32 <addr>|@rN+off|*<global>+off <kind> [max]`, `tree <isnil_off> <key_off> [n]`
with kinds `tree_cont|tree_node|tree_key`, `data <addr> <hex>` (image bytes, read
them with Ghidra `read_memory`), `ret f1`, `fval <double>`.

**Then run:** `python3 tools/theseus/difftest.py {name}` (must print
`RESULT PASS-ABI`). Gate and mutants come with units B1/B2.
"""


def jsonl(path, key):
    out = {}
    if path.exists():
        for line in path.open():
            rec = json.loads(line)
            out[rec[key]] = rec
    return out


def load_queue():
    q = OUT / "batch" / "queue.jsonl"
    if not q.exists():
        sys.exit("run tools/theseus/batch_pool.py first")
    return [json.loads(l) for l in q.open()]


def strings_for(addr):
    hits = []
    path = OUT / "ghidra" / "strings.jsonl"
    if path.exists():
        for line in path.open():
            rec = json.loads(line)
            if addr in rec.get("funcs", []):
                hits.append(rec["value"])
    return hits


def rtti_for(addr):
    hits = []
    path = OUT / "ghidra" / "rtti.jsonl"
    if path.exists():
        for line in path.open():
            rec = json.loads(line)
            for vt in rec.get("vtables", []):
                for slot, fn in enumerate(vt.get("funcs", [])):
                    if fn == addr:
                        hits.append(f"{rec['class']} vtable {vt['vtable']} slot {slot}")
    return hits


def packet(q, inv, names, ghidra, containers):
    name, addr = q["name"], q["addr"]
    f = inv[name]
    lines = [f"# Theseus packet: {name}", ""]
    flags = [k for k in ("uses_fp", "uses_vmx", "speed_tuned") if q.get(k)]
    lines += [f"- Guest address {addr}, tier **{q['tier']}**, {f['insns']} PPC instructions, "
              f"{f['loads']} loads, {f['stores']} stores, {f['branches']} branches"
              + (f", flags: {', '.join(flags)}" if flags else ""),
              f"- Direct callers: {f['n_callers']}" + ("" if f["n_callers"] else
              " (reached through a vtable or function pointer: scratch-register use can't be checked)"),
              f"- Generated in `generated/default/{f['file']}`"]
    if q.get("speed_tuned"):
        lines.append("- **Speed-tuned original** (cache-control instructions): a plain "
                     "rewrite may cost frames; say so in the header.")
    lines += ["", "## Naming evidence", ""]
    g = ghidra.get(addr.upper().replace("0X", "0x"))
    lines.append(f"- Ghidra name: `{g['name']}`" if g else "- Ghidra name: (not in export)")
    n = names.get(name)
    if n and n.get("proposed"):
        lines.append(f"- Proposed by evidence.py: `{n['proposed']}` ({n['confidence']})")
    for ev in (n or {}).get("evidence", []):
        lines.append(f"  - {ev}")
    for key in ("module", "library", "class", "source_file"):
        if n and n.get(key):
            lines.append(f"- {key}: {n[key]}")
    c = containers.get(name)
    if c:
        lines.append(f"- STL shape: {c['kind']} ({'; '.join(c['signals'])})")
    for s in strings_for(addr)[:15]:
        lines.append(f"- references string {json.dumps(s)[:120]}")
    for r in rtti_for(addr)[:10]:
        lines.append(f"- RTTI: {r}")

    callers = sorted(set(f["callers"]), key=lambda c: (c.startswith("sub_"), c))
    lines += ["", "## Callers", "", "Named first: " + ", ".join(f"`{c}`" for c in callers[:25])
              + (f" … ({len(callers)} distinct)" if len(callers) > 25 else ""), ""]
    if f["n_callers"]:
        usage = subprocess.run([sys.executable, str(HERE / "caller_usage.py"), name],
                               capture_output=True, text=True, cwd=ROOT)
        lines += ["```", usage.stdout.strip(), "```"]

    src, body = extract(name)
    lines += ["", f"## Original (`{src}`)", "", "```cpp", *body, "```", ""]
    lines.append(CONTRACT.format(name=name, addr=addr[2:].upper()))
    ex = [e for e in EXAMPLES[q["tier"]] if (HERE / "rewrites" / f"{e}.cpp").exists()]
    lines += ["## Pilot examples to imitate", ""]
    for e in ex:
        rules = HERE / "rewrites" / f"{e}.rules"
        lines.append(f"- `tools/theseus/rewrites/{e}.cpp`" + (f" + `{e}.rules`" if rules.exists() else ""))
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("name", nargs="?")
    ap.add_argument("--next", type=int, default=0, help="packets for the next N queued")
    ap.add_argument("tiers", nargs="*", default=[])
    args = ap.parse_args()

    queue = load_queue()
    if args.next:
        tiers = ([args.name] if args.name else []) + args.tiers
        sel = [q for q in queue if not tiers or q["tier"] in tiers][:args.next]
    elif args.name:
        sel = [q for q in queue if q["name"] == args.name]
        if not sel:
            sys.exit(f"{args.name}: not in the queue (in the ledger already, or not in the pool)")
    else:
        sys.exit("give a function name or --next N [tiers]")

    inv = jsonl(OUT / "functions.jsonl", "name")
    names = jsonl(OUT / "names.jsonl", "name")
    ghidra = jsonl(OUT / "ghidra" / "functions.jsonl", "entry")
    containers = jsonl(OUT / "containers.jsonl", "name")
    for q in sel:
        d = OUT / "batch" / q["name"]
        d.mkdir(parents=True, exist_ok=True)
        (d / "packet.md").write_text(packet(q, inv, names, ghidra, containers))
        print(d / "packet.md")


if __name__ == "__main__":
    main()
