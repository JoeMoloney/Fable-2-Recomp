#!/usr/bin/env python3
"""The Theseus gate: one command per function (plans/THESEUS_BATCH.md, unit B2).

  python3 tools/theseus/gate.py NAME [NAME ...] [-j N] [--no-ledger]

Runs, for each function (rewrite in tools/theseus/rewrites/<name>.cpp):

  1. difftest     difftest.py at 200k cases with --timeout: must be PASS-ABI or PASS-STRICT.
  2. original     mutate.py on the ORIGINAL (20k cases; 200k when the body uses
     mutants     floats/vectors, unfused fmas need it). No kill-rate threshold: every
                 survivor is listed with a pre-label, "likely equivalent: <why>" or
                 "review". Survivors don't fail the gate; they go to the reviewer.
  3. rewrite      single changes to the REWRITE (< <-> <=, > <-> >=, std::fma unfused,
     mutants     integer literal +-1, NaN/sign guard dropped). A survivor means the tests
                 can't see that mistake in the rewrite: gate-fail. If one is truly
                 equivalent, mark its line `// gate: equivalent <reason>` (the line's
                 mutants are then skipped and the reason is recorded in gate.json).
  4. callers      caller_usage.py: a call site may read a scratch register before writing it
                 (the original never touched it and the compiler knew); then the rewrite must
                 leave that register equal (difftest's differing_fields). No direct callers
                 (tier D): every scratch register must match.
  5. override     compiles the src/core/hotfunc/theseus/ wrapper that ship.py will write,
                 with the game's Release flags (from out/build/linux-amd64-release/build.ninja).

Writes out/theseus/batch/<name>/gate.json and a ledger row (status gated / gate-fail;
rows already reviewed/shipped/rejected keep their status, only gate and kill_rate
update). Exit code 0 when every function passed.

-j N gates N functions at once; each one's mutant runs share the remaining cores.
"""
import argparse
import datetime
import json
import os
import random
import re
import shlex
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import batch_pool  # noqa: E402
import difftest  # noqa: E402
import mutate  # noqa: E402

TARGET = difftest.TARGET
ROOT = TARGET.root
BUILD_DIR = ROOT / "out" / "build" / "linux-amd64-release"
KEEP_STATUS = {"reviewed", "shipped", "rejected"}

# ---------------------------------------------------------------- rewrite mutants

MARK = re.compile(r"//\s*gate:\s*equivalent\b[:\s]*(.*)$")
REL = re.compile(r"(?<=\s)(<=|>=|<|>)(?=\s)")
INT_LIT = re.compile(r"(?<![\w.'\"])(0[xX][0-9a-fA-F]+|\d+)([uUlL]*)(?![\w.'\"])")
ARRAY_DECL = re.compile(r"\w\s*\[\s*$")       # literal is an array size: kSin[8] = {...}
MEM_SIZE = re.compile(r"\bmem(?:cpy|set|move)\([^;]*$|\bsizeof\([^)]*$")  # a size: +1 is just overflow UB
GUARD = re.compile(r"\bif \((?=[^;]*\b(?:std::)?(?:isnan|isinf|isfinite|signbit)\()")
FMA_CALL = "std::fma("


def code_part(line):
    """The line without its // comment (string literals don't occur in rewrites)."""
    i = line.find("//")
    return line if i < 0 else line[:i]


def split_args(text, start):
    """Parse 'a, b, c)' starting at text[start]; returns ([a, b, c], index after ')')."""
    args, depth, cur = [], 0, start
    for i in range(start, len(text)):
        ch = text[i]
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            if depth == 0:
                args.append(text[cur:i].strip())
                return args, i + 1
            depth -= 1
        elif ch == "," and depth == 0:
            args.append(text[cur:i].strip())
            cur = i + 1
    return None, None


def rewrite_mutants(lines):
    """All single-change mutants of a rewrite: (op, line_no, new_line, desc). Lines marked
    `// gate: equivalent` are skipped; returns (mutants, {line_no: reason})."""
    out, marked = [], {}
    in_block_comment = False
    for i, line in enumerate(lines):
        s = line.strip()
        if in_block_comment:
            in_block_comment = "*/" not in s
            continue
        if s.startswith("/*"):
            in_block_comment = "*/" not in s
            continue
        if not s or s.startswith(("//", "#", "namespace", "}  // namespace")) or "REX_FUNC(" in s:
            continue
        m = MARK.search(line)
        if m:
            marked[i] = m.group(1).strip() or "(no reason given)"
            continue
        code = code_part(line)
        tail = line[len(code):]
        if "template" in code or "static_assert" in code:
            continue
        for m in REL.finditer(code):
            op = m.group(1)
            new = {"<": "<=", "<=": "<", ">": ">=", ">=": ">"}[op]
            out.append(("bound", i, code[:m.start()] + new + code[m.end():] + tail, f"{op} -> {new}"))
        for m in INT_LIT.finditer(code):
            if ARRAY_DECL.search(code[:m.start()]) or MEM_SIZE.search(code[:m.start()]):
                continue
            text, suf = m.groups()
            v = int(text, 0)
            if v > 0xFFFFFFFF:
                # A 64-bit constant is usually a double's bits: +-1 in the last mantissa
                # bit changes no test result. Flip the top mantissa bit instead (a typo
                # in the first digit after the exponent).
                nv = v ^ (1 << 51)
            else:
                nv = v + 1 if v < 0xFFFFFFFF else v - 1
            if text.lower().startswith("0x"):
                digits = f"{nv:0{len(text) - 2}x}"
                lit = text[:2] + (digits.upper() if text[2:] != text[2:].lower() else digits) + suf
            else:
                lit = str(nv) + suf
            out.append(("imm", i, code[:m.start()] + lit + code[m.end():] + tail, f"literal {text} -> {lit}"))
        pos = code.find(FMA_CALL)
        while pos >= 0:
            args, end = split_args(code, pos + len(FMA_CALL))
            if args and len(args) == 3:
                a, b, c = args
                new = code[:pos] + f"(({a}) * ({b}) + ({c}))" + code[end:]
                out.append(("fuse", i, new + tail, "unfuse std::fma"))
            pos = code.find(FMA_CALL, pos + 1)
        m = GUARD.search(code)
        if m:
            new = code[:m.end()] + "false && " + code[m.end():]
            out.append(("guard", i, new + tail, "drop NaN/sign guard"))
    return out, marked


def rewrite_mutants_cpp(name, lines, chosen):
    """One TU: each mutant is a copy of the rewrite in its own namespace, with
    rewrite_<name> renamed to mutant_<k>. #include lines are hoisted to the top."""
    includes = [l for l in lines if l.lstrip().startswith("#include")]
    rest = [l for l in lines if not l.lstrip().startswith("#include")]
    parts = ["// Generated by tools/theseus/gate.py: mutants of the rewrite, one change each.",
             '#include "fable_2_pch.h"', *includes, ""]
    for k, (op, idx, new, desc) in enumerate(chosen):
        copy = list(lines)
        copy[idx] = new + f"  /* rewrite mutant {k}: {desc} */"
        copy = [l for l in copy if not l.lstrip().startswith("#include")]
        body = "\n".join(copy).replace(f"rewrite_{name}", f"mutant_{k}")
        parts += [f"namespace theseus_mutant_{k} {{", body, f"}}  // namespace theseus_mutant_{k}", ""]
    parts.append("using GuestFn = void (*)(PPCContext&, uint8_t*);")
    parts.append('extern "C" const GuestFn kMutantTable[] = {'
                 + ", ".join(f"theseus_mutant_{k}::mutant_{k}" for k in range(len(chosen))) + "};")
    parts.append(f'extern "C" const int kMutantCount = {len(chosen)};')
    del rest
    return "\n".join(parts) + "\n"


def run_rewrite_mutants(name, cases, per_op, jobs, seed):
    src = HERE / "rewrites" / f"{name}.cpp"
    lines = src.read_text().splitlines()
    muts, marked = rewrite_mutants(lines)
    rng = random.Random(name + ":rewrite")
    by_op = {}
    for mu in muts:
        by_op.setdefault(mu[0], []).append(mu)
    chosen = []
    for op in sorted(by_op):
        g = by_op[op]
        chosen += g if len(g) <= per_op else sorted(rng.sample(g, per_op), key=lambda m: m[1])
    result = {"mutants": len(chosen), "available": len(muts), "killed": 0, "survivors": [],
              "marked_equivalent": [{"line": i + 1, "code": lines[i].strip(), "reason": r}
                                    for i, r in sorted(marked.items())]}
    if not chosen:
        return result
    out = TARGET.out / "mutants" / f"{name}_rewrite"
    difftest.write_original(name, out)
    # A mutant that doesn't compile (e.g. an intrinsic immediate out of range) is a
    # mistake the compiler already catches: drop it, count it as killed, rebuild.
    no_compile = []
    for _ in range(4):
        text = rewrite_mutants_cpp(name, lines, chosen)
        (out / "mutants.cpp").write_text(text)
        try:
            exe = difftest.build(name, out, [out / "mutants.cpp"], ["MUTANTS"])
            break
        except SystemExit:
            log = (out / "build.log").read_text()
            bad_lines = {int(n) for n in re.findall(r"mutants\.cpp:(\d+):\d+: error", log)}
            starts = [i + 1 for i, l in enumerate(text.splitlines()) if l.startswith("namespace theseus_mutant_")]
            bad = {max(k for k, st in enumerate(starts) if st <= ln) for ln in bad_lines if ln >= starts[0]}
            if not bad:
                raise
            no_compile += [{"op": chosen[k][0], "desc": chosen[k][3], "line": chosen[k][1] + 1} for k in bad]
            chosen = [c for k, c in enumerate(chosen) if k not in bad]
            if not chosen:
                break
    else:
        raise SystemExit("rewrite mutants still don't build after dropping the broken ones")
    result["no_compile"] = no_compile
    result["mutants"] = len(chosen) + len(no_compile)
    if not chosen:
        result["killed"] = len(no_compile)
        return result
    with ThreadPoolExecutor(jobs) as pool:
        runs = sorted(pool.map(lambda k: mutate.run_one(exe, k, cases, seed, 60.0), range(len(chosen))))
    per = {}
    for k, verdict, why in runs:
        op, idx, _, desc = chosen[k]
        t = per.setdefault(op, [0, 0])
        t[0] += verdict == "killed"
        t[1] += 1
        if verdict == "inconclusive":
            result.setdefault("inconclusive", 0)
            result["inconclusive"] += 1
        if verdict == "survived":
            result["survivors"].append({"k": k, "op": op, "desc": desc, "line": idx + 1,
                                        "code": lines[idx].strip()})
    result["killed"] = len(chosen) + len(no_compile) - len(result["survivors"]) - result.get("inconclusive", 0)
    result["per_op"] = {op: f"{a}/{b}" for op, (a, b) in sorted(per.items())}
    (out / "result.json").write_text(json.dumps(result, indent=1) + "\n")
    return result

# ---------------------------------------------------------------- original-mutant labels


def last_assignment(body, idx, reg):
    pat = re.compile(r"^\s*ctx\." + reg + r"\.\w+ = (.*);")
    for j in range(idx - 1, 0, -1):
        m = pat.match(body[j])
        if m:
            return m.group(1)
    return None


def label_survivor(row, body):
    """Pre-label a surviving mutant of the original (B1's equivalent kinds); a hint only."""
    line = body[row["line"]]
    op = row["op"]
    if op == "sign":
        m = re.search(r"compare<[^>]+>\(ctx\.(r\d+)\.\w+, (ctx\.(r\d+)\.\w+|-?\d+)", line)
        if m and m.group(3):
            return "likely equivalent: sign swap between two registers (pointers into one object?)"
        if m:
            src = last_assignment(body, row["line"], m.group(1)) or ""
            if re.search(r"REX_LOAD_U(8|16)\(|& 255\b|& 0xFF\b", src):
                return "likely equivalent: sign swap on a byte/halfword value"
            if re.search(r"& 0x[0-7]?[0-9A-Fa-f]{1,6}\b;?$", src):
                return "likely equivalent: sign swap on a value cut to a small bit field"
            reg = m.group(1)
            sets = [a.group(1) for l in body if (a := re.match(r"^\s*ctx\." + reg + r"\.\w+ = (.*);", l))]
            if sets and all(re.fullmatch(r"-?\d+|ctx\." + reg + r"\.s64 [+-] -?\d+", s) for s in sets):
                return "likely equivalent: sign swap on a small counter"
            return "review: sign swap against an immediate (equivalent if the value never reaches the sign bit)"
    if op == "del":
        s = line.strip()
        if "__builtin_rotateleft32(" in s and s.endswith(", 0);"):
            return "likely equivalent: deleted zero-extend (rotlwi 0)"
        if re.match(r"^ctx\.(r\d+)\.u64 = ctx\.\1\.u32;$", s):
            return "likely equivalent: deleted zero-extend"
        if s.startswith("ctx.r") and "REX_LOAD_" in s and sum(l.strip() == s for l in body) > 1:
            return "likely equivalent: deleted reload of a value loaded earlier"
        if re.match(r"^ctx\.(r\d+)\.u64 = ctx\.\1\.u32 & (0xFF|0xFFFF|255|65535);$", s):
            return "likely equivalent: deleted zero-extend (clrlwi) of a value that is already small"
        if re.match(r"^ctx\.(r0|r12|ctr)\.", s) and any(l.strip().startswith("switch (") for l in body[row["line"]:row["line"] + 12]):
            return "likely equivalent: jump-table address (the recompiled switch doesn't use ctr)"
    if op == "imm" and re.match(r"^\s*ctx\.(r0|r12)\.", line) and any(
            l.strip().startswith("switch (") for l in body[row["line"]:row["line"] + 12]):
        return "likely equivalent: jump-table address (the recompiled switch doesn't use ctr)"
    if op in ("field", "imm") and re.search(r"ctx\.r1\.", line):
        return "likely equivalent: stack red-zone offset"
    if op == "fuse":
        return "review: unfused fma survived (rare difference; more cases or fval edge values?)"
    return "review"

# ---------------------------------------------------------------- caller check


def caller_check(name, differing):
    """differing: scratch registers the rewrite leaves different from the original
    (difftest's differing_fields; empty for PASS-STRICT). A caller may read a scratch
    register the original never touches (the compiler knew the callee); that is fine
    only if the rewrite leaves that register equal too."""
    r = subprocess.run([sys.executable, str(HERE / "caller_usage.py"), name], capture_output=True, text=True)
    text = r.stdout
    sites = int(re.search(r": (\d+) call sites", text).group(1))
    scratch_part = text.split("scratch registers read before written", 1)[1]
    scratch = {m.group(2): int(m.group(1)) for m in re.finditer(r"^\s+(\d+)\s+(\w+)\s+e\.g\.", scratch_part, re.M)}
    r3_part = text.split("r3 after the call:", 1)[1].split("scratch registers", 1)[0]
    r3 = {m.group(2).strip(): int(m.group(1)) for m in re.finditer(r"^\s+(\d+)\s+(.+?)\s+e\.g\.", r3_part, re.M)}
    unclear = sum(v for k, v in r3.items() if k.startswith("unclear"))
    bad = sorted(set(scratch) & set(differing))
    if sites == 0:
        ok = not differing
        why = "no direct callers: needs identical scratch registers" + (
            "" if ok else " (differ: " + ", ".join(sorted(differing)) + ")")
    elif bad:
        ok, why = False, "callers read scratch registers the rewrite changes: " + ", ".join(
            f"{k} ({scratch[k]} sites)" for k in bad)
    elif scratch:
        ok, why = True, ("callers read " + ", ".join(f"{k} x{v}" for k, v in scratch.items())
                         + " after the call, but the rewrite leaves them equal")
    else:
        ok, why = True, "no caller reads a scratch register first"
    if unclear and sites:
        why += f" ({unclear}/{sites} sites only scanned up to a branch: linear scan)"
    return {"ok": ok, "sites": sites, "r3": r3, "scratch": scratch, "differing": sorted(differing), "note": why}

# ---------------------------------------------------------------- override build


def override_wrapper(name, rewrite_include):
    return (f"// Theseus override of {name}: runs the verified rewrite\n"
            f"// tools/theseus/rewrites/{name}.cpp (see its header for what it is and\n"
            f"// how it was tested). Do not edit the logic here; edit the rewrite and rerun\n"
            f"//   python3 tools/theseus/gate.py {name}\n"
            f'#include "theseus_switch.h"\n'
            f'#include "{rewrite_include}"\n\n'
            f"THESEUS_OVERRIDE({name})\n")


def game_compile_flags():
    """Defines, includes and flags the Release build uses for a theseus/ override."""
    ninja = BUILD_DIR / "build.ninja"
    if not ninja.exists():
        return None
    lines = ninja.read_text(errors="replace").splitlines()
    for i, l in enumerate(lines):
        if l.startswith("build ") and "/hotfunc/theseus/" in l and ".cpp.o:" in l:
            vals = {}
            for l2 in lines[i + 1:i + 12]:
                m = re.match(r"^\s+(DEFINES|FLAGS|INCLUDES) = (.*)$", l2)
                if m:
                    vals[m.group(1)] = shlex.split(m.group(2))
            return vals.get("DEFINES", []) + vals.get("INCLUDES", []) + vals.get("FLAGS", [])
    return None


def override_check(name, out):
    flags = game_compile_flags()
    if flags is None:
        return {"ok": None, "note": f"skipped: no Release build in {BUILD_DIR}"}
    wrapper = out / "override_check.cpp"
    wrapper.write_text(override_wrapper(name, str(HERE / "rewrites" / f"{name}.cpp")))
    r = subprocess.run(["clang++", *flags, "-I", str(ROOT / "src/core/hotfunc/theseus"), "-c",
                        str(wrapper), "-o", os.devnull], capture_output=True, text=True)
    (out / "override_build.log").write_text(r.stdout + r.stderr)
    if r.returncode:
        return {"ok": False, "note": "override does not compile: " + r.stderr.strip()[-600:]}
    warn = r.stderr.count("warning:")
    return {"ok": True, "note": "compiles with the game's Release flags" + (f" ({warn} warnings)" if warn else "")}

# ---------------------------------------------------------------- the gate


def uses_float(body):
    return any(re.search(r"ctx\.(f\d+|v\d+)\.|std::fma|simde_", l) for l in body)


def gate_one(name, args, jobs):
    t0 = time.time()
    out = TARGET.out / "batch" / name
    out.mkdir(parents=True, exist_ok=True)
    res = {"name": name, "date": datetime.date.today().isoformat(), "steps": {}}
    log = []

    def step(key, value):
        res["steps"][key] = value
        (out / "gate.json").write_text(json.dumps(res, indent=1) + "\n")

    if not (HERE / "rewrites" / f"{name}.cpp").exists():
        res.update(passed=False, gate="no rewrite", summary="no rewrites/<name>.cpp")
        step("difftest", {"ok": False, "note": "missing rewrite"})
        return res

    # 1. difftest
    r = subprocess.run([sys.executable, str(HERE / "difftest.py"), name, "--cases", str(args.cases),
                        "--timeout", str(args.timeout)], capture_output=True, text=True)
    m = re.search(r"^RESULT (.+)$", r.stdout, re.M)
    verdict = m.group(1).strip() if m else ("BUILD-FAIL" if "build failed" in r.stderr + r.stdout else "ERROR")
    first = re.search(r"^first ABI failure.*$", r.stdout, re.M)
    dt_ok = verdict in ("PASS-ABI", "PASS-STRICT")
    step("difftest", {"ok": dt_ok, "result": verdict, "cases": args.cases,
                      "note": first.group(0)[:300] if first else (r.stderr.strip()[-300:] if not m else "")})
    dm = re.search(r"^differing_fields: (.*)$", r.stdout, re.M)
    differing = {f.split("=")[0] for f in dm.group(1).split()} if dm else set()

    _, body = difftest.extract(name)
    floaty = uses_float(body)
    mcases = args.mutant_cases or (200000 if floaty else 20000)

    # 2. mutants of the original
    r = subprocess.run([sys.executable, str(HERE / "mutate.py"), name, "--cases", str(mcases), "-j", str(jobs)],
                       capture_output=True, text=True)
    rj = TARGET.out / "mutants" / name / "result.json"
    if r.returncode == 0 and rj.exists():
        om = json.loads(rj.read_text())
        surv = [{"k": x["k"], "op": x["op"], "desc": x["desc"], "ppc": x["ppc"],
                 "label": label_survivor(x, body)} for x in om["rows"] if x["verdict"] == "survived"]
        step("original_mutants", {"ok": not om.get("inconclusive"), "cases": mcases,
                                  "inconclusive": om.get("inconclusive", 0), "mutants": om["mutants"], "killed": om["killed"],
                                  "kill_rate": om["kill_rate"], "per_op": om["per_op"], "survivors": surv,
                                  "to_review": sum(s["label"].startswith("review") for s in surv)})
    else:
        tail = (r.stdout + r.stderr).strip()[-400:]
        nothing = "no mutants" in tail
        step("original_mutants", {"ok": nothing, "cases": mcases, "mutants": 0, "kill_rate": None,
                                  "survivors": [], "to_review": 0, "note": tail})

    # 3. mutants of the rewrite
    try:
        rm = run_rewrite_mutants(name, mcases, args.per_op, jobs, args.seed)
        rm["ok"] = not rm["survivors"] and not rm.get("inconclusive")
        rm["cases"] = mcases
    except SystemExit as e:
        rm = {"ok": False, "note": f"rewrite mutants did not build: {e}", "survivors": []}
    step("rewrite_mutants", rm)

    # 4. callers, 5. override build
    step("callers", caller_check(name, differing))
    step("override", override_check(name, out))

    st = res["steps"]
    hard = ["difftest", "original_mutants", "rewrite_mutants", "callers", "override"]
    failed = [k for k in hard if st[k]["ok"] is False]
    om = st["original_mutants"]
    res["passed"] = not failed
    res["gate"] = verdict
    res["kill_rate"] = om.get("kill_rate")
    res["seconds"] = round(time.time() - t0)
    parts = [verdict]
    if om.get("mutants"):
        parts.append(f"orig mutants {om['killed']}/{om['mutants']}"
                     + (f" ({om['to_review']} to review)" if om["to_review"] else ""))
    if rm.get("mutants"):
        parts.append(f"rewrite mutants {rm['killed']}/{rm['mutants']}")
    if failed:
        parts.append("FAIL: " + ", ".join(failed))
    res["summary"] = "; ".join(parts)
    step("done", True)
    return res


def update_ledger(results):
    ledger = batch_pool.read_ledger()
    queue = {}
    qpath = TARGET.out / "batch" / "queue.jsonl"
    if qpath.exists():
        queue = {q["name"]: q for q in map(json.loads, qpath.open())}
    for res in results:
        name = res["name"]
        row = ledger.get(name, {"name": name, "tier": queue.get(name, {}).get("tier", "?"), "batch": "", "note": ""})
        row["gate"] = res.get("gate", "")
        kr = res.get("kill_rate")
        row["kill_rate"] = "" if kr is None else f"{kr:.3f}"
        if row.get("status") not in KEEP_STATUS:
            row["status"] = "gated" if res["passed"] else "gate-fail"
        # Keep the human part of the note; replace only the previous gate summary.
        human = re.sub(r"(?:^|; )gate\.py \d{4}-\d\d-\d\d:.*$", "", row.get("note") or "").strip("; ")
        gate_note = f"gate.py {res['date']}: {res['summary']}".replace("\t", " ")
        row["note"] = f"{human}; {gate_note}" if human else gate_note
        ledger[name] = row
    batch_pool.write_ledger(ledger)


def print_report(res):
    st = res["steps"]
    print(f"\n== {res['name']}: {'PASS' if res['passed'] else 'FAIL'}  ({res['summary']}, {res.get('seconds', 0)} s)")
    for k in ("difftest", "callers", "override"):
        if k in st:
            s = st[k]
            mark = {True: "ok  ", False: "FAIL", None: "skip"}[s["ok"]]
            extra = s.get("result", "") if k == "difftest" else ""
            print(f"  {mark} {k:<9} {extra} {s.get('note', '')}".rstrip())
    om = st.get("original_mutants", {})
    if om:
        print(f"  info original mutants: {om.get('killed', 0)}/{om.get('mutants', 0)} killed at {om.get('cases')} cases"
              + (f"  {om.get('note', '')}" if om.get("note") else ""))
        for s in om.get("survivors", []):
            print(f"         survived #{s['k']:<3} {s['op']:<5} {s['desc']:<26} | {s['ppc']:<24} -> {s['label']}")
    rm = st.get("rewrite_mutants", {})
    if rm:
        mark = "ok  " if rm["ok"] else "FAIL"
        print(f"  {mark} rewrite mutants: {rm.get('killed', 0)}/{rm.get('mutants', 0)} killed"
              + (f" ({rm['available']} possible)" if rm.get("available") else "") + f" {rm.get('note', '')}")
        for s in rm.get("survivors", []):
            print(f"         survived #{s['k']:<3} {s['op']:<5} {s['desc']:<26} | line {s['line']}: {s['code'][:70]}")
        if rm.get("no_compile"):
            print(f"         {len(rm['no_compile'])} mutant(s) didn't compile (counted as killed)")
        for s in rm.get("marked_equivalent", []):
            print(f"         marked equivalent, line {s['line']}: {s['reason']}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("names", nargs="+")
    ap.add_argument("-j", type=int, default=1, help="functions gated at once")
    ap.add_argument("--cases", type=int, default=200000, help="difftest cases")
    ap.add_argument("--mutant-cases", type=int, default=0, help="default 20k, 200k for float/vector bodies")
    ap.add_argument("--per-op", type=int, default=12, help="max rewrite mutants per operator")
    ap.add_argument("--timeout", type=float, default=600.0)
    ap.add_argument("--seed", default="0x9E3779B97F4A7C15")
    ap.add_argument("--no-ledger", action="store_true")
    ap.add_argument("--no-nice", action="store_true",
                    help="don't lower the priority (default: nice 10, so a game in the foreground keeps the CPU)")
    args = ap.parse_args()

    if not args.no_nice:
        os.nice(10)  # inherited by every build and test run
    jobs = max(1, (os.cpu_count() or 2) // max(1, args.j))
    with ThreadPoolExecutor(args.j) as pool:
        results = list(pool.map(lambda n: gate_one(n, args, jobs), args.names))
    for res in results:
        print_report(res)
    if not args.no_ledger:
        update_ledger(results)
    passed = sum(r["passed"] for r in results)
    print(f"\n{passed}/{len(results)} passed" + ("" if args.no_ledger else "; ledger updated"))
    sys.exit(0 if passed == len(results) else 1)


if __name__ == "__main__":
    main()
