#!/usr/bin/env python3
"""Differential-test a readable rewrite of a leaf guest function.

  python3 tools/theseus/difftest.py FNV1Hash_821F3C28 [--cases N] [--seed S]

Takes the original body from generated/default (as `original_<name>`), the
rewrite from tools/theseus/rewrites/<name>.cpp (which must define
`rewrite_<name>`), builds tools/theseus/difftest_harness.cpp around them and
runs it. Output goes to out/theseus/difftest/<name>/. Leaf functions only:
the harness has no other guest functions or kernel imports to link against.
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

    flags = ["-std=c++23", "-O2", "-march=x86-64-v2", "-msse4.1", "-fno-strict-aliasing",
             "-ffp-model=strict", f"-DORIGINAL=original_{args.name}",
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
