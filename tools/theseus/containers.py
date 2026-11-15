#!/usr/bin/env python3
"""Recognise C++ standard-library container code (MSVC/Dinkumware STL).

  python3 tools/theseus/containers.py

Template containers are compiled separately for every element type, so the
binary holds thousands of near-identical map/set/vector helpers. They confuse
naming rules ("used by one Lua binding" often means "a vector push_back used by
that binding"). This tool spots them by code shape in the generated C++:

  tree    std::map/set (_Tree): a byte flag in the node (_Isnil, at node+0x11..0x41)
          compared with 0, plus left/right child loads (node+0 / node+8) from the same
          register, or 3+ stores to the link fields +0/+4/+8 (rotation/relink).
  vector  std::vector: two of the three pointers first/last/end (object+4/+8/+12,
          or +0/+4/+8) loaded from the same register, subtracted, then divided by the
          element size (arithmetic shift right or multiply-high).

A function is a container *helper* when the shape is present and it is small
(tree <= 300, vector <= 160 instructions); bigger ones only get a "uses map/vector
inline" note. Composition (context only, not a helper): a function of <= 100
instructions whose callees are all shape-detected helpers or very common basics
(1000+ callers: allocate/free/refcount). Spot checks: shape 6/6 correct,
composition 1/2 (catches find-or-create but also object logic). Output: out/theseus/containers.jsonl {"name","addr","kind","helper","insns","signals"}.
"""
import json
import re
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import target as target_mod  # noqa: E402

TARGET = target_mod.load()
OUT = TARGET.out
HELPER_MAX = {"tree": 300, "vector": 160, "tree+vector": 300}   # erase/rebalance is long
COMPOSE_MAX = 100        # small functions calling only container/basic helpers
BASIC_CALLERS = 1000     # callees with this many callers count as basic (alloc/free/refcount)

LOAD = re.compile(r"^ctx\.(r\d+)\.u64 = REX_LOAD_U(8|16|32)\(ctx\.(r\d+)\.u32 \+ (-?\d+)\);$")
CMP0 = re.compile(r"^ctx\.cr\d\.compare<u?int32_t>\(ctx\.(r\d+)\.u32, 0, ctx\.xer\);$")
SUB = re.compile(r"^ctx\.(r\d+)\.u64 = ctx\.(r\d+)\.u64 - ctx\.(r\d+)\.u64;$")
SHIFT = re.compile(r"^ctx\.(r\d+)\.s64 = ctx\.(r\d+)\.s32 >> (\d+);$")
MULHI = re.compile(r"^ctx\.(r\d+)\.s64 = \(int64_t\(ctx\.(r\d+)\.s32\) \* int64_t\(ctx\.(r\d+)\.s32\)\) >> 32;$")
STORE_LINK = re.compile(r"^REX_STORE_U32\(ctx\.(r\d+)\.u32 \+ (0|4|8), ctx\.(r\d+)\.u32\);$")
STORE_FIELD = re.compile(r"^REX_STORE_U32\(ctx\.(r\d+)\.u32 \+ (4|8|12), ctx\.(r\d+)\.u32\);$")
GOTO = re.compile(r"goto (loc_[0-9A-F]+);")
LABEL = re.compile(r"^(loc_[0-9A-F]+):$")
WRITE = re.compile(r"^ctx\.(r\d+)\.")


def analyse(lines):
    loads = {}            # reg -> (base, offset, width) of the last load into it
    isnil_regs = set()    # node registers whose byte at 0x11..0x31 was compared with 0
    byte_from = {}        # reg holding a node byte -> node reg
    child_loads = Counter()   # (node reg, offset) for 32-bit loads at +0/+8
    vec_sub = False
    sub_regs = set()
    labels, backward = set(), False
    stores_vec_field = False
    link_stores = 0
    for s in lines:
        m = LABEL.match(s)
        if m:
            labels.add(m.group(1))
            continue
        g = GOTO.search(s)
        if g and g.group(1) in labels:
            backward = True
        m = LOAD.match(s)
        if m:
            dst, width, base, off = m.group(1), int(m.group(2)), m.group(3), int(m.group(4))
            loads[dst] = (base, off, width)
            if width == 8 and 0x11 <= off <= 0x41:
                byte_from[dst] = base
            else:
                byte_from.pop(dst, None)
            if width == 32 and off in (0, 8):
                child_loads[(base, off)] += 1
            continue
        m = CMP0.match(s)
        if m and m.group(1) in byte_from:
            isnil_regs.add(byte_from[m.group(1)])
            continue
        m = SUB.match(s)
        if m:
            a, b = loads.get(m.group(2)), loads.get(m.group(3))
            if (a and b and a[0] == b[0] and a[2] == b[2] == 32 and a[1] != b[1]
                    and {a[1], b[1]} <= {0, 4, 8, 12} and abs(a[1] - b[1]) in (4, 8)):
                sub_regs.add(m.group(1))
            loads.pop(m.group(1), None)
            continue
        m = SHIFT.match(s)
        if m and m.group(2) in sub_regs:
            vec_sub = True
        m2 = MULHI.match(s)
        if m2 and (m2.group(2) in sub_regs or m2.group(3) in sub_regs):
            vec_sub = True
        if STORE_FIELD.match(s):
            stores_vec_field = True
        if STORE_LINK.match(s):
            link_stores += 1
        w = WRITE.match(s)
        if w and not m:
            loads.pop(w.group(1), None)
            byte_from.pop(w.group(1), None)

    tree_nodes = [r for r in isnil_regs if child_loads[(r, 0)] and child_loads[(r, 8)]]
    signals = []
    kind = None
    if tree_nodes and backward:
        kind = "tree"
        signals.append("isnil byte + left/right child loads in a loop")
    elif tree_nodes:
        kind = "tree"
        signals.append("isnil byte + left/right child loads")
    elif isnil_regs and link_stores >= 3:
        kind = "tree"
        signals.append(f"isnil byte + {link_stores} node link stores (rotation/relink)")
    if vec_sub:
        signals.append("first/last/end pointer difference / element size")
        if stores_vec_field:
            signals.append("writes a first/last/end field")
        if kind is None:
            kind = "vector"
        else:
            kind = "tree+vector"
    return kind, signals


def main():
    funcs = {r["name"]: r for r in map(json.loads, open(OUT / "functions.jsonl"))}
    results, stats = [], Counter()
    for path in TARGET.partitions():
        cur, body = None, []
        for line in path.open(encoding="utf-8", errors="replace"):
            if line.startswith("DEFINE_REX_FUNC("):
                cur, body = line[16:line.index(")")], []
                continue
            if cur is None:
                continue
            if line.startswith("}"):
                kind, signals = analyse(body)
                if kind:
                    f = funcs.get(cur, {})
                    insns = f.get("insns", 0)
                    helper = insns <= HELPER_MAX[kind]
                    results.append({"name": cur, "addr": f.get("addr"), "kind": kind,
                                    "helper": helper, "method": "shape", "insns": insns, "signals": signals})
                    stats[f"{kind}_{'helper' if helper else 'inline'}"] += 1
                cur = None
                continue
            s = line.strip()
            if s and not s.startswith("//"):
                body.append(s)
    # Composition: small functions that only call container helpers and basics.
    by_name = {r["name"]: r for r in results}
    basic = {n for n, f in funcs.items() if f.get("n_callers", 0) >= BASIC_CALLERS}
    # One step only, from shape-detected helpers: spot checks (2026-11-15) found
    # composed results unreliable (1 of 2 was object logic), so they are context only.
    for _ in range(1):
        added = 0
        helpers = {n for n, r in by_name.items() if r["helper"] and r["method"] == "shape"}
        for n, f in funcs.items():
            if n in by_name or not f.get("addr") or f["insns"] > COMPOSE_MAX or f.get("indirect_calls"):
                continue
            callees = set(f["callees"])
            cont = callees & helpers
            if cont and callees <= helpers | basic and not f.get("imports"):
                kinds = Counter(by_name[c]["kind"] for c in cont)
                rec = {"name": n, "addr": f["addr"], "kind": kinds.most_common(1)[0][0], "helper": False,
                       "method": "composed",
                       "insns": f["insns"], "signals": [f"calls only container helpers ({', '.join(sorted(cont)[:3])})"]}
                by_name[n] = rec
                results.append(rec)
                stats[f"{rec['kind']}_composed_context"] += 1
                added += 1
        if not added:
            break
    with open(OUT / "containers.jsonl", "w") as w:
        for r in results:
            w.write(json.dumps(r) + "\n")
    print(json.dumps(dict(stats), indent=1))


if __name__ == "__main__":
    main()
