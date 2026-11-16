#!/usr/bin/env python3
"""Mechanical mutants of a leaf function's ORIGINAL body (plans/THESEUS_BATCH.md, B1).

  python3 tools/theseus/mutate.py FNV1Hash_821F3C28            # kill rate with its rules
  python3 tools/theseus/mutate.py NAME --ablate fval,trap,loguniform
  python3 tools/theseus/mutate.py NAME --list                  # show mutants, don't run

Why the original and not the rewrite: a mutant of the original is a function that
differs from it in one small way. If difftest (with this function's rules) can't
tell the mutant from the original, the test inputs don't reach that spot, so a
rewrite with the same mistake would pass too. Survivors are coverage gaps or
equivalent mutants (dead code, a compare whose result is never used); both are
listed for a look. This replaces the pilot's hand-written negative controls.

All mutants go into one binary (built once, ~25 s); each runs `--cases` cases
and stops at the first difference. A mutant that hangs or crashes counts as
killed (the original returns normally on the same inputs).

Operators (one change per mutant, on the recompiled C++ lines):
  neg    if (ctx.crN.x) <-> if (!ctx.crN.x)           branch taken the other way
  bound  < <-> <=, > <-> >=  (lt <-> !gt, gt <-> !lt) off-by-one at a boundary;
         fsel x >= 0 -> x > 0
  sign   compare<uint32_t> <-> compare<int32_t> (64 too); skipped when only .eq is used
  imm    compare / add immediate +1 (or -1 for big values)
  field  load/store displacement +4 (neighbouring field)
  store  drop one store
  trap   drop one ppc_trap (debug trap)
  fuse   std::fma(a, b, c) -> a * b + c (unfused)
  del    delete one other assignment statement (not an XER carry nobody reads)
Each operator is capped (--per-op) with a fixed seed so runs are repeatable.

Writes out/theseus/mutants/<name>[_ablate]/: mutants.cpp, result.json, build.log.
"""
import argparse
import json
import os
import random
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import difftest  # noqa: E402

TARGET = difftest.TARGET
ABLATE = {"trap": "NO_TRAP_COUNT", "loguniform": "NO_LOG_UNIFORM", "fval": None}

COND = re.compile(r"^(\s*if \()(!?)(ctx\.cr\d\.(?:lt|gt|eq|so))(\).*)$")
CMP = re.compile(r"compare<(u?int(?:32|64)_t)>")
CMP_IMM = re.compile(r"(compare<[^>]+>\([^,]+, )(-?\d+)(, ctx\.xer\))")
ADD_IMM = re.compile(r"^(\s*ctx\.r\d+\.s64 = ctx\.r\d+\.s64 \+ )(-?\d+)(;)$")
DISP = re.compile(r"(REX_(?:LOAD|STORE)_\w+\(ctx\.r\d+\.u32 \+ )(-?\d+)(\))")
STORE = re.compile(r"^\s*REX_STORE_\w+\(.*\);\s*$")
TRAP = re.compile(r"^\s*ppc_trap\(.*\);\s*$")
FMA = re.compile(r"std::fma\(([^,()]+), ([^,()]+), ([^,()]+)\)")
FSEL = re.compile(r"(ctx\.f\d+\.f64) >= 0\.0 \?")
XER_SET = re.compile(r"^\s*ctx\.xer\.(ca|ov|so) = ")
ASSIGN = re.compile(r"^\s*ctx\.\w+(?:\.\w+)? = .*;\s*$")


def xer_read_later(body, i):
    """Does any later line read XER's carry/overflow (adde, subfe, ...)? If not,
    deleting a carry update is an equivalent mutant (XER is a scratch register)."""
    return any(re.search(r"=.*ctx\.xer\.(ca|ov)\b", l) for l in body[i + 1:])


def eq_only(body, i, line):
    """True if the CR field this compare sets is only ever tested for .eq afterwards
    (then signed vs unsigned can't matter: an equivalent mutant, not worth running)."""
    cr = re.search(r"ctx\.(cr\d)\.compare", line)
    if not cr:
        return False
    flags = set()
    for later in body[i + 1:]:
        if re.search(r"ctx\." + cr.group(1) + r"\.compare", later):
            break
        flags |= set(re.findall(r"ctx\." + cr.group(1) + r"\.(lt|gt|eq|so)\b", later))
    return flags <= {"eq"}


def mutants_of(body):
    """All single-change mutants: list of (op, line_no, new_line or None=delete, desc)."""
    out = []
    for i, line in enumerate(body):
        s = line.strip()
        if i == 0 or not s or s.startswith("//") or s.startswith("REX_FUNC_PROLOGUE") or s == "}":
            continue
        m = COND.match(line)
        if m:
            pre, bang, flag, rest = m.groups()
            out.append(("neg", i, f"{pre}{'' if bang else '!'}{flag}{rest}", f"{bang}{flag} -> {'' if bang else '!'}{flag}"))
            swap = {("", "lt"): "!gt", ("!", "gt"): "lt", ("", "gt"): "!lt", ("!", "lt"): "gt"}
            reg, cond = flag.rsplit(".", 1)
            if (bang, cond) in swap:
                new = swap[(bang, cond)]
                nb, nc = ("!", new[1:]) if new.startswith("!") else ("", new)
                out.append(("bound", i, f"{pre}{nb}{reg}.{nc}{rest}", f"{bang}{cond} -> {new}"))
            continue
        m = CMP.search(line)
        if m and not eq_only(body, i, line):
            t = m.group(1)
            other = t[1:] if t.startswith("u") else "u" + t
            out.append(("sign", i, line.replace(f"compare<{t}>", f"compare<{other}>", 1), f"{t} -> {other}"))
        m = CMP_IMM.search(line)
        if m:
            v = int(m.group(2))
            nv = v + 1 if abs(v) < 0x7FFF else v - 1
            out.append(("imm", i, line[:m.start(2)] + str(nv) + line[m.end(2):], f"compare imm {v} -> {nv}"))
        m = ADD_IMM.match(line)
        if m:
            v = int(m.group(2))
            out.append(("imm", i, f"{m.group(1)}{v + 1}{m.group(3)}", f"add imm {v} -> {v + 1}"))
        m = DISP.search(line)
        if m:
            v = int(m.group(2))
            out.append(("field", i, line[:m.start(2)] + str(v + 4) + line[m.end(2):], f"displacement {v} -> {v + 4}"))
        if FSEL.search(line):
            out.append(("bound", i, FSEL.sub(r"\1 > 0.0 ?", line, count=1), "fsel >= 0 -> > 0"))
        if STORE.match(line):
            out.append(("store", i, None, "drop store"))
        elif TRAP.match(line):
            out.append(("trap", i, None, "drop trap"))
        elif ASSIGN.match(line) and not (XER_SET.match(line) and not xer_read_later(body, i)):
            out.append(("del", i, None, "delete statement"))
        m = FMA.search(line)
        if m:
            a, b, c = m.groups()
            out.append(("fuse", i, line[:m.start()] + f"({a} * {b} + {c})" + line[m.end():], "unfuse fma"))
    return out


def pick(muts, per_op, seed):
    rng = random.Random(seed)
    by_op = {}
    for mu in muts:
        by_op.setdefault(mu[0], []).append(mu)
    chosen = []
    for op in sorted(by_op):
        group = by_op[op]
        chosen += group if len(group) <= per_op else sorted(rng.sample(group, per_op), key=lambda m: m[1])
    return chosen


def source_line(body, idx):
    """The PPC instruction comment above a generated line, for reports."""
    for j in range(idx - 1, max(idx - 4, 0), -1):
        s = body[j].strip()
        if s.startswith("//"):
            return s[2:].strip()
    return ""


def mutants_cpp(name, body, chosen):
    parts = ['// Generated by tools/theseus/mutate.py: one change per function.',
             '#include "fable_2_pch.h"', '']
    for k, (op, idx, new, desc) in enumerate(chosen):
        lines = list(body)
        if new is None:
            lines[idx] = f"\t/* mutant {k}: {desc} */"
        else:
            lines[idx] = new + f"  /* mutant {k}: {desc} */"
        lines[0] = f'extern "C" REX_FUNC(mutant_{k}) {{'
        parts += lines + [""]
    parts.append("using GuestFn = void (*)(PPCContext&, uint8_t*);")
    parts.append('extern "C" const GuestFn kMutantTable[] = {'
                 + ", ".join(f"mutant_{k}" for k in range(len(chosen))) + "};")
    parts.append(f'extern "C" const int kMutantCount = {len(chosen)};')
    return "\n".join(parts) + "\n"


def run_one(exe, k, cases, seed, timeout):
    """timeout is in CPU seconds (difftest.run_limited): a busy machine slows a run
    down but can't make it look hung. The wall-clock backstop gives "inconclusive"."""
    env = dict(os.environ, DIFFTEST_STOP="1")
    status, code, stdout, _ = difftest.run_limited([str(exe), str(cases), seed, str(k)], timeout, env=env)
    if status == "cpu":
        return k, "killed", "timeout (hang: used up its CPU seconds)"
    if status == "wall":
        return k, "inconclusive", "wall-clock backstop fired (machine busy?): rerun"
    if code == 0:
        return k, "survived", ""
    if code == 1:
        why = [l for l in stdout.splitlines() if l.startswith(("first ABI failure", "abi_mismatches",
                                                                "trap_or_log", "host_mxcsr", "touched_page"))]
        return k, "killed", "; ".join(why)[:300]
    return k, "killed", f"crash (exit {code})"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("name")
    ap.add_argument("--cases", type=int, default=20000)
    ap.add_argument("--seed", default="0x9E3779B97F4A7C15")
    ap.add_argument("--per-op", type=int, default=8)
    ap.add_argument("--ablate", default="", help=f"comma list of {','.join(ABLATE)}: turn a harness feature off")
    ap.add_argument("--timeout", type=float, default=30.0)
    ap.add_argument("-j", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()

    ablate = [a for a in args.ablate.split(",") if a]
    for a in ablate:
        if a not in ABLATE:
            sys.exit(f"unknown --ablate {a!r}; choose from {', '.join(ABLATE)}")
    out = TARGET.out / "mutants" / (args.name + ("_" + "_".join(ablate) if ablate else ""))
    _, body = difftest.write_original(args.name, out)
    chosen = pick(mutants_of(body), args.per_op, args.name)
    if args.list:
        for k, (op, idx, new, desc) in enumerate(chosen):
            print(f"{k:3d} {op:<6} {desc:<32} | {source_line(body, idx)}")
        return
    if not chosen:
        sys.exit(f"{args.name}: no mutants (nothing to change)")
    (out / "mutants.cpp").write_text(mutants_cpp(args.name, body, chosen))

    rules = HERE / "rewrites" / f"{args.name}.rules"
    if "fval" in ablate and rules.exists():
        kept = [l for l in rules.read_text().splitlines() if not l.split("#", 1)[0].split()[:1] == ["fval"]]
        rules = out / "ablated.rules"
        rules.write_text("\n".join(kept) + "\n")
    defines = ["MUTANTS"] + [ABLATE[a] for a in ablate if ABLATE[a]]
    exe = difftest.build(args.name, out, [out / "mutants.cpp"], defines, rules)

    with ThreadPoolExecutor(args.j) as pool:
        results = sorted(pool.map(lambda k: run_one(exe, k, args.cases, args.seed, args.timeout),
                                  range(len(chosen))))
    rows, per_op = [], {}
    for k, verdict, why in results:
        op, idx, _, desc = chosen[k]
        rows.append({"k": k, "op": op, "desc": desc, "ppc": source_line(body, idx),
                     "line": idx, "verdict": verdict, "why": why})
        t = per_op.setdefault(op, [0, 0])
        t[0] += verdict == "killed"
        t[1] += 1
    killed = sum(r["verdict"] == "killed" for r in rows)
    inconclusive = sum(r["verdict"] == "inconclusive" for r in rows)
    summary = {"name": args.name, "ablate": ablate, "cases": args.cases, "mutants": len(rows),
               "killed": killed, "inconclusive": inconclusive, "kill_rate": round(killed / len(rows), 3),
               "per_op": {op: f"{a}/{b}" for op, (a, b) in sorted(per_op.items())}, "rows": rows}
    (out / "result.json").write_text(json.dumps(summary, indent=1) + "\n")
    print(f"{args.name}{' (ablate ' + ','.join(ablate) + ')' if ablate else ''}: "
          f"killed {killed}/{len(rows)} ({summary['kill_rate']:.0%})  "
          + "  ".join(f"{op} {v}" for op, v in summary["per_op"].items()))
    if inconclusive:
        print(f"  {inconclusive} inconclusive (wall-clock backstop; machine busy?): rerun")
    for r in rows:
        if r["verdict"] == "survived":
            print(f"  survived #{r['k']:<3} {r['op']:<6} {r['desc']:<30} | {r['ppc']}")


if __name__ == "__main__":
    main()
