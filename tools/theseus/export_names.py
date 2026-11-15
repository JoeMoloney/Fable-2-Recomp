#!/usr/bin/env python3
"""Write the record-ready names (N3) as a tracked reference table.

  python3 tools/theseus/export_names.py

Reads out/theseus/names.jsonl (tools/theseus/evidence.py) and writes
docs/re/NAMES_EVIDENCE.tsv: one row per function with a high/medium proposal
that is not a folded accessor (rules and hit rates: docs/re/NAMES_EVIDENCE.md).
Columns: addr, current_name, proposed_name, confidence, rule, lua_table,
library, class. Nothing is renamed; this is a lookup table.
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import target as target_mod  # noqa: E402

TARGET = target_mod.load()


def rule(r):
    n = r["proposed"]
    if n.startswith("Lua_"):
        return "lua_binding"
    if n.startswith("std_"):
        return "stl_shape"
    if "::vf" in n:
        return "vtable"
    if any(e.startswith("method string") for e in r["evidence"]):
        return "method_string"
    return "log_message"


def main():
    rows = []
    for line in open(TARGET.out / "names.jsonl"):
        r = json.loads(line)
        if not r["proposed"] or r["confidence"] not in ("high", "medium") or r.get("folded"):
            continue
        mod = r.get("module") or ""
        rows.append([r["addr"], r["name"], r["proposed"], r["confidence"], rule(r),
                     mod[4:] if mod.startswith("lua:") else "", r.get("library") or "",
                     r.get("class") or ""])
    out = TARGET.root / "docs/re/NAMES_EVIDENCE.tsv"
    with open(out, "w") as w:
        w.write("addr\tcurrent_name\tproposed_name\tconfidence\trule\tlua_table\tlibrary\tclass\n")
        for row in sorted(rows, key=lambda x: int(x[0], 16)):
            w.write("\t".join(row) + "\n")
    print(f"{len(rows)} rows -> {out.relative_to(TARGET.root)}")


if __name__ == "__main__":
    main()
