#!/usr/bin/env python3
"""Find Lua script bindings: which C++ function implements each script name.

  python3 tools/theseus/lua_bindings.py

Fable II registers engine functions with Lua in large registration functions:
before each call to a register helper, r4 = address of the Lua name string and
r5 = address of the C++ function (see sub_823ADF80: "GetTime" -> 0x82191B48).
This tool tracks constants built with lis/addi/ori in the generated code of the
functions that reference many Lua API names, and at every call records the
(r4 string, r5 function) pairs.

Inputs:  out/theseus/functions.jsonl, out/theseus/ghidra/strings.jsonl,
         docs/re/FABLE2_LUA_API.md (catalog of script-visible names)
Output:  out/theseus/lua_bindings.jsonl  {"lua_name","table","function","addr",
         "registrar","helper","in_catalog"}
         out/theseus/lua_bindings_summary.json

Several helper signatures exist (function pointer in r5, r6 or r9; the name
sometimes first turned into a string object, or kept in r29/r30). Rule: at a call
passing exactly one function pointer in r4-r10, pair it with a Lua name passed
to the same call (high) or else the most recent unused Lua name loaded (medium).
Register values are forgotten at labels and volatile registers after calls.
"""
import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import target as target_mod  # noqa: E402

TARGET = target_mod.load()
OUT = TARGET.out
CATALOG = TARGET.root / "docs/re/FABLE2_LUA_API.md"

SET_IMM = re.compile(r"^ctx\.(r\d+)\.s64 = (-?\d+);$")
ADD_IMM = re.compile(r"^ctx\.(r\d+)\.s64 = ctx\.(r\d+)\.s64 \+ (-?\d+);$")
OR_IMM = re.compile(r"^ctx\.(r\d+)\.u64 = ctx\.(r\d+)\.u64 \| (\d+);$")
MOVE = re.compile(r"^ctx\.(r\d+)\.u64 = ctx\.(r\d+)\.u64;$")
WRITE = re.compile(r"^ctx\.(r\d+)\.")
CALL = re.compile(r"^(\w+)\(ctx, base\);$")
VOLATILE = ["r0"] + [f"r{i}" for i in range(3, 13)]


def load_catalog():
    doc = CATALOG.read_text(encoding="utf-8")
    g = doc[doc.index("## 1. Engine global functions"):doc.index("## 2. Engine global tables")]
    globals_ = set(re.findall(r"^\| `([A-Za-z_]\w*)` \| \d+", g, re.M))
    tables_of = defaultdict(set)
    for t, m in re.findall(r"^- `([A-Za-z_]\w*)\.([A-Za-z_]\w*)` · ", doc, re.M):
        tables_of[m].add(t)
    return globals_, tables_of


def main():
    funcs = {r["addr"]: r for r in map(json.loads, open(OUT / "functions.jsonl")) if r["addr"]}
    name_of = {int(a, 16): r["name"] for a, r in funcs.items()}
    strings = [json.loads(l) for l in open(OUT / "ghidra/strings.jsonl")]
    str_at = {int(s["addr"], 16): s["value"] for s in strings}
    globals_, tables_of = load_catalog()
    api = globals_ | set(tables_of)

    # Registrars: functions referencing >= 5 catalog names.
    refs = Counter()
    for s in strings:
        if s["value"] in api:
            for f in s["funcs"]:
                refs[f] += 1
    registrars = {f for f, c in refs.items() if c >= 5}

    # Map registrar addresses to generated function names; scan their bodies.
    wanted = {funcs[a]["name"] for a in registrars if a in funcs}
    bindings, stats = [], Counter()
    fstart = {a for a in name_of}
    ARGS = [f"r{i}" for i in range(4, 11)]

    # Any identifier-like string counts: the catalog only lists names the shipped
    # scripts call, and the engine registers more (tuning/debug setters etc.).
    IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]{2,}")

    table_names = {t for owners in tables_of.values() for t in owners}

    def is_name(v):
        # Table names ("Village", "OpinionReaction") are not function names: the
        # registrar loads them before each method. They are tracked separately.
        if v is None:
            return False
        val = str_at.get(v & 0xFFFFFFFF)
        if val is None or (val in table_names and val not in api):
            return False
        return val in api or IDENT.fullmatch(val) is not None

    def is_table(v):
        return v is not None and str_at.get(v & 0xFFFFFFFF) in table_names

    for path in TARGET.partitions():
        cur, regs, pending, table = None, {}, None, None
        for line in path.open(encoding="utf-8", errors="replace"):
            if line.startswith("DEFINE_REX_FUNC("):
                n = line[16:line.index(")")]
                cur, regs, pending, table = (n if n in wanted else None), {}, None, None
                continue
            if cur is None:
                continue
            if line.startswith("}"):
                cur = None
                continue
            s = line.strip()
            if not s or s.startswith("//"):
                continue
            if s.startswith("loc_") and s.endswith(":"):
                regs = {}          # values may come from another path; keep `pending`
                continue
            c = CALL.match(s)
            if c:
                fn_regs = [r for r in ARGS if regs.get(r) is not None and (regs[r] & 0xFFFFFFFF) in fstart]
                name_regs = [r for r in ARGS if is_name(regs.get(r))]
                if len(fn_regs) == 1:
                    fn_addr = regs[fn_regs[0]] & 0xFFFFFFFF
                    if name_regs:
                        lua, how = str_at[regs[name_regs[0]] & 0xFFFFFFFF], "same call"
                    elif pending is not None:
                        lua, how = str_at[pending & 0xFFFFFFFF], "name loaded earlier"
                    else:
                        lua = None
                    if lua:
                        bindings.append({"lua_name": lua, "function": name_of[fn_addr],
                                         "addr": f"0x{fn_addr:08X}", "registrar": cur,
                                         "helper": c.group(1), "name_reg": name_regs[0] if name_regs else None,
                                         "func_reg": fn_regs[0], "pairing": how,
                                         "confidence": "high" if how == "same call" else "medium",
                                         "in_catalog": lua in api, "table_loaded": table})
                        stats["pairs_" + ("same_call" if how == "same call" else "deferred")] += 1
                        pending = None
                    else:
                        stats["function_without_name"] += 1
                elif len(fn_regs) > 1:
                    stats["ambiguous_several_functions"] += 1
                for r in VOLATILE:
                    regs.pop(r, None)
                continue
            m = SET_IMM.match(s)
            if m:
                regs[m.group(1)] = int(m.group(2))
            else:
                m = ADD_IMM.match(s)
                if m:
                    src = regs.get(m.group(2))
                    if src is None:
                        regs.pop(m.group(1), None)
                    else:
                        regs[m.group(1)] = src + int(m.group(3))
                else:
                    m = OR_IMM.match(s)
                    if m:
                        src = regs.get(m.group(2))
                        if src is None:
                            regs.pop(m.group(1), None)
                        else:
                            regs[m.group(1)] = src | int(m.group(3))
                    else:
                        m = MOVE.match(s)
                        if m:
                            if m.group(2) in regs:
                                regs[m.group(1)] = regs[m.group(2)]
                            else:
                                regs.pop(m.group(1), None)
                        else:
                            w = WRITE.match(s)
                            if w:
                                regs.pop(w.group(1), None)
                            continue
            # A Lua name string just materialised in a register becomes "pending".
            dst = m.group(1)
            if is_name(regs.get(dst)):
                pending = regs[dst]
            elif is_table(regs.get(dst)):
                table = str_at[regs[dst] & 0xFFFFFFFF]

    # Table attribution: per registrar, the catalog table that owns most of its names.
    by_registrar = defaultdict(list)
    for b in bindings:
        by_registrar[b["registrar"]].append(b)
    for reg, bs in by_registrar.items():
        votes = Counter(t for b in bs for t in tables_of.get(b["lua_name"], ()))
        top = votes.most_common(1)[0][0] if votes else None
        for b in bs:
            owners = tables_of.get(b["lua_name"], set())
            if b.get("table_loaded") and (not owners or b["table_loaded"] in owners):
                b["table"] = b["table_loaded"]   # the registrar loaded this table's name
            elif b["lua_name"] in globals_ and not owners:
                b["table"] = "(global)"
            elif top in owners:
                b["table"] = top
            elif not owners and top:
                b["table"] = top          # not in the catalog: assume the registrar's table
                b["table_assumed"] = True
            elif len(owners) == 1:
                b["table"] = next(iter(owners))
            else:
                b["table"] = None

    with open(OUT / "lua_bindings.jsonl", "w") as w:
        for b in bindings:
            w.write(json.dumps(b) + "\n")
    distinct_funcs = {b["function"] for b in bindings}
    summary = {
        "registrars_scanned": len(wanted),
        "pairs": len(bindings),
        "distinct_functions": len(distinct_funcs),
        "distinct_lua_names": len({b["lua_name"] for b in bindings}),
        "pairs_high_same_call": sum(b["confidence"] == "high" for b in bindings),
        "pairs_medium_name_loaded_earlier": sum(b["confidence"] == "medium" for b in bindings),
        "pairs_with_table": sum(b["table"] is not None for b in bindings),
        "pairs_in_catalog": sum(b["in_catalog"] for b in bindings),
        "functions_bound_to_several_names": sum(1 for f, c in Counter(b["function"] for b in bindings).items() if c > 1),
        "previously_unnamed_functions": sum(1 for f in distinct_funcs if f.startswith("sub_")),
        "helpers": Counter(b["helper"] for b in bindings).most_common(10),
        "top_tables": Counter(b["table"] for b in bindings).most_common(25),
        **stats,
    }
    (OUT / "lua_bindings_summary.json").write_text(json.dumps(summary, indent=1))
    print(json.dumps(summary, indent=1))


if __name__ == "__main__":
    main()
