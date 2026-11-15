#!/usr/bin/env python3
"""Turn the Ghidra evidence export into proposed names and module tags.

  python3 tools/theseus/evidence.py

Inputs:  out/theseus/functions.jsonl            (tools/theseus/inventory.py)
         out/theseus/ghidra/{strings,rtti}.jsonl (tools/theseus/ghidra/export.sh)
Outputs: out/theseus/names.jsonl   one record per function with any evidence:
           {"name","addr","proposed","confidence","module","source_file","class",
            "evidence":[...]}
         out/theseus/names_summary.json

Rules (each proposal carries its evidence so a reviewer can check it):
  lua binding    The function registered for a Lua script name (lua_bindings.py):
                 "Lua_<Table>_<Name>", high when name and function are passed in the
                 same call, medium when the name was loaded just before.
  method string  A string like "CFleeAgent::ComputeDestination" (profiler zone;
                 Havok timer prefixes Tt/Lt/St/Et stripped) referenced by exactly
                 one function names that function: high. By 2-3: medium each.
  source path    An assert path like "D:\\dev\\Fable2\\GoldBranch\\SourceCode\\ctg\\
                 lhcomponents\\lhaudio\\...\\CSplitter.cpp" tags each referencing
                 function with its source file and folder (module).
  rtti vtable    Each function in a class's vtable gets "Class::vfN" (N = slot):
                 high, unless the same function sits in 4+ vtables (shared stub).
  propagation    An untagged function whose direct callers are all tagged with
                 the same module inherits it (medium), repeated a few rounds.
Nothing here renames anything; it only proposes.
"""
import json
import re
from collections import Counter, defaultdict
from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import target as target_mod  # noqa: E402

TARGET = target_mod.load()
OUT = TARGET.out
library_of_path = TARGET.library_of_path
library_of_name = TARGET.library_of_name

METHOD = re.compile(r"^(?:[TLSE]t)?((?:[A-Za-z_]\w*::)+~?[A-Za-z_]\w*)$")
PATH = re.compile(r"([A-Za-z]:\\|\.\\|\\)[^\n]*\.(cpp|c|h|hpp|inl)$", re.I)


MESSAGE = re.compile(r"^(?:Error:\s*|Warning:\s*|-+\s*)?((?:[A-Za-z_]\w*::)+~?[A-Za-z_]\w*)"
                     r"(?:\(\))?\s*(?:[-:(]|\s(?:cannot|called|failed|unknown|missing))")
def load_jsonl(path):
    with open(path, encoding="utf-8") as f:
        return [json.loads(line) for line in f if line.strip()]


def norm_path(p):
    p = p.replace("/", "\\")
    marker = TARGET.source_root_marker
    m = re.search(re.escape(marker) + r"(.*)$", p, re.I) if marker else None
    rel = m.group(1) if m else p.lstrip(".\\")
    parts = rel.split("\\")
    module = "\\".join(parts[:-1]) or "(relative)"
    return module, parts[-1]


def main():
    funcs = {r["addr"]: r for r in load_jsonl(OUT / "functions.jsonl") if r["addr"]}
    strings = load_jsonl(OUT / "ghidra/strings.jsonl")
    rtti = load_jsonl(OUT / "ghidra/rtti.jsonl") if (OUT / "ghidra/rtti.jsonl").exists() else []

    ev = defaultdict(lambda: {"evidence": [], "names": [], "module": None, "file": None, "class": None,
                              "library": None})
    unknown_addrs = set()
    # Ghidra and the recompiler can disagree on where a function starts, so map
    # each Ghidra address to the recompiled function that contains it.
    import bisect
    starts = sorted((int(a, 16), a) for a in funcs)
    start_vals = [v for v, _ in starts]
    inexact = Counter()

    def containing(addr):
        if addr in funcs:
            return addr
        v = int(addr, 16)
        i = bisect.bisect_right(start_vals, v) - 1
        if i >= 0:
            sv, a = starts[i]
            if v < sv + 4 * max(funcs[a]["insns"], 1):
                inexact["mapped_to_containing_function"] += 1
                return a
        return None

    def note(addr, **kw):
        found = containing(addr)
        if found is None:
            unknown_addrs.add(addr)
            return None
        addr = found
        e = ev[addr]
        for k, v in kw.items():
            if k == "evidence":
                e["evidence"].append(v)
            elif k == "name":
                e["names"].append(v)
            elif e.get(k) is None:
                e[k] = v
        return e

    stats = Counter()
    for s in strings:
        value, refs = s["value"].strip(), s["funcs"]
        m = METHOD.match(value)
        if m and len(refs) <= 3:
            # Three-part names like "CPathFinder::FindNextMove::Goto" are usually a
            # profiler scope *inside* a method: medium, a reviewer decides.
            conf = "high" if len(refs) == 1 and m.group(1).count("::") == 1 else "medium"
            lib = library_of_name(m.group(1))
            for a in refs:
                note(a, name=(m.group(1), conf), library=lib,
                     evidence=f"method string {value!r} @{s['addr']} ({len(refs)} referencing)")
            stats["method_strings"] += 1
            continue
        if PATH.search(value):
            module, file = norm_path(value)
            lib = library_of_path(value)
            for a in refs:
                note(a, module=module, file=file, library=lib, evidence=f"source path {value!r} @{s['addr']}")
            stats["path_strings"] += 1
            continue
        mm = MESSAGE.match(value)
        # Log/error messages that start with "Class::Method" usually come from that
        # method. Assert expressions ("a==B::c", "!x->y") are skipped.
        if mm and len(refs) <= 2 and not re.search(r"==|!=|->|^!", value):
            lib = library_of_name(mm.group(1))
            for a in refs:
                note(a, name=(mm.group(1), "medium"), library=lib,
                     evidence=f"message starts with method name {value[:80]!r} @{s['addr']}")
            stats["message_strings"] += 1
            continue
        # Any other string with a recognisable library prefix is a library hint.
        lib = library_of_name(value)
        if lib and len(refs) <= 3:
            for a in refs:
                note(a, library=lib, evidence=f"library-prefixed string {value[:60]!r}")
            stats["library_hint_strings"] += 1

    # Lua script bindings (tools/theseus/lua_bindings.py): the C++ function that
    # implements a script-visible name. Run lua_bindings.py first to include them.
    lua_path = OUT / "lua_bindings.jsonl"
    if lua_path.exists():
        for b in load_jsonl(lua_path):
            table = b.get("table") or "unknown"
            note(b["addr"], name=(f"Lua_{table}_{b['lua_name']}", b["confidence"]),
                 module=f"lua:{table}",
                 evidence=f"Lua binding {table}.{b['lua_name']} registered by {b['registrar']} "
                          f"via {b['helper']} ({b['pairing']})")
            stats["lua_bindings"] += 1

    # A function can sit in several vtables: an inherited, not-overridden method
    # appears at the same slot in each subclass; one function in several slots
    # (or in many classes) is an empty/shared stub. Slot 0 in MSVC is usually the
    # (scalar deleting) destructor.
    uses = defaultdict(set)
    for c in rtti:
        stats["rtti_classes"] += 1
        for vt in c["vtables"]:
            stats["rtti_vtables"] += 1
            for i, a in enumerate(vt["funcs"]):
                uses[a].add((c["class"], i, vt["vtable"]))
    for a, u in uses.items():
        classes = sorted({c for c, _, _ in u})
        slots = sorted({i for _, i, _ in u})
        lib = library_of_name(classes[0])
        if len(slots) > 1 or len(classes) >= 4:
            # No library tag: shared stubs (e.g. an empty method) are used across libraries.
            note(a, evidence=f"vtable stub: in {len(classes)} classes at slots {slots[:6]} "
                                          f"(e.g. {', '.join(classes[:3])})")
            stats["vtable_stubs"] += 1
            continue
        slot = slots[0]
        conf = "high" if len(classes) == 1 else "medium"
        dtor = " (slot 0: usually the deleting destructor)" if slot == 0 else ""
        for cls in classes:
            note(a, name=(f"{cls}::vf{slot}", conf), library=lib,
                 evidence=f"vtable slot {slot} of {cls}{dtor}"
                          + (f"; shared with {len(classes) - 1} other class(es), likely inherited" if len(classes) > 1 else ""))
        if len(classes) == 1:
            ev_rec = ev.get(containing(a))
            if ev_rec is not None and not ev_rec["class"]:
                ev_rec["class"] = classes[0]

    # Propagate module and library tags to functions whose direct callers all
    # share one tag (helpers used only inside that module/library).
    addr_of = {r["name"]: r["addr"] for r in funcs.values()}
    callers = {a: [addr_of[n] for n in r["callers"] if n in addr_of] for a, r in funcs.items()}
    propagated = Counter()
    for key in ("module", "library"):
        for _ in range(6):
            changed = 0
            for a, cl in callers.items():
                if not cl or (a in ev and ev[a][key]):
                    continue
                tags = {ev[c][key] if c in ev else None for c in cl}
                if len(tags) == 1 and None not in tags:
                    tag = tags.pop()
                    note(a, **{key: tag}, evidence=f"all {len(cl)} direct callers are in {key} {tag}")
                    ev[a][key + "_propagated"] = True
                    changed += 1
            propagated[key] += changed
            if not changed:
                break
    propagated = propagated["module"]

    # Address neighbourhood: the linker lays each library out contiguously, so an
    # untagged function with the SAME library tag close on both sides (within
    # SANDWICH_GAP functions) is tagged too (medium). Wider gaps are left alone:
    # large blocks can mix libraries (e.g. Microsoft runtime code near the end).
    SANDWICH_GAP = 16
    order = sorted(funcs, key=lambda a: int(a, 16))
    # Anchor only on DIRECT evidence: propagated tags can be wrong for shared
    # runtime helpers that happen to be called from one library only.
    def direct_library(a):
        e = ev.get(a)
        if e is None or e.get("library_propagated"):
            return None
        return e["library"]
    tags = [direct_library(a) for a in order]
    last_i, last_tag, sandwiched = None, None, 0
    for i, t in enumerate(tags):
        if t is None:
            continue
        if last_tag == t and last_i is not None and 1 < i - last_i <= SANDWICH_GAP + 1:
            for j in range(last_i + 1, i):
                if tags[j] is None and not (order[j] in ev and ev[order[j]]["library"]):
                    note(order[j], library=t,
                         evidence=f"address neighbourhood: between {order[last_i]} and {order[i]} ({t})")
                    ev[order[j]]["library_sandwiched"] = True
                    sandwiched += 1
        last_i, last_tag = i, t
    stats["library_sandwiched"] = sandwiched

    # Library address map for the write-up: runs of consecutive same-library tags.
    runs = []
    for a in order:
        t = ev[a]["library"] if a in ev else None
        if t is None:
            continue
        if runs and runs[-1]["library"] == t:
            runs[-1]["end"] = a
            runs[-1]["tagged"] += 1
        else:
            runs.append({"library": t, "start": a, "end": a, "tagged": 1})
    library_runs = [r for r in runs if r["tagged"] >= 20]
    stats["library_propagated"] = sum(1 for e in ev.values() if e.get("library_propagated"))
    stats["module_propagated"] = propagated

    rank = {"high": 0, "medium": 1, "low": 2}
    with open(OUT / "names.jsonl", "w") as w:
        for a in sorted(ev):
            e, f = ev[a], funcs[a]
            best = sorted(e["names"], key=lambda n: rank[n[1]])[0] if e["names"] else (None, None)
            distinct = sorted({n for n, _ in e["names"]})
            rec = {"name": f["name"], "addr": a, "proposed": best[0], "confidence": best[1],
                   "alternatives": distinct[1:] if len(distinct) > 1 else [],
                   "module": e["module"], "module_propagated": e.get("module_propagated", False),
                   "source_file": e["file"], "class": e["class"],
                   "library": e["library"], "library_propagated": e.get("library_propagated", False),
                   "library_sandwiched": e.get("library_sandwiched", False),
                   "currently_named": f["named"], "insns": f["insns"],
                   "evidence": e["evidence"][:12]}
            w.write(json.dumps(rec) + "\n")
            stats["functions_with_evidence"] += 1
            stats["with_proposed_name"] += best[0] is not None
            stats[f"name_{best[1]}"] += best[1] is not None
            stats["with_module"] += e["module"] is not None
            stats["with_library"] += e["library"] is not None
            stats["proposed_for_unnamed"] += best[0] is not None and not f["named"]
            stats["conflicting_names"] += len(distinct) > 1

    modules = Counter(e["module"] for e in ev.values() if e["module"])
    libraries = Counter(e["library"] for e in ev.values() if e["library"])
    lib_insns = Counter()
    for a, e in ev.items():
        if e["library"]:
            lib_insns[e["library"]] += funcs[a]["insns"]
    summary = {"totals": dict(stats), "functions_in_inventory": len(funcs),
               "evidence_for_addresses_not_in_inventory": len(unknown_addrs),
               "ghidra_addresses_mapped_to_containing_function": inexact["mapped_to_containing_function"],
               "top_modules": modules.most_common(40),
               "library_runs": library_runs,
               "libraries": {k: {"functions": v, "insns": lib_insns[k]} for k, v in libraries.most_common()}}
    (OUT / "names_summary.json").write_text(json.dumps(summary, indent=1))
    print(json.dumps(summary["totals"], indent=1))
    print("libraries:")
    for k, v in libraries.most_common():
        print(f"  {v:6d} functions  {lib_insns[k]:8d} insns  {k}")


if __name__ == "__main__":
    main()
