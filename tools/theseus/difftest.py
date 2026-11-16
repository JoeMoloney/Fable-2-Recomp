#!/usr/bin/env python3
"""Differential-test a readable rewrite of a leaf guest function.

  python3 tools/theseus/difftest.py FNV1Hash_821F3C28 [--cases N] [--seed S]

Takes the original body from generated/default (as `original_<name>`), the
rewrite from tools/theseus/rewrites/<name>.cpp (which must define
`rewrite_<name>`), builds tools/theseus/difftest_harness.cpp around them and
runs it. Output goes to out/theseus/difftest/<name>/. Leaf functions only:
the harness has no other guest functions or kernel imports to link against.

Optional input rules: tools/theseus/rewrites/<name>.rules, one register per
line (`r3 ptr`, `r4 ptr_or_null`, `r5 len 256`, `r6 int`, `r7 u32`, `r8 any`,
`r4 slen 64` = -64..64, `r3 fixed 0x4000C000` = always that value, `#` comments;
`tree <isnil_off> <key_off> [n]` builds a valid MSVC std::map tree each case,
then kinds `tree_cont` / `tree_node` / `tree_key` point into it;
`data <addr> <hex>` fixed bytes (image constants); `ret f1` = float return;
`fval <double>` an edge value for float arguments (nudged by +-2 ulps); `r4 near r3 32` = half the time r3 + 0..32), and `mem32 <addr> <kind> [max]` to set a 32-bit global (e.g. a
table pointer) before each case; `mem32 @r3+12 <kind>` sets a field of an
argument's struct; `mem32 *0x834BDBC4+16 <kind>` a field of the struct a
global points to. Without it every argument register gets a
random mix and all memory outside the windows is zero.
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import target as target_mod  # noqa: E402

TARGET = target_mod.load()
ROOT = TARGET.root
GEN = TARGET.generated
SDK = TARGET.sdk


def extract(name):
    start = re.compile(r"^DEFINE_REX_FUNC\(" + re.escape(name) + r"\)\s*\{")
    for path in TARGET.partitions():
        lines, body = path.read_text(errors="replace").splitlines(), None
        for i, line in enumerate(lines):
            if start.match(line):
                body = [line]
                for rest in lines[i + 1:]:
                    body.append(rest)
                    if rest.startswith("}"):
                        return path.name, body
    sys.exit(f"{name}: not found in generated code")


KINDS = {"slen": "kSLen", "ptr": "kPtr", "ptr_or_null": "kPtrOrNull", "len": "kLen", "int": "kInt",
         "u32": "kU32", "any": "kAny", "tree_cont": "kTreeCont", "tree_node": "kTreeNode",
         "tree_key": "kTreeKey", "fixed": "kFixed"}


def rules_inc(path):
    """Turn a .rules file into the C++ kRules table for the harness."""
    rows, mems, tree, datas, fret, fvals = [], [], "{0u, 0u, 0u}", [], "false", []
    for n, line in enumerate(path.read_text().splitlines(), 1):
        words = line.split("#", 1)[0].split()
        if not words:
            continue
        if words[0] == "data":
            if len(words) != 3 or not re.fullmatch(r"(?:[0-9a-fA-F]{2})+", words[2]):
                sys.exit(f"{path}:{n}: expected 'data <addr> <hex bytes>'")
            datas.append(f'{{{int(words[1], 0):#x}u, "{words[2]}"}}')
            continue
        if words[0] == "fval":
            if len(words) != 2:
                sys.exit(f"{path}:{n}: expected 'fval <double>' (decimal or 0x hex bits)")
            if words[1].lower().startswith("0x"):
                fvals.append(f"std::bit_cast<double>(uint64_t({words[1]}ull))")
            else:
                fvals.append(repr(float(words[1])))
            continue
        if words[0] == "ret":
            if words[1:] != ["f1"]:
                sys.exit(f"{path}:{n}: only 'ret f1' is supported")
            fret = "true"
            continue
        if words[0] == "tree":
            if len(words) not in (3, 4):
                sys.exit(f"{path}:{n}: expected 'tree <isnil_off> <key_off> [max_nodes]'")
            isnil, key = int(words[1], 0), int(words[2], 0)
            maxn = int(words[3], 0) if len(words) > 3 else 24
            if not (isnil < 32 and key + 4 <= 32 and 12 <= min(isnil, key) and 0 < maxn <= 255):
                sys.exit(f"{path}:{n}: fields must sit in 12..31 of a 32-byte node, max_nodes 1..255")
            tree = f"{{{isnil}u, {key}u, {maxn}u}}"
            continue
        if words[0] == "mem32":
            if len(words) < 3 or words[2] not in KINDS:
                sys.exit(f"{path}:{n}: expected 'mem32 <addr> <{'|'.join(KINDS)}> [max]'")
            maxv = int(words[3], 0) if len(words) > 3 else 0
            rel = re.fullmatch(r"@r([3-9]|10)\+(\w+)", words[1])
            der = re.fullmatch(r"\*(0x[0-9a-fA-F]+)\+(\w+)", words[1])
            if rel:
                mems.append(f"{{{int(rel.group(2), 0):#x}u, {KINDS[words[2]]}, {maxv}u, {rel.group(1)}}}")
            elif der:
                mems.append(f"{{{int(der.group(2), 0):#x}u, {KINDS[words[2]]}, {maxv}u, 0, {int(der.group(1), 16):#x}u}}")
            else:
                mems.append(f"{{{int(words[1], 0):#x}u, {KINDS[words[2]]}, {maxv}u}}")
            continue
        if len(words) == 4 and words[1] == "near":
            base = re.fullmatch(r"r(\d+)", words[2])
            if not re.fullmatch(r"r([3-9]|10)", words[0]) or not base or not 3 <= int(base.group(1)) <= 10:
                sys.exit(f"{path}:{n}: expected 'rN near rM <max>'")
            rows.append(f"{{{words[0][1:]}, kNear, {int(words[3], 0)}u, {base.group(1)}}}")
            continue
        m = re.fullmatch(r"r(\d+)", words[0])
        if not m or not 3 <= int(m.group(1)) <= 10 or len(words) < 2 or words[1] not in KINDS:
            sys.exit(f"{path}:{n}: expected 'r3..r10 <{'|'.join(KINDS)}> [max]'")
        if words[1] in ("len", "slen") and (len(words) < 3 or not 0 < int(words[2], 0) <= 0x8000):
            sys.exit(f"{path}:{n}: len needs a max between 1 and 0x8000 (the pointer room)")
        maxv = int(words[2], 0) if len(words) > 2 else 0
        rows.append(f"{{{m.group(1)}, {KINDS[words[1]]}, {maxv}u}}")
    rows = rows or ["{0, kAny, 0u}"]
    mems = mems or ["{0u, kAny, 0u}"]
    return ("const ArgRule kRules[] = {" + ", ".join(rows) + "};\n"
            "const MemRule kMemRules[] = {" + ", ".join(mems) + "};\n"
            f"const TreeRule kTreeRule = {tree};\n"
            "const DataRule kDataRules[] = {" + (", ".join(datas) or '{0u, ""}') + "};\n"
            f"constexpr bool kFloatReturn = {fret};\n"
            "const double kFloatValues[] = {" + (", ".join(fvals) or "0.0") + "};\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("name")
    ap.add_argument("--cases", type=int, default=200000)
    ap.add_argument("--seed", default="0x9E3779B97F4A7C15")
    ap.add_argument("--rewrite", type=Path, help="test this file instead of rewrites/<name>.cpp (e.g. a deliberately broken copy)")
    ap.add_argument("--out-suffix", default="")
    args = ap.parse_args()

    src_file, body = extract(args.name)
    text = "\n".join(body)
    if re.search(r"^\s*\w+\(ctx, base\);", text, re.M) or "REX_CALL_INDIRECT_FUNC" in text:
        sys.exit(f"{args.name} calls other functions; only leaf functions are supported")
    rewrite = args.rewrite.resolve() if args.rewrite else HERE / "rewrites" / f"{args.name}.cpp"
    if not rewrite.exists():
        sys.exit(f"missing rewrite: {rewrite}")

    out = TARGET.out / "difftest" / (args.name + args.out_suffix)
    out.mkdir(parents=True, exist_ok=True)
    original = out / "original.cpp"
    body[0] = f'extern "C" REX_FUNC(original_{args.name}) {{'
    original.write_text(f'// Extracted from generated/default/{src_file}\n#include "fable_2_pch.h"\n'
                        + "\n".join(body) + "\n")

    rules = HERE / "rewrites" / f"{args.name}.rules"
    extra = []
    if rules.exists():
        (out / "difftest_rules.inc").write_text(rules_inc(rules))
        extra = ["-DHAS_RULES", "-I", str(out)]
    flags = [*extra, "-std=c++23", "-O2", "-march=x86-64-v2", "-msse4.1", "-fno-strict-aliasing",
             "-ffp-model=strict", "-DFMT_HEADER_ONLY", f"-DORIGINAL=original_{args.name}",
             f"-DREWRITE=rewrite_{args.name}"]
    for inc in (GEN, SDK / "include", SDK / "thirdparty/simde", SDK / "thirdparty/fmt/include",
                SDK / "thirdparty/spdlog/include"):
        flags += ["-I", str(inc)]
    exe = out / "difftest"
    build = subprocess.run(["clang++", *flags, str(HERE / "difftest_harness.cpp"), str(original),
                            str(rewrite), "-o", str(exe)], capture_output=True, text=True)
    (out / "build.log").write_text(build.stdout + build.stderr)
    if build.returncode:
        print(build.stderr[-4000:])
        sys.exit("build failed (see build.log)")
    run = subprocess.run([str(exe), str(args.cases), args.seed], capture_output=True, text=True)
    (out / "result.txt").write_text(run.stdout + run.stderr)
    print(run.stdout + run.stderr, end="")
    sys.exit(run.returncode)


if __name__ == "__main__":
    main()
