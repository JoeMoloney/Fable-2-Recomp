#!/usr/bin/env python3
"""How do callers use a function's results? (static, approximate)

  python3 tools/theseus/caller_usage.py FNV1Hash_821F3C28

For every call site in generated/default, scans the caller's code straight after
the call and classifies the first use of each register a rewrite might leave
different: r3 (return value; does the caller read all 64 bits or only the low
32?) and the volatile scratch registers (r0, r4-r12, ctr, xer, cr0, cr1, cr5-cr7),
which a well-behaved caller never reads before writing.

The scan is linear text, not control flow: it stops at the first label, goto,
return or further call and reports "unclear" there, so "unclear" sites need a
manual look (or a later control-flow-aware version of this tool).
"""
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import target as target_mod  # noqa: E402

TARGET = target_mod.load()

SCRATCH = ["r0", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "r12",
           "ctr", "xer", "cr0", "cr1", "cr5", "cr6", "cr7"]
U64_COPY = re.compile(r"^ctx\.(r\d+)\.u64 = ctx\.r3\.u64;$")


def classify_r3(lines):
    for s in lines:
        if s.startswith("loc_") or s.startswith("goto") or "goto " in s or s == "return;":
            return "returned" if s == "return;" else "unclear (branch)"
        if re.match(r"^\w+\(ctx, base\);", s) or "REX_CALL_INDIRECT_FUNC" in s:
            return "unclear (another call)"
        if "ctx.r3" not in s:
            continue
        lhs, _, rhs = s.partition(" = ")
        if "ctx.r3" in rhs or ("ctx.r3" in lhs and "compare" in lhs):
            m = U64_COPY.match(s)
            if m:
                return "copied 64-bit (mr)"
            reads = set(re.findall(r"ctx\.r3\.(\w+)", rhs if rhs else lhs))
            if reads & {"u64", "s64"}:
                return "READS 64-bit"
            return "reads low 32 only"
        if lhs.startswith("ctx.r3."):
            return "overwritten (unused)"
    return "unclear (end)"


def scratch_reads(lines):
    """Scratch registers read before being written (linear scan, best effort)."""
    seen_write, bad = set(), set()
    for s in lines:
        if s.startswith("loc_") or "goto " in s or s.startswith("goto") or s == "return;" \
                or re.match(r"^\w+\(ctx, base\);", s):
            break
        cmp = re.match(r"ctx\.(cr\d)\.compare<[^>]*>\((.*), ctx\.xer\);", s)
        if cmp:
            # A compare writes its CR field and reads only XER's sticky SO bit,
            # which a rewrite that never touches XER leaves unchanged.
            lhs, eq, rhs = f"ctx.{cmp.group(1)}.x", True, cmp.group(2)
        else:
            lhs, eq, rhs = s.partition(" = ")
        src = rhs if eq else s
        for reg in SCRATCH:
            if reg in seen_write:
                continue
            if re.search(r"ctx\." + reg + r"\b", src):
                bad.add(reg)
        if eq:
            m = re.match(r"ctx\.(\w+)", lhs)
            if m:
                seen_write.add(m.group(1))
        for reg in SCRATCH:
            if re.match(r"ctx\." + reg + r"\.(compare|set)", s):
                seen_write.add(reg)
    return bad


def main():
    name = sys.argv[1]
    call = re.compile(r"^\s*" + re.escape(name) + r"\(ctx, base\);")
    r3_use, scratch, examples = Counter(), Counter(), defaultdict(list)
    sites = 0
    for path in TARGET.partitions():
        lines = path.read_text(errors="replace").splitlines()
        caller = None
        for i, line in enumerate(lines):
            if line.startswith("DEFINE_REX_FUNC("):
                caller = line[16:line.index(")")]
            if not call.match(line):
                continue
            sites += 1
            after = [l.strip() for l in lines[i + 1:i + 80] if l.strip() and not l.strip().startswith("//")]
            kind = classify_r3(after)
            r3_use[kind] += 1
            if len(examples[kind]) < 3:
                examples[kind].append(f"{caller} ({path.name}:{i + 1})")
            for reg in scratch_reads(after):
                scratch[reg] += 1
                if len(examples["scratch " + reg]) < 3:
                    examples["scratch " + reg].append(f"{caller} ({path.name}:{i + 1})")
    print(f"{name}: {sites} call sites")
    print("r3 after the call:")
    for k, v in r3_use.most_common():
        print(f"  {v:6d}  {k}   e.g. {', '.join(examples[k])}")
    print("scratch registers read before written (should be none):")
    if not scratch:
        print("  none")
    for k, v in scratch.most_common():
        print(f"  {v:6d}  {k}   e.g. {', '.join(examples['scratch ' + k])}")


if __name__ == "__main__":
    main()
