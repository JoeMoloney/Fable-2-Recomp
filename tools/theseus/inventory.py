#!/usr/bin/env python3
"""Build a function inventory from the recompiled guest code (read-only).

Scans generated/default/fable_2_recomp.*.cpp, the manifest and the hotfunc
overrides, and writes:
  out/theseus/functions.jsonl   one JSON record per guest function
  out/theseus/summary.json      totals used by docs/analysis/THESEUS_INVENTORY.md

Static text analysis only: call counts are call *sites*, not how often a call
runs, and "leaf" means "no direct, indirect or kernel calls in the body".

Usage: python3 tools/theseus/inventory.py   (THESEUS_TARGET=<toml> for another game)
"""
import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import target as target_mod  # noqa: E402

TARGET = target_mod.load()
ROOT = TARGET.root
GEN = TARGET.generated
OUT = TARGET.out

FUNC_START = re.compile(r"^DEFINE_REX_FUNC\((\w+)\)\s*\{")
CALL = re.compile(r"^\s*(\w+)\(ctx, base\);")
ADDR_SUFFIX = re.compile(r"_([0-9A-F]{8})$")
SAVE_REST = re.compile(r"^__(save|rest)(gpr|fpr|vmx)")
HOOK_CALL = re.compile(r"\b(fable2_hook_\w+)\(")


MANIFEST_ADDRS = {}


def load_manifest_addrs():
    """Names without an address suffix (e.g. FrontMenu_EnsureElement) get their
    address from the manifest's [entrypoint.functions."0xADDR"] tables."""
    addr = None
    for line in TARGET.manifest.open(encoding="utf-8", errors="replace"):
        m = re.match(r'\[entrypoint\.functions\."0x([0-9A-Fa-f]+)"\]', line)
        if m:
            addr = int(m.group(1), 16)
            continue
        if addr is not None:
            n = re.match(r'name\s*=\s*"([^"]+)"', line)
            if n:
                MANIFEST_ADDRS[n.group(1)] = addr
                addr = None
            elif line.startswith("["):
                addr = None


def guest_addr(name):
    m = ADDR_SUFFIX.search(name)
    return int(m.group(1), 16) if m else MANIFEST_ADDRS.get(name)


def parse_generated():
    funcs = {}
    for path in TARGET.partitions():
        cur = None
        for line in path.open(encoding="utf-8", errors="replace"):
            m = FUNC_START.match(line)
            if m:
                name = m.group(1)
                cur = funcs[name] = {
                    "name": name, "addr": guest_addr(name), "file": path.name,
                    "insns": 0, "labels": 0, "calls": Counter(), "imports": Counter(),
                    "indirect": 0, "save_restore": False, "fp": 0, "vmx": 0,
                    "loads": 0, "stores": 0, "global_lock": 0, "timebase": 0,
                    "hooks": set(),
                }
                continue
            if cur is None:
                continue
            if line.startswith("}"):
                cur = None
                continue
            s = line.strip()
            if s.startswith("// "):
                cur["insns"] += 1
                continue
            if s.startswith("loc_") and s.endswith(":"):
                cur["labels"] += 1
                continue
            c = CALL.match(line)
            if c:
                callee = c.group(1)
                if callee.startswith("__imp__"):
                    cur["imports"][callee[7:]] += 1
                elif SAVE_REST.match(callee):
                    cur["save_restore"] = True
                else:
                    cur["calls"][callee] += 1
            if "REX_CALL_INDIRECT_FUNC" in s:
                cur["indirect"] += 1
            if "ctx.f" in s:
                cur["fp"] += 1
            if "ctx.v" in s:
                cur["vmx"] += 1
            if "REX_LOAD_" in s:
                cur["loads"] += 1
            if "REX_STORE_" in s:
                cur["stores"] += 1
            if "REX_ENTER_GLOBAL_LOCK" in s:
                cur["global_lock"] += 1
            if "REX_QUERY_TIMEBASE" in s:
                cur["timebase"] += 1
            for h in HOOK_CALL.findall(s):
                cur["hooks"].add(h)
    return funcs


def hotfunc_overrides():
    """Each override file is named after the guest function it replaces."""
    names = {}
    for p in sorted(TARGET.hotfunc_dir.rglob("*.cpp")):
        if re.search(r'extern "C" void ' + re.escape(p.stem) + r"\(", p.read_text(errors="replace")):
            names[p.stem] = str(p.relative_to(ROOT))
    return names


def levels(funcs):
    """Bottom-up level: 0 = leaf, else 1 + max(callee level). Cycles are cut."""
    memo, onstack = {}, set()
    sys.setrecursionlimit(100000)

    def lvl(n):
        if n in memo:
            return memo[n]
        if n in onstack or n not in funcs:
            return 0
        onstack.add(n)
        callees = funcs[n]["calls"]
        v = 0 if not callees and not funcs[n]["indirect"] and not funcs[n]["imports"] \
            else 1 + max((lvl(c) for c in callees), default=0)
        onstack.discard(n)
        memo[n] = v
        return v

    for n in funcs:
        lvl(n)
    return memo


def main():
    load_manifest_addrs()
    funcs = parse_generated()
    hot = hotfunc_overrides()
    callers = defaultdict(Counter)
    for n, f in funcs.items():
        for c, k in f["calls"].items():
            callers[c][n] += k
    lv = levels(funcs)

    OUT.mkdir(parents=True, exist_ok=True)
    named = lambda n: not n.startswith("sub_")
    stats = Counter()
    by_file_size = []
    with (OUT / "functions.jsonl").open("w") as out:
        for n, f in sorted(funcs.items(), key=lambda kv: kv[1]["addr"] or 0):
            leaf = not f["calls"] and not f["imports"] and not f["indirect"]
            rec = {
                "name": n, "addr": f"0x{f['addr']:08X}" if f["addr"] else None,
                "named": named(n), "file": f["file"], "insns": f["insns"],
                "branches": f["labels"], "leaf": leaf, "level": lv.get(n, 0),
                "callees": sorted(f["calls"]), "callers": sorted(callers.get(n, {})),
                "n_callers": len(callers.get(n, {})),
                "imports": sorted(f["imports"]), "indirect_calls": f["indirect"],
                "uses_fp": f["fp"] > 0, "uses_vmx": f["vmx"] > 0,
                "loads": f["loads"], "stores": f["stores"],
                "global_lock": f["global_lock"] > 0, "timebase": f["timebase"] > 0,
                "midasm_hooks": sorted(f["hooks"]), "hotfunc": hot.get(n),
                "named_callers": sorted(c for c in callers.get(n, {}) if named(c))[:8],
            }
            out.write(json.dumps(rec) + "\n")
            stats["functions"] += 1
            stats["no_address"] += rec["addr"] is None
            stats["insns"] += f["insns"]
            stats["named"] += rec["named"]
            stats["leaf"] += leaf
            stats["leaf_named"] += leaf and rec["named"]
            stats["no_callers"] += rec["n_callers"] == 0
            stats["indirect_sites"] += f["indirect"]
            stats["uses_fp"] += rec["uses_fp"]
            stats["uses_vmx"] += rec["uses_vmx"]
            stats["imports_any"] += bool(f["imports"])
            stats["hotfunc"] += rec["hotfunc"] is not None
            stats["hooked"] += bool(f["hooks"])
            for b, lim in (("tiny<=8", 8), ("small<=32", 32), ("medium<=128", 128),
                           ("large<=512", 512), ("huge>512", 10**9)):
                if f["insns"] <= lim:
                    stats["size_" + b] += 1
                    break
            by_file_size.append((f["insns"], n))

    lvl_hist = Counter(min(v, 10) for v in lv.values())
    imports = Counter()
    for f in funcs.values():
        imports.update(f["imports"])
    summary = {
        "totals": dict(stats),
        "level_histogram_capped10": {str(k): lvl_hist[k] for k in sorted(lvl_hist)},
        "largest": [{"name": n, "insns": i} for i, n in sorted(by_file_size, reverse=True)[:15]],
        "most_called": [{"name": n, "callers": len(c)} for n, c in
                        sorted(callers.items(), key=lambda kv: -len(kv[1]))[:25]],
        "top_imports": imports.most_common(25),
        "hotfunc_overrides": hot,
        "missing_hotfunc_targets": sorted(set(hot) - set(funcs)),
    }
    (OUT / "summary.json").write_text(json.dumps(summary, indent=1))
    print(json.dumps(summary["totals"], indent=1))


if __name__ == "__main__":
    main()
